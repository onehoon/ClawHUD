#include "Api2Evidence.h"

// The DLL remains an operator-supplied sibling of the EXE.
#include "DiagPresentMonApi2Client.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <optional>
#include <sstream>
#include <unordered_set>

struct Api2Evidence::State
{
    DiagPresentMonApi2Client client;
    std::vector<PM_QUERY_ELEMENT> elements;
    PM_DYNAMIC_QUERY_HANDLE query{};
    std::size_t rowBytes{};
    std::vector<std::uint8_t> blob;
    std::unordered_set<DWORD> tracked;
};

namespace
{
bool Available(const PM_INTROSPECTION_METRIC* metric, PM_METRIC wanted,
    PM_QUERY_ELEMENT& element)
{
    if (!metric || metric->id != wanted || metric->type != PM_METRIC_TYPE_DYNAMIC ||
        !metric->pDeviceMetricInfo || metric->pDeviceMetricInfo->size == 0)
        return false;
    const PM_STAT preferred = wanted == PM_METRIC_SWAP_CHAIN_ADDRESS ? PM_STAT_NEWEST_POINT : PM_STAT_AVG;
    bool supportsPreferred = false;
    if (metric->pStatInfo) for (size_t index = 0; index < metric->pStatInfo->size; ++index)
    {
        const auto* info = static_cast<const PM_INTROSPECTION_STAT_INFO*>(metric->pStatInfo->pData[index]);
        supportsPreferred = supportsPreferred || (info && info->stat == preferred);
    }
    if (!supportsPreferred) return false;
    for (size_t index = 0; index < metric->pDeviceMetricInfo->size; ++index)
    {
        const auto* info = static_cast<const PM_INTROSPECTION_DEVICE_METRIC_INFO*>(
            metric->pDeviceMetricInfo->pData[index]);
        if (info && info->availability == PM_METRIC_AVAILABILITY_AVAILABLE)
        {
            element = { wanted, preferred, info->deviceId, 0, 0, 0 };
            return true;
        }
    }
    return false;
}

std::string RowValue(const std::uint8_t* row, const std::vector<PM_QUERY_ELEMENT>& elements,
    PM_METRIC wanted)
{
    for (const auto& element : elements)
        if (element.metric == wanted) return Api2DecodeValue(row, element);
    return "null";
}
}

std::string_view Api2StatusName(PM_STATUS status) noexcept
{
    switch (status)
    {
    case PM_STATUS_SUCCESS: return "PM_STATUS_SUCCESS";
    case PM_STATUS_FAILURE: return "PM_STATUS_FAILURE";
    case PM_STATUS_BAD_ARGUMENT: return "PM_STATUS_BAD_ARGUMENT";
    case PM_STATUS_BAD_HANDLE: return "PM_STATUS_BAD_HANDLE";
    case PM_STATUS_SERVICE_ERROR: return "PM_STATUS_SERVICE_ERROR";
    case PM_STATUS_INVALID_ETL_FILE: return "PM_STATUS_INVALID_ETL_FILE";
    case PM_STATUS_INVALID_PID: return "PM_STATUS_INVALID_PID";
    case PM_STATUS_ALREADY_TRACKING_PROCESS: return "PM_STATUS_ALREADY_TRACKING_PROCESS";
    case PM_STATUS_UNABLE_TO_CREATE_NSM: return "PM_STATUS_UNABLE_TO_CREATE_NSM";
    case PM_STATUS_INVALID_ADAPTER_ID: return "PM_STATUS_INVALID_ADAPTER_ID";
    case PM_STATUS_OUT_OF_RANGE: return "PM_STATUS_OUT_OF_RANGE";
    case PM_STATUS_INSUFFICIENT_BUFFER: return "PM_STATUS_INSUFFICIENT_BUFFER";
    case PM_STATUS_PIPE_ERROR: return "PM_STATUS_PIPE_ERROR";
    case PM_STATUS_SESSION_NOT_OPEN: return "PM_STATUS_SESSION_NOT_OPEN";
    case PM_STATUS_MIDDLEWARE_MISSING_PATH: return "PM_STATUS_MIDDLEWARE_MISSING_PATH";
    case PM_STATUS_NONEXISTENT_FILE_PATH: return "PM_STATUS_NONEXISTENT_FILE_PATH";
    case PM_STATUS_MIDDLEWARE_INVALID_SIGNATURE: return "PM_STATUS_MIDDLEWARE_INVALID_SIGNATURE";
    case PM_STATUS_MIDDLEWARE_MISSING_ENDPOINT: return "PM_STATUS_MIDDLEWARE_MISSING_ENDPOINT";
    case PM_STATUS_MIDDLEWARE_VERSION_LOW: return "PM_STATUS_MIDDLEWARE_VERSION_LOW";
    case PM_STATUS_MIDDLEWARE_VERSION_HIGH: return "PM_STATUS_MIDDLEWARE_VERSION_HIGH";
    case PM_STATUS_MIDDLEWARE_SERVICE_MISMATCH: return "PM_STATUS_MIDDLEWARE_SERVICE_MISMATCH";
    case PM_STATUS_QUERY_MALFORMED: return "PM_STATUS_QUERY_MALFORMED";
    case PM_STATUS_MODE_MISMATCH: return "PM_STATUS_MODE_MISMATCH";
    case PM_STATUS_FEATURE_DISABLED: return "PM_STATUS_FEATURE_DISABLED";
    }
    return "PM_STATUS_UNKNOWN";
}

