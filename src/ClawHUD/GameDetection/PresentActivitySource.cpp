#include "PresentActivitySource.h"

#include "RuntimeLogger.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cctype>
#include <evntrace.h>
#include <filesystem>
#include <initializer_list>
#include <limits>
#include <sstream>

namespace clawhud
{
namespace
{
std::vector<std::string> CsvLine(const std::string& line)
{
    std::vector<std::string> fields;
    std::string field;
    bool quoted = false;
    for (std::size_t index = 0; index < line.size(); ++index)
    {
        if (line[index] == '"')
        {
            if (quoted && index + 1 < line.size() && line[index + 1] == '"')
                field += line[++index];
            else
                quoted = !quoted;
        }
        else if (line[index] == ',' && !quoted)
        {
            fields.push_back(std::move(field));
            field.clear();
        }
        else
            field += line[index];
    }
    fields.push_back(std::move(field));
    return fields;
}

std::optional<std::size_t> Column(const std::vector<std::string>& headers,
    std::initializer_list<const char*> names)
{
    for (const char* name : names)
    {
        const auto it = std::find(headers.begin(), headers.end(), name);
        if (it != headers.end())
            return static_cast<std::size_t>(it - headers.begin());
    }
    return std::nullopt;
}

std::string Field(const std::vector<std::string>& row,
    std::optional<std::size_t> index)
{
    return index && *index < row.size() ? row[*index] : std::string{};
}

std::optional<DWORD> ParseProcessId(const std::string& text)
{
    if (text.empty() || text == "NA") return std::nullopt;
    DWORD value{};
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size())
        return std::nullopt;
    return value;
}

std::optional<double> ParseTimestamp(const std::string& text)
{
    if (text.empty() || text == "NA") return std::nullopt;
    try
    {
        std::size_t consumed{};
        const double value = std::stod(text, &consumed);
        if (consumed != text.size() || !std::isfinite(value) || value < 0.0)
            return std::nullopt;
        return value;
    }
    catch (...) { return std::nullopt; }
}

std::wstring HresultText(HRESULT hr)
{
    wchar_t buffer[11]{};
    swprintf_s(buffer, L"0x%08X", static_cast<unsigned int>(hr));
    return buffer;
}

void LogDebug(const std::wstring& message) noexcept
{
    RuntimeLogger::Log(RuntimeLogLevel::Debug, L"[PresentActivity] " + message);
}

void LogWarning(const std::wstring& message) noexcept
{
    RuntimeLogger::Log(RuntimeLogLevel::Warn, L"[PresentActivity] " + message);
}

struct TraceStopProperties
{
    EVENT_TRACE_PROPERTIES properties{};
    wchar_t loggerName[128]{};
};

void StopTraceSession(const std::wstring& sessionName) noexcept
{
    if (sessionName.empty()) return;
    TraceStopProperties buffer{};
    buffer.properties.Wnode.BufferSize = sizeof(buffer);
    buffer.properties.LoggerNameOffset =
        static_cast<ULONG>(offsetof(TraceStopProperties, loggerName));
    (void)ControlTraceW(0, sessionName.c_str(), &buffer.properties,
        EVENT_TRACE_CONTROL_STOP);
}

bool ContainsError(const std::string& line)
{
    std::string lower = line;
    std::transform(lower.begin(), lower.end(), lower.begin(),
        [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
    return lower.find("error") != std::string::npos ||
        lower.find("failed") != std::string::npos;
}

std::wstring Widen(const std::string& value)
{
    return std::wstring(value.begin(), value.end());
}
}

PresentActivitySchema ParsePresentActivitySchema(
    const std::vector<std::string>& headers)
{
    PresentActivitySchema schema;
    schema.application = Column(headers, {"Application"});
    schema.processId = Column(headers, {"ProcessID"});
    schema.qpcTimeMs = Column(headers, {"CPUStartQPCTimeInMs", "QpcTimeMs", "QPCStartTime"});
    schema.presentMode = Column(headers, {"PresentMode"});
    schema.frameType = Column(headers, {"FrameType"});
    schema.swapChainAddress = Column(headers, {"SwapChainAddress"});
    schema.msBetweenDisplayChange = Column(headers, {"MsBetweenDisplayChange"});
    return schema;
}

std::optional<PresentActivitySample> ParsePresentActivityRow(
    const PresentActivitySchema& schema, const std::vector<std::string>& row)
{
    if (!schema.HasRequiredColumns()) return std::nullopt;
    const auto processId = ParseProcessId(Field(row, schema.processId));
    const auto timestamp = ParseTimestamp(Field(row, schema.qpcTimeMs));
    const auto application = Field(row, schema.application);
    if (!processId || !timestamp || application.empty() || application == "NA")
        return std::nullopt;

    PresentActivitySample sample;
    sample.processId = *processId;
    sample.application = application;
    sample.qpcTimeMs = *timestamp;
    sample.presentMode = Field(row, schema.presentMode);
    sample.frameType = Field(row, schema.frameType);
    sample.swapChainAddress = Field(row, schema.swapChainAddress);
    if (const auto displayInterval = ParseTimestamp(
        Field(row, schema.msBetweenDisplayChange)))
        sample.displayed = *displayInterval > 0.0;
    return sample;
}

std::wstring BuildPresentActivityCommandLine(const std::wstring& executable,
    const std::wstring& sessionName)
{
    return L"\"" + executable + L"\" --output_stdout --no_console_stats --qpc_time_ms"
        L" --track_frame_type --session_name \"" + sessionName + L"\"";
}

std::wstring EscapePresentActivityValue(std::string_view value)
{
    std::wstring result;
    result.reserve(value.size());
    for (const unsigned char character : value)
    {
        if (character == '\\') result += L"\\\\";
        else if (character == '"') result += L"\\\"";
        else if (character == '\r') result += L"\\r";
        else if (character == '\n') result += L"\\n";
        else if (character == '\t') result += L"\\t";
        else result += static_cast<wchar_t>(character);
    }
    return result;
}

std::vector<PresentActivitySummary> PresentActivityAggregator::Consume(
    const PresentActivitySample& sample)
{
    constexpr double kActivityWindowMs = 500.0;
    constexpr double kStaleActivityMs = 5000.0;
    constexpr std::size_t kMaximumAccumulators = 4096;
    std::vector<PresentActivitySummary> summaries;

    for (auto it = accumulators_.begin(); it != accumulators_.end();)
    {
        if (sample.qpcTimeMs >= it->second.summary.lastQpcMs &&
            sample.qpcTimeMs - it->second.summary.lastQpcMs > kStaleActivityMs)
            it = accumulators_.erase(it);
        else
            ++it;
    }

    auto found = accumulators_.find(sample.processId);
    if (found != accumulators_.end() &&
        sample.qpcTimeMs >= found->second.summary.firstQpcMs &&
        sample.qpcTimeMs - found->second.summary.firstQpcMs >= kActivityWindowMs)
    {
        summaries.push_back(found->second.summary);
        found = accumulators_.erase(found);
    }

    if (found == accumulators_.end())
    {
        Accumulator accumulator;
        accumulator.summary.processId = sample.processId;
        accumulator.summary.application = sample.application;
        accumulator.summary.firstQpcMs = sample.qpcTimeMs;
        accumulator.summary.lastQpcMs = sample.qpcTimeMs;
        accumulator.summary.presentCount = 1;
        accumulator.summary.displayedCount = sample.displayed == true ? 1 : 0;
        accumulator.summary.displayCountAvailable = sample.displayed.has_value();
        accumulator.summary.presentMode = sample.presentMode;
        accumulator.summary.frameType = sample.frameType;
        accumulator.summary.swapChainAddress = sample.swapChainAddress;
        accumulators_.emplace(sample.processId, std::move(accumulator));
    }
    else
    {
        auto& summary = found->second.summary;
        summary.application = sample.application;
        summary.lastQpcMs = sample.qpcTimeMs;
        ++summary.presentCount;
        summary.displayCountAvailable = summary.displayCountAvailable || sample.displayed.has_value();
        if (sample.displayed == true) ++summary.displayedCount;
        if (!sample.presentMode.empty()) summary.presentMode = sample.presentMode;
        if (!sample.frameType.empty()) summary.frameType = sample.frameType;
        if (!sample.swapChainAddress.empty()) summary.swapChainAddress = sample.swapChainAddress;
    }

    if (accumulators_.size() > kMaximumAccumulators)
    {
        const auto oldest = std::min_element(accumulators_.begin(), accumulators_.end(),
            [](const auto& left, const auto& right)
            {
                return left.second.summary.lastQpcMs < right.second.summary.lastQpcMs;
            });
        if (oldest != accumulators_.end()) accumulators_.erase(oldest);
    }
    return summaries;
}

PresentActivitySource::~PresentActivitySource()
{
    Stop();
}

bool PresentActivitySource::Start(const std::wstring& executable)
{
    Stop();
    if (executable.empty())
    {
        LogWarning(L"start.result=API_FAILED stage=ExecutableMissing");
        return false;
    }
    std::error_code fileError;
    if (std::filesystem::exists(executable, fileError) == false || fileError)
    {
        LogWarning(L"start.result=API_FAILED stage=ExecutableMissing error=" +
            std::to_wstring(fileError.value()));
        return false;
    }

    SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
    HANDLE outputRead{}, outputWrite{};
    if (!CreatePipe(&outputRead, &outputWrite, &security, 0))
    {
        LogWarning(L"start.result=API_FAILED stage=CreatePipe error=" +
            std::to_wstring(GetLastError()));
        return false;
    }
    SetHandleInformation(outputRead, HANDLE_FLAG_INHERIT, 0);

    sessionName_ = L"ClawHUD-PresentActivity-" +
        std::to_wstring(GetCurrentProcessId()) + L"-" +
        std::to_wstring(GetTickCount64());
    std::wstring command = BuildPresentActivityCommandLine(executable, sessionName_);
    std::vector<wchar_t> commandLine(command.begin(), command.end());
    commandLine.push_back(L'\0');
    STARTUPINFOW startup{sizeof(startup)};
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdOutput = outputWrite;
    startup.hStdError = outputWrite;
    PROCESS_INFORMATION processInfo{};
    const BOOL created = CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr,
        TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &processInfo);
    const DWORD createError = created ? ERROR_SUCCESS : GetLastError();
    CloseHandle(outputWrite);
    if (!created)
    {
        LogWarning(L"start.result=API_FAILED stage=CreateProcess error=" +
            std::to_wstring(createError));
        CloseHandle(outputRead);
        sessionName_.clear();
        return false;
    }
    CloseHandle(processInfo.hThread);
    process_ = processInfo.hProcess;
    output_ = outputRead;
    stop_ = false;
    try
    {
        worker_ = std::thread(&PresentActivitySource::ReadLoop, this);
    }
    catch (...)
    {
        LogWarning(L"start.result=API_FAILED stage=ReaderThread");
        Stop();
        return false;
    }
    LogDebug(L"start.result=SUCCESS session=\"" + sessionName_ + L"\"");
    return true;
}

void PresentActivitySource::Stop() noexcept
{
    const bool hadResources = process_ || output_ || worker_.joinable();
    stop_ = true;
    StopTraceSession(sessionName_);
    if (process_ && WaitForSingleObject(process_, 3000) == WAIT_TIMEOUT)
    {
        TerminateProcess(process_, 2);
        WaitForSingleObject(process_, 2000);
    }
    if (worker_.joinable()) worker_.join();
    if (output_)
    {
        CloseHandle(output_);
        output_ = nullptr;
    }
    if (process_)
    {
        CloseHandle(process_);
        process_ = nullptr;
    }
    sessionName_.clear();
    if (hadResources)
        LogDebug(L"stop.result=SUCCESS");
}

void PresentActivitySource::LogSummary(const PresentActivitySummary& summary) noexcept
{
    try
    {
        std::wstringstream message;
        message << L"seq=" << nextSequence_.fetch_add(1, std::memory_order_relaxed)
            << L" pid=" << summary.processId
            << L" application=\"" << EscapePresentActivityValue(summary.application) << L"\""
            << L" firstQpcMs=" << summary.firstQpcMs
            << L" lastQpcMs=" << summary.lastQpcMs
            << L" presentCount=" << summary.presentCount;
        if (summary.displayCountAvailable)
            message << L" displayedCount=" << summary.displayedCount;
        if (!summary.presentMode.empty())
            message << L" presentMode=\"" << EscapePresentActivityValue(summary.presentMode) << L"\"";
        if (!summary.frameType.empty())
            message << L" frameType=\"" << EscapePresentActivityValue(summary.frameType) << L"\"";
        if (!summary.swapChainAddress.empty())
            message << L" swapChain=\"" << EscapePresentActivityValue(summary.swapChainAddress) << L"\"";
        LogDebug(message.str());
    }
    catch (...) {}
}

void PresentActivitySource::ReadLoop() noexcept
{
    const HANDLE output = output_;
    std::vector<std::string> headers;
    PresentActivitySchema schema;
    PresentActivityAggregator aggregator;
    std::string pending;
    auto consumeLine = [&](std::string line)
    {
        if (headers.empty())
        {
            if (line.size() >= 3 && static_cast<unsigned char>(line[0]) == 0xEF &&
                static_cast<unsigned char>(line[1]) == 0xBB &&
                static_cast<unsigned char>(line[2]) == 0xBF)
                line.erase(0, 3);
            const auto headerFields = CsvLine(line);
            const auto headerSchema = ParsePresentActivitySchema(headerFields);
            if (headerSchema.application || headerSchema.processId ||
                headerSchema.qpcTimeMs)
            {
                headers = headerFields;
                schema = ParsePresentActivitySchema(headers);
                if (!schema.HasRequiredColumns())
                {
                    const wchar_t* missing = !schema.application ? L"Application" :
                        !schema.processId ? L"ProcessID" : L"QpcTime";
                    LogWarning(std::wstring(L"schema.result=UNSUPPORTED reason="
                        L"missing-required-column column=") + missing);
                    stop_ = true;
                    StopTraceSession(sessionName_);
                }
                else
                {
                    LogDebug(L"schema.result=SUCCESS Application=" +
                        std::to_wstring(schema.application.has_value()) +
                        L" ProcessID=" + std::to_wstring(schema.processId.has_value()) +
                        L" QpcTime=" + std::to_wstring(schema.qpcTimeMs.has_value()) +
                        L" PresentMode=" + std::to_wstring(schema.presentMode.has_value()) +
                        L" FrameType=" + std::to_wstring(schema.frameType.has_value()) +
                        L" SwapChainAddress=" + std::to_wstring(schema.swapChainAddress.has_value()));
                }
            }
            else if (ContainsError(line))
                LogWarning(L"start.result=API_FAILED stage=PresentMon message=\"" +
                    EscapePresentActivityValue(line) + L"\"");
            return;
        }
        const auto sample = ParsePresentActivityRow(schema, CsvLine(line));
        if (!sample) return;
        for (const auto& summary : aggregator.Consume(*sample))
            LogSummary(summary);
    };

    std::array<char, 8192> buffer{};
    while (!stop_)
    {
        DWORD bytesRead{};
        if (!ReadFile(output, buffer.data(), static_cast<DWORD>(buffer.size()),
            &bytesRead, nullptr) || !bytesRead)
            break;
        pending.append(buffer.data(), bytesRead);
        for (;;)
        {
            const auto newline = pending.find('\n');
            if (newline == std::string::npos) break;
            std::string line = pending.substr(0, newline);
            pending.erase(0, newline + 1);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (!line.empty()) consumeLine(std::move(line));
            if (stop_) break;
        }
    }
}
}
