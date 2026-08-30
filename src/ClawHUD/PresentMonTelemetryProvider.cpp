#include "PresentMonTelemetryProvider.h"
namespace clawhud
{
namespace
{
template<class T> const T* Entry(const PM_INTROSPECTION_OBJARRAY* a, size_t i)
{ return !a || !a->pData || i >= a->size ? nullptr : static_cast<const T*>(a->pData[i]); }
std::string Name(const PM_INTROSPECTION_STRING* s) { return s && s->pData ? s->pData : ""; }
}
PresentMonTelemetryCapabilities BuildPresentMonTelemetryCapabilities(const PM_INTROSPECTION_ROOT* root)
{
    PresentMonTelemetryCapabilities result; if (!root) return result;
    if (root->pDevices) for (size_t i = 0; i < root->pDevices->size; ++i)
    { const auto* d = Entry<PM_INTROSPECTION_DEVICE>(root->pDevices, i); if (d) result.devices.push_back({ d->id, d->type, d->vendor, Name(d->pName) }); }
    if (!root->pMetrics) return result;
    for (size_t i = 0; i < root->pMetrics->size; ++i)
    {
        const auto* m = Entry<PM_INTROSPECTION_METRIC>(root->pMetrics, i); if (!m) continue;
        PresentMonMetricCapability copy{ m->id, m->type, m->unit, m->preferredUnitHint, m->pTypeInfo ? m->pTypeInfo->polledType : PM_DATA_TYPE_VOID, m->pTypeInfo ? m->pTypeInfo->frameType : PM_DATA_TYPE_VOID, m->pTypeInfo ? m->pTypeInfo->enumId : PM_ENUM_NULL_ENUM };
        if (m->pStatInfo) for (size_t s = 0; s < m->pStatInfo->size; ++s) { const auto* stat = Entry<PM_INTROSPECTION_STAT_INFO>(m->pStatInfo, s); if (stat) copy.statistics.push_back(stat->stat); }
        if (m->pDeviceMetricInfo) for (size_t d = 0; d < m->pDeviceMetricInfo->size; ++d) { const auto* info = Entry<PM_INTROSPECTION_DEVICE_METRIC_INFO>(m->pDeviceMetricInfo, d); if (info) copy.devices.push_back({ info->deviceId, info->availability, info->arraySize }); }
        result.metrics.push_back(std::move(copy));
    }
    return result;
}
PresentMonTelemetryProvider::~PresentMonTelemetryProvider() { Shutdown(); }
bool PresentMonTelemetryProvider::Initialize()
{
    std::scoped_lock lock(apiMutex_);
    ShutdownUnlocked();
    if (!client_.Initialize() || client_.OpenSession() != PM_STATUS_SUCCESS) { ShutdownUnlocked(); return false; }
    const PM_INTROSPECTION_ROOT* root{};
    if (client_.GetIntrospectionRoot(&root) != PM_STATUS_SUCCESS || !root) { ShutdownUnlocked(); return false; }
    auto copied = BuildPresentMonTelemetryCapabilities(root);
    if (client_.FreeIntrospectionRoot(root) != PM_STATUS_SUCCESS) { ShutdownUnlocked(); return false; }
    capabilities_ = std::move(copied);
    ready_ = true;
    processTelemetry_.Initialize(client_, capabilities_);
    return true;
}
void PresentMonTelemetryProvider::Shutdown() noexcept
{
    std::scoped_lock lock(apiMutex_);
    ShutdownUnlocked();
}
void PresentMonTelemetryProvider::ShutdownUnlocked() noexcept
{
    processTelemetry_.Shutdown(client_);
    ready_ = false;
    capabilities_ = {};
    client_.Shutdown();
}
std::optional<PresentMonProcessSnapshot> PresentMonTelemetryProvider::ReadProcess(
    std::uint32_t processId)
{
    std::scoped_lock lock(apiMutex_);
    if (!ready_)
        return std::nullopt;
    return processTelemetry_.Read(client_, processId);
}
const PresentMonMetricCapability* PresentMonTelemetryProvider::FindMetric(PM_METRIC metric) const noexcept { for (const auto& m : capabilities_.metrics) if (m.id == metric) return &m; return nullptr; }
const PresentMonDeviceCapability* PresentMonTelemetryProvider::FindDevice(std::uint32_t id) const noexcept { for (const auto& d : capabilities_.devices) if (d.id == id) return &d; return nullptr; }
const PresentMonDeviceCapability* PresentMonTelemetryProvider::FindFirstDevice(PM_DEVICE_TYPE type, std::optional<PM_DEVICE_VENDOR> vendor) const noexcept { for (const auto& d : capabilities_.devices) if (d.type == type && (!vendor || d.vendor == *vendor)) return &d; return nullptr; }
bool PresentMonTelemetryProvider::MetricAvailable(PM_METRIC metric, std::uint32_t id) const noexcept { const auto* m = FindMetric(metric); if (!m) return false; for (const auto& d : m->devices) if (d.deviceId == id) return d.availability == PM_METRIC_AVAILABILITY_AVAILABLE; return false; }
bool PresentMonTelemetryProvider::SupportsStatistic(PM_METRIC metric, PM_STAT stat) const noexcept { const auto* m = FindMetric(metric); if (!m) return false; for (const auto s : m->statistics) if (s == stat) return true; return false; }
}
