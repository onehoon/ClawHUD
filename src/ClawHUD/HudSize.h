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
HudRenderOptions BuildHudRenderOptionsForSize(
    int offset, const HudLayoutOptions& layout) noexcept;
}
