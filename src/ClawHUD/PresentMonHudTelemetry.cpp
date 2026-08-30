#include "PresentMonHudTelemetry.h"
#include "RuntimeLogger.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <evntrace.h>
#include <filesystem>
#include <utility>

namespace clawhud
{
namespace
{
constexpr DWORD kPresentMonGracefulStopMs = 150;
constexpr DWORD kPresentMonForcedStopConfirmMs = 250;
constexpr UINT kPresentMonForcedExitCode = 2;

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

struct TraceStopProperties
{
    EVENT_TRACE_PROPERTIES properties{};
    wchar_t loggerName[128]{};
};

void StopTraceSession(const std::wstring& sessionName) noexcept
{
    if (sessionName.empty())
        return;
    TraceStopProperties buffer{};
    buffer.properties.Wnode.BufferSize = sizeof(buffer);
    buffer.properties.LoggerNameOffset =
        static_cast<ULONG>(offsetof(TraceStopProperties, loggerName));
    (void)ControlTraceW(0, sessionName.c_str(), &buffer.properties,
        EVENT_TRACE_CONTROL_STOP);
}
}

std::optional<PresentMonFrameSample> ParseDisplayedFrame(
    const std::vector<std::string>& headers,
    const std::vector<std::string>& row)
{
    const auto interval = Column(headers, "MsBetweenDisplayChange");
    if (!interval)
        return std::nullopt;
    const auto valueText = Field(row, interval);
    if (valueText.empty() || valueText == "NA")
        return std::nullopt;
    try
    {
        const double value = std::stod(valueText);
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

std::optional<double> CalculateDisplayedFpsFromIntervals(
    const std::vector<double>& displayIntervalsMs)
{
    constexpr double kFpsWindowMs = 500.0;
    double elapsedMs{};
    for (const double interval : displayIntervalsMs)
    {
        if (!std::isfinite(interval) || interval <= 0.0)
            return std::nullopt;
        elapsedMs += interval;
    }
    if (elapsedMs < kFpsWindowMs)
        return std::nullopt;
    return CalculateDisplayedFps(
        displayIntervalsMs.size(), elapsedMs / 1000.0);
}

std::vector<PresentMonHudEvent> PresentMonFrameAccumulator::Observe(
    const PresentMonFrameSample& frame)
{
    std::vector<PresentMonHudEvent> events;
    if (!std::isfinite(frame.msBetweenDisplayChange) ||
        frame.msBetweenDisplayChange <= 0.0)
        return events;

    if (!firstDisplayedFrameEmitted_)
    {
        firstDisplayedFrameEmitted_ = true;
        events.push_back({PresentMonHudEventType::FirstDisplayedFrame,
            std::nullopt});
    }

    ++displayedFrameCount_;
    displayedElapsedMs_ += frame.msBetweenDisplayChange;
    if (displayedElapsedMs_ < 500.0)
        return events;

    events.push_back({PresentMonHudEventType::FpsUpdate,
        CalculateDisplayedFps(displayedFrameCount_, displayedElapsedMs_ / 1000.0)});
    displayedFrameCount_ = 0;
    displayedElapsedMs_ = 0.0;
    return events;
}

void PresentMonFrameAccumulator::Reset() noexcept
{
    firstDisplayedFrameEmitted_ = false;
    displayedFrameCount_ = 0;
    displayedElapsedMs_ = 0.0;
}

PresentMonHudTelemetry::~PresentMonHudTelemetry()
{
    Stop();
}

DWORD PresentMonHudTelemetry::ExitCode() const noexcept
{
    if (!process_)
        return 0;
    DWORD exitCode = STILL_ACTIVE;
    if (!GetExitCodeProcess(process_, &exitCode))
        return 0;
    return exitCode;
}

std::wstring BuildPresentMonCommandLine(const std::wstring& executable,
    DWORD processId, const std::wstring& sessionName)
{
    return L"\"" + executable + L"\" --process_id " +
        std::to_wstring(processId) + L" --output_stdout --no_console_stats --qpc_time_ms" +
        L" --track_frame_type --terminate_on_proc_exit --stop_existing_session --session_name \"" +
        sessionName + L"\"";
}

bool PresentMonHudTelemetry::Start(const std::wstring& executable, DWORD processId,
    UpdateCallback callback)
{
    Stop();
    if (executable.empty() || !processId)
        return false;
    std::error_code fileError;
    const bool presentMonExists = std::filesystem::exists(executable, fileError);
    if (fileError)
    {
        RuntimeLogger::Log(RuntimeLogLevel::Error,
            L"PresentMon executable check failed error=" +
            std::to_wstring(fileError.value()));
        return false;
    }
    if (!presentMonExists)
    {
        RuntimeLogger::Log(RuntimeLogLevel::Error, L"PresentMon executable missing");
        return false;
    }

    SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
    HANDLE outputRead{}, outputWrite{};
    if (!CreatePipe(&outputRead, &outputWrite, &security, 0))
    {
        RuntimeLogger::Log(RuntimeLogLevel::Error,
            L"CreatePipe failed error=" + std::to_wstring(GetLastError()));
        return false;
    }
    SetHandleInformation(outputRead, HANDLE_FLAG_INHERIT, 0);

    sessionName_ =
        L"ClawHUD-HUD-" + std::to_wstring(processId);
    processId_ = processId;
    std::wstring command = BuildPresentMonCommandLine(
        executable, processId, sessionName_);
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
        RuntimeLogger::Log(RuntimeLogLevel::Error,
            L"CreateProcess failed error=" + std::to_wstring(createError));
        CloseHandle(outputRead);
        sessionName_.clear();
        processId_ = 0;
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

DWORD PresentMonHudTelemetry::Stop() noexcept
{
    stop_ = true;

    if (process_ && WaitForSingleObject(process_, 0) == WAIT_TIMEOUT)
    {
        StopTraceSession(sessionName_);
        if (WaitForSingleObject(process_, kPresentMonGracefulStopMs) == WAIT_TIMEOUT)
        {
            clawhud::RuntimeLogger::Log(clawhud::RuntimeLogLevel::Debug,
                L"[PresentMon] stop.force");
            if (!TerminateProcess(process_, kPresentMonForcedExitCode))
            {
                const DWORD error = GetLastError();
                clawhud::RuntimeLogger::Log(clawhud::RuntimeLogLevel::Warn,
                    L"[PresentMon] stop.force-failed error=" +
                    std::to_wstring(error));
            }
            const DWORD confirmation = WaitForSingleObject(
                process_, kPresentMonForcedStopConfirmMs);
            if (confirmation == WAIT_TIMEOUT)
                clawhud::RuntimeLogger::Log(clawhud::RuntimeLogLevel::Warn,
                    L"[PresentMon] stop.confirmation-timeout");
            else if (confirmation == WAIT_FAILED)
                clawhud::RuntimeLogger::Log(clawhud::RuntimeLogLevel::Warn,
                    L"[PresentMon] stop.confirmation-failed error=" +
                    std::to_wstring(GetLastError()));
        }
    }

    DWORD finalExitCode{};
    if (process_)
        (void)GetExitCodeProcess(process_, &finalExitCode);

    if (worker_.joinable())
        worker_.join();
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
    processId_ = 0;
    callback_ = {};
    return finalExitCode;
}

void PresentMonHudTelemetry::ReadLoop()
{
    const HANDLE output = output_;
    std::vector<std::string> headers;
    PresentMonFrameAccumulator accumulator;
    std::string pending;
    auto consumeLine = [&](std::string line)
    {
        if (headers.empty())
        {
            if (line.size() >= 3 &&
                static_cast<unsigned char>(line[0]) == 0xEF &&
                static_cast<unsigned char>(line[1]) == 0xBB &&
                static_cast<unsigned char>(line[2]) == 0xBF)
                line.erase(0, 3);
            const auto candidate = CsvLine(line);
            if (Column(candidate, "Application") &&
                Column(candidate, "ProcessID") &&
                Column(candidate, "MsBetweenDisplayChange"))
                headers = candidate;
            else if (line.find("error") != std::string::npos ||
                line.find("Error") != std::string::npos ||
                line.find("failed") != std::string::npos ||
                line.find("Failed") != std::string::npos)
                RuntimeLogger::Log(RuntimeLogLevel::Warn,
                    L"PresentMon error pid=" + std::to_wstring(processId_) +
                    L" message=\"" + std::wstring(line.begin(), line.end()) + L"\"");
            return;
        }

        const auto frame = ParseDisplayedFrame(headers, CsvLine(line));
        if (!frame)
            return;
        if (!callback_)
            return;
        for (const auto& event : accumulator.Observe(*frame))
            callback_(event);
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
            if (newline == std::string::npos)
                break;
            std::string line = pending.substr(0, newline);
            pending.erase(0, newline + 1);
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            if (!line.empty())
                consumeLine(std::move(line));
        }
    }
    if (!stop_ && callback_)
    {
        const PresentMonHudEvent event{
            PresentMonHudEventType::StreamEnded, std::nullopt};
        callback_(event);
    }
}
}
