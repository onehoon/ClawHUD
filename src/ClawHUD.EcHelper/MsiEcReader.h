#pragma once

#define _WIN32_DCOM
#include <windows.h>
#include <Wbemidl.h>
#include <wrl/client.h>

#include "../shared/EcHelperProtocol.h"

#include <cstdint>
#include <vector>

class MsiEcReader
{
public:
    ~MsiEcReader();
    bool Initialize();
    void Shutdown();
    bool Read(clawhud::ec::EcOperation operation, std::uint8_t selector,
        std::vector<std::uint8_t>& payload);
    HRESULT LastError() const noexcept { return error_; }
    clawhud::ec::EcFailureStage LastStage() const noexcept { return stage_; }

private:
    bool BuildInput(const wchar_t* method, std::uint8_t selector,
        Microsoft::WRL::ComPtr<IWbemClassObject>& input);
    bool ExtractBytes(IWbemClassObject* output, std::vector<std::uint8_t>& bytes);
    bool PutPackage(IWbemClassObject* data, std::uint8_t selector);
    void Failure(HRESULT error, clawhud::ec::EcFailureStage stage);

    Microsoft::WRL::ComPtr<IWbemServices> services_;
    Microsoft::WRL::ComPtr<IWbemClassObject> classObject_;
    bool comInitialized_{};
    HRESULT error_{ E_FAIL };
    clawhud::ec::EcFailureStage stage_{ clawhud::ec::EcFailureStage::None };
};
