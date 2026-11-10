#pragma once

#include <memory>
#include <optional>

namespace clawhud
{
enum class AdlxPowerStatus
{
    Ok,
    RuntimeUnavailable,
    ExportUnavailable,
    InitializationFailed,
    NoGpu,
    PerformanceUnavailable,
    MetricUnsupported,
    SampleFailed,
    InvalidValue
};

struct AdlxPowerReadResult
{
    std::optional<int> watts;
    AdlxPowerStatus status{ AdlxPowerStatus::InitializationFailed };
};

namespace detail
{
struct AdlxPowerApi;
}

class AmdAdlxPowerTelemetry
{
public:
    AmdAdlxPowerTelemetry();
    ~AmdAdlxPowerTelemetry();

    AmdAdlxPowerTelemetry(const AmdAdlxPowerTelemetry&) = delete;
    AmdAdlxPowerTelemetry& operator=(const AmdAdlxPowerTelemetry&) = delete;

    AdlxPowerStatus Initialize() noexcept;
    AdlxPowerReadResult ReadPowerW() noexcept;
    void Shutdown() noexcept;

#if defined(CLAWHUD_ADLX_POWER_TELEMETRY_TESTS)
    explicit AmdAdlxPowerTelemetry(detail::AdlxPowerApi api);
#endif

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
