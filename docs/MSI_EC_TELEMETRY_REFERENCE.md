# ClawHUD MSI EC Telemetry Reference

> **Status:** implementation reference / source of truth for ClawHUD EC telemetry.
>
> **Scope:** read-only MSI Claw EC/WMI telemetry needed by ClawHUD. This document intentionally does **not** define fan-control or TDP-write product features.
>
> **Last consolidated:** 2026-08-25.

## 1. Purpose

ClawHUD is a lightweight Windows 11 performance HUD for supported MSI Claw handhelds. The EC portion of ClawHUD only needs to read a small set of MSI platform values that are either unavailable from Intel telemetry or are better sourced from the MSI embedded controller.

The required EC-backed values are:

- CPU temperature.
- GPU temperature.
- Fan 1 RPM.
- Fan 2 RPM.
- CPU package power.
- Battery-side current and voltage used for DC system-power estimation.

The intended production design is deliberately small:

```text
ClawHUD.EcHelper / MsiEcReader
  -> private Named Pipe -> ClawHUD.EcHelperClient
  -> MSI_ACPI WMI transport (helper only)
  -> validated raw payload
  -> small decode functions
  -> MsiEcSnapshot
  -> Diagnostics / TelemetrySnapshot / HUD
```

Do not introduce a service host, provider registry, DI container, manager hierarchy, or generic RPC framework. The approved second process is only `ClawHUD.EcHelper.exe`, a narrowly scoped elevated read-only transport for this EC boundary.

## 2. ClawHUD product constraints

These constraints are authoritative for EC integration:

1. Windows 11 x64 only.
2. MSI Claw supported boards only.
3. One Windows user / one interactive session.
4. ClawHUD is a lightweight personal gaming add-on; avoid enterprise-style architecture.
5. Startup is tray-first; the settings UI is not created until explicitly opened.
6. Diagnostics must have approximately zero idle cost when the Diagnostics tab is not active.
7. EC access is **read-only** for ClawHUD telemetry.
8. Keep the main application unelevated; supported EC reads require elevation, so the approved production transport uses a narrowly scoped elevated read-only helper, launched lazily on the first production EC demand. See section 14 for the helper lifetime rules.
9. Missing/invalid EC data means **Unavailable**, never a synthetic zero that looks like valid hardware data.
10. No continuous retry/reconcile loop. A failed sample may be retried on the next normal sample.

Supported board IDs currently relevant to this project:

```text
A2VM: MS-1T42, MS-1T52
EX:   MS-1T91
```

Unknown boards must not be assumed protocol-compatible merely because `MSI_ACPI` exists.

---

## 3. Evidence and reference priority

### Primary ClawHUD implementation references

Current repository code in `onehoon/SteamAddonforClaw`:

```text
src/SteamInputAddonforClaw/Devices/MSI/Claw/MsiClawWmiTdpTransport.cs
src/SteamInputAddonforClaw/Devices/MSI/Claw/IMsiClawTdpTransport.cs
src/SteamInputAddonforClaw/Devices/MSI/Claw/FanProbe.cs
src/SteamInputAddonforClaw.TdpHelper/Program.cs
src/SteamInputAddonforClaw.TdpHelper/TdpHelperProtocol.cs
```

These files provide the known-good MSI WMI packaging/invocation behavior, fallback behavior, response validation, and real-device fan probe observations.

### Google Drive research documents

```text
Documents/MSI_CLAW_FAN_CONTROL_COMPLETE_RE_UPDATED_2026-08-24.md
Documents/MSI_OVERLAY_REALTIME_MONITOR_RESEARCH_RESULT.md
Documents/HHC_msiapcfg_analysis.txt
```

The fan-control report contains direct MSI Center M / `MSIWMIACPI2.dll` / `MCMOSDInfo.exe` reverse engineering plus real-device EX probe results. The OSD report establishes the exact MSI telemetry selectors used by the stock monitor. The HHC analysis independently corroborates the MSI ACPI/WMI transport family.

### Reference priority

For ClawHUD EC reads:

```text
Direct MSI binary RE / real Claw observations
    > current SteamAddonforClaw transport/probe code
    > current HHC/CTW corroboration
    > old comments or historical assumptions
```

Do not copy proprietary MSI code. Reimplement the externally observable WMI protocol and formulas documented here.

---

## 4. MSI ACPI-WMI protocol

The current evidence consistently identifies the transport as:

```text
Namespace: root\WMI
Class:     MSI_ACPI
Instance:  MSI_ACPI.InstanceName='ACPI\\PNP0C14\\0_0'
```

### Request package

The new-EC path used by the current Claw implementations uses a 32-byte request package:

