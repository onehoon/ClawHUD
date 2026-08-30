#pragma once

#include <windows.h>

#include <cstdint>
#include <optional>

namespace clawhud
{
// A transient PresentMon "no sample" gap must not immediately remove the FPS
// section when the same target PID was just producing valid FPS.
inline constexpr ULONGLONG kFpsStaleHoldMs = 2000;

// Retains the last valid FPS for a single production FPS target PID across
// short API2 misses. The hold is target-local: a PID change or a hold that has
// aged past kFpsStaleHoldMs discards the retained value so it can never
// reappear for a different target or after a long gap.
//
// This lives above the provider/query layer and is evaluated on the existing
// 500 ms sampling tick; it adds no timer of its own.
class FpsStaleHold
{
public:
    // Records one poll for `processId` and returns the FPS the HUD should show
    // (nullopt => hide the FPS section). `freshFps` is the valid measured FPS
    // from this poll, or nullopt when the poll produced no usable value.
    std::optional<double> Observe(DWORD processId,
        std::optional<double> freshFps, ULONGLONG nowMs) noexcept;

    // Drops all retained state (foreground/target PID change, target cleared).
    void Reset() noexcept;

private:
    DWORD processId_{};
    std::optional<double> lastValidFps_;
    ULONGLONG lastValidTick_{};
};
}
