# ClawHUD MSI EC Hardware-Validated Parsing Reference

> **Status:** production parsing authority for ClawHUD MSI EC HUD telemetry as of 2026-08-25.
>
> **Purpose:** give implementation agents a small, hardware-backed decoder contract that can be coded without re-interpreting the older MSI RE material.
>
> **Precedence:** for **production metric selection, payload parsing, snapshot fields, and HUD inclusion**, this document supersedes the corresponding candidate-metric sections in `MSI_EC_TELEMETRY_REFERENCE.md`. The older document remains useful for the MSI WMI transport, request/response packaging, historical RE, and diagnostic background.

## 1. Final production scope

After idle and real-game hardware validation, the production MSI EC path for ClawHUD is intentionally limited to three data families:

| Production value | MSI call | Bytes used | HUD use |
|---|---|---|---|
| CPU temperature | `Get_Temperature(0)` | payload `[0]` | `CPU ... xx°C` |
| Fan 1 / Fan 2 RPM | `Get_Fan(0)` | `[0],[1]` and `[2],[3]` | one averaged `FAN` value |
| CPU package power | `Get_Data(221)` | payload `[0]` | displayed as `TDP xx W` |

The following are **not production ClawHUD HUD metrics**:

- GPU temperature.
- System Power / `SYS`.
- EC battery current.
- EC battery voltage.

Do not add an Intel/IGCL/PresentMon GPU-temperature fallback merely to restore the removed GPU-temperature metric. The product decision is to omit GPU temperature.

Do not keep or add `SYS` just because MSI exposes battery-side current/voltage blocks. The product decision is to omit System Power on both AC and DC.

Battery percentage and any battery remaining-time presentation belong to the Windows battery/power layer, not to this EC parsing contract.

### Final HUD examples

AC:

```text
DX12 60 FPS | CPU 36% 67°C | GPU 98% | TDP 18 W | FAN 3540 RPM | BAT 72%
```

DC / battery:

```text
DX12 60 FPS | CPU 36% 67°C | GPU 98% | TDP 18 W | FAN 3540 RPM | BAT 72% 2.5h
```

There is no `GPU xx°C` field and no `SYS xx W` field.

---

## 2. Evidence used for this parsing freeze

### Direct project RE references

- `docs/MSI_EC_TELEMETRY_REFERENCE.md`
- `Documents/MSI_CLAW_FAN_CONTROL_COMPLETE_RE_UPDATED_2026-08-24.md`
- `Documents/MSI_OVERLAY_REALTIME_MONITOR_RESEARCH_RESULT.md`
- `Documents/HHC_msiapcfg_analysis.txt`
- direct MSI `MCMOSDInfo.exe` / `MSIWMIACPI2.dll` RE summarized in the above documents

### Real hardware logs

Idle diagnostic:

```text
GoogleDrive\ClawHUD\logs\EC\ec-20260825-203002.txt
```

Real game diagnostic:

```text
GoogleDrive\ClawHUD\logs\EC\ec-20260825-225615.txt
```

The real-game log was captured while a game was actively running. It was taken while AC was connected, so its battery-current values are **not** a DC discharge-validation dataset.

The current diagnostic failed to record board/BIOS identity (`Board: Unavailable`, `BIOS: Unavailable`). Future hardware-validation logs should fix that so every capture is tied to an exact board and BIOS revision.

---

## 3. WMI transport contract — do not redesign

The existing transport is already working on hardware. Parsing work must not change it merely because the production metric set became smaller.

Semantic target:

```text
Namespace: ROOT\WMI
Class:     MSI_ACPI
Instance:  MSI_ACPI.InstanceName='ACPI\PNP0C14\0_0'
```

Request:

```text
32-byte package
request[0] = selector / data-block index
remaining bytes = zero for these reads
```

Response:

```text
response[0] == 1  -> MSI success
response[1..]     -> logical payload returned to the decoder
```

The generic wrapper can return a 31-byte post-success payload even when only the first few bytes are meaningful. Every decoder must validate and consume only its required prefix.

Production parsing requires only:

```text
Get_Temperature(0)
Get_Fan(0)
Get_Data(221)
```

`Get_Temperature(1)` and `Get_Temperature(2)` are fan-curve/default-axis reads and must **not** be substituted for current temperature telemetry.

---

## 4. CPU temperature parsing

### Mapping

```text
Get_Temperature(0)
payload[0] = current CPU temperature in °C
```