```text
byte[32]

request[0] = selector / data block / index
request[1] = value for simple Set_Data-style writes
request[1..] = method payload for payload writes
remaining bytes = zero
```

ClawHUD only needs reads, therefore its request package is normally:

```cpp
std::array<std::uint8_t, 32> request{};
request[0] = selector;
```

### Response package

For `Get_*` calls:

```text
response[0] = success flag
response[0] == 1 -> success
response[1..] = returned payload
```

The current SteamAddon transport removes the success byte and returns `bytes[1..]`.

**Important real-device detail:** the generic wrapper can return a much larger payload than the logical value. EX fan probing observed a 31-byte post-success payload for `Get_Fan`, while the logical fan block is only the first 8 bytes. Therefore every decoder must validate and consume only the documented prefix it owns.

Never treat a zero-filled large buffer as proof of a valid EC read.

### Compatibility fallback

Current SteamAddon behavior:

1. Ask the target method for its method parameters.
2. Obtain the embedded `Data` object.
3. If the direct input template is unavailable, invoke `Get_WMI` and use the returned `Data` object as the compatibility template.
4. Put the 32-byte package into `Data.Bytes`.
5. Invoke the requested method.
6. Validate the output object, embedded `Data`, byte array, and leading success flag.

ClawHUD should preserve this fallback because it is already proven on the supported MSI path and was used successfully during EX hardware fan tests.

### WMI version note

Do **not** build a speculative WMI2/WMI3 dispatch architecture for ClawHUD telemetry.

An EX probe reported raw `Get_WMI` data beginning with `02 09 ...`, while the existing helper's version interpretation produced a suspicious `9.0`. Regardless of that decoder ambiguity, the real EX hardware repeatedly succeeded with the 32-byte `Get_Fan` / `Set_Fan` path and the `Get_WMI` parameter-template fallback. No separate `Get_Fan_64` method was present in the observed method inventory.

For ClawHUD, probe the methods actually required. Do not gate basic telemetry on a guessed major-version decoder.

---

## 5. Required ClawHUD EC selectors

| Metric | MSI WMI call | Logical data used | Confidence |
|---|---|---|---|
| Fan 1 tach | `Get_Fan(0)` | payload bytes `[0],[1]` | CONFIRMED by MSI OSD binary |
| Fan 2 tach | `Get_Fan(0)` | payload bytes `[2],[3]` | STRONGLY SUPPORTED by two-fan RE/logs; hardware validate |
| CPU temp | `Get_Temperature(0)` | payload `[0]` | CONFIRMED by MSI OSD RE |
| GPU temp | `Get_Temperature(0)` | payload `[1]` | CONFIRMED by MSI OSD RE |
| CPU package power | `Get_Data(221)` | first logical data byte | CONFIRMED for normal Intel Claw OSD path |
| Battery current low | `Get_Data(70)` | first logical data byte | CONFIRMED source index |
| Battery current high | `Get_Data(71)` | first logical data byte | CONFIRMED source index |
| Battery voltage low | `Get_Data(74)` | first logical data byte | CONFIRMED source index |
| Battery voltage high | `Get_Data(75)` | first logical data byte | CONFIRMED source index |

### Do not confuse current temperature with fan-curve temperature axes

Two different uses of `Get_Temperature` exist:

```text
Get_Temperature(0)
    -> current CPU/GPU temperatures for OSD telemetry

Get_Temperature(1/2)
    -> fan-curve/default temperature-axis information
```

ClawHUD telemetry uses selector **0**.

The fan-control research uses selectors 1/2 for six curve labels; those are not the current CPU/GPU temperature source.

### Do not use PL1/PL2 blocks as current power

```text
Get/Set_Data(80) = PL1 target
Get/Set_Data(81) = PL2 target
```

These are configured power-limit targets, not current CPU package power. ClawHUD's CPU power metric must use `Get_Data(221)` where supported.

---

## 6. Snapshot model

Recommended minimal data model:

```cpp
#pragma once

#include <cstdint>
#include <optional>

struct MsiEcSnapshot
{
    std::optional<int> cpuTempC;
    std::optional<int> gpuTempC;

    std::optional<int> fan1Rpm;
    std::optional<int> fan2Rpm;

    std::optional<double> cpuPackagePowerW;

    // Raw battery-side EC values. Keep raw values available for Diagnostics.
    std::optional<std::uint16_t> batteryCurrentRaw;
    std::optional<std::uint16_t> batteryVoltageMv;

    // Meaningful only while discharging / DC according to product policy.
    std::optional<double> systemPowerW;
};
```

Do not make `0` the failure sentinel. A stopped fan may legitimately be 0 RPM and a missing read must remain distinguishable from valid zero.

---

## 7. Minimal reader surface

Avoid a generic provider framework. A small concrete class is enough:

