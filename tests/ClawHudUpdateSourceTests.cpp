#include "ClawHudUpdateSource.h"

#include <cassert>
#include <filesystem>
#include <string>

// Cleanup 3 review: the two VeloPack callback-facing overrides are reached from
// VeloPack's C trampoline with no try/catch. An exception escaping either one
// would unwind across the C/Rust FFI boundary. These forced failure paths must
// return a normal VeloPack failure signal (empty feed / false), never throw.
// No network is used.

int main()
{
    clawhud::ClawHudUpdateSource source;

    // Feed: unsupported release name fails in the impl before any socket work.
    {
        const std::string feed = source.GetReleaseFeed("releases.evil.json");
        assert(feed.empty());
    }

    // Download: asset fails validation (bad version token + traversal filename).
    {
        Velopack::VelopackAsset bad;
        bad.Version = "not a version";
        bad.FileName = "../../etc/passwd";
        const bool ok = source.DownloadReleaseEntry(bad, "ignored.nupkg", nullptr);
        assert(!ok);
    }

    // Download: valid asset, unwritable destination (parent dir does not exist).
    // The impl checks the destination before any network round trip.
    {
        Velopack::VelopackAsset asset;
        asset.Version = "0.1.90";
        asset.FileName = "ClawHUD-0.1.90-stable-full.nupkg";
        const auto dest = std::filesystem::temp_directory_path() /
            "clawhud-updatesource-tests-no-such-dir" / "pkg.nupkg";
        std::error_code ec;
        std::filesystem::remove_all(dest.parent_path(), ec);
        const bool ok = source.DownloadReleaseEntry(
            asset, dest.string(), nullptr);
        assert(!ok);
        assert(!std::filesystem::exists(dest));
    }

    return 0;
}
