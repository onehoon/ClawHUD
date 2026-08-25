#pragma once

#define _WIN32_DCOM
#include <windows.h>
#include <Wbemidl.h>
#include <wrl/client.h>

#include <cstdint>
#include <optional>
#include <vector>

class MsiEcReader
{
public:
    ~MsiEcReader();
    bool Initialize();
    void Shutdown();
    bool ReadTemperature(std::vector<std::uint8_t>& payload);
    bool ReadFan(std::vector<std::uint8_t>& payload);
    bool ReadData(std::uint8_t selector, std::vector<std::uint8_t>& payload);
    HRESULT LastError() const { return error_; }

private:
    bool InvokeGet(const wchar_t* method, std::uint8_t selector,
        std::vector<std::uint8_t>& payload);
    bool BuildInput(const wchar_t* method, std::uint8_t selector,
        Microsoft::WRL::ComPtr<IWbemClassObject>& input);
    bool ExtractBytes(IWbemClassObject* output, std::vector<std::uint8_t>& bytes);

    Microsoft::WRL::ComPtr<IWbemServices> services_;
    Microsoft::WRL::ComPtr<IWbemClassObject> classObject_;
    bool comInitialized_{};
    HRESULT error_{ E_FAIL };
};

std::optional<int> DecodeFanRpm(std::uint8_t first, std::uint8_t second);
