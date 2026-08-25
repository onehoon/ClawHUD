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
namespace
{
constexpr wchar_t kNs[] = L"ROOT\\WMI";
constexpr wchar_t kClass[] = L"MSI_ACPI";
constexpr wchar_t kInstance[] = L"MSI_ACPI.InstanceName='ACPI\\PNP0C14\\0_0'";
constexpr std::size_t kSize = 32;

const wchar_t* StageName(MsiEcFailureStage stage) noexcept
{
    switch (stage)
    {
    case MsiEcFailureStage::GetMethod: return L"GetMethod";
    case MsiEcFailureStage::SpawnInstance: return L"SpawnInstance";
    case MsiEcFailureStage::GetInputData: return L"Get input.Data";
    case MsiEcFailureStage::GetWmiFallback: return L"Get_WMI compatibility fallback";
    case MsiEcFailureStage::PutBytes: return L"Put Data.Bytes";
    case MsiEcFailureStage::PutInputData: return L"Put input.Data";
    case MsiEcFailureStage::ExecMethod: return L"ExecMethod";
    case MsiEcFailureStage::GetOutputData: return L"Extract output.Data";
    case MsiEcFailureStage::GetOutputBytes: return L"Extract output.Data.Bytes";
    case MsiEcFailureStage::InvalidSuccessFlag: return L"Validate response[0]";
    default: return L"None";
    }
}

bool GetDataObject(IWbemClassObject* object, ComPtr<IWbemClassObject>& data, HRESULT& error)
{
    VARIANT value{};
    error = object->Get(L"Data", 0, &value, nullptr, nullptr);
    if (FAILED(error)) return false;
    HRESULT queryError = E_NOINTERFACE;
    if (V_VT(&value) == VT_UNKNOWN && V_UNKNOWN(&value))
        queryError = V_UNKNOWN(&value)->QueryInterface(IID_PPV_ARGS(&data));
    if (V_VT(&value) == VT_DISPATCH && V_DISPATCH(&value))
        queryError = V_DISPATCH(&value)->QueryInterface(IID_PPV_ARGS(&data));
    VariantClear(&value);
    if (!data) error = FAILED(queryError) ? queryError : E_INVALIDARG;
    return data != nullptr;
}
}

MsiEcReader::~MsiEcReader() { Shutdown(); }

void MsiEcReader::Fail(MsiEcFailureStage stage, HRESULT error) noexcept
{
    stage_ = stage;
    error_ = error;
}

const wchar_t* MsiEcReader::LastStage() const noexcept
{
    return StageName(stage_);
}

bool MsiEcReader::Initialize()
{
    Shutdown();
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr) && hr != S_FALSE) { Fail(MsiEcFailureStage::None, hr); return false; }
    comInitialized_ = true;
    hr = CoInitializeSecurity(nullptr, -1, nullptr, nullptr, RPC_C_AUTHN_LEVEL_DEFAULT,
        RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE, nullptr);
    if (FAILED(hr) && hr != RPC_E_TOO_LATE) { Fail(MsiEcFailureStage::None, hr); Shutdown(); return false; }
    ComPtr<IWbemLocator> locator;
    hr = CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&locator));
    if (FAILED(hr)) { Fail(MsiEcFailureStage::None, hr); Shutdown(); return false; }
    BSTR ns = SysAllocString(kNs);
    hr = locator->ConnectServer(ns, nullptr, nullptr, nullptr, 0, nullptr, nullptr, &services_);
    SysFreeString(ns);
    if (FAILED(hr)) { Fail(MsiEcFailureStage::None, hr); Shutdown(); return false; }
    hr = CoSetProxyBlanket(services_.Get(), RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr,
        RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE);
    if (FAILED(hr)) { Fail(MsiEcFailureStage::None, hr); Shutdown(); return false; }
    BSTR className = SysAllocString(kClass);
    hr = services_->GetObject(className, 0, nullptr, &classObject_, nullptr);
    SysFreeString(className);
    if (FAILED(hr)) { Fail(MsiEcFailureStage::None, hr); Shutdown(); return false; }
    stage_ = MsiEcFailureStage::None;
    error_ = S_OK;
    return true;
}

