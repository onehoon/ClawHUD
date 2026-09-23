#include "VrrTargetAcquisition.h"

#include "DiagBlocklist.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <thread>
#include <vector>

namespace
{
constexpr auto kForegroundPollInterval = std::chrono::milliseconds(100);
constexpr auto kRendererVerificationTimeout = std::chrono::seconds(3);

class ScopedHandle
{
public:
    explicit ScopedHandle(HANDLE handle) noexcept : handle_(handle) {}
    ~ScopedHandle() { if (handle_ && handle_ != INVALID_HANDLE_VALUE) CloseHandle(handle_); }
    ScopedHandle(const ScopedHandle&) = delete;
    ScopedHandle& operator=(const ScopedHandle&) = delete;
    HANDLE Get() const noexcept { return handle_; }

private:
    HANDLE handle_{};
};

std::wstring_view Trim(std::wstring_view value) noexcept
{
    while (!value.empty() && (value.front() == L' ' || value.front() == L'\t' ||
        value.front() == L'\r' || value.front() == L'\n')) value.remove_prefix(1);
    while (!value.empty() && (value.back() == L' ' || value.back() == L'\t' ||
        value.back() == L'\r' || value.back() == L'\n')) value.remove_suffix(1);
    return value;
}

wchar_t FoldAscii(wchar_t value) noexcept
{
    return value >= L'A' && value <= L'Z'
        ? static_cast<wchar_t>(value + (L'a' - L'A')) : value;
}

std::uint64_t CreationFileTime(HANDLE process) noexcept
{
    FILETIME creation{}, exit{}, kernel{}, user{};
    if (!process || !GetProcessTimes(process, &creation, &exit, &kernel, &user)) return 0;
    ULARGE_INTEGER value{};
    value.LowPart = creation.dwLowDateTime;
    value.HighPart = creation.dwHighDateTime;
    return value.QuadPart;
}

bool IsForegroundProcess(DWORD processId) noexcept
{
    const HWND foreground = GetForegroundWindow();
    DWORD foregroundProcessId{};
    return foreground && GetWindowThreadProcessId(foreground, &foregroundProcessId) != 0 &&
        foregroundProcessId == processId;
}

bool ReadMonitor(HWND window, HMONITOR& monitor, MONITORINFOEXW& info) noexcept
{
    monitor = MonitorFromWindow(window, MONITOR_DEFAULTTONULL);
    if (!monitor) return false;
    info = {};
    info.cbSize = sizeof(info);
    return GetMonitorInfoW(monitor, reinterpret_cast<MONITORINFO*>(&info)) != FALSE;
}
}

bool SameVrrProcessGeneration(const VrrProcessIdentity& left,
    const VrrProcessIdentity& right) noexcept
{
    return left.processId != 0 && left.creationFileTime != 0 &&
        left.processId == right.processId &&
        left.creationFileTime == right.creationFileTime;
}

bool VrrExecutableIsBlocked(std::wstring_view executableName,
    std::string_view blocklist) noexcept
{
    executableName = Trim(executableName);
    if (executableName.empty()) return true;

    std::size_t offset{};
    while (offset <= blocklist.size())
    {
        const auto end = blocklist.find_first_of("\r\n", offset);
        const auto length = end == std::string_view::npos ? blocklist.size() - offset : end - offset;
        auto line = blocklist.substr(offset, length);
        while (!line.empty() && (line.front() == ' ' || line.front() == '\t')) line.remove_prefix(1);
        while (!line.empty() && (line.back() == ' ' || line.back() == '\t')) line.remove_suffix(1);
        if (!line.empty() && line.front() != '#')
        {
            bool equal = line.size() == executableName.size();
            for (std::size_t i = 0; equal && i < line.size(); ++i)
            {
                const auto token = static_cast<unsigned char>(line[i]);
                const wchar_t tokenChar = static_cast<wchar_t>(token);
                equal = FoldAscii(tokenChar) == FoldAscii(executableName[i]);
            }
            if (equal) return true;
        }
        if (end == std::string_view::npos) break;
        offset = end + 1;
        if (blocklist[end] == '\r' && offset < blocklist.size() && blocklist[offset] == '\n') ++offset;
    }
    return false;
}

bool VrrWindowIsEligible(HWND window) noexcept
{
    return window && IsWindow(window) && GetAncestor(window, GA_ROOT) == window &&
        IsWindowVisible(window) && GetWindow(window, GW_OWNER) == nullptr && !IsIconic(window);
}

bool VrrHasDisplayedFrameEvidence(const std::vector<VrrFrameSample>& samples,
    DWORD processId) noexcept
{
    return std::any_of(samples.begin(), samples.end(), [processId](const auto& sample)
    {
        return sample.processId == processId && sample.swapChainAddress != 0 &&
            sample.betweenDisplayChangeMs && std::isfinite(*sample.betweenDisplayChangeMs) &&
            *sample.betweenDisplayChangeMs > 0.0;
    });
}

