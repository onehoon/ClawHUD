#include "MsiEcReader.h"

#include <comdef.h>
#include <array>
#include <cmath>
#include <cstring>

#pragma comment(lib, "wbemuuid.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "comsuppw.lib")

using Microsoft::WRL::ComPtr;
namespace { constexpr wchar_t kNs[] = L"ROOT\\WMI"; constexpr wchar_t kClass[] = L"MSI_ACPI";
constexpr wchar_t kInstance[] = L"MSI_ACPI.InstanceName='ACPI\\PNP0C14\\0_0'"; constexpr size_t kSize = 32; }

namespace
{
ComPtr<IWbemClassObject> DataObject(IWbemClassObject* object)
{
    VARIANT value{}; if (FAILED(object->Get(L"Data", 0, &value, nullptr, nullptr))) return {};
    ComPtr<IWbemClassObject> data;
    if (V_VT(&value) == VT_UNKNOWN && V_UNKNOWN(&value)) V_UNKNOWN(&value)->QueryInterface(IID_PPV_ARGS(&data));
    if (V_VT(&value) == VT_DISPATCH && V_DISPATCH(&value)) V_DISPATCH(&value)->QueryInterface(IID_PPV_ARGS(&data));
    VariantClear(&value); return data;
}

bool PutPackage(IWbemClassObject* data, std::uint8_t selector)
{
    std::array<std::uint8_t, kSize> package{}; package[0] = selector; VARIANT bytes{};
    V_VT(&bytes) = VT_ARRAY | VT_UI1; V_ARRAY(&bytes) = SafeArrayCreateVector(VT_UI1, 0, static_cast<ULONG>(package.size()));
    if (!V_ARRAY(&bytes)) return false; void* raw{}; SafeArrayAccessData(V_ARRAY(&bytes), &raw);
    std::memcpy(raw, package.data(), package.size()); SafeArrayUnaccessData(V_ARRAY(&bytes));
    const HRESULT hr = data->Put(L"Bytes", 0, &bytes, CIM_UINT8); VariantClear(&bytes); return SUCCEEDED(hr);
}
}

MsiEcReader::~MsiEcReader() { Shutdown(); }

bool MsiEcReader::Initialize()
{
    Shutdown();
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr) && hr != S_FALSE) { error_ = hr; return false; }
    comInitialized_ = true;
    hr = CoInitializeSecurity(nullptr, -1, nullptr, nullptr, RPC_C_AUTHN_LEVEL_DEFAULT,
        RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE, nullptr);
    if (FAILED(hr) && hr != RPC_E_TOO_LATE) { error_ = hr; Shutdown(); return false; }
    ComPtr<IWbemLocator> locator;
    hr = CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&locator));
    if (FAILED(hr)) { error_ = hr; Shutdown(); return false; }
    BSTR ns = SysAllocString(kNs);
    hr = locator->ConnectServer(ns, nullptr, nullptr, nullptr, 0, nullptr, nullptr, &services_);
    SysFreeString(ns);
    if (FAILED(hr)) { error_ = hr; Shutdown(); return false; }
    hr = CoSetProxyBlanket(services_.Get(), RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr,
        RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE);
    if (FAILED(hr)) { error_ = hr; Shutdown(); return false; }
    BSTR className = SysAllocString(kClass);
    hr = services_->GetObject(className, 0, nullptr, &classObject_, nullptr);
    SysFreeString(className);
    if (FAILED(hr)) { error_ = hr; Shutdown(); return false; }
    error_ = S_OK; return true;
}

void MsiEcReader::Shutdown()
{
    classObject_.Reset(); services_.Reset();
    if (comInitialized_) CoUninitialize();
    comInitialized_ = false;
}

