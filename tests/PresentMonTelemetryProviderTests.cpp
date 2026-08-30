#include "PresentMonTelemetryProvider.h"
#include <array>
#include <iostream>
using namespace clawhud;
namespace { bool Check(bool v, const char* m) { if (!v) std::cerr << "FAILED: " << m << '\n'; return v; } }
int main()
{
    bool ok = true; const char n1[] = "Independent"; const char n2[] = "Intel Graphics"; PM_INTROSPECTION_STRING s1{ n1 }, s2{ n2 };
    PM_INTROSPECTION_DEVICE devices[] = {{0, PM_DEVICE_TYPE_INDEPENDENT, PM_DEVICE_VENDOR_UNKNOWN, &s1, nullptr}, {1, PM_DEVICE_TYPE_GRAPHICS_ADAPTER, PM_DEVICE_VENDOR_INTEL, &s2, nullptr}, {2, PM_DEVICE_TYPE_SYSTEM, PM_DEVICE_VENDOR_UNKNOWN, nullptr, nullptr}}; std::array<const void*, 3> de{&devices[0], &devices[1], &devices[2]}; PM_INTROSPECTION_OBJARRAY da{de.data(), de.size()};
    PM_INTROSPECTION_DATA_TYPE_INFO ti{PM_DATA_TYPE_DOUBLE, PM_DATA_TYPE_UINT64, PM_ENUM_METRIC}; PM_INTROSPECTION_STAT_INFO stats[] = {{PM_STAT_AVG}, {PM_STAT_NEWEST_POINT}}; std::array<const void*, 2> se{&stats[0], &stats[1]}; PM_INTROSPECTION_OBJARRAY sa{se.data(), se.size()}; PM_INTROSPECTION_DEVICE_METRIC_INFO mi[] = {{1, PM_METRIC_AVAILABILITY_AVAILABLE, 4}, {65536, PM_METRIC_AVAILABILITY_UNAVAILABLE, 2}}; std::array<const void*, 2> me{&mi[0], &mi[1]}; PM_INTROSPECTION_OBJARRAY mia{me.data(), me.size()};
    PM_INTROSPECTION_METRIC metrics[] = {{PM_METRIC_GPU_FREQUENCY, PM_METRIC_TYPE_DYNAMIC, PM_UNIT_HERTZ, PM_UNIT_MEGAHERTZ, &ti, &sa, &mia}, {PM_METRIC_APPLICATION, PM_METRIC_TYPE_STATIC, PM_UNIT_DIMENSIONLESS, PM_UNIT_DIMENSIONLESS, nullptr, nullptr, nullptr}}; std::array<const void*, 2> xe{&metrics[0], &metrics[1]}; PM_INTROSPECTION_OBJARRAY xa{xe.data(), xe.size()}; PM_INTROSPECTION_ROOT root{&xa, nullptr, &da, nullptr};
    auto caps = BuildPresentMonTelemetryCapabilities(&root); ok &= Check(caps.devices.size() == 3, "devices copied"); ok &= Check(caps.devices[1].name == "Intel Graphics" && caps.devices[1].vendor == PM_DEVICE_VENDOR_INTEL, "device metadata copied"); ok &= Check(caps.metrics.size() == 2 && caps.metrics[0].statistics.size() == 2 && caps.metrics[0].devices[0].arraySize == 4 && caps.metrics[0].devices[1].availability == PM_METRIC_AVAILABILITY_UNAVAILABLE, "metric metadata copied");
    PresentMonTelemetryProvider p; ok &= Check(!p.Ready() && !p.FindMetric(PM_METRIC_GPU_FREQUENCY), "empty provider lookup"); p.Shutdown(); p.Shutdown(); ok &= Check(!p.Ready(), "idempotent shutdown"); return ok ? 0 : 1;
}