std::optional<VrrProcessIdentity> QueryVrrProcessIdentity(DWORD processId)
{
    if (!processId) return std::nullopt;
    ScopedHandle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId));
    if (!process.Get()) return std::nullopt;

    std::optional<VrrProcessIdentity> result;
    const auto creation = CreationFileTime(process.Get());
    std::vector<wchar_t> path(32768);
    DWORD pathLength = static_cast<DWORD>(path.size());
    if (creation && QueryFullProcessImageNameW(process.Get(), 0, path.data(), &pathLength))
    {
        VrrProcessIdentity identity;
        identity.processId = processId;
        identity.creationFileTime = creation;
        identity.imagePath.assign(path.data(), pathLength);
        identity.executableName = std::filesystem::path(identity.imagePath).filename().wstring();
        if (!identity.executableName.empty()) result = std::move(identity);
    }
    return result;
}

bool VrrProcessGenerationIsAlive(const VrrProcessIdentity& identity) noexcept
{
    if (!identity.processId || !identity.creationFileTime) return false;
    ScopedHandle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, identity.processId));
    return process.Get() && CreationFileTime(process.Get()) == identity.creationFileTime;
}

VrrTargetAcquisition::VrrTargetAcquisition(DWORD diagnosticProcessId) noexcept
    : diagnosticProcessId_(diagnosticProcessId)
{
    launchContext_.foregroundWindow = GetForegroundWindow();
    if (launchContext_.foregroundWindow)
        GetWindowThreadProcessId(launchContext_.foregroundWindow, &launchContext_.foregroundProcessId);
}

VrrTargetAcquireResult VrrTargetAcquisition::Acquire(VrrApi2FrameCapture& frameCapture,
    std::chrono::milliseconds timeout) const
{
    if (!frameCapture.Ready()) return { VrrTargetAcquireStatus::ApiUnavailable, std::nullopt };

    struct RejectedCandidate
    {
        HWND window{};
        VrrProcessIdentity process;
    };
    std::optional<RejectedCandidate> rejected;
    const auto end = std::chrono::steady_clock::now() + std::max(timeout, std::chrono::milliseconds::zero());

    while (std::chrono::steady_clock::now() < end)
    {
        const HWND window = GetForegroundWindow();
        DWORD processId{};
        if (!window || !GetWindowThreadProcessId(window, &processId) ||
            processId == diagnosticProcessId_ || !VrrWindowIsEligible(window))
        {
            std::this_thread::sleep_for(kForegroundPollInterval);
            continue;
        }

        auto process = QueryVrrProcessIdentity(processId);
        if (!process || VrrExecutableIsBlocked(process->executableName, kDiagPresentMonBlocklist))
        {
            std::this_thread::sleep_for(kForegroundPollInterval);
            continue;
        }
        if (rejected && rejected->window == window &&
            SameVrrProcessGeneration(rejected->process, *process))
        {
            std::this_thread::sleep_for(kForegroundPollInterval);
            continue;
        }

        if (!frameCapture.StartTracking(processId))
        {
            rejected = RejectedCandidate{ window, std::move(*process) };
            std::this_thread::sleep_for(kForegroundPollInterval);
            continue;
        }

        bool candidateFailed{};
        const auto verificationEnd = std::min(end,
            std::chrono::steady_clock::now() + kRendererVerificationTimeout);
        while (std::chrono::steady_clock::now() < verificationEnd)
        {
            if (!IsForegroundProcess(processId) || !VrrProcessGenerationIsAlive(*process))
            {
                candidateFailed = true;
                break;
            }
            if (!frameCapture.DrainFrames())
            {
                frameCapture.StopTracking();
                return { VrrTargetAcquireStatus::CaptureFailed, std::nullopt };
            }
            if (VrrHasDisplayedFrameEvidence(frameCapture.Samples(), processId))
            {
                const HWND currentWindow = GetForegroundWindow();
                DWORD currentProcessId{};
                HMONITOR currentMonitor{};
                MONITORINFOEXW currentMonitorInfo{};
                auto currentProcess = QueryVrrProcessIdentity(processId);
                if (currentWindow && GetWindowThreadProcessId(currentWindow, &currentProcessId) &&
                    currentProcessId == processId && currentProcess &&
                    SameVrrProcessGeneration(*process, *currentProcess) &&
                    VrrWindowIsEligible(currentWindow))
                {
                    if (!ReadMonitor(currentWindow, currentMonitor, currentMonitorInfo))
                    {
                        frameCapture.StopTracking();
                        return { VrrTargetAcquireStatus::MonitorUnavailable, std::nullopt };
                    }
                    return { VrrTargetAcquireStatus::Locked,
                        VrrLockedTarget{ currentWindow, std::move(*currentProcess),
                            currentMonitor, currentMonitorInfo } };
                }
                candidateFailed = true;
                break;
            }
            std::this_thread::sleep_for(kForegroundPollInterval);
        }

        frameCapture.StopTracking();
        rejected = RejectedCandidate{ window, std::move(*process) };
        if (!candidateFailed && std::chrono::steady_clock::now() >= end) break;
    }
    return { VrrTargetAcquireStatus::TimedOut, std::nullopt };
}
