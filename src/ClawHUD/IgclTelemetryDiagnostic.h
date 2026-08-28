#pragma once

#include <windows.h>

#include <cstdint>
#include <atomic>
#include <string>
#include <thread>
#include <vector>

namespace clawhud
{
enum class IgclDiagnosticClass
{
    SupportedActive, SupportedZero, SupportedConstant, Unsupported,
    NoDomain, SymbolMissing, ApiError, SkippedMutationCapable
};

const char* IgclDiagnosticClassName(IgclDiagnosticClass value) noexcept;

struct IgclSampleSeries
{
    std::vector<double> values;
    std::vector<std::uint64_t> rawValues;
    std::vector<std::uint64_t> timestamps;
    std::vector<std::uint32_t> types;
    bool symbolPresent{true};
    bool supported{true};
    bool apiSucceeded{true};
    bool hasDomain{true};
};

IgclDiagnosticClass ClassifyIgclSamples(const IgclSampleSeries& samples) noexcept;
double IgclSampleMinimum(const IgclSampleSeries& samples) noexcept;
double IgclSampleMaximum(const IgclSampleSeries& samples) noexcept;

class IgclTelemetryDiagnostic
{
public:
    explicit IgclTelemetryDiagnostic(HWND notifyWindow) : notifyWindow_(notifyWindow) {}
    ~IgclTelemetryDiagnostic();
    bool Start();
    void Stop() noexcept;
    bool Running() const noexcept { return running_.load(); }

private:
    void Run();
    void Status(const wchar_t* text) const;
    HWND notifyWindow_{};
    class Impl;
    Impl* impl_{};
    std::thread* worker_{};
    std::atomic_bool stop_{};
    std::atomic_bool running_{};
};

constexpr UINT kIgclDiagnosticStatus = WM_APP + 23;
constexpr UINT kIgclDiagnosticCompleted = WM_APP + 24;
}
