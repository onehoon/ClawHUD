#include "AffectedPanelDetector.h"

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
    auto containsInsensitive = [](const std::string& value, const char* needle) {
        if (std::strlen(needle) > value.size()) return false;
        for (std::size_t i = 0; i + std::strlen(needle) <= value.size(); ++i)
            if (_strnicmp(value.c_str() + i, needle, std::strlen(needle)) == 0) return true;
        return false;
    };
    return identity.active && _stricmp(identity.manufacturer.c_str(), "CSW") == 0
        && _stricmp(identity.productCode.c_str(), "0801") == 0
        && containsInsensitive(identity.panelName, "PN8007QB1-2");
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
        while (true)
        {
            IWbemClassObject* object{}; ULONG returned{};
            if (FAILED(enumerator->Next(WBEM_INFINITE, 1, &object, &returned)) || returned == 0) break;
            PanelIdentity identity{ PropertyString(object, L"ManufacturerName"), PropertyString(object, L"ProductCodeID"),
                PropertyString(object, L"UserFriendlyName"), PropertyBool(object, L"Active"), PropertyString(object, L"InstanceName") };
            if (!identity.manufacturer.empty()) results.push_back(std::move(identity));
            object->Release();
        }
    } while (false);
    if (enumerator) enumerator->Release(); if (services) services->Release(); if (locator) locator->Release();
    CoUninitialize();
    if (!querySucceeded) throw std::runtime_error("WMI monitor query failed");
    return results;
}
}