std::string Api2DecodeValue(const std::uint8_t* blob, const PM_QUERY_ELEMENT& element)
{
    if (!blob) return "null";
    if (element.metric == PM_METRIC_SWAP_CHAIN_ADDRESS)
    {
        if (element.dataSize != sizeof(std::uint64_t)) return "null";
        std::uint64_t value{}; std::memcpy(&value, blob + element.dataOffset, sizeof(value));
        return value ? std::to_string(value) : "null";
    }
    if (element.metric == PM_METRIC_DISPLAYED_FPS || element.metric == PM_METRIC_PRESENTED_FPS)
    {
        if (element.dataSize != sizeof(double)) return "null";
        double value{}; std::memcpy(&value, blob + element.dataOffset, sizeof(value));
        return std::isfinite(value) ? std::to_string(value) : "null";
    }
    return "null";
}

std::string Api2ComposeSample(PM_STATUS pollStatus, std::uint32_t swapChainCount,
    const std::vector<Api2SwapChainRow>& rows, std::string trackStatusJson,
    std::string trackStatusCodeJson)
{
    std::string body = "\"trackStatus\":" + trackStatusJson +
        ",\"trackStatusCode\":" + trackStatusCodeJson +
        ",\"pollStatus\":\"" + std::string(Api2StatusName(pollStatus)) +
        "\",\"pollStatusCode\":" + std::to_string(static_cast<int>(pollStatus));
    if (pollStatus != PM_STATUS_SUCCESS)
        return body + ",\"swapChainCount\":null,\"rendererActive\":null,\"swapChainAddress\":null,"
            "\"displayedFps\":null,\"presentedFps\":null,\"swapChains\":null";
    const auto& first = rows.empty() ? Api2SwapChainRow{} : rows.front();
    body += ",\"swapChainCount\":" + std::to_string(swapChainCount) +
        ",\"rendererActive\":" + (swapChainCount ? "true" : "false") +
        ",\"swapChainAddress\":" + first.address +
        ",\"displayedFps\":" + first.displayedFps +
        ",\"presentedFps\":" + first.presentedFps + ",\"swapChains\":[";
    for (size_t index = 0; index < rows.size(); ++index)
    {
        if (index) body += ',';
        body += "{\"swapChainAddress\":" + rows[index].address +
            ",\"displayedFps\":" + rows[index].displayedFps +
            ",\"presentedFps\":" + rows[index].presentedFps + "}";
    }
    body += ']';
    return body;
}

bool Api2Evidence::Start(std::string& detail) noexcept
{
    Stop();
    try
    {
        auto state = std::make_unique<State>();
        if (!state->client.Initialize()) { detail = "loader_unavailable"; return false; }
        if (state->client.OpenSession() != PM_STATUS_SUCCESS) { detail = "session_unavailable"; return false; }
        const PM_INTROSPECTION_ROOT* root{};
        if (state->client.GetIntrospectionRoot(&root) != PM_STATUS_SUCCESS || !root) { detail = "introspection_unavailable"; return false; }
        for (size_t i = 0; i < root->pMetrics->size; ++i)
        {
            const auto* metric = static_cast<const PM_INTROSPECTION_METRIC*>(root->pMetrics->pData[i]);
            for (const auto wanted : { PM_METRIC_SWAP_CHAIN_ADDRESS, PM_METRIC_DISPLAYED_FPS, PM_METRIC_PRESENTED_FPS })
            {
                PM_QUERY_ELEMENT element{};
                if (Available(metric, wanted, element) && std::none_of(state->elements.begin(), state->elements.end(), [wanted](const auto& value) { return value.metric == wanted; })) state->elements.push_back(element);
            }
        }
        state->client.FreeIntrospectionRoot(root);
        if (state->elements.empty() || state->client.RegisterDynamicQuery(&state->query,
            state->elements.data(), state->elements.size(), 1000, 0) != PM_STATUS_SUCCESS || !state->query)
        { detail = "query_unavailable"; return false; }
        std::uint64_t bytes{}; for (const auto& element : state->elements) bytes = std::max(bytes, element.dataOffset + element.dataSize);
        state->rowBytes = static_cast<size_t>(std::max<std::uint64_t>(bytes, 1));
        // PresentMon rejects a zero input capacity, so the blob must hold
        // kSwapChainCapacity result rows and Sample() passes that count in.
        state->blob.assign(state->rowBytes * kSwapChainCapacity, std::uint8_t{});
        const auto version = state->client.ApiVersion();
        state_ = state.release(); detail = "ready_" + std::to_string(version.major) + "." + std::to_string(version.minor) + "." + std::to_string(version.patch); return true;
    }
    catch (...) { detail = "api2_exception"; return false; }
}

