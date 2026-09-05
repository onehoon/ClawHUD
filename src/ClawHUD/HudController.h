#pragma once

#include <windows.h>

#include <functional>
#include <memory>
#include <optional>

#include "HudModel.h"
#include "HudPresentationLifecycle.h"
#include "HudSize.h"

namespace clawhud
{
class HudPresentation;

// Persisted HUD state that App loads from HudSettingsStore and seeds into the
// controller at startup. Persistence itself stays in App.
struct HudControllerState
{
    bool enabled{};
    HudLayoutOptions options{};
    HudFont font{HudFont::SegoeUiVariable};
    int sizeOffset{};
};

// What a visibility reconcile wants App to do with the production telemetry
// sampling lifecycle. The controller never starts/stops sampling itself.
struct HudVisibilityEffects
{
    bool startProductionSampling{};
    bool stopProductionSampling{};
};

// Owns the HUD user state (enabled / options / font / size / manual visibility
// override), the existing concrete HudPresentation object, and every
// Initialize / Render / Show / Hide / Shutdown call site plus the recreate +
// rollback lifecycle and the presentation failure log latches.
//
// HudPresentation stays a VRR-critical black box and is allocated lazily: the
// controller exists from App construction but creates no D3D/DComp/presentation
// resource until Ensure(). The controller never touches telemetry, game
// detection, settings UI, or suspend/resume policy. App drives it with explicit
// inputs and, via the render callback, supplies fresh telemetry snapshots.
class HudController
{
public:
    explicit HudController(HINSTANCE instance);
    ~HudController();

    HudController(const HudController&) = delete;
    HudController& operator=(const HudController&) = delete;

    // requestRender(allowHidden): App fetches a fresh HudTelemetrySnapshot and
    // calls back into Render(snapshot, allowHidden). Bound once at startup.
    void SetRenderCallback(std::function<void(bool)> requestRender);

    void RestoreState(const HudControllerState& state);

    // --- queries (App forwards these to the Settings frontend / tray / recovery) --
    bool Enabled() const noexcept { return enabled_; }
    bool Visible() const noexcept;
    bool HasPresentation() const noexcept { return presentation_ != nullptr; }
    int SizeOffset() const noexcept { return sizeOffset_; }
    const HudLayoutOptions& Options() const noexcept { return options_; }
    HudFont Font() const noexcept { return font_; }
    HudVisibilityMode VisibilityMode() const noexcept { return options_.visibilityMode; }
    std::optional<bool> ManualOverride() const noexcept { return manualOverride_; }

    HudRenderOptions BuildRenderOptions() const;

    // --- presentation lifecycle ------------------------------------------
    bool Ensure();                 // create + Initialize the existing HudPresentation
    void ShutdownPresentation();   // Shutdown() then release (matches StopHud)
    void DestroyPresentation();    // release only (matches the App destructor)
    bool Recreate(bool restoreVisible);
    void Render(const HudTelemetrySnapshot& snapshot, bool allowHidden);
    HRESULT RenderRecoveryFrame(); // resume recovery: render an empty snapshot
    // --- enabled state (App owns persistence + cross-domain reactions) ----
    void MarkEnabled(bool logTransition);
    void MarkDisabled();           // logs, resets the manual override (StopHud)
    void AbandonEnable() noexcept; // startup Ensure() failure: bare enabled_=false
    void SetManualOverride(bool visible) { manualOverride_ = visible; }
    void ResetManualOverride() { manualOverride_.reset(); }

    // --- HUD setting mutations; true => App should persist ---------------
    bool SetAlignment(HudAlignment alignment);
    bool SetFont(HudFont font);            // true => App persists + refreshes Settings UI
    bool SetBackgroundMode(HudBackgroundMode mode);
    bool SetSizeOffset(int offset);
    // false => presentation SetHudOpacity failed (App returns false, no persist).
    bool SetOpacity(float opacity);
    HudVisibilityMode SetVisibilityMode(HudVisibilityMode mode); // returns previous

    // --- visibility -----------------------------------------------------
    void HideForLifecycleGate();   // suspend / resume-recovery active
    void HideForSuspend();         // logs "HUD suspended" / warn on failure
    void HideForResumeFallback();  // missed-suspend fallback: silent Hide if visible
    HudVisibilityEffects ReconcileVisibility(bool foregroundGameActive);

private:
    void Refresh();  // render current snapshot iff enabled + visible

    HINSTANCE instance_{};
    std::function<void(bool)> requestRender_;
    std::unique_ptr<HudPresentation> presentation_;
    HudLayoutOptions options_{};
    HudFont font_{HudFont::SegoeUiVariable};
    int sizeOffset_{};
    bool enabled_{};
    std::optional<bool> manualOverride_;
    bool initializedLogged_{};
    bool renderFailureLogged_{};
    bool showFailureLogged_{};
    bool hideFailureLogged_{};
};
}
