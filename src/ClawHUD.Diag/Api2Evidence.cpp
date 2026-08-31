#include "Api2Evidence.h"

// The DLL remains an operator-supplied sibling of the EXE.
#include "DiagPresentMonApi2Client.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <unordered_set>

struct Api2Evidence::State
{
    DiagPresentMonApi2Client client;
    std::vector<PM_QUERY_ELEMENT> elements;
    PM_DYNAMIC_QUERY_HANDLE query{};
    std::size_t rowBytes{};
    std::vector<std::uint8_t> blob;
    std::unordered_set<DWORD> tracked;
    bool pidValidationAvailable{};  // PM_METRIC_PROCESS_ID made it into the query
};

namespace
{
bool IsIdentityMetric(PM_METRIC metric) noexcept
{
    return metric == PM_METRIC_SWAP_CHAIN_ADDRESS || metric == PM_METRIC_PROCESS_ID;
}

PM_STAT PreferredStat(PM_METRIC metric) noexcept
{
    return IsIdentityMetric(metric) ? PM_STAT_NEWEST_POINT : PM_STAT_AVG;
}

bool Available(const PM_INTROSPECTION_METRIC* metric, PM_METRIC wanted,
    PM_QUERY_ELEMENT& element)
{
    if (!metric || metric->id != wanted ||
        !metric->pDeviceMetricInfo || metric->pDeviceMetricInfo->size == 0)
        return false;
    // FPS / swap-chain address are dynamic; PROCESS_ID is a static identity
    // metric that a dynamic query still carries as a constant per row.
    if (metric->type != PM_METRIC_TYPE_DYNAMIC && wanted != PM_METRIC_PROCESS_ID)
        return false;
    PM_STAT stat = PreferredStat(wanted);
    bool supportsPreferred = false;
    PM_STAT firstStat = stat;
    bool haveAnyStat = false;
    if (metric->pStatInfo) for (size_t index = 0; index < metric->pStatInfo->size; ++index)
    {
        const auto* info = static_cast<const PM_INTROSPECTION_STAT_INFO*>(metric->pStatInfo->pData[index]);
        if (!info) continue;
        if (!haveAnyStat) { firstStat = info->stat; haveAnyStat = true; }
        supportsPreferred = supportsPreferred || info->stat == stat;
    }
    if (!supportsPreferred)
    {
        // Identity metrics: fall back to whatever statistic introspection lists
        // rather than assuming the FPS AVG. Non-identity metrics still require
        // their preferred stat.
        if (!IsIdentityMetric(wanted)) return false;
        if (haveAnyStat) stat = firstStat;
    }
    for (size_t index = 0; index < metric->pDeviceMetricInfo->size; ++index)
    {
        const auto* info = static_cast<const PM_INTROSPECTION_DEVICE_METRIC_INFO*>(
            metric->pDeviceMetricInfo->pData[index]);
        if (info && info->availability == PM_METRIC_AVAILABILITY_AVAILABLE)
        {
            element = { wanted, stat, info->deviceId, 0, 0, 0 };
            return true;
        }
    }
    return false;
}

Api2SwapChainRow DecodeRow(const std::uint8_t* row, const std::vector<PM_QUERY_ELEMENT>& elements)
{
    Api2SwapChainRow decoded;
    for (const auto& element : elements)
    {
        if (element.metric == PM_METRIC_SWAP_CHAIN_ADDRESS)
            decoded.address = Api2DecodeAddress(row, element);
        else if (element.metric == PM_METRIC_DISPLAYED_FPS)
            decoded.displayedFps = Api2DecodeFps(row, element);
        else if (element.metric == PM_METRIC_PRESENTED_FPS)
            decoded.presentedFps = Api2DecodeFps(row, element);
        else if (element.metric == PM_METRIC_PROCESS_ID)
            decoded.processId = Api2DecodeProcessId(row, element);
    }
    return decoded;
}

std::string NumberOrNull(std::optional<std::uint64_t> value)
{
    return value ? std::to_string(*value) : "null";
}

std::string NumberOrNull(std::optional<double> value)
{
    return value ? std::to_string(*value) : "null";
}

constexpr const char* kNullRendererTail =
    ",\"pollRowCount\":null,\"swapChainCount\":null,\"rendererActive\":null,"
    "\"pidValidationAvailable\":null,\"returnedPid\":null,\"pidMatches\":null,"
    "\"swapChainAddress\":null,\"displayedFps\":null,\"presentedFps\":null,\"swapChains\":null";
const std::string kUnavailableBody =
    std::string("\"trackStatus\":null,\"trackStatusCode\":null,\"pollStatus\":\"UNAVAILABLE\","
        "\"pollStatusCode\":null") + kNullRendererTail;
const std::string kExceptionBody =
    std::string("\"trackStatus\":null,\"trackStatusCode\":null,\"pollStatus\":\"EXCEPTION\","
        "\"pollStatusCode\":null") + kNullRendererTail;
}

