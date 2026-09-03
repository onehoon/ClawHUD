#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace clawhud
{
// Pure URL construction / validation for ClawHUD's public stable GitHub Release
// layout. No networking, no VeloPack dependency -- unit tested directly.

// WinHTTP timeout budget for the bounded update source. Finite and small enough
// that a broken update endpoint cannot hold normal ClawHUD startup; the receive
// value is an inactivity bound, not a wall-clock cap, so a slow but live
// download still completes.
inline constexpr int kUpdateResolveTimeoutMs = 5'000;
inline constexpr int kUpdateConnectTimeoutMs = 5'000;
inline constexpr int kUpdateSendTimeoutMs = 10'000;
inline constexpr int kUpdateReceiveTimeoutMs = 15'000;

// Hard ceiling on the release feed response held in memory. The real feed is a
// few KiB; anything approaching this is treated as a failure.
inline constexpr unsigned long kUpdateFeedMaxBytes = 1u << 20;  // 1 MiB

// The only release feed name this build serves (single stable channel).
inline constexpr std::string_view kStableReleaseFeedName = "releases.stable.json";

// Exactly the supported stable feed name -- no path separators, no "..", no
// other channels.
bool IsSupportedReleaseFeedName(std::string_view name) noexcept;

// A plain "<n>.<n>.<n>[...][-<suffix>]" version token (VeloPack asset Version):
// starts with a digit, no slashes / spaces / "..".
bool IsSimpleVersionToken(std::string_view value) noexcept;

// A bare filename: non-empty, no '/', '\\', or "..", not "." / "..".
bool IsPlainFileName(std::string_view value) noexcept;

// https://github.com/onehoon/ClawHUD/releases/latest/download/<name>
// std::nullopt when `name` is not the supported feed name.
std::optional<std::wstring> BuildReleaseFeedUrl(std::string_view releasesName);

// https://github.com/onehoon/ClawHUD/releases/download/v<version>/<fileName>
// std::nullopt when the version token or filename fails validation.
std::optional<std::wstring> BuildPackageUrl(std::string_view assetVersion,
    std::string_view assetFileName);
}