void Api2Evidence::Stop() noexcept
{
    if (!state_) return;
    for (const auto pid : state_->tracked) state_->client.StopTrackingProcess(pid);
    if (state_->query) state_->client.FreeDynamicQuery(state_->query);
    state_->client.Shutdown(); delete state_; state_ = nullptr;
}

void Api2Evidence::Retire(DWORD processId) noexcept
{
    if (!state_ || !state_->tracked.erase(processId)) return;
    state_->client.StopTrackingProcess(processId);
}

std::string Api2Evidence::Sample(DWORD processId) noexcept
{
    if (!state_ || !processId)
        return "\"trackStatus\":null,\"trackStatusCode\":null,\"pollStatus\":\"UNAVAILABLE\","
            "\"pollStatusCode\":null,\"swapChainCount\":null,\"rendererActive\":null,"
            "\"swapChainAddress\":null,\"displayedFps\":null,\"presentedFps\":null,\"swapChains\":null";
    try
    {
        std::optional<PM_STATUS> startStatus;
        if (!state_->tracked.contains(processId))
        {
            const auto status = state_->client.StartTrackingProcess(processId);
            startStatus = status;
            if (status != PM_STATUS_SUCCESS && status != PM_STATUS_ALREADY_TRACKING_PROCESS)
                return "\"trackStatus\":\"" + std::string(Api2StatusName(status)) +
                    "\",\"trackStatusCode\":" + std::to_string(static_cast<int>(status)) +
                    ",\"pollStatus\":null,\"pollStatusCode\":null,\"swapChainCount\":null,"
                    "\"rendererActive\":null,\"swapChainAddress\":null,\"displayedFps\":null,"
                    "\"presentedFps\":null,\"swapChains\":null";
            state_->tracked.insert(processId);
        }
        // Input value = caller-provided capacity; PresentMon 2.5.1 fails a zero.
        std::uint32_t swaps = kSwapChainCapacity;
        const auto poll = state_->client.PollDynamicQuery(state_->query, processId,
            state_->blob.data(), &swaps);
        const std::string trackJson = startStatus
            ? "\"" + std::string(Api2StatusName(*startStatus)) + "\"" : "null";
        const std::string trackCodeJson = startStatus
            ? std::to_string(static_cast<int>(*startStatus)) : "null";
        std::vector<Api2SwapChainRow> rows;
        if (poll == PM_STATUS_SUCCESS)
        {
            const std::uint32_t count = std::min(swaps, kSwapChainCapacity);
            for (std::uint32_t index = 0; index < count; ++index)
            {
                const std::uint8_t* row = state_->blob.data() + state_->rowBytes * index;
                rows.push_back({ RowValue(row, state_->elements, PM_METRIC_SWAP_CHAIN_ADDRESS),
                    RowValue(row, state_->elements, PM_METRIC_DISPLAYED_FPS),
                    RowValue(row, state_->elements, PM_METRIC_PRESENTED_FPS) });
            }
        }
        return Api2ComposeSample(poll, poll == PM_STATUS_SUCCESS ? swaps : 0, rows,
            trackJson, trackCodeJson);
    }
    catch (...)
    {
        return "\"trackStatus\":null,\"trackStatusCode\":null,\"pollStatus\":\"EXCEPTION\","
            "\"pollStatusCode\":null,\"swapChainCount\":null,\"rendererActive\":null,"
            "\"swapChainAddress\":null,\"displayedFps\":null,\"presentedFps\":null,\"swapChains\":null";
    }
}
