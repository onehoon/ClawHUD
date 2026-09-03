#pragma once

#include <windows.h>

#include <filesystem>

namespace clawhud
{
enum class PresentMonRuntimeBootstrapResult
{
    AlreadyReady,
    Installed,
    InstalledRebootRequired,
    MsiMissing,
    ElevationCancelled,
    // ClawHUD's bounded wait on msiexec expired. The installer is deliberately
    // left running under Windows Installer ownership -- never TerminateProcess'd.
    InstallTimedOut,
    InstallFailed,
    ValidationFailed,
};

struct PresentMonRuntimeReadinessEvidence
{
    bool serviceRunning{};
    bool registryPathPresent{};
    bool middlewareExists{};
    bool middlewareNameValid{};
    bool compatible{};
};

enum class PresentMonRuntimeMsiExit
{
    SuccessCandidate,
    RebootRequiredCandidate,
    Failed,
};

// Outcome of ClawHUD's bounded WaitForSingleObject on the msiexec process.
// TimedOut must NOT lead to TerminateProcess: Windows Installer keeps ownership
// of the in-flight transaction and the next launch re-validates readiness.
enum class InstallerWaitOutcome
{
    Completed,
    TimedOut,
    Failed,
};

constexpr InstallerWaitOutcome ClassifyInstallerWait(DWORD waitResult) noexcept
{
    if (waitResult == WAIT_OBJECT_0)
        return InstallerWaitOutcome::Completed;
    if (waitResult == WAIT_TIMEOUT)
        return InstallerWaitOutcome::TimedOut;
    return InstallerWaitOutcome::Failed;
}

PresentMonRuntimeBootstrapResult EnsurePresentMonRuntime() noexcept;
bool IsPresentMonRuntimeReady() noexcept;
bool IsPresentMonRuntimeReady(
    const PresentMonRuntimeReadinessEvidence& evidence) noexcept;
std::filesystem::path PresentMonRuntimeMsiPathForModule(
    const std::filesystem::path& modulePath);
PresentMonRuntimeMsiExit ClassifyPresentMonRuntimeMsiExit(
    DWORD exitCode) noexcept;
}
