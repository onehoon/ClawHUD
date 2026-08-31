#include "Api2Evidence.h"

// The DLL remains an operator-supplied sibling of the EXE.
#include "DiagPresentMonApi2Client.h"

#include <algorithm>
#include <cstring>
#include <memory>
#include <sstream>
#include <unordered_set>

struct Api2Evidence::State
{
    DiagPresentMonApi2Client client;
    std::vector<PM_QUERY_ELEMENT> elements;
    PM_DYNAMIC_QUERY_HANDLE query{};
    std::vector<std::uint8_t> blob;
    std::unordered_set<DWORD> tracked;
    std::unordered_set<DWORD> newlyTracked;
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

std::string Number(const std::uint8_t* blob, const PM_QUERY_ELEMENT& element)
{
    if (element.dataSize == sizeof(double)) { double value{}; std::memcpy(&value, blob + element.dataOffset, sizeof(value)); return std::to_string(value); }
    if (element.dataSize == sizeof(std::uint64_t)) { std::uint64_t value{}; std::memcpy(&value, blob + element.dataOffset, sizeof(value)); return std::to_string(value); }
    return "null";
}
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
        state->blob.resize(static_cast<size_t>(std::max<std::uint64_t>(bytes, 1)));
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
    if (!state_ || !processId) return "\"pollStatus\":\"UNAVAILABLE\",\"swapChainCount\":null,\"swapChainAddress\":null,\"displayedFps\":null,\"presentedFps\":null";
    try
    {
        std::string trackStatus = "ALREADY_TRACKING";
        if (!state_->tracked.contains(processId))
        {
            const auto start = state_->client.StartTrackingProcess(processId);
            if (start != PM_STATUS_SUCCESS && start != PM_STATUS_ALREADY_TRACKING_PROCESS)
                return "\"pollStatus\":\"START_TRACKING_FAILED\",\"swapChainCount\":null,\"swapChainAddress\":null,\"displayedFps\":null,\"presentedFps\":null";
            state_->tracked.insert(processId);
            trackStatus = start == PM_STATUS_SUCCESS ? "SUCCESS" : "ALREADY_TRACKING_PROCESS";
        }
        std::uint32_t swaps{};
        const auto status = state_->client.PollDynamicQuery(state_->query, processId, state_->blob.data(), &swaps);
        if (status != PM_STATUS_SUCCESS) return "\"pollStatus\":\"QUERY_FAILED\",\"swapChainCount\":null,\"swapChainAddress\":null,\"displayedFps\":null,\"presentedFps\":null";
        std::string address = "null", displayed = "null", presented = "null";
        for (const auto& element : state_->elements)
            if (element.metric == PM_METRIC_SWAP_CHAIN_ADDRESS) address = Number(state_->blob.data(), element);
            else if (element.metric == PM_METRIC_DISPLAYED_FPS) displayed = Number(state_->blob.data(), element);
            else if (element.metric == PM_METRIC_PRESENTED_FPS) presented = Number(state_->blob.data(), element);
        return "\"trackStatus\":\"" + trackStatus + "\",\"pollStatus\":\"SUCCESS\",\"swapChainCount\":" + std::to_string(swaps) + ",\"rendererActive\":" + (swaps ? "true" : "false") + ",\"swapChainAddress\":" + address + ",\"displayedFps\":" + displayed + ",\"presentedFps\":" + presented;
    }
    catch (...) { return "\"pollStatus\":\"EXCEPTION\",\"swapChainCount\":null,\"swapChainAddress\":null,\"displayedFps\":null,\"presentedFps\":null"; }
}
