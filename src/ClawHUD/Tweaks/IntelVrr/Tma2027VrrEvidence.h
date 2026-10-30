#pragma once

#include "Tma2027VrrPolicy.h"

#include <string>

namespace clawhud
{
// Branch-neutral input for the Windows evidence adapter. Callers normalize
// both monitor paths using their branch's display-path implementation.
struct Tma2027EvidenceRequest
{
    std::string manufacturer;
    std::string productCode;
    std::string panelName;
    std::wstring normalizedMonitorInstance;
    std::wstring normalizedTargetInstance;
    std::wstring sourceDeviceName;
    std::int32_t adapterLuidHighPart{};
    std::uint32_t adapterLuidLowPart{};
    std::uint32_t sourceId{};
    std::uint32_t targetId{};
    bool targetAvailable{};
};

// Reads only the exact WMI monitor instance's reported EDID and override key.
// The direct Intel query is optional and used only for the 24-120 Hz telemetry
// exception.
Tma2027Evidence CollectTma2027Evidence(const Tma2027EvidenceRequest& request,
    bool queryDirectRange) noexcept;
std::optional<std::string> ComputeTma2027EdidSha256(
    std::span<const std::uint8_t> bytes) noexcept;
const char* Tma2027EdidStatusName(Tma2027EdidStatus status) noexcept;
const char* Tma2027DirectRangeStatusName(const Tma2027DirectRange& range) noexcept;
}