bool MsiEcReader::BuildInput(const wchar_t* method, std::uint8_t selector, ComPtr<IWbemClassObject>& input)
{
    ComPtr<IWbemClassObject> signature, data;
    HRESULT hr = classObject_->GetMethod(const_cast<BSTR>(method), 0, &signature, nullptr);
    if (SUCCEEDED(hr) && signature) hr = signature->SpawnInstance(0, &input);
    if (SUCCEEDED(hr) && input) data = DataObject(input.Get());
    if (!data)
    {
        // Compatibility fallback used by the known SteamAddon MSI transport.
        ComPtr<IWbemClassObject> fallbackSignature, fallbackInput;
        hr = classObject_->GetMethod(L"Get_WMI", 0, &fallbackSignature, nullptr);
        if (SUCCEEDED(hr) && fallbackSignature) hr = fallbackSignature->SpawnInstance(0, &fallbackInput);
        auto fallbackData = fallbackInput ? DataObject(fallbackInput.Get()) : nullptr;
        if (!fallbackData || !PutPackage(fallbackData.Get(), 0)) { error_ = hr; return false; }
        VARIANT replacement{}; V_VT(&replacement) = VT_UNKNOWN; fallbackData.CopyTo(&V_UNKNOWN(&replacement));
        if (FAILED(fallbackInput->Put(L"Data", 0, &replacement, 0))) { VariantClear(&replacement); error_ = E_INVALIDARG; return false; }
        VariantClear(&replacement);
        BSTR instance = SysAllocString(kInstance), operation = SysAllocString(L"Get_WMI"); ComPtr<IWbemClassObject> output;
        hr = services_->ExecMethod(instance, operation, 0, nullptr, fallbackInput.Get(), &output, nullptr);
        SysFreeString(instance); SysFreeString(operation);
        data = SUCCEEDED(hr) && output ? DataObject(output.Get()) : nullptr;
    }
    if (!input)
    {
        hr = classObject_->GetMethod(const_cast<BSTR>(method), 0, &signature, nullptr);
        if (SUCCEEDED(hr) && signature) hr = signature->SpawnInstance(0, &input);
    }
    if (!input || !data || !PutPackage(data.Get(), selector)) { error_ = FAILED(hr) ? hr : E_INVALIDARG; return false; }
    VARIANT replacement{}; V_VT(&replacement) = VT_UNKNOWN;
    if (FAILED(data.CopyTo(&V_UNKNOWN(&replacement)))) { error_ = E_NOINTERFACE; return false; }
    hr = input->Put(L"Data", 0, &replacement, 0); VariantClear(&replacement);
    error_ = hr; return SUCCEEDED(hr);
}

bool MsiEcReader::ExtractBytes(IWbemClassObject* output, std::vector<std::uint8_t>& bytes)
{
    VARIANT data{}; HRESULT hr = output->Get(L"Data", 0, &data, nullptr, nullptr);
    if (FAILED(hr)) { error_ = hr; return false; }
    ComPtr<IWbemClassObject> object;
    if (V_VT(&data) == VT_UNKNOWN && V_UNKNOWN(&data)) V_UNKNOWN(&data)->QueryInterface(IID_PPV_ARGS(&object));
    if (V_VT(&data) == VT_DISPATCH && V_DISPATCH(&data)) V_DISPATCH(&data)->QueryInterface(IID_PPV_ARGS(&object));
    VariantClear(&data); if (!object) { error_ = E_INVALIDARG; return false; }
    VARIANT value{}; hr = object->Get(L"Bytes", 0, &value, nullptr, nullptr);
    if (FAILED(hr) || V_VT(&value) != (VT_ARRAY | VT_UI1)) { error_ = FAILED(hr) ? hr : E_INVALIDARG; VariantClear(&value); return false; }
    LONG low{}, high{}; SafeArrayGetLBound(V_ARRAY(&value), 1, &low); SafeArrayGetUBound(V_ARRAY(&value), 1, &high);
    if (high < low) { VariantClear(&value); error_ = E_INVALIDARG; return false; }
    bytes.resize(static_cast<size_t>(high - low + 1)); void* raw{}; SafeArrayAccessData(V_ARRAY(&value), &raw);
    std::memcpy(bytes.data(), raw, bytes.size()); SafeArrayUnaccessData(V_ARRAY(&value)); VariantClear(&value); return true;
}

bool MsiEcReader::InvokeGet(const wchar_t* method, std::uint8_t selector, std::vector<std::uint8_t>& payload)
{
    payload.clear(); if (!services_ || !classObject_) return false;
    ComPtr<IWbemClassObject> input; if (!BuildInput(method, selector, input)) return false;
    BSTR instance = SysAllocString(kInstance), operation = SysAllocString(method); ComPtr<IWbemClassObject> output;
    HRESULT hr = services_->ExecMethod(instance, operation, 0, nullptr, input.Get(), &output, nullptr);
    SysFreeString(instance); SysFreeString(operation);
    if (FAILED(hr) || !output || !ExtractBytes(output.Get(), payload)) { error_ = FAILED(hr) ? hr : E_FAIL; return false; }
    if (payload.empty() || payload[0] != 1) { error_ = E_FAIL; payload.clear(); return false; }
    payload.erase(payload.begin()); error_ = S_OK; return true;
}

bool MsiEcReader::ReadTemperature(std::vector<std::uint8_t>& payload) { return InvokeGet(L"Get_Temperature", 0, payload); }
bool MsiEcReader::ReadFan(std::vector<std::uint8_t>& payload) { return InvokeGet(L"Get_Fan", 0, payload); }
bool MsiEcReader::ReadData(std::uint8_t selector, std::vector<std::uint8_t>& payload) { return InvokeGet(L"Get_Data", selector, payload); }

std::optional<int> DecodeFanRpm(std::uint8_t first, std::uint8_t second)
{
    const int delta = static_cast<int>(first) - static_cast<int>(second);
    if (!delta) return std::nullopt;
    return static_cast<int>(std::abs(480000.0 / delta));
}
