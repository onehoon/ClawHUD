#pragma once

#include "HudRenderer.h"

#include <string_view>

namespace clawhud
{
constexpr int kMinHudSizeOffset = -2;
constexpr int kMaxHudSizeOffset = 2;

int ClampHudSizeOffset(int offset) noexcept;
int ParseHudSizeOffset(std::wstring_view value);
int CommitHudSizeOffsetAfterRecreation(
    int previous, int requested, bool recreationSucceeded) noexcept;
HudAlignment CommitHudAlignmentAfterRecreation(
    HudAlignment previous, HudAlignment requested, bool recreationSucceeded) noexcept;
HudBackgroundMode CommitHudBackgroundModeAfterRecreation(
    HudBackgroundMode previous, HudBackgroundMode requested, bool recreationSucceeded) noexcept;
bool ShouldRestoreHudVisibility(bool wasVisible) noexcept;
HudRenderOptions BuildHudRenderOptionsForSize(
    int offset, const HudLayoutOptions& layout) noexcept;
}
