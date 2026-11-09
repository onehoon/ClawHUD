#include "MsiEcTelemetryReader.h"
#include "TelemetryRetention.h"

#include <iostream>
#include <cmath>
#include <string>
#include <vector>

using namespace clawhud;

namespace
{
bool Check(bool condition, const char* message)
{
    if (condition) return true;
    std::cerr << "FAILED: " << message << '\n';
    return false;
}

struct FakeEcReader
{
    std::vector<std::string> calls;
    std::string failCall;
    ec::EcFailureStage failureStage{ ec::EcFailureStage::None };
    ec::EcFailureStage failureStageOnFail{ ec::EcFailureStage::Pipe };

    bool Fail(const std::string& call)
    {
        calls.push_back(call);
        if (call != failCall)
            return false;
        failureStage = failureStageOnFail;
        return true;
    }

    bool ReadTemperature(std::vector<std::uint8_t>& payload)
    {
        if (Fail("temperature"))
            return false;
        payload = { 67 };
        return true;
    }

    bool ReadFan(std::vector<std::uint8_t>& payload)
    {
        if (Fail("fan"))
            return false;
        payload = { 0x00, 0x70, 0x00, 0x6F };
        return true;
    }

    bool ReadData(std::uint8_t selector, std::vector<std::uint8_t>& payload)
    {
        const auto call = "data:" + std::to_string(selector);
        if (Fail(call))
            return false;
        switch (selector)
        {
        case 221: payload = { 13 }; break;
        case 70: payload = { 0xF9 }; break;
        case 71: payload = { 0xF5 }; break;
        case 74: payload = { 0xF3 }; break;
        case 75: payload = { 0x3D }; break;
        default: return false;
        }
        return true;
    }

