#pragma once

// CH-RTF-8 — process-internal launch mode.
//
// Standalone is the permanent default; Managed is entered only with an explicit
// `--managed` command-line token. The mode changes shell composition only (tray
// + legacy Settings); the runtime implementation is identical in both modes.
// It is never persisted and never inferred from SteamAddon / environment /
// previous launches.

#include <span>
#include <string_view>

#include "ClawHudControlProtocol.h"

namespace clawhud
{
enum class LaunchMode
{
    Standalone,
    Managed,
};

// Standalone unless the exact case-sensitive token "--managed" appears among
// the arguments (argv[1..], i.e. excluding the program name). Unknown
// arguments never enable Managed mode. Pure: no filesystem / registry /
// process / settings access.
LaunchMode ResolveLaunchMode(std::span<const std::wstring_view> arguments) noexcept;

// Explicit mapping to the wire enum for GetRuntimeInfo — never an ordinal cast.
constexpr control::WireLaunchMode ToWireLaunchMode(LaunchMode mode) noexcept
{
    return mode == LaunchMode::Managed ? control::WireLaunchMode::Managed
                                       : control::WireLaunchMode::Standalone;
}

constexpr const wchar_t* LaunchModeName(LaunchMode mode) noexcept
{
    return mode == LaunchMode::Managed ? L"Managed" : L"Standalone";
}
}
