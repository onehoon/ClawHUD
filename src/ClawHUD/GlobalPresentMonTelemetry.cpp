#include "GlobalPresentMonTelemetry.h"
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
constexpr DWORD kGracefulStopMs = 150;
constexpr DWORD kForcedStopConfirmMs = 250;
constexpr UINT kForcedExitCode = 2;

std::optional<std::size_t> Column(const std::vector<std::string>& headers,
    const char* name)
{
    const auto it = std::find(headers.begin(), headers.end(), name);
    return it == headers.end() ? std::nullopt
        : std::optional<std::size_t>(static_cast<std::size_t>(it - headers.begin()));
}

std::string Field(const std::vector<std::string>& row,
    std::optional<std::size_t> index)
{
    return index && *index < row.size() ? row[*index] : std::string{};
}

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
            fields.push_back(std::move(field));
            field.clear();
        }
        else
            field += line[i];
    }
    fields.push_back(std::move(field));
    return fields;
}

void StopTraceSession(const std::wstring& sessionName) noexcept
{
    if (!sessionName.empty())
    {
        struct TraceStopProperties
        {
            EVENT_TRACE_PROPERTIES properties{};
            wchar_t loggerName[128]{};
        } buffer{};
        buffer.properties.Wnode.BufferSize = sizeof(buffer);
        buffer.properties.LoggerNameOffset =
            static_cast<ULONG>(offsetof(TraceStopProperties, loggerName));
        (void)ControlTraceW(0, sessionName.c_str(), &buffer.properties,
            EVENT_TRACE_CONTROL_STOP);
    }
}
}

std::optional<GlobalPresentFrame> ParseGlobalPresentFrame(
    const std::vector<std::string>& headers,
    const std::vector<std::string>& row,
    std::uint64_t observedTick)
{
    const auto intervalText = Field(row, Column(headers, "MsBetweenDisplayChange"));
    const auto processText = Field(row, Column(headers, "ProcessID"));
    const auto application = Field(row, Column(headers, "Application"));
    const auto swapChainText = Field(row, Column(headers, "SwapChainAddress"));
    if (intervalText.empty() || intervalText == "NA" || processText.empty() ||
        application.empty() || swapChainText.empty())
        return std::nullopt;
    try
    {
        const double interval = std::stod(intervalText);
        if (!std::isfinite(interval) || interval <= 0.0)
            return std::nullopt;
        std::size_t processParsed{};
        const auto processValue = std::stoul(processText, &processParsed);
        if (processParsed != processText.size() ||
            processValue > static_cast<unsigned long>(~DWORD{}))
            return std::nullopt;
        const DWORD processId = static_cast<DWORD>(processValue);
        std::size_t parsed{};
        const auto swapChain = std::stoull(swapChainText, &parsed, 0);
        if (!processId || parsed != swapChainText.size())
            return std::nullopt;
        return GlobalPresentFrame{
            processId,
            std::wstring(application.begin(), application.end()),
            swapChain,
            interval,
            Field(row, Column(headers, "FrameType")),
            observedTick};
    }
    catch (...)
    {
        return std::nullopt;
    }
}

std::wstring BuildGlobalPresentMonCommandLine(
    const std::wstring& executable, const std::wstring& sessionName)
{
    return L"\"" + executable +
        L"\" --output_stdout --no_console_stats --qpc_time_ms"
        L" --track_frame_type --stop_existing_session --session_name \"" +
        sessionName + L"\"";
}

GlobalPresentMonTelemetry::~GlobalPresentMonTelemetry()
{
    Stop();
}

bool GlobalPresentMonTelemetry::Start(const std::wstring& executable,
    UpdateCallback callback)
{
    Stop();
    if (executable.empty() || !callback)
        return false;
    std::error_code fileError;
    if (!std::filesystem::exists(executable, fileError) || fileError)
        return false;

    SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
    HANDLE outputRead{}, outputWrite{};
    if (!CreatePipe(&outputRead, &outputWrite, &security, 0))
        return false;
    SetHandleInformation(outputRead, HANDLE_FLAG_INHERIT, 0);

    sessionName_ = L"ClawHUD-Renderer";
    const auto command = BuildGlobalPresentMonCommandLine(executable, sessionName_);
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
        sessionName_.clear();
        return false;
    }
    CloseHandle(processInfo.hThread);
    process_ = processInfo.hProcess;
    output_ = outputRead;
    callback_ = std::move(callback);
    stop_ = false;
    worker_ = std::thread(&GlobalPresentMonTelemetry::ReadLoop, this);
    return true;
}

DWORD GlobalPresentMonTelemetry::Stop() noexcept
{
    stop_ = true;
    if (process_ && WaitForSingleObject(process_, 0) == WAIT_TIMEOUT)
    {
        StopTraceSession(sessionName_);
        if (WaitForSingleObject(process_, kGracefulStopMs) == WAIT_TIMEOUT)
        {
            if (!TerminateProcess(process_, kForcedExitCode))
                RuntimeLogger::Log(RuntimeLogLevel::Warn,
                    L"[RendererTelemetry] stop.force-failed error=" +
                    std::to_wstring(GetLastError()));
            (void)WaitForSingleObject(process_, kForcedStopConfirmMs);
        }
    }
    DWORD exitCode{};
    if (process_)
        (void)GetExitCodeProcess(process_, &exitCode);
    if (worker_.joinable())
        worker_.join();
    if (output_)
        CloseHandle(output_);
    if (process_)
        CloseHandle(process_);
    output_ = nullptr;
    process_ = nullptr;
    callback_ = {};
    sessionName_.clear();
    return exitCode;
}

void GlobalPresentMonTelemetry::ReadLoop()
{
    const HANDLE output = output_;
    std::vector<std::string> headers;
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
                Column(candidate, "SwapChainAddress") &&
                Column(candidate, "MsBetweenDisplayChange"))
                headers = candidate;
            return;
        }
        const auto frame = ParseGlobalPresentFrame(
            headers, CsvLine(line), GetTickCount64());
        if (frame && callback_)
        {
            try
            {
                callback_({GlobalPresentMonEvent::Type::DisplayedFrame, *frame});
            }
            catch (...)
            {
            }
        }
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
        try
        {
            callback_({GlobalPresentMonEvent::Type::StreamEnded, {}});
        }
        catch (...)
        {
        }
    }
}
}
