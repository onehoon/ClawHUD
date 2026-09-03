#include "PresentMonRuntimeStartupPolicy.h"

#include <cassert>
#include <string>

// Cleanup 2 (work order 11.1): every PresentMonRuntimeBootstrapResult must have
// an explicit startup meaning. The bootstrap result is no longer ignored.

using clawhud::PresentMonRuntimeBootstrapResult;
using clawhud::PresentMonRuntimeStartupAction;
using clawhud::PresentMonRuntimeStartupActionForResult;
using clawhud::PresentMonRuntimeBootstrapResultName;

static_assert(PresentMonRuntimeStartupActionForResult(
    PresentMonRuntimeBootstrapResult::AlreadyReady) ==
    PresentMonRuntimeStartupAction::Continue);
static_assert(PresentMonRuntimeStartupActionForResult(
    PresentMonRuntimeBootstrapResult::Installed) ==
    PresentMonRuntimeStartupAction::Continue);
static_assert(PresentMonRuntimeStartupActionForResult(
    PresentMonRuntimeBootstrapResult::InstalledRebootRequired) ==
    PresentMonRuntimeStartupAction::ExitInformational);
static_assert(PresentMonRuntimeStartupActionForResult(
    PresentMonRuntimeBootstrapResult::ElevationCancelled) ==
    PresentMonRuntimeStartupAction::ExitInformational);
static_assert(PresentMonRuntimeStartupActionForResult(
    PresentMonRuntimeBootstrapResult::MsiMissing) ==
    PresentMonRuntimeStartupAction::ExitFailure);
static_assert(PresentMonRuntimeStartupActionForResult(
    PresentMonRuntimeBootstrapResult::InstallTimedOut) ==
    PresentMonRuntimeStartupAction::ExitFailure);
static_assert(PresentMonRuntimeStartupActionForResult(
    PresentMonRuntimeBootstrapResult::InstallFailed) ==
    PresentMonRuntimeStartupAction::ExitFailure);
static_assert(PresentMonRuntimeStartupActionForResult(
    PresentMonRuntimeBootstrapResult::ValidationFailed) ==
    PresentMonRuntimeStartupAction::ExitFailure);

int main()
{
    // Continue
    assert(PresentMonRuntimeStartupActionForResult(
        PresentMonRuntimeBootstrapResult::AlreadyReady) ==
        PresentMonRuntimeStartupAction::Continue);
    assert(PresentMonRuntimeStartupActionForResult(
        PresentMonRuntimeBootstrapResult::Installed) ==
        PresentMonRuntimeStartupAction::Continue);

    // ExitInformational
    assert(PresentMonRuntimeStartupActionForResult(
        PresentMonRuntimeBootstrapResult::InstalledRebootRequired) ==
        PresentMonRuntimeStartupAction::ExitInformational);
    assert(PresentMonRuntimeStartupActionForResult(
        PresentMonRuntimeBootstrapResult::ElevationCancelled) ==
        PresentMonRuntimeStartupAction::ExitInformational);

    // ExitFailure
    assert(PresentMonRuntimeStartupActionForResult(
        PresentMonRuntimeBootstrapResult::MsiMissing) ==
        PresentMonRuntimeStartupAction::ExitFailure);
    assert(PresentMonRuntimeStartupActionForResult(
        PresentMonRuntimeBootstrapResult::InstallTimedOut) ==
        PresentMonRuntimeStartupAction::ExitFailure);
    assert(PresentMonRuntimeStartupActionForResult(
        PresentMonRuntimeBootstrapResult::InstallFailed) ==
        PresentMonRuntimeStartupAction::ExitFailure);
    assert(PresentMonRuntimeStartupActionForResult(
        PresentMonRuntimeBootstrapResult::ValidationFailed) ==
        PresentMonRuntimeStartupAction::ExitFailure);

    assert(std::wstring(PresentMonRuntimeBootstrapResultName(
        PresentMonRuntimeBootstrapResult::InstallTimedOut)) == L"InstallTimedOut");
    assert(std::wstring(PresentMonRuntimeBootstrapResultName(
        PresentMonRuntimeBootstrapResult::AlreadyReady)) == L"AlreadyReady");
    return 0;
}
