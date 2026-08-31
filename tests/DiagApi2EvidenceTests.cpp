#include "Api2Evidence.h"

#include <cassert>
#include <cmath>
#include <cstring>
#include <limits>

int main()
{
    Api2Evidence evidence;
    // The standalone EXE intentionally has no bundled loader.  Before a
    // manually copied loader is available, raw API2 fields must remain null.
    const auto record = evidence.Sample(1234);
    assert(record == "\"pollStatus\":\"UNAVAILABLE\",\"swapChainCount\":null,\"swapChainAddress\":null,\"displayedFps\":null,\"presentedFps\":null");

    std::uint8_t blob[sizeof(std::uint64_t)]{};
    const std::uint64_t address = 0x1234abcd5678ef90ull;
    std::memcpy(blob, &address, sizeof(address));
    PM_QUERY_ELEMENT element{ PM_METRIC_SWAP_CHAIN_ADDRESS, PM_STAT_NEWEST_POINT, 0, 0, 0, sizeof(address) };
    assert(Api2DecodeValue(blob, element) == "1311862289879068560");
    const double fps = 119.875;
    std::memcpy(blob, &fps, sizeof(fps));
    element.metric = PM_METRIC_DISPLAYED_FPS;
    element.dataSize = sizeof(fps);
    assert(Api2DecodeValue(blob, element) == "119.875000");
    const double invalid = std::numeric_limits<double>::infinity();
    std::memcpy(blob, &invalid, sizeof(invalid));
    assert(Api2DecodeValue(blob, element) == "null");
    evidence.Stop();
}
