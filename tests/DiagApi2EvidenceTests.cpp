#include "Api2Evidence.h"

#include <cassert>
#include <cmath>
#include <cstring>
#include <limits>
#include <optional>
#include <string>
#include <vector>

int main()
{
    Api2Evidence evidence;
    // The standalone EXE intentionally has no bundled loader. Before a manually
    // copied loader is available, raw API2 fields must remain null and the
    // structured result must report no renderer evidence.
    const auto unavailable = evidence.Sample(1234);
    assert(unavailable.json ==
        "\"trackStatus\":null,\"trackStatusCode\":null,\"pollStatus\":\"UNAVAILABLE\","
        "\"pollStatusCode\":null,\"pollRowCount\":null,\"swapChainCount\":null,\"rendererActive\":null,"
        "\"pidValidationAvailable\":null,\"returnedPid\":null,\"pidMatches\":null,"
        "\"swapChainAddress\":null,\"displayedFps\":null,\"presentedFps\":null,\"swapChains\":null");
    assert(!unavailable.pollSucceeded);
    assert(unavailable.pollRowCount == 0 && unavailable.swapChainCount == 0);
    assert(!unavailable.anyDisplayedFpsPositive && !unavailable.anyPresentedFpsPositive);
    evidence.Stop();

    static_assert(Api2Evidence::kSwapChainCapacity > 0);

    // Row stride matches PresentMon's PadToAlignment(cursor, 16).
    const std::vector<PM_QUERY_ELEMENT> threeFields{
        { PM_METRIC_SWAP_CHAIN_ADDRESS, PM_STAT_NEWEST_POINT, 0, 0, 0, 8 },
        { PM_METRIC_DISPLAYED_FPS, PM_STAT_AVG, 0, 0, 8, 8 },
        { PM_METRIC_PRESENTED_FPS, PM_STAT_AVG, 0, 0, 16, 8 } };
    assert(Api2AlignedRowBytes(threeFields) == 32);
    assert(Api2AlignedRowBytes({}) == 16);
    assert(Api2AlignedRowBytes({ { PM_METRIC_DISPLAYED_FPS, PM_STAT_AVG, 0, 0, 0, 8 } }) == 16);

    // Raw PM_STATUS values are preserved.
    assert(Api2StatusName(PM_STATUS_SUCCESS) == "PM_STATUS_SUCCESS");
    assert(Api2StatusName(PM_STATUS_BAD_ARGUMENT) == "PM_STATUS_BAD_ARGUMENT");
    assert(Api2StatusName(PM_STATUS_INVALID_PID) == "PM_STATUS_INVALID_PID");
    assert(Api2StatusName(static_cast<PM_STATUS>(9999)) == "PM_STATUS_UNKNOWN");

    // Typed decode: address as uint64, FPS as finite double, PID as uint32/uint64.
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

    const std::uint32_t pid32 = 14584;
    std::memcpy(blob, &pid32, sizeof(pid32));
    PM_QUERY_ELEMENT pidElement{ PM_METRIC_PROCESS_ID, PM_STAT_NEWEST_POINT, 0, 0, 0, sizeof(pid32) };
    assert(Api2DecodeProcessId(blob, pidElement) == std::optional<DWORD>(14584));
    const std::uint64_t pid64 = 14584;
    std::memcpy(blob, &pid64, sizeof(pid64));
    pidElement.dataSize = sizeof(pid64);
    assert(Api2DecodeProcessId(blob, pidElement) == std::optional<DWORD>(14584));
    std::memset(blob, 0, sizeof(blob));
    assert(Api2DecodeProcessId(blob, pidElement) == std::nullopt);

    // A failed poll: raw status, all renderer fields null.
    const auto failed = Api2ComposeSample(PM_STATUS_BAD_ARGUMENT, 0, 0, {}, false, "null", "null");
    assert(failed.find("\"pollStatus\":\"PM_STATUS_BAD_ARGUMENT\"") != std::string::npos);
    assert(failed.find("\"pollStatusCode\":2") != std::string::npos);
    assert(failed.find("\"pollRowCount\":null") != std::string::npos);
    assert(failed.find("\"pidMatches\":null") != std::string::npos);
    assert(failed.find("\"swapChains\":null") != std::string::npos);

    // A successful poll with only the null-address probe row is NOT renderer evidence.
    Api2SwapChainRow probeRow;  // all-nullopt
    const std::vector<Api2SwapChainRow> probeOnly{ probeRow };
    const auto probeAgg = Api2AggregateRows(probeOnly, 14584);
    assert(probeAgg.swapChainCount == 0);
    assert(!probeAgg.anyDisplayedFpsPositive && !probeAgg.anyPresentedFpsPositive);
    const auto probe = Api2ComposeSample(PM_STATUS_SUCCESS, 1, probeAgg.swapChainCount,
        probeOnly, false, "\"PM_STATUS_SUCCESS\"", "0");
    assert(probe.find("\"pollRowCount\":1") != std::string::npos);
    assert(probe.find("\"swapChainCount\":0") != std::string::npos);
    assert(probe.find("\"rendererActive\":false") != std::string::npos);
    assert(probe.find("\"pidValidationAvailable\":false") != std::string::npos);

    // A row whose returned PID is a different process does not advance this
    // PID's renderer / FPS evidence.
    Api2SwapChainRow otherPidRow;
    otherPidRow.processId = 99999;
    otherPidRow.pidMismatch = true;
    otherPidRow.address = 0xAAAA;
    otherPidRow.displayedFps = 144.0;
    otherPidRow.presentedFps = 144.0;
    const auto mismatchAgg = Api2AggregateRows({ otherPidRow }, 14584);
    assert(mismatchAgg.swapChainCount == 0);
    assert(!mismatchAgg.anyDisplayedFpsPositive && !mismatchAgg.anyPresentedFpsPositive);
    const auto mismatchJson = Api2ComposeSample(PM_STATUS_SUCCESS, 1, mismatchAgg.swapChainCount,
        { otherPidRow }, true, "\"PM_STATUS_SUCCESS\"", "0");
    assert(mismatchJson.find("\"pidValidationAvailable\":true") != std::string::npos);
    assert(mismatchJson.find("\"pidMatches\":false") != std::string::npos);
    assert(mismatchJson.find("\"returnedPid\":99999") != std::string::npos);

    // A multi-row match: full per-swap-chain evidence.
    Api2SwapChainRow rowA;
    rowA.processId = 14584; rowA.address = 111; rowA.displayedFps = 60.0; rowA.presentedFps = 60.0;
    Api2SwapChainRow rowB;
    rowB.processId = 14584; rowB.address = 222; rowB.displayedFps = 30.0; rowB.presentedFps = 45.0;
    const std::vector<Api2SwapChainRow> rows{ rowA, rowB };
    const auto agg = Api2AggregateRows(rows, 14584);
    assert(agg.swapChainCount == 2 && agg.anyDisplayedFpsPositive && agg.anyPresentedFpsPositive);
    const auto okJson = Api2ComposeSample(PM_STATUS_SUCCESS, 2, agg.swapChainCount, rows,
        true, "\"PM_STATUS_SUCCESS\"", "0");
    assert(okJson.find("\"pollRowCount\":2") != std::string::npos);
    assert(okJson.find("\"swapChainCount\":2") != std::string::npos);
    assert(okJson.find("\"rendererActive\":true") != std::string::npos);
    assert(okJson.find("\"returnedPid\":14584") != std::string::npos);
    assert(okJson.find("\"swapChainAddress\":111,") != std::string::npos);
    assert(okJson.find("\"swapChains\":[{\"returnedPid\":14584,\"pidMatches\":true,"
        "\"swapChainAddress\":111,\"displayedFps\":60.000000,\"presentedFps\":60.000000},"
        "{\"returnedPid\":14584,\"pidMatches\":true,\"swapChainAddress\":222,"
        "\"displayedFps\":30.000000,\"presentedFps\":45.000000}]") != std::string::npos);
}