    ec::EcFailureStage LastStage() const noexcept { return failureStage; }
};

bool CheckSensorGating()
{
    bool ok = true;
    int constructions = 0;
    FakeEcReader unusedReader;
    const auto read = [&](HudSensorSupport sensors, bool onBattery,
        FakeEcReader& reader)
    {
        return ReadMsiEcHudTelemetry(sensors, onBattery, [&]() -> FakeEcReader&
        {
            ++constructions;
            return reader;
        });
    };

    (void)read({}, true, unusedReader);
    ok &= Check(constructions == 0 && unusedReader.calls.empty(),
        "disabled EC policy creates no reader and performs no reads");

    const HudSensorSupport dischargeOnly{ false, false, false, true };
    (void)read(dischargeOnly, false, unusedReader);
    ok &= Check(constructions == 0 && unusedReader.calls.empty(),
        "AC or unknown battery state does not create a discharge-only reader");

    FakeEcReader temperatureReader;
    constructions = 0;
    const auto temperature = read({ true, false, false, false }, false,
        temperatureReader);
    ok &= Check(constructions == 1 &&
        temperatureReader.calls == std::vector<std::string>{ "temperature" } &&
        temperature.cpuTempC == 67 && !temperature.fan1Rpm &&
        !temperature.cpuPackagePowerW,
        "temperature permission invokes only the temperature operation");

    FakeEcReader fanReader;
    constructions = 0;
    const auto fan = read({ false, true, false, false }, false, fanReader);
    ok &= Check(constructions == 1 &&
        fanReader.calls == std::vector<std::string>{ "fan" } &&
        fan.fan1Rpm == 4285 && fan.fan2Rpm == 4324 && !fan.cpuTempC,
        "fan permission invokes only the fan operation and keeps existing decoding");

    FakeEcReader packageReader;
    constructions = 0;
    const auto package = read({ false, false, true, false }, false,
        packageReader);
    ok &= Check(constructions == 1 && packageReader.calls ==
        std::vector<std::string>{ "data:221" } && package.cpuPackagePowerW == 13,
        "CPU package power permission invokes only selector 221");

    FakeEcReader dischargeReader;
    constructions = 0;
    const auto discharge = read(dischargeOnly, true, dischargeReader);
    ok &= Check(constructions == 1 && dischargeReader.calls ==
        (std::vector<std::string>{ "data:70", "data:71", "data:74", "data:75" }) &&
        discharge.batteryDischargePowerW &&
        std::abs(*discharge.batteryDischargePowerW - 40.710713) < 0.001,
        "battery discharge permission invokes only its ordered four-selector DC sequence");

    FakeEcReader intelReader;
    constructions = 0;
    const auto intel = read({ true, true, true, true }, true, intelReader);
    ok &= Check(constructions == 1 && intelReader.calls ==
        (std::vector<std::string>{ "temperature", "fan", "data:221", "data:70",
            "data:71", "data:74", "data:75" }) && intel.cpuTempC == 67 &&
        intel.fan1Rpm == 4285 && intel.fan2Rpm == 4324 &&
        intel.cpuPackagePowerW == 13 && intel.batteryDischargePowerW,
        "all-enabled Intel policy preserves its original operation order and values");

    FakeEcReader acReader;
    constructions = 0;
    (void)read({ true, true, true, true }, false, acReader);
    ok &= Check(constructions == 1 && acReader.calls ==
        (std::vector<std::string>{ "temperature", "fan", "data:221" }),
        "AC sampling keeps allowed sensors and suppresses only the discharge quartet");

    FakeEcReader failedReader;
    failedReader.failCall = "fan";
    constructions = 0;
    (void)read({ true, true, true, true }, true, failedReader);
    ok &= Check(constructions == 1 && failedReader.calls ==
        (std::vector<std::string>{ "temperature", "fan" }),
        "session failure aborts remaining reads without constructing another reader");

    FakeEcReader nonSessionFailureReader;
    nonSessionFailureReader.failCall = "fan";
    nonSessionFailureReader.failureStageOnFail = ec::EcFailureStage::ExecMethod;
    constructions = 0;
    (void)read({ true, true, true, true }, true, nonSessionFailureReader);
    ok &= Check(constructions == 1 && nonSessionFailureReader.calls ==
        (std::vector<std::string>{ "temperature", "fan", "data:221", "data:70",
            "data:71", "data:74", "data:75" }),
        "non-session EC operation failure retains existing continue-sampling behavior");

    FakeEcReader failedBatteryReader;
    failedBatteryReader.failCall = "data:71";
    failedBatteryReader.failureStageOnFail = ec::EcFailureStage::ExecMethod;
    constructions = 0;
    (void)read(dischargeOnly, true, failedBatteryReader);
    ok &= Check(constructions == 1 && failedBatteryReader.calls ==
        (std::vector<std::string>{ "data:70", "data:71" }),
        "incomplete battery selector quartet remains unavailable and stops immediately");

    return ok;
}
}