```cpp
class MsiEcReader
{
public:
    bool Initialize();
    void Shutdown();

    bool ReadFanTelemetry(std::vector<std::uint8_t>& payload);
    bool ReadCurrentTemperature(std::vector<std::uint8_t>& payload);
    bool ReadData(std::uint8_t block, std::uint8_t& value);

    MsiEcSnapshot ReadSnapshot(bool onAcPower);

private:
    bool InvokeGet(
        const wchar_t* method,
        std::uint8_t selector,
        std::vector<std::uint8_t>& payload);
};
```

`Initialize()` should establish the WMI connection only when EC telemetry is actually needed. Do not initialize WMI solely because the tray process started if EC telemetry/HUD is disabled.

---

## 8. Native C++ WMI implementation blueprint

ClawHUD is native C++. Use the normal COM/WMI APIs rather than introducing .NET solely for EC reads.

Expected libraries/headers:

```cpp
#define _WIN32_DCOM
#include <Windows.h>
#include <Wbemidl.h>
#include <comdef.h>
#include <wrl/client.h>

#pragma comment(lib, "wbemuuid.lib")
```

Microsoft's normal provider-method flow is:

```text
CoInitializeEx
CoInitializeSecurity
CoCreateInstance(CLSID_WbemLocator)
IWbemLocator::ConnectServer(root\WMI)
CoSetProxyBlanket
IWbemServices::GetObject(MSI_ACPI class)
IWbemClassObject::GetMethod
IWbemClassObject::SpawnInstance
populate input parameters
IWbemServices::ExecMethod
extract output parameters
```

Official references:

- https://learn.microsoft.com/windows/win32/wmisdk/calling-a-provider-method
- https://learn.microsoft.com/windows/win32/api/wbemcli/nf-wbemcli-iwbemservices-execmethod
- https://learn.microsoft.com/windows/win32/api/wbemcli/nf-wbemcli-iwbemclassobject-getmethod

### Connection constants

```cpp
namespace MsiEc
{
    inline constexpr wchar_t kNamespace[] = L"ROOT\\WMI";
    inline constexpr wchar_t kClass[] = L"MSI_ACPI";
    inline constexpr wchar_t kInstancePath[] =
        L"MSI_ACPI.InstanceName='ACPI\\\\PNP0C14\\\\0_0'";
    inline constexpr std::size_t kPackageSize = 32;
}
```

When implementing the final literal, verify the exact object-path escaping in the debugger/Diagnostics output. The semantic instance path is `ACPI\PNP0C14\0_0`.

### COM initialization sketch

```cpp
bool MsiEcReader::Initialize()
{
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE)
        return false;

    // CoInitializeSecurity is process-wide. If App startup already owns COM
    // security initialization, do not call it twice. Keep one authority.

    Microsoft::WRL::ComPtr<IWbemLocator> locator;
    hr = CoCreateInstance(
        CLSID_WbemLocator,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&locator));
    if (FAILED(hr))
        return false;

    Microsoft::WRL::ComPtr<IWbemServices> services;
    hr = locator->ConnectServer(
        _bstr_t(MsiEc::kNamespace),
        nullptr, nullptr, nullptr,
        0, nullptr, nullptr,
        &services);
    if (FAILED(hr))
        return false;

    hr = CoSetProxyBlanket(
        services.Get(),
        RPC_C_AUTHN_WINNT,
        RPC_C_AUTHZ_NONE,
        nullptr,
        RPC_C_AUTHN_LEVEL_CALL,
        RPC_C_IMP_LEVEL_IMPERSONATE,
        nullptr,
        EOAC_NONE);
    if (FAILED(hr))
        return false;

    services_ = std::move(services);
    return true;
}
```

Keep COM ownership consistent with the eventual main app. If the UI thread/runtime already initializes COM, do not blindly add a competing process-wide COM/security lifecycle.

### Method invocation contract

The MSI method parameter named `Data` is an embedded WMI object containing a byte-array property named `Bytes`. The native implementation must mirror the proven `.NET System.Management` behavior from SteamAddon:

```text
input = GetMethodParameters(method)
data  = input.Data embedded object

if input/data unavailable:
    templateOutput = Invoke Get_WMI
    data = templateOutput.Data embedded object

package = byte[32]
package[0] = selector

data.Bytes = package
input.Data = data

output = ExecMethod(instancePath, method, input)
response = output.Data.Bytes

require response length >= 1
require response[0] == 1
return response[1..]
```

This is the important protocol contract. The exact COM helper used to construct/assign the embedded `Data` object may be factored into one private function, but do not build a general WMI framework around it.

Recommended private helpers:

