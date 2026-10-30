#include "AffectedPanelDetector.h"
#include "Lg27gs65fVrrRangeTweak.h"

#include <windows.h>
#include <wbemidl.h>
#include <comdef.h>
#include <algorithm>
#include <stdexcept>
#include <cstring>

namespace clawhud
{
namespace
{
bool EqualsInsensitive(const std::string& left, const char* right)
{
    return _stricmp(left.c_str(), right) == 0;
}

bool EqualsInsensitive(std::wstring_view left, std::wstring_view right)
{
    return left.size() == right.size() &&
        CompareStringOrdinal(left.data(), static_cast<int>(left.size()),
            right.data(), static_cast<int>(right.size()), TRUE) == CSTR_EQUAL;
}

std::wstring WidenAscii(const std::string& value)
{
    std::wstring result;
    result.reserve(value.size());
    for (const auto character : value)
        result.push_back(static_cast<unsigned char>(character));
    return result;
}

std::string Narrow(const wchar_t* value)
{
    if (!value) return {};
    const int count = WideCharToMultiByte(CP_UTF8, 0, value, -1, nullptr, 0, nullptr, nullptr);
    std::string result(static_cast<std::size_t>(std::max(0, count)), '\0');
    if (count > 1) { WideCharToMultiByte(CP_UTF8, 0, value, -1, result.data(), count, nullptr, nullptr); result.resize(static_cast<std::size_t>(count - 1)); }
    return result;
}

std::vector<unsigned short> VariantUShorts(VARIANT& value)
{
    std::vector<unsigned short> result;
    if ((value.vt & VT_ARRAY) == 0 || (value.vt & VT_UI2) == 0 || !value.parray) return result;
    LONG lower{}, upper{};
    if (FAILED(SafeArrayGetLBound(value.parray, 1, &lower)) || FAILED(SafeArrayGetUBound(value.parray, 1, &upper))) return result;
    for (LONG i = lower; i <= upper; ++i) { unsigned short item{}; if (SUCCEEDED(SafeArrayGetElement(value.parray, &i, &item))) result.push_back(item); }
    return result;
}

std::string PropertyString(IWbemClassObject* object, const wchar_t* name)
{
    VARIANT value{}; std::string result;
    if (SUCCEEDED(object->Get(name, 0, &value, nullptr, nullptr)))
    {
        if (value.vt == VT_BSTR) result = Narrow(value.bstrVal);
        else if (value.vt & VT_ARRAY) result = DecodeWmiUShortString(VariantUShorts(value));
    }
    VariantClear(&value); return result;
}

bool PropertyBool(IWbemClassObject* object, const wchar_t* name)
{
    VARIANT value{}; bool result = false;
    if (SUCCEEDED(object->Get(name, 0, &value, nullptr, nullptr))) result = value.boolVal != VARIANT_FALSE;
    VariantClear(&value); return result;
}
}

std::string DecodeWmiUShortString(const std::vector<unsigned short>& values)
{
    std::string result;
    for (const auto value : values) if (value != 0 && value <= 0x7f) result.push_back(static_cast<char>(value));
    return result;
}

bool IsAffectedPanel(const PanelIdentity& identity)
{
    const auto kind = ClassifySupportedPanel(identity);
    return kind == SupportedPanelKind::ClawInternal ||
        kind == SupportedPanelKind::Claw7Internal;
}

bool IsLg27gs65fPanel(const PanelIdentity& identity)
{
    const auto kind = ClassifySupportedPanel(identity);
    return kind == SupportedPanelKind::Lg27gs65f;
}

std::optional<SupportedPanelKind> ClassifySupportedPanel(const PanelIdentity& identity)
{
    if (EqualsInsensitive(identity.manufacturer, "CSW") &&
        EqualsInsensitive(identity.productCode, "0801") &&
        EqualsInsensitive(identity.panelName, "PN8007QB1-2"))
        return SupportedPanelKind::ClawInternal;
    if (EqualsInsensitive(identity.manufacturer, "TMA") &&
        EqualsInsensitive(identity.productCode, "2027") &&
        EqualsInsensitive(identity.panelName, "TL070FVXS02-0"))
        return SupportedPanelKind::Claw7Internal;
    if (EqualsInsensitive(identity.manufacturer, "GSM") &&
        EqualsInsensitive(identity.productCode, "5C8C") &&
        (EqualsInsensitive(identity.panelName, "LG ULTRAGEAR") ||
            EqualsInsensitive(identity.panelName, "LG 27GS65F")))
        return SupportedPanelKind::Lg27gs65f;
    return std::nullopt;
}

std::wstring NormalizeMonitorInstancePath(std::wstring_view value)
{
    std::wstring normalized(value);
    while (!normalized.empty() && (normalized.front() == L' ' || normalized.front() == L'\t'))
        normalized.erase(normalized.begin());
    while (!normalized.empty() && (normalized.back() == L' ' || normalized.back() == L'\t'))
        normalized.pop_back();

    if (normalized.size() >= 4 &&
        (EqualsInsensitive(std::wstring_view(normalized).substr(0, 4), L"\\\\?\\") ||
            EqualsInsensitive(std::wstring_view(normalized).substr(0, 4), L"\\\\.\\")))
        normalized.erase(0, 4);
    std::replace(normalized.begin(), normalized.end(), L'#', L'\\');

    // DisplayConfig paths end in an interface-class GUID; WMI instance IDs do not.
    const auto interfaceGuid = normalized.find(L"\\{");
    if (interfaceGuid != std::wstring::npos)
        normalized.resize(interfaceGuid);
    while (!normalized.empty() && normalized.back() == L'\\')
        normalized.pop_back();

    // WmiMonitorID appends its monitor index (usually _0) to the PnP instance.
    const auto separator = normalized.find_last_of(L'\\');
    const auto instanceStart = separator == std::wstring::npos ? 0 : separator + 1;
    const auto indexSeparator = normalized.find_last_of(L'_');
    if (indexSeparator != std::wstring::npos && indexSeparator >= instanceStart &&
        indexSeparator + 1 < normalized.size() &&
        std::all_of(normalized.begin() + static_cast<std::ptrdiff_t>(indexSeparator + 1),
            normalized.end(), [](wchar_t ch) { return ch >= L'0' && ch <= L'9'; }))
        normalized.resize(indexSeparator);

    for (auto& character : normalized)
        if (character >= L'A' && character <= L'Z')
            character = static_cast<wchar_t>(character + (L'a' - L'A'));
    return normalized;
}

PanelTargetMatch ResolvePanelWindowsTarget(const PanelIdentity& panel,
    const IntelVrrWindowsTargets& windowsTargets)
{
    if (!windowsTargets.enumerationComplete)
        return { PanelTargetMappingStatus::Unavailable, std::nullopt };

    const auto panelPath = NormalizeMonitorInstancePath(WidenAscii(panel.instanceName));
    if (panelPath.empty())
        return { PanelTargetMappingStatus::Unavailable, std::nullopt };

    std::optional<std::size_t> match;
    for (std::size_t index = 0; index < windowsTargets.targets.size(); ++index)
    {
        const auto& target = windowsTargets.targets[index];
        if (NormalizeMonitorInstancePath(target.monitorDevicePath) != panelPath)
            continue;

        const auto kind = ClassifySupportedPanel(panel);
        if (!kind)
            continue;
        const std::wstring expectedDeviceId = *kind == SupportedPanelKind::ClawInternal
            ? L"display\\csw0801\\" : *kind == SupportedPanelKind::Claw7Internal
                ? L"display\\tma2027\\" : L"display\\gsm5c8c\\";
        const auto targetPath = NormalizeMonitorInstancePath(target.monitorDevicePath);
        if (targetPath.rfind(expectedDeviceId, 0) != 0)
            continue;
        if (*kind == SupportedPanelKind::Lg27gs65f &&
            !IsLg27gs65fWindowsIdentity(target.monitorDevicePath, target.friendlyName))
            continue;
        if (match)
            return { PanelTargetMappingStatus::Ambiguous, std::nullopt };
        match = index;
    }

    if (!match)
        return { PanelTargetMappingStatus::NotActive, std::nullopt };
    if (!windowsTargets.targets[*match].available)
        return { PanelTargetMappingStatus::NotActive, std::nullopt };
    return { PanelTargetMappingStatus::Exact, match };
}

std::vector<PanelIdentity> EnumeratePanelIdentities()
{
    std::vector<PanelIdentity> results;
    const HRESULT apartment = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(apartment)) throw std::runtime_error("COM initialization failed");
    bool querySucceeded = false;
    IWbemLocator* locator{}; IWbemServices* services{}; IEnumWbemClassObject* enumerator{};
    do
    {
        if (FAILED(CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&locator)))) break;
        if (FAILED(locator->ConnectServer(_bstr_t(L"ROOT\\WMI"), nullptr, nullptr, nullptr, 0, nullptr, nullptr, &services))) break;
        if (FAILED(CoSetProxyBlanket(services, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr, RPC_C_AUTHN_LEVEL_CALL,
            RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE))) break;
        if (FAILED(services->ExecQuery(_bstr_t(L"WQL"), _bstr_t(L"SELECT * FROM WmiMonitorID"), WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
            nullptr, &enumerator))) break;
        querySucceeded = true;
        HRESULT enumerationResult = S_OK;
        while (true)
        {
            IWbemClassObject* object{}; ULONG returned{};
            enumerationResult = enumerator->Next(WBEM_INFINITE, 1, &object, &returned);
            if (FAILED(enumerationResult) || returned == 0) break;
            PanelIdentity identity{ PropertyString(object, L"ManufacturerName"), PropertyString(object, L"ProductCodeID"),
                PropertyString(object, L"UserFriendlyName"), PropertyBool(object, L"Active"), PropertyString(object, L"InstanceName") };
            if (!identity.manufacturer.empty()) results.push_back(std::move(identity));
            object->Release();
        }
        if (FAILED(enumerationResult)) querySucceeded = false;
    } while (false);
    if (enumerator) enumerator->Release(); if (services) services->Release(); if (locator) locator->Release();
    CoUninitialize();
    if (!querySucceeded) throw std::runtime_error("WMI monitor query failed");
    return results;
}
}
