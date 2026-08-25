#include "MsiEcReader.h"

#include <array>
#include <cstring>

#pragma comment(lib, "wbemuuid.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")

using Microsoft::WRL::ComPtr;
namespace
{
constexpr wchar_t kNamespace[] = L"ROOT\\WMI";
constexpr wchar_t kClass[] = L"MSI_ACPI";
constexpr wchar_t kInstance[] = L"MSI_ACPI.InstanceName='ACPI\\PNP0C14\\0_0'";
constexpr std::size_t kPackageSize = 32;

ComPtr<IWbemClassObject> DataObject(IWbemClassObject* object)
{
    VARIANT value{}; if (FAILED(object->Get(L"Data", 0, &value, nullptr, nullptr))) return {};
    ComPtr<IWbemClassObject> data;
    if (V_VT(&value) == VT_UNKNOWN && V_UNKNOWN(&value)) V_UNKNOWN(&value)->QueryInterface(IID_PPV_ARGS(&data));
    if (V_VT(&value) == VT_DISPATCH && V_DISPATCH(&value)) V_DISPATCH(&value)->QueryInterface(IID_PPV_ARGS(&data));
    VariantClear(&value); return data;
}
}

MsiEcReader::~MsiEcReader() { Shutdown(); }

void MsiEcReader::Failure(HRESULT error, clawhud::ec::EcFailureStage stage)
{
    error_ = error; stage_ = stage;
}

bool MsiEcReader::Initialize()
{
    Shutdown();
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr) && hr != S_FALSE) { Failure(hr, clawhud::ec::EcFailureStage::CoInitialize); return false; }
    comInitialized_ = true;
    hr = CoInitializeSecurity(nullptr, -1, nullptr, nullptr, RPC_C_AUTHN_LEVEL_DEFAULT,
        RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE, nullptr);
    if (FAILED(hr) && hr != RPC_E_TOO_LATE) { Failure(hr, clawhud::ec::EcFailureStage::CoInitialize); Shutdown(); return false; }
    ComPtr<IWbemLocator> locator;
    hr = CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&locator));
    if (FAILED(hr)) { Failure(hr, clawhud::ec::EcFailureStage::ConnectWmi); Shutdown(); return false; }
    BSTR ns = SysAllocString(kNamespace); hr = locator->ConnectServer(ns, nullptr, nullptr, nullptr, 0, nullptr, nullptr, &services_); SysFreeString(ns);
    if (FAILED(hr)) { Failure(hr, clawhud::ec::EcFailureStage::ConnectWmi); Shutdown(); return false; }
    hr = CoSetProxyBlanket(services_.Get(), RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr,
        RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE);
    if (FAILED(hr)) { Failure(hr, clawhud::ec::EcFailureStage::ConnectWmi); Shutdown(); return false; }
    BSTR className = SysAllocString(kClass); hr = services_->GetObject(className, 0, nullptr, &classObject_, nullptr); SysFreeString(className);
    if (FAILED(hr)) { Failure(hr, clawhud::ec::EcFailureStage::GetClass); Shutdown(); return false; }
    error_ = S_OK; stage_ = clawhud::ec::EcFailureStage::None; return true;
}

void MsiEcReader::Shutdown()
{
    classObject_.Reset(); services_.Reset();
    if (comInitialized_) CoUninitialize();
    comInitialized_ = false;
}

bool MsiEcReader::PutPackage(IWbemClassObject* data, std::uint8_t selector)
{
    std::array<std::uint8_t, kPackageSize> package{}; package[0] = selector;
    VARIANT bytes{}; V_VT(&bytes) = VT_ARRAY | VT_UI1;
    V_ARRAY(&bytes) = SafeArrayCreateVector(VT_UI1, 0, static_cast<ULONG>(package.size()));
    if (!V_ARRAY(&bytes)) { Failure(E_OUTOFMEMORY, clawhud::ec::EcFailureStage::CreateSafeArray); return false; }
    void* raw{}; HRESULT hr = SafeArrayAccessData(V_ARRAY(&bytes), &raw);
    if (FAILED(hr) || !raw) { VariantClear(&bytes); Failure(FAILED(hr) ? hr : E_POINTER, clawhud::ec::EcFailureStage::AccessSafeArray); return false; }
    std::memcpy(raw, package.data(), package.size()); hr = SafeArrayUnaccessData(V_ARRAY(&bytes));
    if (FAILED(hr)) { VariantClear(&bytes); Failure(hr, clawhud::ec::EcFailureStage::AccessSafeArray); return false; }
    hr = data->Put(L"Bytes", 0, &bytes, 0); VariantClear(&bytes);
    if (FAILED(hr)) { Failure(hr, clawhud::ec::EcFailureStage::PutBytes); return false; }
    return true;
}