```cpp
bool BuildMethodInput(
    const wchar_t* method,
    std::span<const std::uint8_t> package,
    Microsoft::WRL::ComPtr<IWbemClassObject>& input);

bool ExtractResponseBytes(
    IWbemClassObject* output,
    std::vector<std::uint8_t>& bytes);
```

### Generic read

```cpp
bool MsiEcReader::InvokeGet(
    const wchar_t* method,
    std::uint8_t selector,
    std::vector<std::uint8_t>& payload)
{
    payload.clear();

    if (!services_)
        return false;

    std::array<std::uint8_t, MsiEc::kPackageSize> package{};
    package[0] = selector;

    Microsoft::WRL::ComPtr<IWbemClassObject> input;
    if (!BuildMethodInput(method, package, input))
        return false;

    Microsoft::WRL::ComPtr<IWbemClassObject> output;
    const HRESULT hr = services_->ExecMethod(
        _bstr_t(MsiEc::kInstancePath),
        _bstr_t(method),
        0,
        nullptr,
        input.Get(),
        &output,
        nullptr);

    if (FAILED(hr) || !output)
        return false;

    std::vector<std::uint8_t> response;
    if (!ExtractResponseBytes(output.Get(), response))
        return false;

    if (response.empty() || response[0] != 1)
        return false;

    payload.assign(response.begin() + 1, response.end());
    return true;
}
```

The above is the intended control flow. Do not report success merely because `ExecMethod` returned normally; the MSI success flag and logical payload length must also be valid.

### Simple wrappers

```cpp
bool MsiEcReader::ReadFanTelemetry(std::vector<std::uint8_t>& payload)
{
    return InvokeGet(L"Get_Fan", 0, payload);
}

bool MsiEcReader::ReadCurrentTemperature(std::vector<std::uint8_t>& payload)
{
    return InvokeGet(L"Get_Temperature", 0, payload);
}

bool MsiEcReader::ReadData(std::uint8_t block, std::uint8_t& value)
{
    std::vector<std::uint8_t> payload;
    if (!InvokeGet(L"Get_Data", block, payload) || payload.empty())
        return false;

    value = payload[0];
    return true;
}
```

---

## 9. Fan RPM decode

### Fan 1

MSI `MCMOSDInfo.exe` directly uses `Get_Fan(0)` and the first tach pair. The confirmed formula is:

```text
RPM = abs(60,000,000 / ((byte0 - byte1) * 2 * 62.5))
    = abs(480,000 / (byte0 - byte1))
```

Use signed arithmetic for the subtraction and guard zero denominator.

```cpp
std::optional<int> DecodeFanRpm(std::uint8_t a, std::uint8_t b)
{
    const int delta = static_cast<int>(a) - static_cast<int>(b);
    if (delta == 0)
        return 0; // valid stopped/unavailable-tach state; transport failure is separate

    return static_cast<int>(std::abs(480000.0 / static_cast<double>(delta)));
}
```

A transport failure must remain `std::nullopt`; a successful tach read with equal bytes may be represented as 0 RPM.

### Fan 2

Project RE and real EX logs show `Get_Fan(0)` carrying a second independently changing pair in bytes `[2],[3]`. This strongly supports:

```cpp
fan1Rpm = DecodeFanRpm(payload[0], payload[1]);
fan2Rpm = DecodeFanRpm(payload[2], payload[3]);
```

Evidence distinction must remain documented:

- Fan 1 bytes 0/1 + formula: directly confirmed in MSI OSD binary.
- Fan 2 bytes 2/3 + same formula: strongly supported by two-fan RE/log behavior, but the analyzed MSI OSD method itself only consumes the first pair.

Diagnostics must show both raw pairs so the mapping can be closed on each supported board without changing the transport.

### Decoder

```cpp
struct FanTelemetry
{
    std::optional<int> fan1Rpm;
    std::optional<int> fan2Rpm;
};

std::optional<FanTelemetry> DecodeFanTelemetry(
    std::span<const std::uint8_t> payload)
{
    if (payload.size() < 4)
        return std::nullopt;

    return FanTelemetry{
        .fan1Rpm = DecodeFanRpm(payload[0], payload[1]),
        .fan2Rpm = DecodeFanRpm(payload[2], payload[3]),
    };
}
```

Do not normalize `Get_Fan(0)` to the fan-control 8-byte table semantics before extracting the tach pairs; selector 0 is telemetry.

---

## 10. CPU/GPU temperature decode

MSI OSD reads:

```text
Get_Temperature(0)
payload[0] -> CPU temperature °C
payload[1] -> GPU temperature °C
```

Implementation:

