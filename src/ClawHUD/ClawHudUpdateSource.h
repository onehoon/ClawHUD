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
class ClawHudUpdateSource : public Velopack::IUpdateSource
{
public:
    ClawHudUpdateSource() = default;

    const std::string GetReleaseFeed(const std::string releasesName) override;
    bool DownloadReleaseEntry(const Velopack::VelopackAsset& asset,
        const std::string localFilePath,
        Velopack::vpkc_progress_send_t progress) override;
};
}