bool MsiEcReader::BuildInput(const wchar_t* method, std::uint8_t selector, ComPtr<IWbemClassObject>& input)
{
    ComPtr<IWbemClassObject> signature, targetInput, data;
    HRESULT hr = classObject_->GetMethod(const_cast<BSTR>(method), 0, &signature, nullptr);
    if (FAILED(hr) || !signature) { Failure(FAILED(hr) ? hr : E_FAIL, clawhud::ec::EcFailureStage::GetMethod); return false; }
    hr = signature->SpawnInstance(0, &targetInput);
    if (FAILED(hr) || !targetInput) { Failure(FAILED(hr) ? hr : E_FAIL, clawhud::ec::EcFailureStage::SpawnInput); return false; }
    data = DataObject(targetInput.Get());
    if (!data)
    {
        ComPtr<IWbemClassObject> templateOutput;
        BSTR instance = SysAllocString(kInstance), operation = SysAllocString(L"Get_WMI");
        hr = services_->ExecMethod(instance, operation, 0, nullptr, nullptr, &templateOutput, nullptr);
        SysFreeString(instance); SysFreeString(operation);
        if (FAILED(hr) || !templateOutput) { Failure(FAILED(hr) ? hr : E_FAIL, clawhud::ec::EcFailureStage::GetWmiFallback); return false; }
        data = DataObject(templateOutput.Get());
    }
    if (!data) { Failure(E_INVALIDARG, clawhud::ec::EcFailureStage::GetInputData); return false; }
    if (!PutPackage(data.Get(), selector)) return false;
    VARIANT replacement{}; V_VT(&replacement) = VT_UNKNOWN;
    hr = data.CopyTo(&V_UNKNOWN(&replacement));
    if (FAILED(hr)) { VariantClear(&replacement); Failure(hr, clawhud::ec::EcFailureStage::PutInputData); return false; }
    hr = targetInput->Put(L"Data", 0, &replacement, 0); VariantClear(&replacement);
    if (FAILED(hr)) { Failure(hr, clawhud::ec::EcFailureStage::PutInputData); return false; }
    input = std::move(targetInput); return true;
}

bool MsiEcReader::ExtractBytes(IWbemClassObject* output, std::vector<std::uint8_t>& bytes)
{
    VARIANT data{}; HRESULT hr = output->Get(L"Data", 0, &data, nullptr, nullptr);
    if (FAILED(hr)) { Failure(hr, clawhud::ec::EcFailureStage::GetOutputData); return false; }
    ComPtr<IWbemClassObject> object;
    if (V_VT(&data) == VT_UNKNOWN && V_UNKNOWN(&data)) V_UNKNOWN(&data)->QueryInterface(IID_PPV_ARGS(&object));
    if (V_VT(&data) == VT_DISPATCH && V_DISPATCH(&data)) V_DISPATCH(&data)->QueryInterface(IID_PPV_ARGS(&object));
    VariantClear(&data); if (!object) { Failure(E_INVALIDARG, clawhud::ec::EcFailureStage::GetOutputData); return false; }
    VARIANT value{}; hr = object->Get(L"Bytes", 0, &value, nullptr, nullptr);
    if (FAILED(hr) || V_VT(&value) != (VT_ARRAY | VT_UI1)) { VariantClear(&value); Failure(FAILED(hr) ? hr : E_INVALIDARG, clawhud::ec::EcFailureStage::GetOutputBytes); return false; }
    LONG low{}, high{}; hr = SafeArrayGetLBound(V_ARRAY(&value), 1, &low);
    if (FAILED(hr)) { VariantClear(&value); Failure(hr, clawhud::ec::EcFailureStage::GetOutputBytes); return false; }
    hr = SafeArrayGetUBound(V_ARRAY(&value), 1, &high);
    if (FAILED(hr) || high < low) { VariantClear(&value); Failure(FAILED(hr) ? hr : E_INVALIDARG, clawhud::ec::EcFailureStage::GetOutputBytes); return false; }
    bytes.resize(static_cast<size_t>(high - low + 1)); void* raw{}; hr = SafeArrayAccessData(V_ARRAY(&value), &raw);
    if (FAILED(hr) || !raw) { VariantClear(&value); Failure(FAILED(hr) ? hr : E_POINTER, clawhud::ec::EcFailureStage::AccessSafeArray); return false; }
    std::memcpy(bytes.data(), raw, bytes.size()); hr = SafeArrayUnaccessData(V_ARRAY(&value)); VariantClear(&value);
    if (FAILED(hr)) { Failure(hr, clawhud::ec::EcFailureStage::AccessSafeArray); return false; }
    return true;
}

bool MsiEcReader::Read(clawhud::ec::EcOperation operation, std::uint8_t selector, std::vector<std::uint8_t>& payload)
{
    payload.clear(); if (!services_ || !classObject_) { Failure(E_UNEXPECTED, clawhud::ec::EcFailureStage::ConnectWmi); return false; }
    const wchar_t* method = operation == clawhud::ec::EcOperation::GetTemperature ? L"Get_Temperature" :
        operation == clawhud::ec::EcOperation::GetFan ? L"Get_Fan" : L"Get_Data";
    ComPtr<IWbemClassObject> input; if (!BuildInput(method, selector, input)) return false;
    BSTR instance = SysAllocString(kInstance), operationName = SysAllocString(method); ComPtr<IWbemClassObject> output;
    HRESULT hr = services_->ExecMethod(instance, operationName, 0, nullptr, input.Get(), &output, nullptr);
    SysFreeString(instance); SysFreeString(operationName);
    if (FAILED(hr)) { Failure(hr, clawhud::ec::EcFailureStage::ExecMethod); return false; }
    if (!output) { Failure(E_POINTER, clawhud::ec::EcFailureStage::GetOutputData); return false; }
    if (!ExtractBytes(output.Get(), payload)) return false;
    if (payload.empty()) { Failure(E_INVALIDARG, clawhud::ec::EcFailureStage::InvalidResponse); return false; }
    if (payload[0] != 1) { payload.clear(); Failure(E_FAIL, clawhud::ec::EcFailureStage::InvalidSuccessFlag); return false; }
    payload.erase(payload.begin()); error_ = S_OK; stage_ = clawhud::ec::EcFailureStage::None; return true;
}