std::size_t Api2AlignedRowBytes(const std::vector<PM_QUERY_ELEMENT>& elements) noexcept
{
    // Match PresentMon 2.5.1 DynamicQuery: blobSize_ = PadToAlignment(cursor, 16).
    constexpr std::uint64_t kAlignment = 16;
    std::uint64_t end{};
    for (const auto& element : elements)
        end = std::max(end, element.dataOffset + element.dataSize);
    if (end == 0) end = kAlignment;
    return static_cast<std::size_t>((end + (kAlignment - 1)) & ~(kAlignment - 1));
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

std::optional<std::uint64_t> Api2DecodeAddress(const std::uint8_t* row, const PM_QUERY_ELEMENT& element)
{
    if (!row || element.metric != PM_METRIC_SWAP_CHAIN_ADDRESS ||
        element.dataSize != sizeof(std::uint64_t))
        return std::nullopt;
    std::uint64_t value{};
    std::memcpy(&value, row + element.dataOffset, sizeof(value));
    return value ? std::optional<std::uint64_t>(value) : std::nullopt;
}

std::optional<double> Api2DecodeFps(const std::uint8_t* row, const PM_QUERY_ELEMENT& element)
{
    if (!row || element.dataSize != sizeof(double)) return std::nullopt;
    double value{};
    std::memcpy(&value, row + element.dataOffset, sizeof(value));
    return std::isfinite(value) ? std::optional<double>(value) : std::nullopt;
}

std::optional<DWORD> Api2DecodeProcessId(const std::uint8_t* row, const PM_QUERY_ELEMENT& element) noexcept
{
    if (!row || element.metric != PM_METRIC_PROCESS_ID) return std::nullopt;
    if (element.dataSize == sizeof(std::uint32_t))
    {
        std::uint32_t value{};
        std::memcpy(&value, row + element.dataOffset, sizeof(value));
        return value ? std::optional<DWORD>(value) : std::nullopt;
    }
    if (element.dataSize == sizeof(std::uint64_t))
    {
        std::uint64_t value{};
        std::memcpy(&value, row + element.dataOffset, sizeof(value));
        return value && value <= MAXDWORD ? std::optional<DWORD>(static_cast<DWORD>(value)) : std::nullopt;
    }
    return std::nullopt;
}

std::string Api2DecodeValue(const std::uint8_t* blob, const PM_QUERY_ELEMENT& element)
{
    if (element.metric == PM_METRIC_SWAP_CHAIN_ADDRESS)
        return NumberOrNull(Api2DecodeAddress(blob, element));
    if (element.metric == PM_METRIC_DISPLAYED_FPS || element.metric == PM_METRIC_PRESENTED_FPS)
        return NumberOrNull(Api2DecodeFps(blob, element));
    return "null";
}

Api2RowAggregate Api2AggregateRows(const std::vector<Api2SwapChainRow>& rows, DWORD requestedPid) noexcept
{
    Api2RowAggregate aggregate;
    for (const auto& row : rows)
    {
        // A row whose returned PID is a different process is not this PID's
        // renderer/FPS evidence.
        if (row.processId && *row.processId != requestedPid) continue;
        if (row.address.has_value()) ++aggregate.swapChainCount;
        aggregate.anyDisplayedFpsPositive = aggregate.anyDisplayedFpsPositive || row.displayedFps.value_or(0.0) > 0.0;
        aggregate.anyPresentedFpsPositive = aggregate.anyPresentedFpsPositive || row.presentedFps.value_or(0.0) > 0.0;
    }
    return aggregate;
}

namespace
{
std::string PidOrNull(std::optional<DWORD> value)
{
    return value ? std::to_string(*value) : "null";
}
}

std::string Api2ComposeSample(PM_STATUS pollStatus, std::uint32_t pollRowCount,
    std::uint32_t swapChainCount, const std::vector<Api2SwapChainRow>& rows,
    bool pidValidationAvailable, std::string trackStatusJson,
    std::string trackStatusCodeJson)
{
    std::string body = "\"trackStatus\":" + trackStatusJson +
        ",\"trackStatusCode\":" + trackStatusCodeJson +
        ",\"pollStatus\":\"" + std::string(Api2StatusName(pollStatus)) +
        "\",\"pollStatusCode\":" + std::to_string(static_cast<int>(pollStatus));
    if (pollStatus != PM_STATUS_SUCCESS)
        return body + ",\"pollRowCount\":null,\"swapChainCount\":null,\"rendererActive\":null,"
            "\"pidValidationAvailable\":null,\"returnedPid\":null,\"pidMatches\":null,"
            "\"swapChainAddress\":null,\"displayedFps\":null,\"presentedFps\":null,\"swapChains\":null";
    // pollRowCount is the raw API2 count; PresentMon returns one null-address
    // row before it has observed a real swap chain, so renderer evidence is
    // driven by swapChainCount (rows carrying an actual SWAP_CHAIN_ADDRESS).
    const Api2SwapChainRow first = rows.empty() ? Api2SwapChainRow{} : rows.front();
    bool anyMismatch = false;
    for (const auto& row : rows)
        anyMismatch = anyMismatch || row.pidMismatch;
    const std::string pidMatchesJson = !pidValidationAvailable ? "null"
        : (anyMismatch ? "false" : "true");
    body += ",\"pollRowCount\":" + std::to_string(pollRowCount) +
        ",\"swapChainCount\":" + std::to_string(swapChainCount) +
        ",\"rendererActive\":" + (swapChainCount ? "true" : "false") +
        ",\"pidValidationAvailable\":" + (pidValidationAvailable ? "true" : "false") +
        ",\"returnedPid\":" + PidOrNull(first.processId) +
        ",\"pidMatches\":" + pidMatchesJson +
        ",\"swapChainAddress\":" + NumberOrNull(first.address) +
        ",\"displayedFps\":" + NumberOrNull(first.displayedFps) +
        ",\"presentedFps\":" + NumberOrNull(first.presentedFps) + ",\"swapChains\":[";
    for (size_t index = 0; index < rows.size(); ++index)
    {
        if (index) body += ',';
        body += "{\"returnedPid\":" + PidOrNull(rows[index].processId) +
            ",\"pidMatches\":" + (!rows[index].processId ? "null" : (rows[index].pidMismatch ? "false" : "true")) +
            ",\"swapChainAddress\":" + NumberOrNull(rows[index].address) +
            ",\"displayedFps\":" + NumberOrNull(rows[index].displayedFps) +
            ",\"presentedFps\":" + NumberOrNull(rows[index].presentedFps) + "}";
    }
    body += ']';
    return body;
}

Api2Evidence::Api2Evidence() noexcept = default;

Api2Evidence::~Api2Evidence() { Stop(); }

bool Api2Evidence::Start(std::string& detail) noexcept
{
    std::scoped_lock lock(mutex_);
    StopLocked();
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
            for (const auto wanted : { PM_METRIC_SWAP_CHAIN_ADDRESS, PM_METRIC_DISPLAYED_FPS,
                PM_METRIC_PRESENTED_FPS, PM_METRIC_PROCESS_ID })
            {
                PM_QUERY_ELEMENT element{};
                if (Available(metric, wanted, element) && std::none_of(state->elements.begin(), state->elements.end(), [wanted](const auto& value) { return value.metric == wanted; })) state->elements.push_back(element);
            }
        }
        state->client.FreeIntrospectionRoot(root);
        state->pidValidationAvailable = std::any_of(state->elements.begin(), state->elements.end(),
            [](const auto& element) { return element.metric == PM_METRIC_PROCESS_ID; });
        if (state->elements.empty() || state->client.RegisterDynamicQuery(&state->query,
            state->elements.data(), state->elements.size(), 1000, 0) != PM_STATUS_SUCCESS || !state->query)
        { detail = "query_unavailable"; return false; }
        // 16-byte-aligned per-row stride, matching PresentMon 2.5.1. Using the
        // unaligned element end misdecodes row 1+ and under-allocates the blob.
        state->rowBytes = Api2AlignedRowBytes(state->elements);
        // PresentMon rejects a zero input capacity, so the blob must hold
        // kSwapChainCapacity result rows and Sample() passes that count in.
        state->blob.assign(state->rowBytes * kSwapChainCapacity, std::uint8_t{});
        const auto version = state->client.ApiVersion();
        state_ = std::move(state);
        detail = "ready_" + std::to_string(version.major) + "." + std::to_string(version.minor) + "." + std::to_string(version.patch);
        return true;
    }
    catch (...) { detail = "api2_exception"; return false; }
}

