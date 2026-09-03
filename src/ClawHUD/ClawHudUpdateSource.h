#pragma once

#include "ClawHudUpdateUrl.h"

#include <Velopack.hpp>

#include <string>

namespace clawhud
{
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
