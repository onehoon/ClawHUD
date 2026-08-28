#include "IgclGpuTelemetry.h"

#include "RuntimeLogger.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace clawhud
{
namespace
{
constexpr std::uint32_t kSuccess = 0;
constexpr std::uint32_t kApiVersion = (1u << 16) | 1u;
constexpr std::uint32_t kUseLevelZero = 0x1;

template<class Function>
Function Resolve(HMODULE library, const char* name)
{
    return reinterpret_cast<Function>(GetProcAddress(library, name));
}

std::optional<double> DecodeItem(const igcl::Item& item) noexcept
{
    if (!item.supported)
        return std::nullopt;
    double value{};
    switch (item.type)
    {
    case 0: value = item.value.i8; break;
    case 1: value = item.value.u8; break;
    case 2: value = item.value.i16; break;
    case 3: value = item.value.u16; break;
    case 4: value = item.value.i32; break;
    case 5: value = item.value.u32; break;
    case 6: value = static_cast<double>(item.value.i64); break;
    case 7: value = static_cast<double>(item.value.u64); break;
    case 8: value = item.value.f; break;
    case 9: value = item.value.d; break;
    default: return std::nullopt;
    }
    return std::isfinite(value) ? std::optional<double>(value) : std::nullopt;
}
}

std::optional<double> CalculateIgclGpuUsage(
    double previousTimestamp, double previousActivity,
    double currentTimestamp, double currentActivity) noexcept
{
    if (!std::isfinite(previousTimestamp) || !std::isfinite(previousActivity) ||
        !std::isfinite(currentTimestamp) || !std::isfinite(currentActivity) ||
        currentTimestamp <= previousTimestamp || currentActivity < previousActivity)
        return std::nullopt;
    const double usage = (currentActivity - previousActivity) /
        (currentTimestamp - previousTimestamp) * 100.0;
    if (!std::isfinite(usage))
        return std::nullopt;
    return std::clamp(usage, 0.0, 100.0);
}

IgclGpuTelemetrySampler::~IgclGpuTelemetrySampler()
{
    Reset();
}

void IgclGpuTelemetrySampler::Reset() noexcept
{
    initializationAttempted_ = false;
    previousTimestamp_.reset();
    previousActivity_.reset();
    if (close_ && api_)
        close_(api_);
    close_ = nullptr;
    telemetry_ = nullptr;
    api_ = nullptr;
    device_ = nullptr;
    if (library_)
        FreeLibrary(library_);
    library_ = nullptr;
}

bool IgclGpuTelemetrySampler::Initialize()
{
    Reset();
    initializationAttempted_ = true;
    const auto initializationFailure = [this]
    {
        Reset();
        initializationAttempted_ = true;
        return false;
    };
    library_ = LoadLibraryExW(L"ControlLib.dll", nullptr,
        LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!library_)
    {
        RuntimeLogger::Log(RuntimeLogLevel::Info,
            L"IGCL GPU telemetry unavailable: ControlLib.dll load failed");
        return initializationFailure();
    }
    const auto init = Resolve<igcl::InitFn>(library_, "ctlInit");
    close_ = Resolve<igcl::CloseFn>(library_, "ctlClose");
    const auto enumerate = Resolve<igcl::EnumDevicesFn>(library_,
        "ctlEnumerateDevices");
    telemetry_ = Resolve<igcl::TelemetryFn>(library_,
        "ctlPowerTelemetryGetV2");
    if (!init || !close_ || !enumerate || !telemetry_)
    {
        RuntimeLogger::Log(RuntimeLogLevel::Info,
            L"IGCL GPU telemetry unavailable: required symbol missing");
        return initializationFailure();
    }

    igcl::InitArgs args{};
    args.size = sizeof(args);
    args.appVersion = kApiVersion;
    args.flags = kUseLevelZero;
    if (init(&args, &api_) != kSuccess)
    {
        RuntimeLogger::Log(RuntimeLogLevel::Info,
            L"IGCL GPU telemetry unavailable: initialization failed");
        return initializationFailure();
    }
    std::uint32_t count{};
    if (enumerate(api_, &count, nullptr) != kSuccess || count == 0)
    {
        RuntimeLogger::Log(RuntimeLogLevel::Info,
            L"IGCL GPU telemetry unavailable: no usable device");
        return initializationFailure();
    }
    std::vector<igcl::Device> devices(count);
    if (enumerate(api_, &count, devices.data()) != kSuccess || count == 0)
    {
        RuntimeLogger::Log(RuntimeLogLevel::Info,
            L"IGCL GPU telemetry unavailable: device enumeration failed");
        return initializationFailure();
    }
    device_ = devices.front();
    RuntimeLogger::Log(RuntimeLogLevel::Info,
        L"IGCL GPU telemetry initialized");
    return true;
}

std::optional<IgclGpuTelemetry> IgclGpuTelemetrySampler::Sample()
{
    if (!Initialized() || !telemetry_ || !device_)
        return std::nullopt;

    igcl::PowerTelemetryV2 telemetry{};
    telemetry.Size = sizeof(telemetry);
    telemetry.Version = igcl::kTelemetryVersion;
    if (telemetry_(device_, &telemetry) != kSuccess)
    {
        previousTimestamp_.reset();
        previousActivity_.reset();
        return std::nullopt;
    }

    IgclGpuTelemetry result{};
    const auto clock = DecodeItem(telemetry.gpuCurrentClockFrequency);
    if (clock && *clock >= 0.0)
        result.gpuClockMHz = clock;

    const auto timestamp = DecodeItem(telemetry.timeStamp);
    const auto activity = DecodeItem(telemetry.renderComputeActivityCounter);
    if (!timestamp || !activity || *timestamp < 0.0 || *activity < 0.0)
    {
        previousTimestamp_.reset();
        previousActivity_.reset();
        return result;
    }
    if (previousTimestamp_ && previousActivity_)
        result.gpuUsagePercent = CalculateIgclGpuUsage(*previousTimestamp_,
            *previousActivity_, *timestamp, *activity);
    previousTimestamp_ = timestamp;
    previousActivity_ = activity;
    return result;
}
}
