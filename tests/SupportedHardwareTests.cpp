#include "SupportedHardware.h"

#include <iostream>

namespace
{
bool Check(bool condition, const char* message)
{
    if (condition)
        return true;
    std::cerr << "FAILED: " << message << '\n';
    return false;
}
}

int main()
{
    bool ok = true;
    using clawhud::HudDeviceCapabilities;
    using clawhud::HudDeviceModel;
    using clawhud::HudGpuVendor;
    using clawhud::HudDeviceModelCatalog;
    using clawhud::IsHudDeviceModelCatalogValid;
    using clawhud::IsHudDeviceSelectionAdmitted;
    using clawhud::ResolveHudDevice;
    using clawhud::SupportsIntelVrrFix;

    const auto productionModels = HudDeviceModelCatalog();
    ok &= Check(productionModels.size() == 4,
        "catalog has three admitted models and one denied candidate");
    ok &= Check(IsHudDeviceModelCatalogValid(productionModels),
        "production catalog identities are unique");
    const HudDeviceModel* a8Candidate = nullptr;
    for (const auto& model : productionModels)
    {
        if (model.modelId == "msi.claw.bz2em")
        {
            a8Candidate = &model;
            break;
        }
    }
    ok &= Check(a8Candidate &&
        a8Candidate->capabilities.expectedGpuVendor == HudGpuVendor::Amd &&
        !a8Candidate->admittedToProduction &&
        !a8Candidate->capabilities.sensors.msiCpuTemperature &&
        !a8Candidate->capabilities.sensors.msiFanRpm &&
        !a8Candidate->capabilities.sensors.msiCpuPackagePower &&
        !a8Candidate->capabilities.sensors.msiBatteryDischarge &&
        !a8Candidate->capabilities.intelVrrFix,
        "A8 metadata candidate carries no production feature permissions");

    ok &= Check(ClassifyBaseBoardProduct(L"MS-1T42") == HardwareSupport::Supported, "MS-1T42 supported");
    ok &= Check(ClassifyBaseBoardProduct(L"MS-1T52") == HardwareSupport::Supported, "MS-1T52 supported");
    ok &= Check(ClassifyBaseBoardProduct(L"MS-1T91") == HardwareSupport::Supported, "MS-1T91 supported");
    ok &= Check(ClassifyBaseBoardProduct(L"ms-1t42") == HardwareSupport::Supported, "case-insensitive match");
    ok &= Check(ClassifyBaseBoardProduct(L" MS-1T91 ") == HardwareSupport::Supported, "trimmed match");
    ok &= Check(ClassifyBaseBoardProduct(L"MS-1T41") == HardwareSupport::Unsupported, "MS-1T41 unsupported");
    ok &= Check(ClassifyBaseBoardProduct(L"MS-1T92") == HardwareSupport::Unsupported, "MS-1T92 unsupported");
    ok &= Check(ClassifyBaseBoardProduct(L"OTHER-BOARD") == HardwareSupport::Unsupported, "other board unsupported");
    ok &= Check(ClassifyBaseBoardProduct(L"MS-1T420") == HardwareSupport::Unsupported, "fuzzy suffix rejected");
    ok &= Check(ClassifyBaseBoardProduct(L"") == HardwareSupport::Indeterminate, "empty product indeterminate");
    ok &= Check(ClassifyBaseBoardProduct(L"   \t\r\n") == HardwareSupport::Indeterminate, "blank product indeterminate");
    const auto missingProduct = ResolveHudDevice({ {}, {} });
    ok &= Check(missingProduct.status == HardwareSupport::Indeterminate && missingProduct.model == nullptr,
        "missing WMI product data yields no selected model");

    const auto claw7 = ResolveHudDevice({ L"MS-1T42", {} });
    const auto claw8 = ResolveHudDevice({ L"MS-1T52", {} });
    const auto clawcg = ResolveHudDevice({ L"MS-1T91", {} });
    ok &= Check(IsHudDeviceSelectionAdmitted(claw7) && claw7.model->modelId == "msi.claw.a2vm.7",
        "MS-1T42 resolves to its admitted canonical model");
    ok &= Check(IsHudDeviceSelectionAdmitted(claw8) && claw8.model->modelId == "msi.claw.a2vm.8",
        "MS-1T52 resolves to its admitted canonical model");
    ok &= Check(IsHudDeviceSelectionAdmitted(clawcg) && clawcg.model->modelId == "msi.claw.cg3em",
        "MS-1T91 resolves to its admitted canonical model");
    ok &= Check(claw7.model->capabilities.expectedGpuVendor == HudGpuVendor::Intel &&
        claw7.model->capabilities.sensors.msiCpuTemperature &&
        claw7.model->capabilities.sensors.msiFanRpm &&
        claw7.model->capabilities.sensors.msiCpuPackagePower &&
        claw7.model->capabilities.sensors.msiBatteryDischarge &&
        claw7.model->capabilities.intelVrrFix,
        "existing Intel model permissions preserve current behavior");
    ok &= Check(SupportsIntelVrrFix(claw7.model) &&
        SupportsIntelVrrFix(claw8.model) && SupportsIntelVrrFix(clawcg.model),
        "all currently admitted Intel models retain the same VRR eligibility");

    const auto a8 = ResolveHudDevice({ L"MS-1T8K", {} });
    ok &= Check(a8.status == HardwareSupport::Unsupported && a8.model == nullptr &&
        !IsHudDeviceSelectionAdmitted(a8), "A8 candidate remains unadmitted without a selected model");
    ok &= Check(!SupportsIntelVrrFix(a8Candidate) && !SupportsIntelVrrFix(nullptr),
        "unadmitted A8 metadata and missing model cannot enable Intel VRR");

    constexpr HudDeviceCapabilities kSyntheticAmdCapabilities{
        HudGpuVendor::Amd, {}, false };
    constexpr HudDeviceModel kSyntheticAmd{
        "synthetic.amd.handheld", "synthetic", "synthetic.handheld", "Synthetic AMD",
        L"SYNTHETIC-AMD", L"Synthetic", true, kSyntheticAmdCapabilities};
    ok &= Check(!SupportsIntelVrrFix(&kSyntheticAmd),
        "an admitted synthetic AMD model without the capability cannot schedule Intel VRR work");

    constexpr HudDeviceCapabilities kSyntheticVendorCapabilities{
        HudGpuVendor::Nvidia, {}, false };
    constexpr HudDeviceModel kSyntheticVendors[] =
    {
        { "vendor-a.handheld.sameboard", "vendor-a", "vendor-a.handheld", "Vendor A",
            L"SHARED-BOARD", L"Vendor A", true, kSyntheticVendorCapabilities },
        { "vendor-b.handheld.sameboard", "vendor-b", "vendor-b.handheld", "Vendor B",
            L"SHARED-BOARD", L"Vendor B", true, kSyntheticVendorCapabilities },
    };
    ok &= Check(IsHudDeviceModelCatalogValid(kSyntheticVendors),
        "synthetic cross-vendor rules can share a board product when manufacturers differ");
    const auto vendorA = ResolveHudDevice({ L"shared-board", L" vendor a " }, kSyntheticVendors);
    const auto vendorB = ResolveHudDevice({ L"SHARED-BOARD", L"VENDOR B" }, kSyntheticVendors);
    ok &= Check(IsHudDeviceSelectionAdmitted(vendorA) && vendorA.model->vendorId == "vendor-a",
        "exact synthetic manufacturer admits only Vendor A model");
    ok &= Check(IsHudDeviceSelectionAdmitted(vendorB) && vendorB.model->vendorId == "vendor-b",
        "exact synthetic manufacturer admits only Vendor B model");
    const auto missingManufacturer = ResolveHudDevice({ L"SHARED-BOARD", {} }, kSyntheticVendors);
    ok &= Check(missingManufacturer.status == HardwareSupport::Indeterminate &&
        missingManufacturer.model == nullptr,
        "missing required manufacturer fails closed as indeterminate");
    const auto wrongManufacturer = ResolveHudDevice({ L"SHARED-BOARD", L"Other Vendor" }, kSyntheticVendors);
    ok &= Check(wrongManufacturer.status == HardwareSupport::Unsupported && wrongManufacturer.model == nullptr,
        "wrong required manufacturer cannot admit a model");

    constexpr HudDeviceModel kAmbiguousRules[] =
    {
        { "ambiguous.one", "vendor-a", "vendor-a.family", "One",
            L"AMBIGUOUS-BOARD", L"Vendor A", true, kSyntheticVendorCapabilities },
        { "ambiguous.two", "vendor-b", "vendor-b.family", "Two",
            L"AMBIGUOUS-BOARD", L"Vendor A", true, kSyntheticVendorCapabilities },
    };
    const auto ambiguous = ResolveHudDevice({ L"AMBIGUOUS-BOARD", L"Vendor A" }, kAmbiguousRules);
    ok &= Check(ambiguous.status == HardwareSupport::Indeterminate && ambiguous.model == nullptr,
        "ambiguous exact matches fail closed without choosing the first row");
    ok &= Check(!IsHudDeviceModelCatalogValid(kAmbiguousRules),
        "catalog validation rejects overlapping matching rules");

    constexpr HudDeviceModel kBroadAndScopedRules[] =
    {
        { "legacy.board-only", "vendor-a", "vendor-a.family", "Legacy",
            L"SHARED-RULE-BOARD", {}, true, kSyntheticVendorCapabilities },
        { "scoped.vendor-a", "vendor-a", "vendor-a.family", "Scoped",
            L"SHARED-RULE-BOARD", L"Vendor A", true, kSyntheticVendorCapabilities },
    };
    const auto overlapping = ResolveHudDevice(
        { L"SHARED-RULE-BOARD", L"Vendor B" }, kBroadAndScopedRules);
    ok &= Check(overlapping.status == HardwareSupport::Indeterminate && overlapping.model == nullptr,
        "overlapping board-only and manufacturer rules fail closed even on a mismatch");

    const auto gpuOnly = ResolveHudDevice({ L"UNREGISTERED-BOARD", L"NVIDIA" });
    ok &= Check(gpuOnly.status == HardwareSupport::Unsupported && gpuOnly.model == nullptr,
        "GPU vendor alone never admits an unregistered board");
    ok &= Check(!IsHudDeviceSelectionAdmitted({ HardwareSupport::Supported, nullptr }),
        "supported status without a model cannot pass startup admission");
    const auto unregistered = ResolveHudDevice({ L"UNKNOWN-BOARD", L"Unknown Maker" });
    ok &= Check(unregistered.status == HardwareSupport::Unsupported && unregistered.model == nullptr,
        "unknown vendor and board remain unsupported");
    return ok ? 0 : 1;
}
