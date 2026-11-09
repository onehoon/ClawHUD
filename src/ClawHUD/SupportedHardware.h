#pragma once

#include <span>
#include <string_view>

enum class HardwareSupport
{
    Supported,
    Unsupported,
    Indeterminate,
};

namespace clawhud
{
enum class HudGpuVendor
{
    Unknown,
    Intel,
    Amd,
    Nvidia,
};

struct HudSensorSupport
{
    bool msiCpuTemperature{};
    bool msiFanRpm{};
    bool msiCpuPackagePower{};
    bool msiBatteryDischarge{};
};

struct HudDeviceCapabilities
{
    HudGpuVendor expectedGpuVendor{ HudGpuVendor::Unknown };
    HudSensorSupport sensors{};
    bool intelVrrFix{};
};

struct HudDeviceIdentity
{
    std::wstring_view boardProduct;
    std::wstring_view boardManufacturer;
};

struct HudDeviceModel
{
    std::string_view modelId;
    std::string_view vendorId;
    std::string_view familyId;
    std::string_view displayName;
    std::wstring_view boardProduct;
    // Empty only for the existing legacy board-only MSI admission rules.
    std::wstring_view requiredBoardManufacturer;
    bool admittedToProduction{};
    HudDeviceCapabilities capabilities{};
};

struct HudDeviceSelection
{
    HardwareSupport status{ HardwareSupport::Indeterminate };
    // Points into the resolver's catalog span; the production overload uses the static catalog.
    const HudDeviceModel* model{};
};

std::span<const HudDeviceModel> HudDeviceModelCatalog() noexcept;
bool IsHudDeviceModelCatalogValid(std::span<const HudDeviceModel> models) noexcept;
HudDeviceSelection ResolveHudDevice(
    HudDeviceIdentity identity,
    std::span<const HudDeviceModel> models) noexcept;
HudDeviceSelection ResolveHudDevice(HudDeviceIdentity identity) noexcept;
bool IsHudDeviceSelectionAdmitted(const HudDeviceSelection& selection) noexcept;
}

HardwareSupport ClassifyBaseBoardProduct(std::wstring_view boardProduct);
HardwareSupport CheckSupportedHardware();
clawhud::HudDeviceSelection DetectSupportedHudDevice();
