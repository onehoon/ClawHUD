#include "SupportedHardware.h"
#include "RuntimeLogger.h"

#include <Windows.h>
#include <Wbemidl.h>

#include <cwctype>
#include <string>

#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

namespace
{
constexpr clawhud::HudDeviceCapabilities kMsiIntelCapabilities{
    clawhud::HudGpuVendor::Intel,
    { true, true, true, true },
    true,
};

constexpr clawhud::HudDeviceCapabilities kMsiAmdCandidateCapabilities{
    clawhud::HudGpuVendor::Amd,
    { true, true, false, false },
    false,
    clawhud::MsiEcFanLayout::SingleTach,
};

constexpr clawhud::HudDeviceModel kHudDeviceModels[] =
{
    { "msi.claw.a2vm.7", "msi", "msi.claw", "MSI Claw A2VM 7",
        L"MS-1T42", {}, true, kMsiIntelCapabilities },
    { "msi.claw.a2vm.8", "msi", "msi.claw", "MSI Claw A2VM 8",
        L"MS-1T52", {}, true, kMsiIntelCapabilities },
    { "msi.claw.cg3em", "msi", "msi.claw", "MSI Claw CG3EM",
        L"MS-1T91", {}, true, kMsiIntelCapabilities },
    { "msi.claw.bz2em", "msi", "msi.claw", "MSI Claw BZ2EM",
        L"MS-1T8K", {}, true, kMsiAmdCandidateCapabilities },
};

std::wstring_view TrimWhitespace(std::wstring_view value) noexcept
{
    std::size_t first = 0;
    while (first < value.size() && std::iswspace(value[first]))
        ++first;

    std::size_t last = value.size();
    while (last > first && std::iswspace(value[last - 1]))
        --last;

    return value.substr(first, last - first);
}

bool EqualsIgnoreCase(std::wstring_view left, std::wstring_view right) noexcept
{
    if (left.size() != right.size())
        return false;

    for (std::size_t i = 0; i < left.size(); ++i)
    {
        if (std::towlower(left[i]) != std::towlower(right[i]))
            return false;
    }
    return true;
}

bool MatchingRulesOverlap(
    const clawhud::HudDeviceModel& left,
    const clawhud::HudDeviceModel& right) noexcept
{
    if (!EqualsIgnoreCase(TrimWhitespace(left.boardProduct), TrimWhitespace(right.boardProduct)))
        return false;

    const auto leftManufacturer = TrimWhitespace(left.requiredBoardManufacturer);
    const auto rightManufacturer = TrimWhitespace(right.requiredBoardManufacturer);
    return leftManufacturer.empty() || rightManufacturer.empty() ||
        EqualsIgnoreCase(leftManufacturer, rightManufacturer);
}

void Log(const std::wstring& message)
{
    clawhud::RuntimeLogger::Log(clawhud::RuntimeLogLevel::Info, message);
}

void LogWarn(const std::wstring& message)
{
    clawhud::RuntimeLogger::Log(clawhud::RuntimeLogLevel::Warn, message);
}

const wchar_t* GpuVendorName(clawhud::HudGpuVendor vendor) noexcept
{
    switch (vendor)
    {
    case clawhud::HudGpuVendor::Intel: return L"Intel";
    case clawhud::HudGpuVendor::Amd: return L"AMD";
    case clawhud::HudGpuVendor::Nvidia: return L"NVIDIA";
    default: return L"Unknown";
    }
}

class ComInitialization
{
public:
    ComInitialization() = default;

    bool Initialize()
    {
        const HRESULT result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (FAILED(result))
            return false;

        initialized_ = true;
        const HRESULT security = CoInitializeSecurity(
            nullptr,
            -1,
            nullptr,
            nullptr,
            RPC_C_AUTHN_LEVEL_DEFAULT,
            RPC_C_IMP_LEVEL_IMPERSONATE,
            nullptr,
            EOAC_NONE,
            nullptr);
        return SUCCEEDED(security) || security == RPC_E_TOO_LATE;
    }

    ~ComInitialization()
    {
        if (initialized_)
            CoUninitialize();
    }

    ComInitialization(const ComInitialization&) = delete;
    ComInitialization& operator=(const ComInitialization&) = delete;

private:
    bool initialized_{};
};
}

namespace clawhud
{
std::span<const HudDeviceModel> HudDeviceModelCatalog() noexcept
{
    return kHudDeviceModels;
}

bool IsHudDeviceModelCatalogValid(std::span<const HudDeviceModel> models) noexcept
{
    for (std::size_t i = 0; i < models.size(); ++i)
    {
        const auto& model = models[i];
        if (model.modelId.empty() || model.vendorId.empty() || model.familyId.empty() ||
            model.displayName.empty() || TrimWhitespace(model.boardProduct).empty())
            return false;

        for (std::size_t j = i + 1; j < models.size(); ++j)
        {
            if (model.modelId == models[j].modelId || MatchingRulesOverlap(model, models[j]))
                return false;
        }
    }
    return true;
}

HudDeviceSelection ResolveHudDevice(
    HudDeviceIdentity identity,
    std::span<const HudDeviceModel> models) noexcept
{
    const auto product = TrimWhitespace(identity.boardProduct);
    const auto manufacturer = TrimWhitespace(identity.boardManufacturer);
    if (product.empty())
        return {};

    const HudDeviceModel* candidate = nullptr;
    bool missingRequiredManufacturer = false;
    bool ambiguous = false;
    bool boardOnlyRule = false;
    bool manufacturerScopedRule = false;
    for (const auto& model : models)
    {
        if (!EqualsIgnoreCase(product, TrimWhitespace(model.boardProduct)))
            continue;

        const auto requiredManufacturer = TrimWhitespace(model.requiredBoardManufacturer);
        if (requiredManufacturer.empty())
            boardOnlyRule = true;
        else
            manufacturerScopedRule = true;
        if (!requiredManufacturer.empty())
        {
            if (manufacturer.empty())
            {
                missingRequiredManufacturer = true;
                continue;
            }
            if (!EqualsIgnoreCase(manufacturer, requiredManufacturer))
                continue;
        }

        if (candidate)
        {
            ambiguous = true;
            continue;
        }
        candidate = &model;
    }

    if (ambiguous || missingRequiredManufacturer || (boardOnlyRule && manufacturerScopedRule))
        return { HardwareSupport::Indeterminate, nullptr };
    if (!candidate || !candidate->admittedToProduction)
        return { HardwareSupport::Unsupported, nullptr };
    return { HardwareSupport::Supported, candidate };
}

HudDeviceSelection ResolveHudDevice(HudDeviceIdentity identity) noexcept
{
    return ResolveHudDevice(identity, HudDeviceModelCatalog());
}

bool IsHudDeviceSelectionAdmitted(const HudDeviceSelection& selection) noexcept
{
    return selection.status == HardwareSupport::Supported && selection.model != nullptr &&
        selection.model->admittedToProduction;
}

bool SupportsIntelVrrFix(const HudDeviceModel* model) noexcept
{
    return model != nullptr && model->admittedToProduction &&
        model->capabilities.intelVrrFix;
}
}

