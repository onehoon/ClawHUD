#pragma once

#include "GlobalPresentMonTelemetry.h"

#include <cstdint>
#include <cstddef>
#include <optional>
#include <map>
#include <utility>
#include <string>

namespace clawhud
{
enum class RendererSelectionReason
{
    Microsoft,
    Foreground,
    Steam,
    Highest,
    None
};

struct RendererProcessState
{
    DWORD processId{};
    std::wstring application;
    std::optional<double> fps;
    bool hasDisplayedFrames{};
    std::uint64_t lastDisplayedFrameTick{};
    std::size_t fpsWindowFrames{};
    double fpsWindowMs{};
};

struct RendererTargetSelection
{
    DWORD processId{};
    std::wstring application;
    std::optional<double> fps;
    RendererSelectionReason reason{RendererSelectionReason::None};
};

constexpr std::uint64_t kRendererStaleThresholdMs = 1750;

std::wstring RendererSelectionReasonName(RendererSelectionReason reason);

class RendererTargetSelector
{
public:
    void ObserveFrame(const GlobalPresentFrame& frame);
    void SetForegroundProcess(DWORD processId);
    void SetMicrosoftHint(std::optional<DWORD> processId);
    void SetSteamHint(std::optional<DWORD> processId);
    void Clear() noexcept;
    void Reevaluate(std::uint64_t now);

    const std::optional<RendererTargetSelection>& Selection() const noexcept
    {
        return selection_;
    }
    bool ForegroundHasActiveRenderer(std::uint64_t now) const noexcept;
    const std::map<std::pair<DWORD, std::uint64_t>, RendererProcessState>& States() const noexcept
    {
        return states_;
    }

private:
    void Select(std::uint64_t now);
    const RendererProcessState* FindActive(DWORD processId,
        std::uint64_t now) const noexcept;
    static bool IsActive(const RendererProcessState& state,
        std::uint64_t now) noexcept;

    std::map<std::pair<DWORD, std::uint64_t>, RendererProcessState> states_;
    DWORD foregroundProcessId_{};
    std::optional<DWORD> microsoftHint_;
    std::optional<DWORD> steamHint_;
    std::optional<RendererTargetSelection> selection_;
};
}
