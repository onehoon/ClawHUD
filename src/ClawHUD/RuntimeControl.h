#pragma once

#include <optional>

#include "HudModel.h"
#include "Tweaks/IntelVrr/IntelVrrRunResult.h"

namespace clawhud
{
// Authoritative, frontend-facing view of ClawHUD runtime settings. A fresh
// snapshot is obtained for every UI refresh; it reflects current effective
// state, including any rollback the runtime performed on a failed mutation.
//
// This is an in-process semantic type only. It is NOT the IPC wire format;
// CH-RTF-4 defines the versioned wire structs separately.
struct RuntimeSettingsSnapshot
{
    bool startWithWindows{};
    bool hudEnabled{};
    int hudSizeOffset{};
    HudFont hudFont{};
    HudLayoutOptions hudOptions{};
    bool intelVrrRangeFixEnabled{};
    std::optional<IntelVrrRunResult> intelVrrLastResult;
};

// The semantic control boundary every ClawHUD frontend uses. Implemented by the
// runtime composition root (App); callers never touch HudController,
// HudPresentation, telemetry, game detection, or HudSettingsStore directly and
// never persist settings themselves.
class IRuntimeControl
{
public:
    virtual ~IRuntimeControl() = default;

    virtual RuntimeSettingsSnapshot GetSettingsSnapshot() const = 0;

    virtual void SetStartWithWindows(bool enabled) = 0;
    virtual bool SetHudEnabled(bool enabled) = 0;
    virtual void SetHudVisibilityMode(HudVisibilityMode mode) = 0;
    virtual void SetHudSizeOffset(int offset) = 0;
    virtual void SetHudFont(HudFont font) = 0;
    virtual void SetHudAlignment(HudAlignment alignment) = 0;
    virtual void SetHudBackgroundMode(HudBackgroundMode mode) = 0;

    // Background-only opacity. Preview applies live without persisting (slider
    // drag); commit applies and persists the final value.
    virtual bool PreviewHudOpacity(float opacity) = 0;
    virtual bool CommitHudOpacity(float opacity) = 0;

    virtual void SetIntelVrrRangeFixEnabled(bool enabled) = 0;
};
}