int main()
{
    bool ok = true;
    ok &= CheckSensorGating();
    ok &= Check(DecodeCpuTempC(std::vector<std::uint8_t>{0x20}).value() == 32, "CPU temperature 0x20");
    ok &= Check(DecodeCpuTempC(std::vector<std::uint8_t>{0x2C}).value() == 44, "CPU temperature 0x2C");
    ok &= Check(DecodeCpuTempC(std::vector<std::uint8_t>{0x34}).value() == 52, "CPU temperature 0x34");
    ok &= Check(!DecodeCpuTempC(std::vector<std::uint8_t>{0x00}) &&
        !DecodeCpuTempC(std::vector<std::uint8_t>{}), "CPU temperature unavailable");

    ok &= Check(DecodeFanRpm(0x00, 0x6F).value() == 4324, "fan 4324 RPM");
    ok &= Check(DecodeFanRpm(0x00, 0x70).value() == 4285, "fan 4285 RPM");
    ok &= Check(DecodeFanRpm(0x00, 0x71).value() == 4247, "fan signed delta");
    ok &= Check(DecodeFanRpm(0x70, 0x70).value() == 0, "fan zero RPM");
    ok &= Check(!DecodeFanTelemetry(std::vector<std::uint8_t>{0x00, 0x6F, 0x00}), "short fan payload");
    const auto fans = DecodeFanTelemetry(std::vector<std::uint8_t>{0x00, 0x70, 0x00, 0x6F});
    ok &= Check(fans && fans->fan1Rpm.value() == 4285 && fans->fan2Rpm.value() == 4324,
        "fan pair decode");
    ok &= Check(DecodeCpuPackagePowerW(std::vector<std::uint8_t>{0x02}).value() == 2 &&
        DecodeCpuPackagePowerW(std::vector<std::uint8_t>{0x18}).value() == 24 &&
        DecodeCpuPackagePowerW(std::vector<std::uint8_t>{0x00}).value() == 0 &&
        !DecodeCpuPackagePowerW(std::vector<std::uint8_t>{}), "CPU package power decode");

    ok &= Check(DecodeBatteryCurrentMa(0xC8, 0xFC) == -824 &&
        DecodeBatteryCurrentMa(0xDB, 0xF9) == -1573 &&
        DecodeBatteryCurrentMa(0xF9, 0xF5) == -2567 &&
        DecodeBatteryCurrentMa(0xFD, 0xF5) == -2563,
        "signed battery current decode");
    ok &= Check(DecodeBatteryVoltageMv(0xAB, 0x3E) == 16043 &&
        DecodeBatteryVoltageMv(0x1E, 0x3E) == 15902 &&
        DecodeBatteryVoltageMv(0xF3, 0x3D) == 15859 &&
        DecodeBatteryVoltageMv(0xCE, 0x3D) == 15822,
        "battery voltage decode");
    const auto battery = DecodeBatteryPower(0xF9, 0xF5, 0xF3, 0x3D);
    ok &= Check(battery && std::abs(battery->powerW - 40.710713) < 0.001,
        "battery discharge power calculation");
    ok &= Check(!DecodeBatteryPower(0x01, 0x00, 0xF3, 0x3D) &&
        !DecodeBatteryPower(0xF9, 0xF5, 0x00, 0x00),
        "invalid battery power rejected");

    std::optional<int> temperature;
    const std::optional<int> temperature67 = 67;
    const std::optional<int> temperature68 = 68;
    unsigned temperatureMisses = 0;
    const std::optional<int> missing;
    UpdateRetainedTelemetryField(temperature, temperature67, temperatureMisses, 3);
    UpdateRetainedTelemetryField(temperature, missing, temperatureMisses, 3);
    UpdateRetainedTelemetryField(temperature, missing, temperatureMisses, 3);
    ok &= Check(temperature && *temperature == 67,
        "EC temperature retains through two missing samples");
    UpdateRetainedTelemetryField(temperature, missing, temperatureMisses, 3);
    ok &= Check(!temperature,
        "EC temperature clears after three missing samples");
    UpdateRetainedTelemetryField(temperature, temperature68, temperatureMisses, 3);
    ok &= Check(temperature && *temperature == 68 && temperatureMisses == 0,
        "EC temperature recovers immediately");

    std::optional<int> fan1;
    std::optional<int> fan2;
    std::optional<int> tdp;
    unsigned fan1Misses = 0;
    unsigned fan2Misses = 0;
    unsigned tdpMisses = 0;
    const std::optional<int> fan13200 = 3200;
    const std::optional<int> fan23500 = 3500;
    const std::optional<int> fan23600 = 3600;
    const std::optional<int> tdp22 = 22;
    const std::optional<int> tdpZero = 0;
    UpdateRetainedTelemetryField(fan1, fan13200, fan1Misses, 3);
    UpdateRetainedTelemetryField(fan2, fan23500, fan2Misses, 3);
    UpdateRetainedTelemetryField(tdp, tdp22, tdpMisses, 3);
    UpdateRetainedTelemetryField(fan1, missing, fan1Misses, 3);
    UpdateRetainedTelemetryField(fan2, fan23600, fan2Misses, 3);
    ok &= Check(fan1 && *fan1 == 3200 && fan2 && *fan2 == 3600,
        "EC fan fields retain independently");
    UpdateRetainedTelemetryField(tdp, tdpZero, tdpMisses, 3);
    UpdateRetainedTelemetryField(tdp, missing, tdpMisses, 3);
    ok &= Check(tdp && *tdp == 0 && tdpMisses == 1,
        "EC TDP zero is valid and retained");
    return ok ? 0 : 1;
}
