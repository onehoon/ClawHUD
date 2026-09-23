#pragma once

#include "VrrApi2FrameCapture.h"

#include <windows.h>

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

struct VrrProcessIdentity
{
    DWORD processId{};
    std::uint64_t creationFileTime{};
    std::wstring imagePath;
    std::wstring executableName;
};

bool SameVrrProcessGeneration(const VrrProcessIdentity& left,
    const VrrProcessIdentity& right) noexcept;
bool VrrExecutableIsBlocked(std::wstring_view executableName,
    std::string_view blocklist) noexcept;
bool VrrWindowIsEligible(HWND window) noexcept;
bool VrrHasDisplayedFrameEvidence(const std::vector<VrrFrameSample>& samples,
    DWORD processId) noexcept;
std::optional<VrrProcessIdentity> QueryVrrProcessIdentity(DWORD processId);
bool VrrProcessGenerationIsAlive(const VrrProcessIdentity& identity) noexcept;

struct VrrLaunchContext
{
    HWND foregroundWindow{};
    DWORD foregroundProcessId{};
};

struct VrrLockedTarget
{
    HWND window{};
    VrrProcessIdentity process;
    HMONITOR monitor{};
    MONITORINFOEXW monitorInfo{};
};

enum class VrrTargetAcquireStatus
{
    Locked,
    TimedOut,
    ApiUnavailable,
    CaptureFailed,
    MonitorUnavailable,
};

struct VrrTargetAcquireResult
{
    VrrTargetAcquireStatus status{ VrrTargetAcquireStatus::TimedOut };
    std::optional<VrrLockedTarget> target;
};

class VrrTargetAcquisition
{
public:
    explicit VrrTargetAcquisition(DWORD diagnosticProcessId = GetCurrentProcessId()) noexcept;

    const VrrLaunchContext& LaunchContext() const noexcept { return launchContext_; }
    DWORD DiagnosticProcessId() const noexcept { return diagnosticProcessId_; }

    VrrTargetAcquireResult Acquire(VrrApi2FrameCapture& frameCapture,
        std::chrono::milliseconds timeout = std::chrono::seconds(60)) const;

private:
    DWORD diagnosticProcessId_{};
    VrrLaunchContext launchContext_{};
};