Real hardware behavior:

Idle capture:

```text
Raw first byte: 20 / 1F
Decoded:        32 / 31 °C
```

Real-game capture:

```text
2C -> 44 °C
2F -> 47 °C
31 -> 49 °C
32 -> 50 °C
33 -> 51 °C
34 -> 52 °C
```

This value tracked load naturally and is hardware-validated for the current test device.

### Decoder

```cpp
std::optional<int> DecodeCpuTempC(std::span<const std::uint8_t> payload)
{
    if (payload.empty())
        return std::nullopt;

    const int value = static_cast<int>(payload[0]);

    // 0 °C is not a credible live CPU temperature for the supported handheld
    // and should not be presented as a valid HUD value.
    if (value == 0)
        return std::nullopt;

    return value;
}
```

Do not invent a broad temperature-validity range unless real hardware requires one. The important distinction is transport/payload failure versus a valid live value.

---

## 5. GPU temperature — excluded

The old MSI code contains an intended mapping:

```text
Get_Temperature(0)[1] -> GPU temperature
```

However, direct MSI OSD RE also shows that its temperature routine is not actually used by the stock real-time OSD, and the current hardware captures show the second byte remaining zero in both idle and real-game conditions.

Real-game examples:

```text
2C 00 ... -> CPU 44 °C, byte[1] 00
31 00 ... -> CPU 49 °C, byte[1] 00
34 00 ... -> CPU 52 °C, byte[1] 00
```

Therefore production behavior is:

```text
Do not expose gpuTempC in the EC snapshot.
Do not render GPU temperature.
Do not switch to Get_Temperature(1/2).
Do not add a fallback provider solely for GPU temperature.
```

Diagnostics may display the raw `Get_Temperature(0)` payload for RE purposes, but byte `[1]` must not be presented as a valid `0 °C` GPU reading.

---

## 6. Fan RPM parsing

### Mapping

```text
Get_Fan(0)

payload[0], payload[1] -> Fan 1 tach pair
payload[2], payload[3] -> Fan 2 tach pair
```

Fan 1 mapping/formula is directly confirmed by MSI `MCMOSDInfo.exe`.

Fan 2 uses the same formula. It is supported by the fan RE plus repeated real-device captures where the second pair changes independently and produces plausible RPM.

### Formula

```text
RPM = abs(60,000,000 / ((a - b) * 2 * 62.5))
    = abs(480,000 / (a - b))
```

Use signed arithmetic for the byte subtraction.

### Decoder