```cpp
struct TemperatureTelemetry
{
    int cpuTempC;
    int gpuTempC;
};

std::optional<TemperatureTelemetry> DecodeCurrentTemperature(
    std::span<const std::uint8_t> payload)
{
    if (payload.size() < 2)
        return std::nullopt;

    return TemperatureTelemetry{
        .cpuTempC = static_cast<int>(payload[0]),
        .gpuTempC = static_cast<int>(payload[1]),
    };
}
```

No IGCL temperature fallback is required to make the MSI Claw HUD temperature metric work. This EC path is particularly useful on hardware/driver combinations where a given IGCL temperature metric is unsupported.

---

## 11. CPU package power

For the normal Intel Claw OSD path, MSI reads:

```text
Get_Data(221)
```

The first logical data byte is treated as CPU package power in watts.

```cpp
std::optional<double> ReadCpuPackagePowerW(MsiEcReader& ec)
{
    std::uint8_t value{};
    if (!ec.ReadData(221, value))
        return std::nullopt;

    return static_cast<double>(value);
}
```

Keep the field name explicit:

```text
cpuPackagePowerW
```

Do not call it `tdp` and do not derive it from PL1/PL2 blocks 80/81.

---

## 12. Battery-side system power

MSI OSD uses four EC data blocks:

```text
Get_Data(70)
Get_Data(71)
Get_Data(74)
Get_Data(75)
```

The project interpretation is:

```text
70/71 -> 16-bit battery current
74/75 -> 16-bit battery voltage
```

The voltage pair is interpreted in millivolts. The current pair is a signed/discharge representation and must be converted to a positive discharge magnitude before computing system power.

### Raw combination

```cpp
constexpr std::uint16_t CombineU16(
    std::uint8_t lo,
    std::uint8_t hi) noexcept
{
    return static_cast<std::uint16_t>(lo) |
           (static_cast<std::uint16_t>(hi) << 8);
}
```

### Working discharge interpretation

Prior project RE records the MSI discharge path as converting the 16-bit current field to a positive magnitude using the complemented raw value for discharge. Preserve the raw value in Diagnostics and validate the exact edge behavior on hardware before treating the formula as an immutable protocol guarantee.

```cpp
std::uint16_t DecodeDischargeMilliAmps(std::uint16_t raw) noexcept
{
    // Working MSI OSD interpretation from project RE.
    // Revalidate exact complement/+1 edge behavior against raw hardware samples.
    return static_cast<std::uint16_t>(~raw);
}
```

### System power

```cpp
std::optional<double> DecodeSystemPowerW(
    std::uint16_t currentRaw,
    std::uint16_t voltageMv,
    bool onAcPower)
{
    if (onAcPower)
        return std::nullopt;

    const auto currentMa = DecodeDischargeMilliAmps(currentRaw);
    if (currentMa == 0 || voltageMv == 0)
        return std::nullopt;

    return (static_cast<double>(currentMa) / 1000.0) *
           (static_cast<double>(voltageMv) / 1000.0);
}
```

### Product policy

This is a battery-discharge-based **System Power** metric. Therefore:

```text
DC / battery:
    show System Power

AC connected:
    hide System Power
```

Do not show 0 W or a meaningless charging-side number merely because the raw current encoding changed sign/meaning.

### Required implementation validation

Before production release, Diagnostics should log:

```text
Current raw 70/71: 0x....
Voltage raw 74/75: .... mV
Decoded current: ... mA
System Power: ... W
Windows AC/DC state
```

Compare the result against MSI Center M's OSD on the same hardware/load. If the current magnitude is consistently off by one count, resolve the exact two's-complement edge (`~raw` versus `~raw + 1`) from the direct binary/real-device comparison and update this document.

---

## 13. Full snapshot read

Keep the snapshot routine boring and explicit:

```cpp
MsiEcSnapshot MsiEcReader::ReadSnapshot(bool onAcPower)
{
    MsiEcSnapshot snapshot{};

    std::vector<std::uint8_t> fan;
    if (ReadFanTelemetry(fan))
    {
        if (auto decoded = DecodeFanTelemetry(fan))
        {
            snapshot.fan1Rpm = decoded->fan1Rpm;
            snapshot.fan2Rpm = decoded->fan2Rpm;
        }
    }

    std::vector<std::uint8_t> temp;
    if (ReadCurrentTemperature(temp))
    {
        if (auto decoded = DecodeCurrentTemperature(temp))
        {
            snapshot.cpuTempC = decoded->cpuTempC;
            snapshot.gpuTempC = decoded->gpuTempC;
        }
    }

    std::uint8_t cpuPower{};
    if (ReadData(221, cpuPower))
        snapshot.cpuPackagePowerW = static_cast<double>(cpuPower);

    std::uint8_t currentLo{}, currentHi{}, voltageLo{}, voltageHi{};
    const bool batteryReadsOk =
        ReadData(70, currentLo) &&
        ReadData(71, currentHi) &&
        ReadData(74, voltageLo) &&
        ReadData(75, voltageHi);

    if (batteryReadsOk)
    {
        const auto currentRaw = CombineU16(currentLo, currentHi);
        const auto voltageMv = CombineU16(voltageLo, voltageHi);

        snapshot.batteryCurrentRaw = currentRaw;
        snapshot.batteryVoltageMv = voltageMv;
        snapshot.systemPowerW = DecodeSystemPowerW(
            currentRaw,
            voltageMv,
            onAcPower);
    }

    return snapshot;
}
```