void Api2Evidence::Stop() noexcept
{
    std::scoped_lock lock(mutex_);
    StopLocked();
}

void Api2Evidence::StopLocked() noexcept
{
    if (!state_) return;
    for (const auto pid : state_->tracked) state_->client.StopTrackingProcess(pid);
    if (state_->query) state_->client.FreeDynamicQuery(state_->query);
    state_->client.Shutdown();
    state_.reset();
}

void Api2Evidence::Retire(DWORD processId) noexcept
{
    std::scoped_lock lock(mutex_);
    if (!state_ || !state_->tracked.erase(processId)) return;
    state_->client.StopTrackingProcess(processId);
}

Api2SampleResult Api2Evidence::Sample(DWORD processId) noexcept
{
    std::scoped_lock lock(mutex_);
    Api2SampleResult result;
    if (!state_ || !processId) { result.json = kUnavailableBody; return result; }
    try
    {
        std::optional<PM_STATUS> startStatus;
        if (!state_->tracked.contains(processId))
        {
            const auto status = state_->client.StartTrackingProcess(processId);
            startStatus = status;
            if (status != PM_STATUS_SUCCESS && status != PM_STATUS_ALREADY_TRACKING_PROCESS)
            {
                result.json = "\"trackStatus\":\"" + std::string(Api2StatusName(status)) +
                    "\",\"trackStatusCode\":" + std::to_string(static_cast<int>(status)) +
                    ",\"pollStatus\":null,\"pollStatusCode\":null,\"pollRowCount\":null,"
                    "\"swapChainCount\":null,\"rendererActive\":null,\"pidValidationAvailable\":null,"
                    "\"returnedPid\":null,\"pidMatches\":null,\"swapChainAddress\":null,"
                    "\"displayedFps\":null,\"presentedFps\":null,\"swapChains\":null";
                return result;
            }
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
                auto row = DecodeRow(state_->blob.data() + state_->rowBytes * index, state_->elements);
                row.pidMismatch = row.processId.has_value() && *row.processId != processId;
                rows.push_back(row);
            }
            result.pollSucceeded = true;
            result.pollRowCount = swaps;
            const auto aggregate = Api2AggregateRows(rows, processId);
            result.swapChainCount = aggregate.swapChainCount;
            result.anySwapChainAddress = aggregate.swapChainCount != 0;
            result.anyDisplayedFpsPositive = aggregate.anyDisplayedFpsPositive;
            result.anyPresentedFpsPositive = aggregate.anyPresentedFpsPositive;
        }
        result.json = Api2ComposeSample(poll, result.pollRowCount, result.swapChainCount,
            rows, state_->pidValidationAvailable, trackJson, trackCodeJson);
        return result;
    }
    catch (...)
    {
        result = {};
        result.json = kExceptionBody;
        return result;
    }
}