void MsiEcReader::Shutdown()
{
    classObject_.Reset(); services_.Reset();
    if (comInitialized_) CoUninitialize();
    comInitialized_ = false;
}

bool MsiEcReader::BuildInput(const wchar_t* method, std::uint8_t selector,
    ComPtr<IWbemClassObject>& input)
{
    ComPtr<IWbemClassObject> signature, targetInput, data;
    HRESULT hr = classObject_->GetMethod(const_cast<BSTR>(method), 0, &signature, nullptr);
    if (FAILED(hr) || !signature) { Fail(MsiEcFailureStage::GetMethod, FAILED(hr) ? hr : E_FAIL); return false; }
    hr = signature->SpawnInstance(0, &targetInput);
    if (FAILED(hr) || !targetInput) { Fail(MsiEcFailureStage::SpawnInstance, FAILED(hr) ? hr : E_FAIL); return false; }

    HRESULT dataError{};
    if (!GetDataObject(targetInput.Get(), data, dataError))
    {
        // Get_WMI supplies only a compatible embedded Data template.
        ComPtr<IWbemClassObject> templateOutput;
        BSTR instance = SysAllocString(kInstance), operation = SysAllocString(L"Get_WMI");
        hr = services_->ExecMethod(instance, operation, 0, nullptr, nullptr, &templateOutput, nullptr);
        SysFreeString(instance); SysFreeString(operation);
        if (FAILED(hr) || !templateOutput)
        {
            Fail(MsiEcFailureStage::GetWmiFallback, FAILED(hr) ? hr : E_FAIL);
            return false;
        }
        if (!GetDataObject(templateOutput.Get(), data, dataError))
        {
            Fail(MsiEcFailureStage::GetWmiFallback, dataError);
            return false;
        }
    }

    std::array<std::uint8_t, kSize> package{};
    package[0] = selector;
    VARIANT bytes{};
    V_VT(&bytes) = VT_ARRAY | VT_UI1;
    V_ARRAY(&bytes) = SafeArrayCreateVector(VT_UI1, 0, static_cast<ULONG>(package.size()));
    if (!V_ARRAY(&bytes)) { Fail(MsiEcFailureStage::PutBytes, E_OUTOFMEMORY); return false; }
    void* raw{};
    hr = SafeArrayAccessData(V_ARRAY(&bytes), &raw);
    if (FAILED(hr)) { VariantClear(&bytes); Fail(MsiEcFailureStage::PutBytes, hr); return false; }
    std::memcpy(raw, package.data(), package.size());
    hr = SafeArrayUnaccessData(V_ARRAY(&bytes));
    if (FAILED(hr)) { VariantClear(&bytes); Fail(MsiEcFailureStage::PutBytes, hr); return false; }
    // Bytes is an existing WMI embedded-object property; do not override its CIM type.
    hr = data->Put(L"Bytes", 0, &bytes, 0);
    VariantClear(&bytes);
    if (FAILED(hr)) { Fail(MsiEcFailureStage::PutBytes, hr); return false; }

    VARIANT replacement{};
    V_VT(&replacement) = VT_UNKNOWN;
    hr = data.CopyTo(&V_UNKNOWN(&replacement));
    if (FAILED(hr)) { Fail(MsiEcFailureStage::PutInputData, hr); return false; }
    hr = targetInput->Put(L"Data", 0, &replacement, 0);
    VariantClear(&replacement);
    if (FAILED(hr)) { Fail(MsiEcFailureStage::PutInputData, hr); return false; }
    input = std::move(targetInput);
    stage_ = MsiEcFailureStage::None;
    error_ = S_OK;
    return true;
}

