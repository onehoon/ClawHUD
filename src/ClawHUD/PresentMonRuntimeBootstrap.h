#pragma once

#include <windows.h>

#include <cstdint>
#include <filesystem>
#include <optional>

namespace clawhud
{
// Product revision of the installed PresentMon shared runtime, read from the
// middleware binary's version resource. This is the shared-runtime product
// version, independent of the API/ABI version reported by pmGetApiVersion.
struct RuntimeVersion
{
    std::uint16_t major{};
    std::uint16_t minor{};
    std::uint16_t patch{};
};

// Version floor only: true when `installed` is the same or newer than
// `required`. ABI compatibility is a separate predicate -- a newer compatible
// runtime is reused and never downgraded.
constexpr bool RuntimeVersionAtLeast(RuntimeVersion installed,
    RuntimeVersion required) noexcept
{
    if (installed.major != required.major)
        return installed.major > required.major;
    if (installed.minor != required.minor)
        return installed.minor > required.minor;
    return installed.patch >= required.patch;
}

// The bundled PresentMon runtime revision ClawHUD requires (from the CMake
// PRESENTMON_VERSION pin via generated PresentMonRuntimeVersion.h).
RuntimeVersion RequiredPresentMonRuntimeVersion() noexcept;

// Reads FileVersion from a PresentMon runtime binary's version resource.
std::optional<RuntimeVersion> ReadRuntimeVersionResource(
    const std::filesystem::path& binary) noexcept;

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
    bool compatible{};            // API/ABI compatible (pmGetApiVersion vs headers)
    bool versionFloorMet{};       // installed runtime revision >= required pin
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
