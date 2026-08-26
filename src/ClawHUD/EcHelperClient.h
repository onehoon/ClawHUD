#pragma once

#include "../shared/EcHelperProtocol.h"

#include <windows.h>

#include <cstdint>
#include <string>
#include <vector>

class EcHelperClient
{
public:
    ~EcHelperClient();
    bool ReadTemperature(std::vector<std::uint8_t>& payload);
    bool ReadFan(std::vector<std::uint8_t>& payload);
    bool ReadData(std::uint8_t selector, std::vector<std::uint8_t>& payload);
    void Close();
    HRESULT LastError() const noexcept { return error_; }
    clawhud::ec::EcFailureStage LastStage() const noexcept { return stage_; }
    DWORD HelperPid() const noexcept { return helperPid_; }
    bool Connected() const noexcept { return pipe_ != INVALID_HANDLE_VALUE; }
    bool HelperElevatedVerified() const noexcept { return helperElevated_; }

private:
    bool EnsureConnected();
    bool StartHelper(const std::wstring& pipeName);
    bool Send(clawhud::ec::EcOperation operation, std::uint8_t selector,
        std::vector<std::uint8_t>& payload);
    bool WriteAll(const void* data, DWORD size);
    bool ReadAll(void* data, DWORD size);
    void Failure(HRESULT error, clawhud::ec::EcFailureStage stage);

    HANDLE pipe_{ INVALID_HANDLE_VALUE };
    HANDLE helperProcess_{};
    DWORD helperPid_{};
    HRESULT error_{ E_FAIL };
    clawhud::ec::EcFailureStage stage_{ clawhud::ec::EcFailureStage::None };
    std::wstring pipeName_;
    bool attempted_{};
    bool helperElevated_{};
    bool runtimeFailureActive_{};
};