There is no need for one WMI call per field if later profiling shows a safe batching opportunity, but do not optimize before measuring. Correctness and simple failure isolation are more important than saving a few method calls in the first implementation.

---

## 14. Polling / lifecycle policy

MSI Center M's stock OSD polls its sensor data at approximately 2 seconds. ClawHUD may eventually need a faster HUD refresh, but the EC itself should not be polled at frame-render rate.

Initial recommendation:

```text
EC sample interval: 1000-2000 ms
HUD render interval: independent / faster
```

The renderer should reuse the latest snapshot between EC samples.

Do not tie WMI reads to every overlay draw.

### Tray idle

If HUD telemetry is disabled and Diagnostics is closed:

```text
no EC polling
no WMI diagnostic loop
no D3D diagnostic resources
```

If the production HUD is enabled, only the production telemetry loop owns EC sampling.

### EC elevation and helper lifetime (authoritative)

Settled after Cleanup 1:

- Supported MSI Claw EC reads used by ClawHUD require elevation.
- `ClawHUD.exe` remains unelevated.
- `ClawHUD.EcHelper.exe` is the narrow read-only, selector-whitelisted elevated boundary.
- The helper is launched lazily on the first production EC demand (the first `SampleSystemEc()` of a visible HUD session), not at install / app / tray startup.
- A healthy helper is reused across transient HUD sampling pauses: In-Game Only hide/show, F8 hide/show, visibility reconciliation, and suspend/resume all preserve the connection.
- No EC polling occurs while production sampling is stopped, even though the helper process stays alive.
- UAC cancellation or a failed helper bootstrap does not automatically re-prompt: the launch attempt is consumed for that `EcHelperClient` lifetime, so the next 1 s sample issues no new `runas`. EC-derived values are simply reported unavailable.
- Explicit **Enable HUD** off releases the helper (its private pipe closes and the elevated child exits); a later explicit re-enable may begin a new helper lifetime and request elevation once.
- App exit, `RequestShutdown`, and Velopack-driven shutdown release the helper before ClawHUD exits; no orphan helper remains.

### Diagnostics

When Diagnostics is open:

- If production EC telemetry is already active, show its latest decoded values and optionally expose an explicit one-shot raw read.
- If production EC telemetry is inactive, Diagnostics may create the reader lazily and perform one-shot or user-started bounded live reads.
- Leaving the Diagnostics tab/settings window must stop Diagnostics-owned live sampling and release Diagnostics-only resources.

Do not run a second permanent EC polling loop just for Diagnostics.

---

## 15. Unelevated read validation

> **Resolved:** on the supported MSI Claw hardware the required read methods do **not** succeed unelevated. ClawHUD ships the elevated `ClawHUD.EcHelper.exe` boundary (section 14). The probe below is retained as the historical validation that reached that conclusion; it is not a pending investigation.

Current fan/TDP hardware probes in SteamAddon used an elevated helper because they performed writes. That did **not** by itself prove that the read-only methods needed by ClawHUD require elevation, so the read path was probed as a normal user first.

Required unelevated probe set:

```text
Get_Fan(0)
Get_Temperature(0)
Get_Data(221)
Get_Data(70)
Get_Data(71)
Get_Data(74)
Get_Data(75)
```

### PowerShell reference probe

Run from a **non-elevated Windows PowerShell 5.1** session:

```powershell
$scope = New-Object System.Management.ManagementScope("\\.\root\WMI")
$scope.Connect()

$path = New-Object System.Management.ManagementPath(
    "MSI_ACPI.InstanceName='ACPI\\PNP0C14\\0_0'"
)

$obj = New-Object System.Management.ManagementObject(
    $scope,
    $path,
    $null
)

$obj.Get()
"Connected to MSI_ACPI"
$obj.Path.Path
```

The method-call helper should follow the same object model used by SteamAddon: obtain the method parameters, populate the embedded `Data.Bytes` 32-byte package, invoke the method, and inspect the response success byte.

Regardless of the probe outcome, `ClawHUD.exe` remains a normal-user process. The production transport keeps that boundary explicit: `ClawHUD.exe` never calls MSI WMI directly, and `ClawHUD.EcHelper.exe` performs only the whitelisted read methods after verifying elevation. This helper is intentionally limited to the EC read path; it is not a service, writer, or generic WMI RPC endpoint.