HardwareSupport ClassifyBaseBoardProduct(std::wstring_view boardProduct)
{
    return clawhud::ResolveHudDevice({ boardProduct, {} }).status;
}

HardwareSupport CheckSupportedHardware()
{
    return DetectSupportedHudDevice().status;
}

clawhud::HudDeviceSelection DetectSupportedHudDevice()
{
    ComInitialization com;
    if (!com.Initialize())
    {
        LogWarn(L"BaseBoard Product unavailable; startup aborted");
        return {};
    }

    ComPtr<IWbemLocator> locator;
    if (FAILED(CoCreateInstance(
            CLSID_WbemLocator,
            nullptr,
            CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&locator))))
    {
        LogWarn(L"BaseBoard Product unavailable; startup aborted");
        return {};
    }

    ComPtr<IWbemServices> services;
    BSTR namespaceName = SysAllocString(L"ROOT\\CIMV2");
    const HRESULT connectResult = namespaceName
        ? locator->ConnectServer(namespaceName, nullptr, nullptr, nullptr, 0, nullptr, nullptr, &services)
        : E_OUTOFMEMORY;
    SysFreeString(namespaceName);
    if (FAILED(connectResult) || !services || FAILED(CoSetProxyBlanket(
            services.Get(),
            RPC_C_AUTHN_WINNT,
            RPC_C_AUTHZ_NONE,
            nullptr,
            RPC_C_AUTHN_LEVEL_CALL,
            RPC_C_IMP_LEVEL_IMPERSONATE,
            nullptr,
            EOAC_NONE)))
    {
        LogWarn(L"BaseBoard Product unavailable; startup aborted");
        return {};
    }

    ComPtr<IEnumWbemClassObject> results;
    BSTR language = SysAllocString(L"WQL");
    BSTR query = SysAllocString(L"SELECT Product, Manufacturer FROM Win32_BaseBoard");
    const HRESULT queryResult = language && query
        ? services->ExecQuery(language, query, WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
            nullptr, &results)
        : E_OUTOFMEMORY;
    SysFreeString(language);
    SysFreeString(query);
    if (FAILED(queryResult) || !results)
    {
        LogWarn(L"BaseBoard Product unavailable; startup aborted");
        return {};
    }

    ComPtr<IWbemClassObject> board;
    ULONG returned = 0;
    if (results->Next(WBEM_INFINITE, 1, &board, &returned) != S_OK || returned != 1 || !board)
    {
        LogWarn(L"BaseBoard Product unavailable; startup aborted");
        return {};
    }

    VARIANT product{};
    const HRESULT productResult = board->Get(L"Product", 0, &product, nullptr, nullptr);
    if (FAILED(productResult) || V_VT(&product) != VT_BSTR || !V_BSTR(&product))
    {
        VariantClear(&product);
        LogWarn(L"BaseBoard Product unavailable; startup aborted");
        return {};
    }

    const std::wstring boardProduct(V_BSTR(&product), SysStringLen(V_BSTR(&product)));
    VariantClear(&product);

    // Manufacturer is optional for the three legacy MSI board-only rules. If it
    // is missing or malformed, those exact existing identities still resolve.
    std::wstring boardManufacturer;
    VARIANT manufacturer{};
    const HRESULT manufacturerResult = board->Get(L"Manufacturer", 0, &manufacturer, nullptr, nullptr);
    if (SUCCEEDED(manufacturerResult) && V_VT(&manufacturer) == VT_BSTR && V_BSTR(&manufacturer))
        boardManufacturer.assign(V_BSTR(&manufacturer), SysStringLen(V_BSTR(&manufacturer)));
    VariantClear(&manufacturer);

    const auto selection = clawhud::ResolveHudDevice({ boardProduct, boardManufacturer });
    if (clawhud::IsHudDeviceSelectionAdmitted(selection))
        Log(L"Hardware supported board=" + std::wstring(TrimWhitespace(boardProduct)) +
            L" model=" + std::wstring(selection.model->modelId.begin(), selection.model->modelId.end()) +
            L" expectedGpu=" + GpuVendorName(selection.model->capabilities.expectedGpuVendor));
    else if (selection.status == HardwareSupport::Unsupported)
        Log(L"Unsupported BaseBoard Product=" + std::wstring(TrimWhitespace(boardProduct)));
    else
        LogWarn(L"BaseBoard Product unavailable; startup aborted");
    return selection;
}
