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
    NeedsInstall,
    MsiMissing,
    ElevationCancelled,
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

PresentMonRuntimeBootstrapResult EnsurePresentMonRuntime() noexcept;
bool IsPresentMonRuntimeReady() noexcept;
bool IsPresentMonRuntimeReady(
    const PresentMonRuntimeReadinessEvidence& evidence) noexcept;
std::filesystem::path PresentMonRuntimeMsiPathForModule(
    const std::filesystem::path& modulePath);
PresentMonRuntimeMsiExit ClassifyPresentMonRuntimeMsiExit(
    DWORD exitCode) noexcept;
}
