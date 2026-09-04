#include "HudController.h"

#include "HudPresentation.h"
#include "RuntimeLogger.h"
#include "Win32Format.h"

#include <cmath>
#include <string>

namespace clawhud
{
namespace
{
void Log(const std::wstring& message)
{
    RuntimeLogger::Log(RuntimeLogLevel::Info, message);
}
}

HudController::HudController(HINSTANCE instance) : instance_(instance) {}

HudController::~HudController() = default;

void HudController::SetRenderCallback(std::function<void(bool)> requestRender)
{
    requestRender_ = std::move(requestRender);
}

void HudController::SetPresentationWarmupScheduler(std::function<void()> scheduler)
{
    scheduleWarmup_ = std::move(scheduler);
}

void HudController::RestoreState(const HudControllerState& state)
{
    enabled_ = state.enabled;
    options_ = state.options;
    font_ = state.font;
    sizeOffset_ = state.sizeOffset;
}

bool HudController::Visible() const noexcept
{
    return presentation_ && presentation_->Visible();
}

HudRenderOptions HudController::BuildRenderOptions() const
{
    auto options = BuildHudRenderOptionsForSize(sizeOffset_, options_, font_);
    // The renderer remains opaque; the existing layered HWND owns HUD opacity.
    options.layout.backgroundOpacity = 1.0f;
    return options;
}

bool HudController::Ensure()
{
    if (!presentation_)
        presentation_ = std::make_unique<HudPresentation>();
    const auto options = BuildRenderOptions();
    HRESULT hr = presentation_->Initialize(instance_, options,
        options_.backgroundOpacity * 100.0f);
    if (FAILED(hr))
    {
        RuntimeLogger::Log(RuntimeLogLevel::Error,
            L"HUD initialization failed hr=" + HexHresult(hr));
        return false;
    }
    if (!initializedLogged_)
    {
        Log(L"HUD initialized");
        initializedLogged_ = true;
    }
    return true;
}

void HudController::ShutdownPresentation()
{
    if (presentation_)
    {
        presentation_->Shutdown();
        presentation_.reset();
    }
}

void HudController::DestroyPresentation()
{
    presentation_.reset();
}

bool HudController::Recreate(bool restoreVisible)
{
    if (!presentation_)
        return true;

    const bool wasInitialized = presentation_->Initialized();
    if (!wasInitialized && !enabled_)
        return true;

    const auto options = BuildRenderOptions();
    presentation_->Shutdown();
    HRESULT hr = presentation_->Initialize(instance_, options,
        options_.backgroundOpacity * 100.0f);
    if (FAILED(hr))
    {
        RuntimeLogger::Log(RuntimeLogLevel::Error,
            L"HUD presentation recreation failed hr=" + HexHresult(hr));
        return false;
    }
    if (enabled_ && requestRender_)
        requestRender_(true);
    if (ShouldRestoreHudVisibility(restoreVisible))
    {
        hr = presentation_->Show();
        if (FAILED(hr))
        {
            RuntimeLogger::Log(RuntimeLogLevel::Warn,
                L"HUD visibility restore failed hr=" + HexHresult(hr));
            return false;
        }
    }
    return true;
}

void HudController::Refresh()
{
    if (enabled_ && presentation_ && presentation_->Visible() && requestRender_)
        requestRender_(false);
}

void HudController::Render(const HudTelemetrySnapshot& snapshot, bool allowHidden)
{
    if (!enabled_ || !presentation_ ||
        (!allowHidden && !presentation_->Visible()))
        return;

    const auto options = BuildRenderOptions();
    const HRESULT hr = presentation_->Render(snapshot, options);
    if (FAILED(hr))
    {
        if (!renderFailureLogged_)
            RuntimeLogger::Log(RuntimeLogLevel::Error,
                L"HUD render failed hr=" + HexHresult(hr));
        renderFailureLogged_ = true;
    }
    else
        renderFailureLogged_ = false;

    if (!firstVisiblePresentationWarmupAttempted_ && scheduleWarmup_ &&
        ShouldScheduleFirstVisibleHudWarmup(
            firstVisiblePresentationWarmupAttempted_, presentation_->Visible(),
            !FormatHud(snapshot).empty(), hr))
    {
        // Set the one-shot before scheduling so the requestRender(true) inside
        // the deferred Recreate() cannot re-enter this branch.
        firstVisiblePresentationWarmupAttempted_ = true;
        scheduleWarmup_();
    }
}

HRESULT HudController::RenderRecoveryFrame()
{
    return presentation_->Render(HudTelemetrySnapshot{}, BuildRenderOptions());
}

void HudController::RunFirstVisiblePresentationWarmup()
{
    // The one-shot was already consumed when this was scheduled. Recreate once
    // through the exact production path; never re-arm, never loop.
    if (!presentation_)
        return;
    const bool restoreVisible = presentation_->Visible();
    Log(L"HUD first-visible presentation warm-up");
    if (!Recreate(restoreVisible))
        RuntimeLogger::Log(RuntimeLogLevel::Warn,
            L"HUD first-visible presentation warm-up recreate failed");
}

void HudController::MarkEnabled(bool logTransition)
{
    if (logTransition && !enabled_)
        Log(L"HUD enabled");
    enabled_ = true;
}

void HudController::MarkDisabled()
{
    if (enabled_) Log(L"HUD disabled");
    enabled_ = false;
    manualOverride_.reset();
}

void HudController::AbandonEnable() noexcept
{
    enabled_ = false;
}

bool HudController::SetAlignment(HudAlignment alignment)
{
    if (options_.alignment == alignment)
        return false;
    const auto previousAlignment = options_.alignment;
    options_.alignment = alignment;
    if (options_.backgroundMode == HudBackgroundMode::ContentWidth)
    {
        const bool restoreVisible = presentation_ &&
            presentation_->Initialized() && presentation_->Visible();
        const bool recreated = Recreate(restoreVisible);
        options_.alignment = CommitHudAlignmentAfterRecreation(
            previousAlignment, alignment, recreated);
        if (!recreated)
        {
            Recreate(restoreVisible);
            RuntimeLogger::Log(RuntimeLogLevel::Warn,
                L"HUD alignment change rolled back after presentation recreation failure");
            return true;
        }
    }
    else
    {
        Refresh();
    }
    return true;
}

bool HudController::SetFont(HudFont font)
{
    if (font_ == font)
        return false;
    const auto previousFont = font_;
    const bool restoreVisible = presentation_ &&
        presentation_->Initialized() && presentation_->Visible();
    font_ = font;
    if (!Recreate(restoreVisible))
    {
        font_ = previousFont;
        Recreate(restoreVisible);
        RuntimeLogger::Log(RuntimeLogLevel::Warn,
            L"HUD font change rolled back after presentation recreation failure");
        return false;
    }
    return true;
}

bool HudController::SetBackgroundMode(HudBackgroundMode mode)
{
    if (options_.backgroundMode == mode)
        return false;
    const auto previousMode = options_.backgroundMode;
    options_.backgroundMode = mode;
    const bool restoreVisible = presentation_ &&
        presentation_->Initialized() && presentation_->Visible();
    const bool recreated = Recreate(restoreVisible);
    options_.backgroundMode = CommitHudBackgroundModeAfterRecreation(
        previousMode, mode, recreated);
    if (!recreated)
    {
        Recreate(restoreVisible);
        RuntimeLogger::Log(RuntimeLogLevel::Warn,
            L"HUD background mode change rolled back after presentation recreation failure");
        return true;
    }
    return true;
}

bool HudController::SetSizeOffset(int offset)
{
    offset = ClampHudSizeOffset(offset);
    if (sizeOffset_ == offset)
        return false;

    const int previousOffset = sizeOffset_;
    const bool restoreVisible = presentation_ &&
        presentation_->Initialized() && presentation_->Visible();
    sizeOffset_ = offset;
    const bool recreated = Recreate(restoreVisible);
    sizeOffset_ = CommitHudSizeOffsetAfterRecreation(
        previousOffset, offset, recreated);
    if (!recreated)
    {
        Recreate(restoreVisible);
        RuntimeLogger::Log(RuntimeLogLevel::Warn,
            L"HUD size change rolled back after presentation recreation failure");
        return false;
    }
    Log(L"HUD presentation recreated");
    return true;
}

bool HudController::SetOpacity(float opacity)
{
    const long requestedPercent = static_cast<long>(std::lround(opacity * 100.0f));
    const long percent = ClampHudOpacityPercent(requestedPercent);
    const float newOpacity = static_cast<float>(percent) / 100.0f;
    if (options_.backgroundOpacity == newOpacity)
        return true;
    if (presentation_ && presentation_->Initialized())
    {
        const HRESULT hr = presentation_->SetHudOpacity(static_cast<float>(percent));
        if (FAILED(hr))
        {
            RuntimeLogger::Log(RuntimeLogLevel::Error,
                L"SetLayeredWindowAttributes for HUD opacity failed hr=" + HexHresult(hr));
            return false;
        }
    }
    options_.backgroundOpacity = newOpacity;
    return true;
}

HudVisibilityMode HudController::SetVisibilityMode(HudVisibilityMode mode)
{
    const auto previous = options_.visibilityMode;
    options_.visibilityMode = mode;
    manualOverride_.reset();
    return previous;
}

void HudController::HideForLifecycleGate()
{
    presentation_->Hide();
}

void HudController::HideForSuspend()
{
    if (presentation_ && presentation_->Visible())
    {
        if (SUCCEEDED(presentation_->Hide()))
            Log(L"HUD suspended");
        else
            RuntimeLogger::Log(RuntimeLogLevel::Warn,
                L"HUD suspend hide failed");
    }
}

void HudController::HideForResumeFallback()
{
    if (presentation_ && presentation_->Visible())
        presentation_->Hide();
}

HudVisibilityEffects HudController::ReconcileVisibility(bool foregroundGameActive)
{
    // Precondition (App-guaranteed): presentation_ != nullptr and neither
    // suspended nor resume-recovery-active (those route to HideForLifecycleGate).
    HudVisibilityEffects effects{};
    const bool resolvedShow = ResolveHudVisible(enabled_, manualOverride_,
        options_.visibilityMode, foregroundGameActive);
    if (resolvedShow)
    {
        const bool wasVisible = presentation_->Visible();
        const HRESULT hr = presentation_->Show();
        if (SUCCEEDED(hr))
        {
            if (!wasVisible) Log(L"HUD shown");
            showFailureLogged_ = false;
            // Old code: ShouldSampleProductionTelemetry(resolvedShow=true,
            // suspended_=false) is unconditionally true here.
            effects.startProductionSampling = true;
        }
        else
        {
            if (!showFailureLogged_)
                RuntimeLogger::Log(RuntimeLogLevel::Error,
                    L"HUD show failed hr=" + HexHresult(hr));
            showFailureLogged_ = true;
        }
    }
    else
    {
        const bool wasVisible = presentation_->Visible();
        const HRESULT hr = presentation_->Hide();
        if (SUCCEEDED(hr))
        {
            if (wasVisible) Log(L"HUD hidden");
            hideFailureLogged_ = false;
        }
        else if (!hideFailureLogged_)
            RuntimeLogger::Log(RuntimeLogLevel::Error,
                L"HUD hide failed hr=" + HexHresult(hr));
        if (FAILED(hr))
            hideFailureLogged_ = true;
        effects.stopProductionSampling = true;
    }
    return effects;
}
}
