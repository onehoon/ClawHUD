#pragma once

#include "../shared/EcHelperProtocol.h"

#include <windows.h>

#include <cstdint>
#include <string>
#include <vector>

namespace clawhud
{
constexpr bool ShouldAbortEcTelemetrySample(
    ec::EcFailureStage stage) noexcept
{
    using ec::EcFailureStage;
    switch (stage)
    {
    case EcFailureStage::Pipe:
    case EcFailureStage::HelperLaunch:
    case EcFailureStage::HelperMissing:
    case EcFailureStage::HelperNotElevated:
    case EcFailureStage::CoInitialize:
    case EcFailureStage::ConnectWmi:
    case EcFailureStage::GetClass:
        return true;
    default:
        return false;
    }
}
}

class EcHelperClient
{
public:
    explicit EcHelperClient(bool runtimeLogging = true) noexcept
        : runtimeLogging_(runtimeLogging) {}
    ~EcHelperClient();
    bool ReadTemperature(std::vector<std::uint8_t>& payload);
    bool ReadFan(std::vector<std::uint8_t>& payload);
    bool ReadData(std::uint8_t selector, std::vector<std::uint8_t>& payload);
    // Explicit end of this elevated-helper lifetime: closes transport and clears
    // the consumed-launch state, so a later read may begin one new elevation
    // request. Callers use this only at real lifetime boundaries (explicit HUD
    // disable, app / update shutdown), never for automatic failure cleanup.
    void Close();
    HRESULT LastError() const noexcept { return error_; }
    clawhud::ec::EcFailureStage LastStage() const noexcept { return stage_; }
    DWORD HelperPid() const noexcept { return helperPid_; }
    bool Connected() const noexcept { return pipe_ != INVALID_HANDLE_VALUE; }
    bool HelperElevatedVerified() const noexcept { return helperElevated_; }

private:
    bool EnsureConnected();
    bool StartHelper(const std::wstring& pipeName);
    // Closes pipe / helper-process handles only. Deliberately keeps attempted_
    // set: a failed automatic launch/connect stays consumed for this
    // EcHelperClient lifetime so the next 1 s telemetry sample cannot fire
    // another runas / UAC prompt.
    void CloseTransport();
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
    bool runtimeLogging_{ true };
    bool runtimeFailureActive_{};

#ifdef CLAWHUD_EC_HELPER_CLIENT_TESTS
public:
    // Test seam, compiled only into the EcHelperClient lifetime tests. Counts
    // real helper-launch attempts and lets a test force the launch outcome
    // without a real elevated ShellExecuteExW. Not a production DI framework.
    static inline int g_startHelperCalls = 0;
    static inline bool g_forceStartHelperFailure = true;
    bool AttemptedForTests() const noexcept { return attempted_; }
#endif
};