```cpp
std::optional<int> DecodeFanRpm(std::uint8_t a, std::uint8_t b)
{
    const int delta = static_cast<int>(a) - static_cast<int>(b);

    // Successful tach read with no delta means stopped/zero RPM.
    // Transport failure is represented separately as std::nullopt.
    if (delta == 0)
        return 0;

    return static_cast<int>(
        std::abs(480000.0 / static_cast<double>(delta)));
}

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

Do not return `Unavailable` solely because `a == b`. A real stopped fan is a valid `0 RPM` result.

### Real-game vectors

```text
Raw                 Fan1     Fan2
00 6F 00 6F         4324     4324
00 70 00 6F         4285     4324
00 71 00 71         4247     4247
00 70 00 72         4285     4210
00 70 00 70         4285     4285
00 6F 00 6E         4324     4363
```

These values are consistent with two live tach pairs rather than a single duplicated metric.

---

## 7. One HUD fan value

Diagnostics should retain Fan 1 and Fan 2 separately. The HUD displays only one `FAN` value.

Final display policy:

```text
both valid -> arithmetic mean of Fan1 and Fan2
only Fan1 valid -> Fan1
only Fan2 valid -> Fan2
neither valid -> Unavailable / omit according to HUD missing-value policy
```

Reference implementation:

```cpp
std::optional<int> SelectHudFanRpm(
    std::optional<int> fan1Rpm,
    std::optional<int> fan2Rpm)
{
    if (fan1Rpm && fan2Rpm)
        return (*fan1Rpm + *fan2Rpm) / 2;

    if (fan1Rpm)
        return fan1Rpm;

    if (fan2Rpm)
        return fan2Rpm;

    return std::nullopt;
}
```

The raw Fan 1 / Fan 2 values should remain available to Diagnostics even though the normal HUD uses only the combined value.

---

## 8. CPU package power parsing

### Mapping

```text
Get_Data(221)
payload[0] = current CPU package power in watts
```

This is the current package-power reading, not a configured PL1/PL2 target.

Do not use:

```text
Get_Data(80) / Set_Data(80) -> PL1 target
Get_Data(81) / Set_Data(81) -> PL2 target
```

for the HUD current-power metric.

### Real hardware behavior

Idle capture:

```text
02 / 03 -> 2 / 3 W
```

Real-game capture:

```text
07 -> 7 W
15 -> 21 W
18 -> 24 W
17 -> 23 W
```

The value rose immediately with game load and then remained around 23-24 W, strongly validating the selector and one-byte watt interpretation.

### Decoder

```cpp
std::optional<int> DecodeCpuPackagePowerW(
    std::span<const std::uint8_t> payload)
{
    if (payload.empty())
        return std::nullopt;

    return static_cast<int>(payload[0]);
}
```

Keep the internal field name explicit:

```text
cpuPackagePowerW
```

The HUD may label this value `TDP` for the compact user-facing layout, but code/comments must not confuse it with a configured power limit.

---

## 9. System Power / battery EC parsing — excluded from production

Historical MSI RE identifies:

```text
Get_Data(70/71) -> battery-side current encoding
Get_Data(74/75) -> battery voltage
```

The current AC-connected real-game capture produced, for example:

```text
Current 70/71: 00 00, F9 FF, F3 FF, F2 FF, ...
Voltage 74/75: 43 41, 3F 41, 42 41, 41 41, ...
```

The voltage pair decodes to a plausible battery voltage around 16.7 V. The current values were captured while AC was connected and therefore are not a valid dataset for freezing a DC system-power decoder.

More importantly, ClawHUD product policy now removes `SYS` entirely.

Therefore production code should **not** add these fields merely because an older RE document described them:

```text
batteryCurrentRaw
batteryVoltageMv
systemPowerW
```

and production HUD telemetry should not require `Get_Data(70)`, `71`, `74`, or `75`.

A Diagnostics-only raw probe may keep those selectors if they remain useful for future RE. If retained, label them as raw/diagnostic and do not let them become a dependency of the production HUD snapshot.

---

## 10. Recommended production EC snapshot

Keep the production model small and reflect only values actually used by the HUD:

```cpp
struct MsiEcHudTelemetry
{
    std::optional<int> cpuTempC;

    std::optional<int> fan1Rpm;
    std::optional<int> fan2Rpm;
    std::optional<int> hudFanRpm;

    std::optional<int> cpuPackagePowerW;
};
```

Do **not** add:

```cpp
std::optional<int> gpuTempC;
std::optional<std::uint16_t> batteryCurrentRaw;
std::optional<std::uint16_t> batteryVoltageMv;
std::optional<double> systemPowerW;
```

to the production HUD telemetry model unless a future product decision explicitly reintroduces those metrics.

---

## 11. Recommended production read flow

```cpp
MsiEcHudTelemetry ReadHudEcTelemetry(MsiEcReader& ec)
{
    MsiEcHudTelemetry out{};

    std::vector<std::uint8_t> temp;
    if (ec.ReadCurrentTemperature(temp))
        out.cpuTempC = DecodeCpuTempC(temp);

    std::vector<std::uint8_t> fan;
    if (ec.ReadFanTelemetry(fan))
    {
        if (auto decoded = DecodeFanTelemetry(fan))
        {
            out.fan1Rpm = decoded->fan1Rpm;
            out.fan2Rpm = decoded->fan2Rpm;
            out.hudFanRpm = SelectHudFanRpm(
                decoded->fan1Rpm,
                decoded->fan2Rpm);
        }
    }

    std::uint8_t power{};
    if (ec.ReadData(221, power))
        out.cpuPackagePowerW = static_cast<int>(power);

    return out;
}
```

No AC/DC branch is needed inside this EC parser because `SYS` is no longer a product metric.

Battery % / remaining time is merged later from the Windows battery layer.

---

## 12. Failure semantics

Use explicit unavailable semantics.

- WMI/method failure -> corresponding metric `std::nullopt`.
- MSI response flag not successful -> `std::nullopt`.
- Payload too short -> `std::nullopt`.
- CPU temperature byte `0` -> unavailable, not `0 °C`.
- Fan successful read with zero tach delta -> valid `0 RPM`.
- CPU package power byte `0` after a successful read is not automatically a transport failure; keep transport validity separate from the value.
- Never synthesize a plausible value from stale or missing raw data.

A failed metric must not invalidate unrelated successful metrics from the same sampling cycle.

---

## 13. Unit-test vectors

At minimum, decoder tests should include these exact hardware-observed vectors.

### CPU temperature

```text
[0x20] -> 32 °C
[0x2C] -> 44 °C
[0x34] -> 52 °C
[0x00] -> Unavailable
[]     -> Unavailable
```

### Fan RPM

```text
00 6F -> 4324 RPM
00 70 -> 4285 RPM
00 71 -> 4247 RPM
00 72 -> 4210 RPM
00 6E -> 4363 RPM
70 70 -> 0 RPM
```

Two-fan payload examples:

```text
00 6F 00 6F -> Fan1 4324, Fan2 4324, HUD 4324
00 70 00 72 -> Fan1 4285, Fan2 4210, HUD 4247
00 6F 00 6E -> Fan1 4324, Fan2 4363, HUD 4343
```

Payload shorter than four bytes -> fan telemetry unavailable.

### CPU package power

```text
[0x02] -> 2 W
[0x07] -> 7 W
[0x15] -> 21 W
[0x18] -> 24 W
[]     -> Unavailable
```

---

## 14. Diagnostics requirements after this hardware validation

Diagnostics remains the place to preserve raw evidence, but it must not imply that excluded metrics are production-ready.

Required useful output:

```text
Board / system product
BIOS version
Helper/elevation state

