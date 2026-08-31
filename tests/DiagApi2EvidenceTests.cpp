#include "Api2Evidence.h"

#include <cassert>
#include <cmath>
#include <cstring>
#include <limits>
#include <optional>
#include <string>

int main()
{
    Api2Evidence evidence;
    // The standalone EXE intentionally has no bundled loader. Before a manually
    // copied loader is available, raw API2 fields must remain null and the
    // structured result must report no renderer evidence.
    const auto unavailable = evidence.Sample(1234);
    assert(unavailable.json ==
        "\"trackStatus\":null,\"trackStatusCode\":null,\"pollStatus\":\"UNAVAILABLE\","
        "\"pollStatusCode\":null,\"swapChainCount\":null,\"rendererActive\":null,"
        "\"swapChainAddress\":null,\"displayedFps\":null,\"presentedFps\":null,\"swapChains\":null");
    assert(!unavailable.pollSucceeded);
    assert(unavailable.swapChainCount == 0);
    assert(!unavailable.anyDisplayedFpsPositive && !unavailable.anyPresentedFpsPositive);
    evidence.Stop();

    // PresentMon 2.5.1 rejects a zero input swap-chain capacity.
    static_assert(Api2Evidence::kSwapChainCapacity > 0);

    // Raw PM_STATUS values are preserved, not collapsed into local labels.
    assert(Api2StatusName(PM_STATUS_SUCCESS) == "PM_STATUS_SUCCESS");
    assert(Api2StatusName(PM_STATUS_BAD_ARGUMENT) == "PM_STATUS_BAD_ARGUMENT");
    assert(Api2StatusName(PM_STATUS_INVALID_PID) == "PM_STATUS_INVALID_PID");
    assert(Api2StatusName(static_cast<PM_STATUS>(9999)) == "PM_STATUS_UNKNOWN");

    // SWAP_CHAIN_ADDRESS decodes as uint64; FPS as finite double.
    std::uint8_t blob[sizeof(std::uint64_t)]{};
    const std::uint64_t address = 0x1234abcd5678ef90ull;
    std::memcpy(blob, &address, sizeof(address));
    PM_QUERY_ELEMENT element{ PM_METRIC_SWAP_CHAIN_ADDRESS, PM_STAT_NEWEST_POINT, 0, 0, 0, sizeof(address) };
    assert(Api2DecodeAddress(blob, element) == std::optional<std::uint64_t>(0x1234abcd5678ef90ull));
    assert(Api2DecodeValue(blob, element) == "1311862289879068560");
    const double fps = 119.875;
    std::memcpy(blob, &fps, sizeof(fps));
    element.metric = PM_METRIC_DISPLAYED_FPS;
    element.dataSize = sizeof(fps);
    assert(Api2DecodeFps(blob, element) == std::optional<double>(119.875));
    const double invalid = std::numeric_limits<double>::infinity();
    std::memcpy(blob, &invalid, sizeof(invalid));
    assert(Api2DecodeFps(blob, element) == std::nullopt);
    assert(Api2DecodeValue(blob, element) == "null");

    // A failed poll records the raw status and null renderer evidence.
    const auto failed = Api2ComposeSample(PM_STATUS_BAD_ARGUMENT, 0, {}, "null", "null");
    assert(failed.find("\"pollStatus\":\"PM_STATUS_BAD_ARGUMENT\"") != std::string::npos);
    assert(failed.find("\"pollStatusCode\":2") != std::string::npos);
    assert(failed.find("\"swapChains\":null") != std::string::npos);

    // A multi-row success preserves per-swap-chain evidence and mirrors row 0
    // into the top-level fields.
    const std::vector<Api2SwapChainRow> rows{
        { std::optional<std::uint64_t>(111), std::optional<double>(60.0), std::optional<double>(60.0) },
        { std::optional<std::uint64_t>(222), std::optional<double>(30.0), std::optional<double>(45.0) } };
    const auto ok = Api2ComposeSample(PM_STATUS_SUCCESS, 2, rows, "\"PM_STATUS_SUCCESS\"", "0");
    assert(ok.find("\"pollStatus\":\"PM_STATUS_SUCCESS\"") != std::string::npos);
    assert(ok.find("\"swapChainCount\":2") != std::string::npos);
    assert(ok.find("\"rendererActive\":true") != std::string::npos);
    assert(ok.find("\"swapChainAddress\":111,") != std::string::npos);
    assert(ok.find("\"swapChains\":[{\"swapChainAddress\":111,\"displayedFps\":60.000000,"
        "\"presentedFps\":60.000000},{\"swapChainAddress\":222,\"displayedFps\":30.000000,"
        "\"presentedFps\":45.000000}]") != std::string::npos);
}
