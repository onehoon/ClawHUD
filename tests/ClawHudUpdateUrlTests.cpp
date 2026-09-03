#include "ClawHudUpdateUrl.h"

#include <cassert>
#include <string>

// Cleanup 3 (work order 20.1): bounded update-source URL construction /
// validation, no network.

using namespace clawhud;

int main()
{
    // Feed name
    assert(IsSupportedReleaseFeedName("releases.stable.json"));
    assert(!IsSupportedReleaseFeedName("releases.beta.json"));
    assert(!IsSupportedReleaseFeedName("../releases.stable.json"));
    assert(!IsSupportedReleaseFeedName("a/releases.stable.json"));
    assert(!IsSupportedReleaseFeedName("a\\releases.stable.json"));
    assert(!IsSupportedReleaseFeedName(""));

    const auto feed = BuildReleaseFeedUrl("releases.stable.json");
    assert(feed && *feed ==
        L"https://github.com/onehoon/ClawHUD/releases/latest/download/releases.stable.json");
    assert(!BuildReleaseFeedUrl("releases.other.json"));
    assert(!BuildReleaseFeedUrl(".."));

    // Version token
    assert(IsSimpleVersionToken("0.1.90"));
    assert(IsSimpleVersionToken("2.5.1"));
    assert(IsSimpleVersionToken("1.0.0-beta.2"));
    assert(!IsSimpleVersionToken(""));
    assert(!IsSimpleVersionToken("0.1.90/../evil"));
    assert(!IsSimpleVersionToken("../0.1.90"));
    assert(!IsSimpleVersionToken("0.1 90"));
    assert(!IsSimpleVersionToken("v0.1.90"));
    assert(!IsSimpleVersionToken(".1.90"));

    // Filename
    assert(IsPlainFileName("ClawHUD-0.1.90-stable-full.nupkg"));
    assert(!IsPlainFileName(""));
    assert(!IsPlainFileName("."));
    assert(!IsPlainFileName(".."));
    assert(!IsPlainFileName("a/b.nupkg"));
    assert(!IsPlainFileName("a\\b.nupkg"));
    assert(!IsPlainFileName("..\\b.nupkg"));

    const auto pkg = BuildPackageUrl("0.1.90", "ClawHUD-0.1.90-stable-full.nupkg");
    assert(pkg && *pkg ==
        L"https://github.com/onehoon/ClawHUD/releases/download/v0.1.90/ClawHUD-0.1.90-stable-full.nupkg");
    assert(!BuildPackageUrl("0.1.90", "../../etc/passwd"));
    assert(!BuildPackageUrl("0.1.90", "sub/dir.nupkg"));
    assert(!BuildPackageUrl("bad version", "ok.nupkg"));

    return 0;
}