---

## 16. Failure handling

ClawHUD EC telemetry is read-only, so failure policy should stay very small. The helper is launched lazily on the first production EC demand, reused for the whole helper lifetime (section 14), and terminates when the main process closes the private named pipe. UAC cancellation or a missing/failed helper leaves the main app running and marks EC unavailable; the launch attempt is consumed for that helper lifetime, so it is never automatically re-attempted on the next sample.

For each sample:

1. WMI object/namespace unavailable -> EC metrics unavailable.
2. Method missing -> only that metric unavailable.
3. `ExecMethod` failure -> metric unavailable; record diagnostics when requested.
4. Missing output `Data`/`Bytes` -> metric unavailable.
5. MSI response flag != 1 -> metric unavailable.
6. Payload too short for the requested decoder -> metric unavailable.
7. Do not substitute zero for any failed read.
8. No write/recovery operation is allowed.
9. No helper/service restart is allowed merely because a telemetry sample failed.
10. The next scheduled sample may try again normally.

Diagnostics may capture:

```text
Method
Selector
HRESULT
WMI/Management status if available
Used Get_WMI fallback: yes/no
Raw response length
Raw response hex
Logical bytes used
```

Production HUD logging should be much quieter than Diagnostics.

---

## 17. Diagnostics tab requirements for EC

The first main-app PR may leave Diagnostics as a placeholder. When EC diagnostics are added, the minimum useful surface is:

```text
MSI EC

Connection              OK / Failed
Privilege               Normal user / Access denied

Get_Temperature(0)
Raw                     ...
CPU Temp                61 C
GPU Temp                58 C

Get_Fan(0)
Raw                     ...
Fan 1 pair              xx / xx
Fan 1 RPM               3520
Fan 2 pair              xx / xx
Fan 2 RPM               3610

Get_Data(221)           18 -> 18 W
Get_Data(70/71)         ...
Get_Data(74/75)         ...
System Power            18.7 W

[Refresh]
[Start Live]
[Stop]
```

The raw values are important because Diagnostics doubles as the hardware RE/validation surface for future Claw revisions.

Do not add control/write buttons to the ClawHUD EC Diagnostics page.

---

## 18. Relationship to Windows battery telemetry

EC system power and Windows battery state solve different problems.

Recommended future split:

```text
Windows battery API
  -> AC/DC state
  -> Battery %
  -> RemainingCapacityWh
  -> charging/discharging state

MSI EC
  -> instantaneous battery current
  -> battery voltage
  -> DC System Power
```

Estimated remaining time should be calculated from remaining energy and a smoothed DC system-power value, not from a single instantaneous EC sample:

```text
remainingHours = RemainingCapacityWh / SmoothedSystemPowerW
```

Use a simple EMA or 30-60 second average. Do not build a prediction engine.

Product visibility policy:

```text
Battery / DC:
  Battery %
  Remaining time
  System Power

AC connected:
  Battery %
  hide Remaining time
  hide System Power
```

CPU Package Power, temperatures, and fan RPM remain useful on both AC and DC.

---

## 19. What ClawHUD must NOT copy from fan-control code

SteamAddon/CTW fan work contains write/control behavior that is useful protocol evidence but is outside ClawHUD's telemetry scope.

Do not bring these into ClawHUD merely because they exist in the reference code:

```text
Set_Fan
Set_Data(80/81)
PL1/PL2 writer
block 212 custom ownership writes
block 152 Cooler Boost writes
fan curve storage
fan curve restore
25-second CTW profile settle
79/60 latch guard
fan-control resume reapply
TDP helper named pipe
fan write verification state machines
```

Useful read-only context that may appear in Diagnostics later:

```text
Get_Fan(1/2)       fan tables, only if useful for RE
Get_Temperature(1/2) fan curve axes, only if useful for RE
Get_Data(152/212)  state observation, only if useful for RE
```

These are not required for the production HUD metrics listed in section 5.

---

## 20. Validation matrix before HUD production use

Run on each supported board family.

### A2VM: MS-1T42 / MS-1T52

```text
[ ] Normal-user WMI connection succeeds
[ ] Get_Temperature(0) returns plausible CPU/GPU values
[ ] Get_Fan(0) returns stable raw tach pairs
[ ] Fan 1 RPM matches physical/MSI OSD behavior
[ ] Fan 2 RPM pair/mapping confirmed
[ ] Get_Data(221) matches CPU-power trend
[ ] 70/71 + 74/75 system power matches MSI OSD trend on battery
[ ] AC hides DC System Power as product policy
```

