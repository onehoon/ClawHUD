#pragma once

#include "ClawHudUpdateUrl.h"

#include <Velopack.hpp>

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace clawhud
{
// VeloPack 1.2.0's C/Rust custom-source bridge passes the staging path as a
// UTF-8 std::string (Rust `Path::to_string_lossy`). std::filesystem::path built
// from a narrow std::string on MSVC assumes the process ANSI code page, which
// corrupts non-ASCII profile paths (e.g. C:\Users\<user>\...). Decode explicitly.
// Returns std::nullopt for invalid UTF-8; an empty input maps to an empty path.
std::optional<std::filesystem::path> VeloPackUtf8Path(std::string_view utf8) noexcept;

// Best-effort removal of a partially written download, named by a UTF-8 VeloPack
// path. Never throws.
void RemovePartialDownloadBestEffort(std::string_view utf8Path) noexcept;

// Bounded VeloPack custom update source for ClawHUD's public stable GitHub
// Release layout. It exists only so every HTTP operation on the synchronous
// startup update path has a finite timeout -- the pinned VeloPack 1.2.0
// GithubSource has no timeout knob and its HTTP default is unbounded.
//
// VeloPack keeps ownership of version comparison, delta/full selection,
// download staging, and package validation. This source only fetches the exact
// feed / asset VeloPack asks for.
//
// SAFETY: the two callback-facing overrides are reached from VeloPack's C
// trampoline with no try/catch, and any exception would unwind across the
// C/Rust FFI boundary. Both overrides therefore catch every exception and
// return a normal VeloPack failure signal (empty feed / false) instead. The
// throwing work lives in the private *Impl helpers.
class ClawHudUpdateSource : public Velopack::IUpdateSource
{
public:
    ClawHudUpdateSource() = default;

    const std::string GetReleaseFeed(const std::string releasesName) override;
    bool DownloadReleaseEntry(const Velopack::VelopackAsset& asset,
        const std::string localFilePath,
        Velopack::vpkc_progress_send_t progress) override;

private:
    std::string GetReleaseFeedImpl(const std::string& releasesName);
    void DownloadReleaseEntryImpl(const Velopack::VelopackAsset& asset,
        const std::string& localFilePath,
        const Velopack::vpkc_progress_send_t& progress);
};
}
