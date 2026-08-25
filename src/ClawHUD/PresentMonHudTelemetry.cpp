#include "PresentMonHudTelemetry.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <deque>
#include <utility>

namespace clawhud
{
namespace
{
std::vector<std::string> CsvLine(const std::string& line)
{
    std::vector<std::string> fields;
    std::string field;
    bool quoted = false;
    for (std::size_t i = 0; i < line.size(); ++i)
    {
        if (line[i] == '"')
        {
            if (quoted && i + 1 < line.size() && line[i + 1] == '"')
                field += line[++i];
            else
                quoted = !quoted;
        }
        else if (line[i] == ',' && !quoted)
        {
            fields.push_back(std::move(field)); field.clear();
        }
        else field += line[i];
    }
    fields.push_back(std::move(field));
    return fields;
}

std::optional<std::size_t> Column(const std::vector<std::string>& headers,
    const char* name)
{
    const auto it = std::find(headers.begin(), headers.end(), name);
    return it == headers.end() ? std::nullopt
        : std::optional<std::size_t>(static_cast<std::size_t>(it - headers.begin()));
}

std::string Field(const std::vector<std::string>& row, std::optional<std::size_t> index)
{
    return index && *index < row.size() ? row[*index] : std::string{};
}
}

std::optional<PresentMonFrameSample> ParseDisplayedFrame(
    const std::vector<std::string>& headers,
    const std::vector<std::string>& row)
{
    const auto displayed = Column(headers, "DisplayedTime");
    const auto interval = Column(headers, "MsBetweenDisplayChange");
    if (!displayed || !interval)
        return std::nullopt;
    const auto displayedTime = Field(row, displayed);
    if (displayedTime.empty() || displayedTime == "NA")
        return std::nullopt;
    try
    {
        const double value = std::stod(Field(row, interval));
        if (!std::isfinite(value) || value <= 0.0)
            return std::nullopt;
        return PresentMonFrameSample{value, Field(row, Column(headers, "FrameType"))};
    }
    catch (...) { return std::nullopt; }
}

std::optional<double> CalculateDisplayedFps(
    std::size_t displayedFrameCount, double elapsedSeconds)
{
    if (!displayedFrameCount || !std::isfinite(elapsedSeconds) || elapsedSeconds <= 0.0)
        return std::nullopt;
    return static_cast<double>(displayedFrameCount) / elapsedSeconds;
}

PresentMonHudTelemetry::~PresentMonHudTelemetry()
{
    Stop();
}

bool PresentMonHudTelemetry::Start(const std::wstring& executable, DWORD processId,
    UpdateCallback callback)
{
    Stop();
    if (executable.empty() || !processId)
        return false;

    SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
    HANDLE outputRead{}, outputWrite{};
    if (!CreatePipe(&outputRead, &outputWrite, &security, 0))
        return false;
    SetHandleInformation(outputRead, HANDLE_FLAG_INHERIT, 0);

    std::wstring command = L"\"" + executable + L"\" --process_id " +
        std::to_wstring(processId) + L" --output_stdout --no_console_stats --qpc_time_ms";
    std::vector<wchar_t> commandLine(command.begin(), command.end());
    commandLine.push_back(L'\0');
    STARTUPINFOW startup{sizeof(startup)};
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdOutput = outputWrite;
    startup.hStdError = outputWrite;
    PROCESS_INFORMATION processInfo{};
    const BOOL created = CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr,
        TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &processInfo);
    CloseHandle(outputWrite);
    if (!created)
    {
        CloseHandle(outputRead);
        return false;
    }
    CloseHandle(processInfo.hThread);
    process_ = processInfo.hProcess;
    output_ = outputRead;
    callback_ = std::move(callback);
    stop_ = false;
    worker_ = std::thread(&PresentMonHudTelemetry::ReadLoop, this);
    return true;
}

void PresentMonHudTelemetry::Stop() noexcept
{
    stop_ = true;
    if (process_)
        TerminateProcess(process_, 2);
    if (output_)
    {
        CloseHandle(output_);
        output_ = nullptr;
    }
    if (worker_.joinable())
        worker_.join();
    if (process_)
    {
        WaitForSingleObject(process_, 2000);
        CloseHandle(process_);
        process_ = nullptr;
    }
    callback_ = {};
}

void PresentMonHudTelemetry::ReadLoop()
{
    const HANDLE output = output_;
    std::string line;
    std::vector<std::string> headers;
    constexpr auto kFpsSamplingPeriod = std::chrono::milliseconds(500);
    const auto fpsWindowStart = std::chrono::steady_clock::now();
    auto windowStart = fpsWindowStart;
    std::size_t displayedFrameCount{};
    char character{};
    while (!stop_)
    {
        DWORD read{};
        if (!ReadFile(output, &character, 1, &read, nullptr) || !read)
            break;
        if (character != '\n')
        {
            if (character != '\r') line += character;
            continue;
        }
        if (headers.empty())
        {
            std::string headerLine = line;
            if (headerLine.size() >= 3 &&
                static_cast<unsigned char>(headerLine[0]) == 0xEF &&
                static_cast<unsigned char>(headerLine[1]) == 0xBB &&
                static_cast<unsigned char>(headerLine[2]) == 0xBF)
                headerLine.erase(0, 3);
            const auto candidate = CsvLine(headerLine);
            if (Column(candidate, "DisplayedTime") &&
                Column(candidate, "MsBetweenDisplayChange"))
                headers = candidate;
            line.clear();
            continue;
        }
        const auto frame = ParseDisplayedFrame(headers, CsvLine(line));
        line.clear();
        if (!frame) continue;
        const auto now = std::chrono::steady_clock::now();
        ++displayedFrameCount;
        const std::chrono::duration<double> elapsed = now - windowStart;
        if (elapsed >= kFpsSamplingPeriod && callback_)
        {
            callback_(CalculateDisplayedFps(displayedFrameCount, elapsed.count()));
            displayedFrameCount = 0;
            windowStart = now;
        }
    }
    if (!stop_ && callback_)
        callback_(std::nullopt);
}
}