### EX: MS-1T91

```text
[ ] Normal-user WMI connection succeeds
[ ] Get_Temperature(0) returns plausible CPU/GPU values
[ ] Get_Fan(0) returns both tach pairs
[ ] Fan 1 confirmed formula matches MSI OSD
[ ] Fan 2 same-formula mapping confirmed on device
[ ] Get_Data(221) matches CPU-power trend
[ ] battery current/voltage decode matches MSI OSD on DC
[ ] no WMI3/64-byte special path is needed for these reads
```

### Cross-check policy

Prefer trend/equality comparisons against MSI's own OSD for the same moment/load. Exact millisecond alignment is not required because MSI OSD itself samples slowly.

---

## 21. Intended integration with ClawHUD telemetry

Eventually:

```cpp
struct TelemetrySnapshot
{
    // frame metrics elsewhere

    std::optional<int> cpuTempC;
    std::optional<int> gpuTempC;
    std::optional<int> fan1Rpm;
    std::optional<int> fan2Rpm;
    std::optional<double> cpuPackagePowerW;

    std::optional<int> batteryPercent;
    std::optional<double> systemPowerW;
    std::optional<int> remainingMinutes;

    bool onAcPower{};
};
```

The EC reader should not know about HUD layout, strings, alignment, profile UI, tray state, or PresentMon. It only returns validated platform data.

The application/telemetry loop decides whether a sample is needed and merges it with Windows/Intel/PresentMon data.

---

## 22. Source-backed known facts summary

### Confirmed / strong enough to implement

```text
root\WMI
MSI_ACPI.InstanceName='ACPI\PNP0C14\0_0'
32-byte request package
selector in request[0]
response[0] == 1 success
response[1..] payload
Get_WMI template fallback
Get_Temperature(0)[0] CPU temp
Get_Temperature(0)[1] GPU temp
Get_Fan(0)[0/1] Fan 1 tach
Fan 1 RPM = abs(480000 / (a-b))
Get_Data(221) CPU package power source
Get_Data(70/71) battery current source
Get_Data(74/75) battery voltage source
large generic response may require logical-prefix extraction
```

### Must remain visibly qualified

```text
Get_Fan(0)[2/3] -> Fan 2 tach using same RPM formula
    strongly supported; finish board-level validation

Exact discharge-current complement edge
    project working interpretation exists;
    compare raw decode against MSI OSD before freezing

Unelevated read permission
    not yet independently proven on supported device;
    must be tested separately from the helper transport on supported hardware
```

---

## 23. Agent implementation rules

When an agent implements this document in ClawHUD:

1. Read this file first.
2. Inspect the current `onehoon/SteamAddonforClaw` reference paths listed in section 3 for transport behavior; do not blindly copy its TDP/helper architecture.
3. Implement only the read surface required by section 5 unless the task explicitly expands scope.
4. Preserve `std::optional`/explicit-unavailable semantics.
5. Validate payload length before indexing.
6. Keep one simple WMI transport implementation.
7. Keep one simple decode unit.
8. Do not elevate the main application. The EC helper is the only approved elevation boundary and must remain read-only and selector-whitelisted.
9. Do not add fan/TDP writes to solve a telemetry problem.
10. Add Diagnostics raw output before guessing at any unresolved byte interpretation.
11. Update this document whenever a board-level test closes or changes an unresolved item.

---

## 24. Recommended future file layout

Only create these when implementation actually begins:

```text
src/ClawHUD/
  Telemetry/
    ClawHUD.EcHelper/MsiEcReader.h
    ClawHUD.EcHelper/MsiEcReader.cpp
    ClawHUD/EcHelperClient.h
    ClawHUD/EcHelperClient.cpp
    shared/EcHelperProtocol.h
    MsiEcDecode.h
    MsiEcDecode.cpp
```

If the project is still small, keeping decode helpers in `MsiEcReader.cpp` is also acceptable. Do not split files merely to satisfy the diagram.

Diagnostics should reuse the helper/client transport and the same reader/decoders, not a second WMI implementation.

---

## 25. Final design statement

For ClawHUD, MSI EC telemetry should remain a **small, read-only native WMI component**.

The target is not to reproduce MSI Center M, HHC, CTW, or SteamAddon hardware-control architecture. Their value here is that they establish the MSI WMI contract and the sensor meanings.

The desired production path is simply:

```text
normal-user ClawHUD process
    -> MSI_ACPI Get_* calls
    -> validate response
    -> decode known fields
    -> latest telemetry snapshot
    -> HUD
```

Regardless of hardware outcome, `ClawHUD.exe` remains unelevated. Hardware validation determines whether the helper's read methods work on the supported boards; it does not authorize adding writes, a service, or a broader elevation boundary.