Get_Temperature(0)
  Raw
  CPU Temp
  GPU byte may be shown as raw/unsupported, not as a valid 0 °C metric

Get_Fan(0)
  Raw
  Fan1 pair / RPM
  Fan2 pair / RPM

Get_Data(221)
  Raw
  CPU Package Power
```

Optional RE-only output:

```text
Get_Data(70/71)
Get_Data(74/75)
```

If those are retained, clearly mark them diagnostic-only. Do not display a decoded `System Power` product metric.

Future captures must fix the current `Board: Unavailable` / `BIOS: Unavailable` problem before using a log to close board-specific support.

---

## 15. Implementation rules for agents

When implementing EC telemetry from this document:

1. Keep the current MSI WMI transport/helper architecture; do not redesign it for parsing work.
2. Production EC reads are `Get_Temperature(0)`, `Get_Fan(0)`, and `Get_Data(221)` on all sampling states; while Windows reports DC power, the remaining-time estimator additionally reads validated battery selectors `70`, `71`, `74`, and `75`.
3. Parse only `Get_Temperature(0)[0]` as CPU temperature.
4. Do not expose or render GPU temperature.
5. Parse both fan tach pairs and keep both in Diagnostics.
6. Use `abs(480000 / (a-b))`; zero delta after a successful read means `0 RPM`.
7. The normal HUD `FAN` value is the average of both valid fans, with one-fan fallback.
8. Parse `Get_Data(221)[0]` as current CPU package watts.
9. Internal name is `cpuPackagePowerW`; compact HUD label may be `TDP`.
10. Do not implement `SYS` / System Power.
11. Do not expose EC blocks 70/71/74/75 as a production HUD metric; they may be used internally by the DC remaining-time estimator.
12. Battery `%` and remaining time are outside this EC parser and come from the battery/power layer.
13. Keep `std::optional`/explicit-unavailable semantics.
14. Validate payload length before indexing.
15. Do not add EC writes, fan-control ownership, PL1/PL2 writes, or any recovery loop to solve a telemetry task.

---

## 16. Final frozen mapping

```text
PRODUCTION EC

Get_Temperature(0)
  [0] -> CPU Temp °C
  [1] -> ignored / not a ClawHUD metric

Get_Fan(0)
  [0],[1] -> Fan1 tach -> abs(480000 / delta)
  [2],[3] -> Fan2 tach -> abs(480000 / delta)
  HUD FAN -> average of available Fan1/Fan2

Get_Data(221)
  [0] -> CPU Package Power W
  HUD label -> TDP

NOT PRODUCTION EC / NOT HUD

GPU Temp -> excluded
System Power / SYS -> excluded
Get_Data(70/71) battery current -> diagnostic/RE only if retained
Get_Data(74/75) battery voltage -> diagnostic/RE only if retained
```

This is the parsing baseline to use for the next ClawHUD telemetry implementation work.
