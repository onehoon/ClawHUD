#include "RendererTargetSelector.h"

#include "ProductionTargetPolicy.h"

#include <algorithm>
#include <cmath>

namespace clawhud
{
namespace
{
constexpr double kFpsWindowMs = 500.0;
}

std::wstring RendererSelectionReasonName(RendererSelectionReason reason)
{
    switch (reason)
    {
    case RendererSelectionReason::Microsoft: return L"microsoft";
    case RendererSelectionReason::Foreground: return L"foreground";
    case RendererSelectionReason::Steam: return L"steam";
    case RendererSelectionReason::Highest: return L"highest";
    case RendererSelectionReason::None: return L"none";
    }
    return L"none";
}

void RendererTargetSelector::ObserveFrame(const GlobalPresentFrame& frame)
{
    if (!IsEligibleProductionTargetImage(frame.application) ||
        !std::isfinite(frame.msBetweenDisplayChange) ||
        frame.msBetweenDisplayChange <= 0.0)
    {
        Reevaluate(frame.observedTick);
        return;
    }
    auto& state = states_[{frame.processId, frame.swapChain}];
    state.processId = frame.processId;
    state.application = frame.application;
    state.hasDisplayedFrames = true;
    state.lastDisplayedFrameTick = frame.observedTick;
    if (!state.fps)
        state.fps = std::nullopt;
    state.fpsWindowMs += frame.msBetweenDisplayChange;
    ++state.fpsWindowFrames;
    if (state.fpsWindowMs >= kFpsWindowMs)
    {
        state.fps = static_cast<double>(state.fpsWindowFrames) /
            (state.fpsWindowMs / 1000.0);
        state.fpsWindowMs = 0.0;
        state.fpsWindowFrames = 0;
    }
    Select(frame.observedTick);
}

void RendererTargetSelector::SetForegroundProcess(DWORD processId)
{
    foregroundProcessId_ = processId;
    Select(GetTickCount64());
}

void RendererTargetSelector::SetMicrosoftHint(std::optional<DWORD> processId)
{
    microsoftHint_ = processId;
    Select(GetTickCount64());
}

void RendererTargetSelector::SetSteamHint(std::optional<DWORD> processId)
{
    steamHint_ = processId;
    Select(GetTickCount64());
}

void RendererTargetSelector::Clear() noexcept
{
    states_.clear();
    selection_.reset();
}

void RendererTargetSelector::Reevaluate(std::uint64_t now)
{
    Select(now);
}

bool RendererTargetSelector::IsActive(const RendererProcessState& state,
    std::uint64_t now) noexcept
{
    return state.hasDisplayedFrames && now >= state.lastDisplayedFrameTick &&
        now - state.lastDisplayedFrameTick <= kRendererStaleThresholdMs;
}

const RendererProcessState* RendererTargetSelector::FindActive(
    DWORD processId, std::uint64_t now) const noexcept
{
    const RendererProcessState* result = nullptr;
    for (const auto& [key, state] : states_)
    {
        (void)key;
        if (state.processId == processId && IsActive(state, now) &&
            (!result || (state.fps.value_or(0.0) > result->fps.value_or(0.0))))
            result = &state;
    }
    return result;
}

void RendererTargetSelector::Select(std::uint64_t now)
{
    const RendererProcessState* state = nullptr;
    RendererSelectionReason reason = RendererSelectionReason::None;
    if (microsoftHint_)
    {
        state = FindActive(*microsoftHint_, now);
        if (state) reason = RendererSelectionReason::Microsoft;
    }
    if (!state && foregroundProcessId_)
    {
        state = FindActive(foregroundProcessId_, now);
        if (state) reason = RendererSelectionReason::Foreground;
    }
    if (!state && steamHint_)
    {
        state = FindActive(*steamHint_, now);
        if (state) reason = RendererSelectionReason::Steam;
    }
    if (!state)
    {
        for (const auto& [key, candidate] : states_)
        {
            (void)key;
            if (IsActive(candidate, now) &&
                (!state || candidate.fps.value_or(0.0) > state->fps.value_or(0.0)))
            {
                state = &candidate;
                reason = RendererSelectionReason::Highest;
            }
        }
    }
    if (!state)
    {
        selection_.reset();
        return;
    }
    selection_ = RendererTargetSelection{
        state->processId, state->application, state->fps, reason};
}

bool RendererTargetSelector::ForegroundHasActiveRenderer(
    std::uint64_t now) const noexcept
{
    return foregroundProcessId_ && FindActive(foregroundProcessId_, now) != nullptr;
}
}
