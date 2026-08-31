#include "Api2Evidence.h"

#include <cassert>

int main()
{
    Api2Evidence evidence;
    // The standalone EXE intentionally has no bundled loader.  Before a
    // manually copied loader is available, raw API2 fields must remain null.
    const auto record = evidence.Sample(1234);
    assert(record == "\"pollStatus\":\"UNAVAILABLE\",\"swapChainCount\":null,\"swapChainAddress\":null,\"displayedFps\":null,\"presentedFps\":null");
    evidence.Stop();
}