bool MsiEcReader::ExtractBytes(IWbemClassObject* output, std::vector<std::uint8_t>& bytes)
{
    if (!output) { Fail(MsiEcFailureStage::GetOutputData, E_INVALIDARG); return false; }
    VARIANT data{};
    HRESULT hr = output->Get(L"Data", 0, &data, nullptr, nullptr);
    if (FAILED(hr)) { Fail(MsiEcFailureStage::GetOutputData, hr); return false; }
    ComPtr<IWbemClassObject> object;
    HRESULT queryError = E_NOINTERFACE;
    if (V_VT(&data) == VT_UNKNOWN && V_UNKNOWN(&data))
        queryError = V_UNKNOWN(&data)->QueryInterface(IID_PPV_ARGS(&object));
    if (V_VT(&data) == VT_DISPATCH && V_DISPATCH(&data))
        queryError = V_DISPATCH(&data)->QueryInterface(IID_PPV_ARGS(&object));
    VariantClear(&data);
    if (!object) { Fail(MsiEcFailureStage::GetOutputData, FAILED(queryError) ? queryError : E_INVALIDARG); return false; }

    VARIANT value{};
    hr = object->Get(L"Bytes", 0, &value, nullptr, nullptr);
    if (FAILED(hr)) { Fail(MsiEcFailureStage::GetOutputBytes, hr); return false; }
    if (V_VT(&value) != (VT_ARRAY | VT_UI1) || !V_ARRAY(&value))
    {
        VariantClear(&value); Fail(MsiEcFailureStage::GetOutputBytes, E_INVALIDARG); return false;
    }
    LONG low{}, high{};
    hr = SafeArrayGetLBound(V_ARRAY(&value), 1, &low);
    if (FAILED(hr)) { VariantClear(&value); Fail(MsiEcFailureStage::GetOutputBytes, hr); return false; }
    hr = SafeArrayGetUBound(V_ARRAY(&value), 1, &high);
    if (FAILED(hr) || high < low)
    {
        VariantClear(&value); Fail(MsiEcFailureStage::GetOutputBytes, FAILED(hr) ? hr : E_INVALIDARG); return false;
    }
    bytes.resize(static_cast<std::size_t>(high - low + 1));
    void* raw{};
    hr = SafeArrayAccessData(V_ARRAY(&value), &raw);
    if (FAILED(hr)) { VariantClear(&value); Fail(MsiEcFailureStage::GetOutputBytes, hr); return false; }
    std::memcpy(bytes.data(), raw, bytes.size());
    hr = SafeArrayUnaccessData(V_ARRAY(&value));
    VariantClear(&value);
    if (FAILED(hr)) { Fail(MsiEcFailureStage::GetOutputBytes, hr); return false; }
    return true;
}

bool MsiEcReader::InvokeGet(const wchar_t* method, std::uint8_t selector,
    std::vector<std::uint8_t>& payload)
{
    payload.clear();
    if (!services_ || !classObject_) { Fail(MsiEcFailureStage::ExecMethod, E_UNEXPECTED); return false; }
    ComPtr<IWbemClassObject> input;
    if (!BuildInput(method, selector, input)) return false;
    BSTR instance = SysAllocString(kInstance), operation = SysAllocString(method);
    ComPtr<IWbemClassObject> output;
    HRESULT hr = services_->ExecMethod(instance, operation, 0, nullptr, input.Get(), &output, nullptr);
    SysFreeString(instance); SysFreeString(operation);
    if (FAILED(hr)) { Fail(MsiEcFailureStage::ExecMethod, hr); return false; }
    if (!output || !ExtractBytes(output.Get(), payload))
    {
        if (stage_ == MsiEcFailureStage::None) Fail(MsiEcFailureStage::GetOutputData, E_FAIL);
        return false;
    }
    if (payload.empty() || payload[0] != 1)
    {
        Fail(MsiEcFailureStage::InvalidSuccessFlag, E_FAIL);
        payload.clear();
        return false;
    }
    payload.erase(payload.begin());
    stage_ = MsiEcFailureStage::None;
    error_ = S_OK;
    return true;
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
