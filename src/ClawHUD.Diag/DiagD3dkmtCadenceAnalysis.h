#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

enum class DiagD3dkmtCaptureFailure
{
    None,
    ApiUnavailable,
    InvalidTarget,
    DisplayPathMismatch,
    AdapterOpenFailed,
    AdapterCloseFailed,
    QpcUnavailable,
    CancelEventFailed,
    SamplerStartFailed,
    WaitFailed,
    QueryCounterFailed,
    SampleStorageFailed,
};

enum class DiagD3dkmtFailureStatusDomain
{
    None,
    Win32,
    NtStatus,
};

struct DiagD3dkmtCadenceCapture
{
    bool available{};
    std::int64_t qpcFrequency{};
    std::uint32_t adapterLuidLow{};
    std::int32_t adapterLuidHigh{};
    std::uint32_t vidPnSourceId{};
    bool noObjectProbeAttempted{};
    std::optional<std::int32_t> noObjectProbeStatus;
    DiagD3dkmtCaptureFailure failure{ DiagD3dkmtCaptureFailure::None };
    std::optional<std::int32_t> failureStatus;
    std::vector<std::uint64_t> timestamps;
    bool attempted{};
    bool targetIdentified{};
    std::string_view failureDetail;
    DiagD3dkmtFailureStatusDomain failureStatusDomain{
        DiagD3dkmtFailureStatusDomain::None };
};

enum class DiagD3dkmtCadenceClass
{
    Unavailable,
    Indeterminate,
    FixedLike,
    VariableLike,
};

enum class DiagD3dkmtCadenceDataStatus
{
    Available,
    Unavailable,
    InvalidFrequency,
    InvalidNominalRefresh,
    InvalidTimestamps,
    TooManyWindows,
};

struct DiagD3dkmtCadenceWindow
{
    double startSeconds{};
    double endSeconds{};
    std::size_t eventCount{};
    double measuredHz{};
};

struct DiagD3dkmtCadenceAnalysis
{
    DiagD3dkmtCadenceDataStatus dataStatus{ DiagD3dkmtCadenceDataStatus::Unavailable };
    DiagD3dkmtCadenceClass classification{ DiagD3dkmtCadenceClass::Unavailable };
    std::vector<DiagD3dkmtCadenceWindow> windows;
    std::optional<double> minimumHz;
    std::optional<double> maximumHz;
    std::optional<double> averageHz;
    std::optional<double> medianHz;
    std::optional<double> standardDeviationHz;
    std::optional<double> nearNominalRatio;
    std::optional<double> rangeWidthHz;
};

double DiagVrrNearNominalToleranceHz(double nominalRefreshHz) noexcept;
bool DiagVrrCadenceIsNearNominal(double measuredHz, double nominalRefreshHz) noexcept;

DiagD3dkmtCadenceAnalysis AnalyzeDiagD3dkmtCadence(
    const DiagD3dkmtCadenceCapture& capture, double nominalRefreshHz);
