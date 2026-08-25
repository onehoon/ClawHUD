# ClawHUD

ClawHUD is an experimental, lightweight performance HUD for **Windows 11 + MSI Claw + Intel Arc**.

The project is intentionally narrow. It is not intended to become a generic overlay framework, hardware-control suite, or multi-device monitoring platform. The goal is a very small gaming add-on that can show useful performance and MSI Claw hardware telemetry while remaining compatible with Intel XeFG, OptiScaler, and VRR.

This README is also an architectural reference for future implementation work. When code, experiments, or agent-generated changes conflict with the constraints below, the constraints in this document should be treated as the default project direction unless a later hardware result explicitly changes them.

---

## Current status

The repository currently contains a **Phase 0 VRR / XeFG presentation PoC** using the Windows 11 Composition Swapchain / Presentation Manager path.

The PoC has successfully reached the point where it can:

- create the Windows Composition Swapchain path,
- query `IPresentationFactory::IsPresentationSupportedWithIndependentFlip()`,
- create a premultiplied-alpha presentation surface,
- host it through DirectComposition,
- show and remove a static external HUD,
- operate without game injection, Present hooking, DXGI hooking, or swapchain interception.

This proves that the candidate presentation path can be built. It **does not yet prove** that VRR, OptiScaler, or XeFG remain correct on real MSI Claw hardware while the HUD is active.

See:

- [Phase 0 VRR / XeFG PoC](docs/PHASE0-VRR-POC.md)
- [MSI EC Telemetry Reference](docs/MSI_EC_TELEMETRY_REFERENCE.md)

Production shell / tray work may proceed independently, but the production HUD presentation backend remains gated by the Phase 0 hardware result.

---

# 1. Product scope

## Supported environment

The intended support boundary is deliberately small:

```text
OS:       Windows 11 x64 only
Device:   supported MSI Claw boards only
GPU:      Intel Arc only
User:     one Windows user
Session:  one interactive session
```

Current MSI board IDs relevant to the project:

```text
Claw A2VM:       MS-1T42 / MS-1T52
Claw 8 EX AI+:   MS-1T91
```

The application does **not** need architecture for:

- Fast User Switching,
- multiple simultaneous interactive sessions,
- RDP usage,
- server/service deployment,
- generic third-party handheld support,
- NVIDIA / AMD GPU abstraction.

If a feature only exists to support those scenarios, it is probably out of scope.

---

# 2. Design philosophy

ClawHUD should stay small.

This is a personal gaming add-on, not enterprise software. Prefer direct, understandable ownership and a small number of concrete objects over abstraction layers designed for hypothetical future platforms.

Avoid unless a real requirement proves them necessary:

```text
DI containers
ServiceHost layers
provider registries
manager hierarchies
factory hierarchies
plugin frameworks
epoch/barrier coordination
multi-session authorities
separate helper processes
complex retry/reconcile state machines
```

A reasonable target is that `App` directly owns the few runtime components that actually exist.

Tests should validate production behavior, but production complexity must not be added merely because a synthetic race or test double can be invented.

---

# 3. Application lifecycle

The production application is **tray-first**.

Normal startup should be:

```text
ClawHUD.exe
    ↓
single-instance check
    ↓
Velopack startup update check
    ↓
apply update and restart if available
    ↓
create tray icon
    ↓
start minimal background runtime
    ↓
NO settings window
    ↓
message loop
```

## Single instance

A simple per-user named mutex is sufficient.

If a second instance starts:

- detect the existing instance,
- exit the second instance,
- do not add IPC merely to activate the first instance,
- do not build multi-session ownership logic.

## Velopack update policy

ClawHUD should check for updates **immediately at application startup**.

The intended behavior is:

```text
startup
  ↓
check update silently
  ├─ update available → download → apply → restart updated app
  └─ no update / check failure → continue current version
```

Policy:

- no update popup,
- no toast,
- no progress dialog,
- no confirmation dialog,
- no user-facing update notification.

The application normally starts before the user begins a game, so delaying tray startup while an update is downloaded/applied is acceptable.

Update failure must not prevent the currently installed version from starting.

Do not create an updater framework around Velopack; use the normal Velopack lifecycle directly.

---

# 4. Tray-first and memory policy

A major goal of ClawHUD is to remain lightweight when sitting in the tray.

On fresh startup, the application should **not** create a settings window and hide it. The settings UI must not exist until the user explicitly requests it.

Fresh tray idle should be close to:

```text
process
mutex
tray icon
message loop
minimal runtime state
```

It should not initialize expensive subsystems just because the process started.

Unless currently needed, tray startup should not automatically create:

- settings HWNDs,
- Direct2D/DirectWrite settings resources,
- Direct3D HUD resources,
- Presentation Manager / DirectComposition HUD resources,
- PresentMon capture sessions,
- IGCL polling,
- MSI EC/WMI polling,
- Diagnostics probes.

## Settings window lifecycle

The settings window is lazy-created:

```text
Tray → Settings
    ↓
create Settings window
    ↓
show window
```

When closed, destroy it and release resources owned by the window.

Do not keep it alive using only `ShowWindow(..., SW_HIDE)`.

Recreating a small settings window on the next open is acceptable; conserving idle resources is more important than avoiding a trivial recreation cost.

The initial settings UI only needs a small native Windows surface. The project does not currently need WinUI 3, WPF, WebView, Electron, or another heavyweight UI framework.

Expected top-level tabs:

```text
General
HUD
Diagnostics
```

`Diagnostics` initially exists as a placeholder. Diagnostic functionality should be added in later PRs and should initialize only when required.

---

# 5. HUD presentation requirements

The project exists only if the HUD can remain non-invasive.

The production HUD must not use:

- game DLL injection,
- `Present` hooks,
- DXGI hooks,
- D3D swapchain interception,
- process-memory modification,
- Reflex / XeLL marker injection,
- frame-generation interception,
- a frame limiter as part of the HUD,
- game-specific injected workarounds.

## Primary presentation candidate

The Phase 0 candidate is the Windows 11 **Composition Swapchain / Presentation Manager** path using:

```text
D3D11 displayable texture
    ↓
IPresentationFactory
    ↓
IPresentationManager
    ↓
IPresentationSurface
    ↓
premultiplied alpha
    ↓
DirectComposition visual
```

The candidate is intentionally different from a traditional `WS_EX_LAYERED + TOPMOST` overlay.

A generic layered top-level overlay is not the preferred production path because external overlays can force a game away from its best presentation path or interfere with VRR on systems where MPO/independent presentation cannot be retained.

The current PoC is documented in [docs/PHASE0-VRR-POC.md](docs/PHASE0-VRR-POC.md).

---

# 6. Absolute GO / NO-GO conditions

Two product requirements are gates rather than optional improvements:

1. **OptiScaler + XeFG must continue to work correctly with the HUD active.**
2. **VRR must remain operational with the HUD active.**

If the external non-injected HUD structurally breaks either requirement, the project should not be rescued by switching to injection/hooking.

The fallback is not "make an RTSS-style injected overlay".

The fallback is **NO-GO / project decision**.

## GO candidate

A successful hardware result should show, with HUD OFF vs HUD ON:

```text
✓ game remains functional
✓ OptiScaler remains functional
✓ XeFG remains functional
✓ VRR display cadence remains active
✓ no clear HUD-caused fixed-refresh quantization
✓ no meaningful dropped-frame regression
✓ no meaningful pacing regression
✓ no presentation-path regression that causes the above failures
```

`Hardware: Independent Flip` or `Hardware Composed: Independent Flip` are strong presentation outcomes, but a label alone is not the final VRR truth.

## Important VRR nuance

Do **not** reduce VRR validation to:

```text
Independent Flip = pass
Composed: Flip = fail
```

Modern Windows can have more than one valid presentation/composition behavior. The decisive observation is whether actual displayed cadence still behaves as VRR rather than becoming fixed-refresh quantized.

Present mode is evidence; display cadence is the more important behavioral result.

---

# 7. VRR validation strategy

Use objective capture rather than visual judgment.

A useful validation case is a non-divisor FPS target safely inside the panel VRR range, for example approximately **73 FPS on a 120 Hz panel** after confirming the real panel/range.

Expected VRR-like interval:

```text
1000 / 73 ≈ 13.70 ms
```

A fixed 120 Hz path tends to show interval behavior related to:

```text
8.33 ms
16.67 ms
25.00 ms
...
```

The hardware validation sequence is:

```text
VRR ON
↓
select test game + stable non-divisor cap
↓
HUD OFF capture 30–60 s
↓
HUD ON capture 30–60 s
↓
compare presentation mode + display timing + pacing
↓
repeat with OptiScaler / XeFG configuration
```

Useful fields include:

- `PresentMode`,
- display timestamps,
- `MsBetweenDisplayChange` / equivalent display-change timing,
- displayed cadence distribution,
- dropped frames,
- pacing behavior.

Do not treat the Windows "VRR enabled" setting or `AllowsTearing=true` as proof that VRR was actually retained.

PresentMon overlay itself must not be used as the test HUD. Use capture/logger functionality only so the validation tool does not become another overlay variable.

---

# 8. FPS model: Render FPS and Displayed FPS are different metrics

ClawHUD must **not** have one ambiguous `fps` field internally.

The telemetry model should distinguish at least:

```text
renderFps
trueDisplayedFps
frameTimeMs
```

This distinction is required because Intel XeFG can generate additional output frames that are not equivalent to application-rendered frames.

Example:

```text
Render FPS:     ~40
XeFG output:    ~120
```

Both values are useful and they mean different things.

The HUD may later choose how to label/present them, but the internal model must not collapse them into one number.

---

# 9. PresentMon role and limitation

PresentMon remains valuable and is expected to be a major ClawHUD telemetry source.

Expected uses include:

```text
application/render FPS
render frametime
PresentMode
OS-visible presentation timing
display-change timing when observable
process/game association
dropped/pacing-related presentation data
some GPU telemetry where appropriate
```

## UMD-based XeFG problem

There is an important Intel XeFG limitation documented in PresentMon issue #604:

https://github.com/GameTechDev/PresentMon/issues/604

The issue concerns **UMD (User-Mode Driver) based XeFG**. In that path, generated frames can be presented in a way that is not fully visible to the normal OS-level PresentMon observation path.

That means PresentMon can observe the application's rendered/presented work but may not count every frame actually produced by the driver-side XeFG output path.

Therefore:

```text
PresentMon Render FPS           → useful / expected
PresentMon generic FPS          → must be named carefully
PresentMon true Displayed FPS   → NOT assumed correct under UMD XeFG
```

This is especially important for MSI Claw because it is Intel-only and Intel Graphics Software XeFG multiplier override is a realistic/common usage path rather than an irrelevant edge case.

Do not implement the final ClawHUD FPS display on the assumption that PresentMon alone always knows the true generated/output FPS.

## VRR vs XeFG FPS counting

PresentMon issue #604 is primarily a **generated-frame / real FPS observability problem**, not evidence that XeFG itself disables VRR.

Keep the problems separate:

```text
VRR validation
    → display cadence / presentation behavior

UMD XeFG true FPS
    → actual generated/output-frame observability
```

When UMD-generated flips are invisible to the normal PresentMon path, those same limits must also be considered before treating PresentMon display-change counters as a complete description of every physical/generated output flip.

If necessary, deeper Intel-driver / ETW actual-flip telemetry should be investigated rather than guessing.

---

# 10. Intel IGCL role

Because ClawHUD is Intel Arc only, Intel Graphics Control Library (IGCL) is a first-class candidate rather than an optional generic-GPU backend.

Expected IGCL responsibilities include:

```text
Intel adapter identification
GPU utilization
GPU clock / frequency
GPU power telemetry
other capability-backed Intel telemetry
XeFG feature capability
IGS XeFG override state
XeFG multiplier override: App / 2x / 3x / 4x
```

IGCL exposes Intel 3D feature information for frame generation and current Intel documentation includes frame-generation override choices corresponding to 2x / 3x / 4x behavior.

However, the publicly documented IGCL live-state surface currently should **not** be assumed to provide the true driver-generated displayed FPS or an authoritative generated-frame counter.

Therefore the intended split is:

```text
IGCL
├─ Intel GPU telemetry
├─ XeFG capability
└─ configured XeFG override / multiplier

PresentMon
├─ Render FPS
├─ render frametime
└─ OS-visible presentation data

Intel actual-flip / future driver telemetry
└─ candidate for TRUE UMD-XeFG output FPS if required
```

Capability checks should be used per metric. Do not assume every Arc generation exposes every IGCL sensor identically.

In particular, known driver/IGCL gaps on newer GPUs mean MSI EC may remain the preferred source for some Claw temperatures even when IGCL telemetry exists.

---

# 11. XeSS Inspector role

Intel XeSS Inspector is useful as a **research / validation oracle**, not as the ClawHUD production telemetry path.

XeSS Inspector can attach to a target application and inspect XeSS / XeFG context state, including frame-generation state and configuration information. This makes it useful for experiments such as comparing:

```text
PresentMon result
vs
XeSS Inspector XeFG state
vs
IGS multiplier override
```

That can help determine where generated frames become invisible to normal OS-level telemetry.

However, XeSS Inspector is not the production solution because its workflow involves attaching to the target process / XeSS context. ClawHUD's product direction explicitly avoids game-process instrumentation and injection-style dependencies.

Use XeSS Inspector to establish ground truth during research, not as a runtime dependency.

---

# 12. MSI EC telemetry

MSI-specific platform telemetry is read through the MSI ACPI/WMI interface rather than guessed from generic GPU APIs.

The authoritative implementation reference is:

[docs/MSI_EC_TELEMETRY_REFERENCE.md](docs/MSI_EC_TELEMETRY_REFERENCE.md)

Expected read-only values include:

```text
Get_Fan(0)
├─ Fan 1 tach
└─ Fan 2 tach

Get_Temperature(0)
├─ CPU temperature
└─ GPU temperature

Get_Data(221)
└─ CPU package power

Get_Data(70 / 71)
└─ battery-side current

Get_Data(74 / 75)
└─ battery voltage
```

ClawHUD does not need fan-control or TDP-write functionality for the HUD telemetry path.

Do not pull in:

- `Set_Fan`,
- fan ownership control,
- Cooler Boost control,
- `Set_Data(80/81)` TDP writes,
- write-oriented lifecycle recovery.

The first real-device EC test must also determine whether these read-only WMI methods work unelevated. Do not add an elevated helper until an actual supported Claw proves it necessary.

---

# 13. Battery and system-power model

The intended battery presentation includes:

```text
Battery %
System Power
Remaining Time
```

Battery state/capacity comes from normal Windows power APIs. MSI EC provides the battery-side current/voltage source used by MSI's own OSD path for DC system power.

The intended remaining-time concept is simple:

```text
remaining energy / smoothed current system power
```

Do not build a complicated prediction engine. A short moving average or EMA is sufficient to avoid a wildly jumping time estimate.

## DC / battery behavior

When discharging:

```text
Battery %       visible
Remaining       visible
System Power    visible
CPU Power       visible
Fan RPM         visible
Temperatures    visible
```

## AC-connected behavior

When AC is connected:

```text
Battery %       visible
Remaining       hidden
System Power    hidden
CPU Power       visible
Fan RPM         visible
Temperatures    visible
```

`System Power` is intentionally battery-discharge based. Showing `0 W` or a misleading value on AC is worse than hiding the metric.

CPU package power is independent of that policy and remains useful on both AC and DC.

---

# 14. Telemetry snapshot direction

The application should converge on one small snapshot consumed by the HUD rather than allowing every renderer element to query hardware directly.

Illustrative model:

```cpp
struct TelemetrySnapshot
{
    // Frame metrics
    std::optional<double> renderFps;
    std::optional<double> trueDisplayedFps;
    std::optional<double> frameTimeMs;

    // Intel GPU
    std::optional<double> gpuUsage;
    std::optional<double> gpuClockMHz;
    std::optional<double> gpuPowerW;

    // MSI EC
    std::optional<int> cpuTempC;
    std::optional<int> gpuTempC;
    std::optional<int> fan1Rpm;
    std::optional<int> fan2Rpm;
    std::optional<double> cpuPackagePowerW;

    // Power / battery
    std::optional<int> batteryPercent;
    std::optional<double> systemPowerW;
    std::optional<int> remainingMinutes;
    bool onAcPower{};
};
```

This is a data shape, not a reason to build a generic provider framework.

Missing values should remain unavailable (`std::optional`) rather than becoming synthetic zeroes.

---

# 15. HUD UI direction

The default HUD position is the **top of the screen**.

The text/content should support:

```text
Left alignment
Center alignment
Right alignment
```

Background behavior should eventually support experimentation with:

```text
Content-only background
Full-width background
Background opacity
```

A representative compact line is:

```text
FPS 72 | CPU 61°C | GPU 58°C | FAN1 3520 | FAN2 3610 | BAT 62% | 18.7W | 2h 07m
```

The exact visual design is not fixed yet. Architecture should not be complicated in anticipation of a future skin/theme system.

---

# 16. Diagnostics direction

The Settings window will contain a `Diagnostics` tab.

Initially it can be a placeholder. Later it should become the project's real-hardware verification surface for features such as:

```text
Presentation / VRR
MSI EC raw reads
Intel / IGCL capability and telemetry
PresentMon capture state
Battery / power cross-checks
raw values / export
```

The Diagnostics workflow should be:

```text
add raw capability/read probe
    ↓
validate on real Claw
    ↓
confirm decode / behavior
    ↓
move proven value into normal TelemetrySnapshot
    ↓
show in HUD
```

This reduces the need for one-off PowerShell scripts and temporary probe executables.

Important lifecycle rule:

> Diagnostics must not become a permanently running diagnostics service.

If the Diagnostics tab is closed or not active, its special probes/captures should be stopped and their resources released unless the same telemetry is already required by the normal HUD runtime.

---

# 17. Planned implementation sequence

Keep PR boundaries small so hardware and lifecycle regressions are easy to isolate.

## PR A — lightweight application shell

```text
production ClawHUD executable
single instance
Velopack silent startup update
tray-first startup
Settings lazy creation
General / HUD / Diagnostics tabs
clean Settings destruction
clean tray Exit
```

Explicitly no telemetry/HUD integration required in this PR.

## PR B — PresentMon baseline

Add the initial frame/presentation telemetry integration while keeping metric naming explicit:

```text
Render FPS
render frametime
PresentMode
OS-visible display timing
```

Do not claim UMD-XeFG true Displayed FPS yet.

## PR C — Diagnostics / hardware probes

Add read-only diagnostics in small pieces:

```text
MSI EC
IGCL
Battery
VRR PoC integration / capture helpers
```

Use Diagnostics to validate behavior before promoting values to the normal HUD telemetry path.

## Later — production HUD

Only after the hardware presentation test passes:

```text
validated Composition Swapchain path
    ↓
production HUD renderer
    ↓
TelemetrySnapshot display
```

If VRR or XeFG compatibility fails structurally, do not quietly replace this path with injection/hooking.

---

# 18. What not to build yet

Until an actual requirement exists, do not add:

```text
generic multi-GPU abstraction
generic handheld abstraction
plugin SDK
telemetry provider marketplace
remote API
web dashboard
multi-user/session coordination
complex profile engine
background Diagnostics service
fan-control UI
TDP-control UI
frame limiter
injected overlay fallback
game-specific hook logic
```

ClawHUD should earn complexity only when a real supported-device requirement demands it.

---

# 19. Definition of project success

ClawHUD is successful if it can remain a very small background application and provide useful MSI Claw / Intel Arc telemetry without interfering with the game experience.

The target product behavior is approximately:

```text
Boot / login
    ↓
ClawHUD starts
    ↓
silent update if required
    ↓
tray idle with minimal memory/resources
    ↓
game / HUD use
    ↓
non-injected external HUD
    ↓
VRR retained
XeFG retained
OptiScaler retained
    ↓
accurate render + hardware telemetry
```

The project should prefer an unavailable metric over a misleading metric, and a NO-GO result over an invasive workaround that violates the original design goal.

---

# 20. Layout and visual design reference

This section records the current HUD layout discussion and the external research used to guide it. It is intentionally a **project reference**, not a promise that every visual constant below is already final. Where an older illustrative HUD example elsewhere in this README differs from this section, this section represents the newer layout direction.

## 20.1 Benchmark target: SteamOS / MangoHud horizontal HUD

The first production layout should benchmark the **SteamOS Performance Overlay horizontal one-line style** and MangoHud's corresponding horizontal layout behavior.

MSI Center M's OSD is useful as a reverse-engineering source for telemetry, but its visual design is **not** a ClawHUD layout benchmark.

Relevant public implementation references:

- Valve Gamescope exposes Steam/MangoApp integration and explicitly advertises horizontal MangoApp support through `STEAM_MANGOAPP_HORIZONTAL_SUPPORTED`, `STEAM_MANGOAPP_PRESETS_SUPPORTED`, and `STEAM_USE_MANGOAPP`.
- MangoHud's current configuration documents preset `2` as the **horizontal view** and exposes `horizontal`, `horizontal_stretch`, `hud_no_margin`, and `hud_compact` layout options.
- MangoHud is used as an implementation/design reference only. ClawHUD is a Windows-native project and is not expected to copy MangoHud's Linux rendering code.

References:

- https://github.com/ValveSoftware/gamescope/blob/master/src/main.cpp
- https://github.com/flightlessmango/MangoHud/blob/master/data/MangoHud.conf
- https://github.com/flightlessmango/MangoHud/blob/master/README.md

The intended design principle is:

```text
SteamOS horizontal visual language
    +
MangoHud public layout/color behavior as a reference
    +
Windows-native ClawHUD renderer
```

Do not build a skin/theme framework around this. The first goal is one clean, fixed visual language.

## 20.2 First layout: one horizontal line

The first HUD layout is a **single horizontal line at the top of the screen**.

No multi-row layout, graphs, cards, gauges, or vertical sensor list are required for the first version.

Current DC / battery example:

```text
DX11 60 FPS | CPU 36% 67°C | GPU 98% 72°C | TDP 18 W | SYS 24 W | FAN 3540 RPM | BAT 72%  2.5h
```

Current AC-connected example:

```text
DX11 60 FPS | CPU 36% 67°C | GPU 98% 72°C | TDP 18 W | FAN 3540 RPM | BAT 72%
```

The graphics API label should conceptually become one of:

```text
DX11
DX12
VULKAN
```

followed immediately by FPS. The desired UI is clear, but the exact reliable source for distinguishing **DX11 vs DX12** still needs real-device/runtime validation; PresentMon's general present/runtime information must not be assumed to solve that distinction until proven.

## 20.3 Metric order and meaning

The current one-line order is:

```text
Graphics API + FPS
| CPU usage + CPU temperature
| GPU usage + GPU temperature
| TDP
| SYS (DC only)
| FAN
| BAT + remaining time (remaining is DC only)
```

Meaning of each label:

| HUD label | Meaning | Intended source |
|---|---|---|
| `DX11` / `DX12` / `VULKAN` + FPS | current graphics API label + frame rate | PresentMon / runtime validation path |
| `CPU` | total CPU usage + live CPU temperature | PresentMon candidate for usage; MSI `Get_Temperature(0)[0]` for temperature |
| `GPU` | GPU usage + live GPU temperature | PresentMon candidate for usage; MSI `Get_Temperature(0)[1]` for temperature |
| `TDP` | **current CPU Package Power**, not the configured PL1/TDP limit | MSI `Get_Data(221)` |
| `SYS` | battery-side whole-system discharge power | MSI `Get_Data(70/71)` current × `Get_Data(74/75)` voltage |
| `FAN` | average of the two Claw fan RPM values | MSI `Get_Fan(0)` |
| `BAT` | battery percentage and, on DC only, estimated remaining time | Windows battery API + smoothed system power |

The user-facing `TDP` label is intentionally concise. Internally the value should retain an accurate name such as `cpuPackagePowerW` so it is not confused with configured PL1/PL2 limits.

## 20.4 MSI temperature selectors: do not confuse telemetry with fan-curve axes

For the HUD, real-time temperature is read from:

```text
Get_Temperature(0)
├─ payload[0] = CPU temperature °C
└─ payload[1] = GPU temperature °C
```

Do **not** use the fan-curve selector values as live temperatures:

```text
Get_Temperature(1) = Fan 1 temperature-axis / curve data
Get_Temperature(2) = Fan 2 temperature-axis / curve data
```

Selectors `1` and `2` are useful for fan-control/curve research but are not the runtime CPU/GPU temperature source for this HUD.

## 20.5 Fan display policy

The HUD displays **one FAN value**, not two separate fan values.

```text
FAN = average(Fan 1 RPM, Fan 2 RPM)
```

This keeps the horizontal HUD compact and matches the goal of showing a quick device-level cooling signal rather than exposing every low-level sensor.

If only one fan reading is valid, using the valid fan reading is preferable to manufacturing a zero for the missing fan. If neither reading is valid, the metric should be unavailable rather than reported as `0 RPM` unless hardware has positively reported an actual stopped-fan condition.

## 20.6 AC / DC visibility policy

System Power is derived from battery discharge telemetry and therefore has a strict power-source rule.

### DC / battery

Show:

```text
Battery %
Remaining time
System Power
CPU Package Power / TDP
Fan RPM
CPU/GPU temperature
CPU/GPU usage
FPS / API
```

### AC connected

Show:

```text
Battery %
CPU Package Power / TDP
Fan RPM
CPU/GPU temperature
CPU/GPU usage
FPS / API
```

Hide:

```text
Remaining time
System Power
```

Do not replace hidden AC values with artificial text such as:

```text
Charging
AC
0 W
-- h
```

The battery segment simply becomes shorter on AC. Battery percentage remains visible.

This is especially important for `SYS`: it is based on battery current and voltage, not wall power, so showing an AC-side zero or misleading value would be worse than hiding the segment.

## 20.7 Static category colors, not warning colors

The first HUD is an information display, not a warning/alert system.

Do **not** change FPS, frametime, CPU, GPU, temperature, power, or other text colors because a metric crosses a threshold.

Examples of behavior intentionally not required:

```text
low FPS → red
high frametime → yellow
high CPU usage → red
high temperature → warning color
```

Color is for **stable metric/category identification only**.

MangoHud's current public configuration provides useful baseline values:

```text
text_color                 = #FFFFFF
gpu_color                  = #2E9762
cpu_color                  = #2E97CB
vram_color                 = #AD64C1
ram_color                  = #C26693
engine_color               = #EB5B5B
battery_color              = #FF9078
background_color           = #020202
horizontal_separator_color = #AD64C1
background_alpha           = 0.5
alpha                      = 1.0
round_corners              = 0
```

ClawHUD should treat those values as a **visual benchmark, not an immutable copied theme**.

Current ClawHUD direction:

- CPU label/category may use the MangoHud-style blue family.
- GPU label/category may use the MangoHud-style green family.
- Battery may use the MangoHud-style coral family.
- metric values remain white.
- TDP, SYS, and FAN should initially remain visually simple rather than inventing additional arbitrary category colors.
- separators should remain low-emphasis and must not dominate the line.
- no state-dependent recoloring.

Exact final colors should be validated on the actual Claw display before being treated as shipping constants.

## 20.8 Background direction

A near-black semitransparent background is the current baseline direction.

MangoHud's public baseline is approximately:

```text
background_color = #020202
background_alpha = 0.5
round_corners    = 0
```

This is a better benchmark for ClawHUD than MSI Center M's OSD styling.

The ClawHUD background should stay visually simple:

```text
near-black
adjustable opacity
square / no-card appearance
no decorative border
no glow
no gradient
```

The exact default opacity is not final yet. **Approximately 50%** is the current benchmark starting point.

## 20.9 Full-width vs content-width background

Two background modes are worth keeping because the rendering cost and implementation difference are minimal:

```text
Full Width
Content Width
```

### Full Width

The background spans the complete display width while only the text moves according to alignment.

Advantages:

- strongest SteamOS-style top performance-bar appearance;
- the bar does not grow/shrink when `SYS` and remaining time disappear on AC;
- Left / Center / Right alignment changes only text position, not the overall HUD silhouette;
- visually stable when metric text length changes.

### Content Width

The background spans only the measured text width plus padding.

Advantages:

- visually smaller footprint;
- shows less darkened game content;
- may feel more like a compact floating telemetry strip.

The final default is **not locked yet**. Full Width is a strong default candidate, but both modes should be easy to support by changing only the background rectangle calculation. Do not create separate renderers for them.

Background mode and text alignment are independent settings.

## 20.10 Position and alignment

The HUD's vertical position is fixed to the **top** for the initial design.

The one-line text supports three horizontal alignments:

```text
Left
Center
Right
```

Conceptually:

```text
Left:   x = leftPadding
Center: x = (screenWidth - contentWidth) / 2
Right:  x = screenWidth - contentWidth - rightPadding
```

No bottom/side placement matrix is required for the first version unless a real need appears later.

## 20.11 Windows typography direction

ClawHUD should use a clean **Windows 11 built-in font** rather than trying to reproduce SteamOS's exact font assets.

Current candidate:

```text
Segoe UI Variable
```

Goals:

- clean Windows-native appearance;
- excellent small-size readability;
- no external font dependency;
- no bundled font licensing/distribution requirement;
- stable numeric rendering.

The final point/pixel size should be chosen by real-device visual testing. Earlier discussion used roughly **14 physical pixels of text in a 28–30 physical-pixel bar** as an initial sizing candidate, but these numbers are not final design constants.

## 20.12 Physical-pixel sizing and DPI independence

The HUD should remain approximately the **same physical pixel size** when Windows DPI scaling or game resolution changes.

Desired behavior:

```text
100% Windows scale → same HUD pixel height
150% Windows scale → same HUD pixel height
175% Windows scale → same HUD pixel height
```

Likewise, reducing game/display resolution should not cause the HUD font to become proportionally larger simply because normal XAML/DIP scaling changed.

Implementation direction:

- define HUD height, font size, padding, and spacing in target physical pixels;
- obtain the current window/monitor DPI;
- convert/inversely compensate when the text/rendering API operates in DIPs;
- do not let generic desktop DPI scaling silently enlarge the HUD.

If horizontal space becomes insufficient at a very low resolution, prefer a deliberate compact/metric policy over randomly shrinking the font from frame to frame.

## 20.13 Direct2D / DirectWrite text rendering direction

The one-line HUD should be treated as a small native renderer, not a collection of heavy UI controls.

The current rendering direction is:

```text
TelemetrySnapshot
    ↓
format small metric text runs
    ↓
DirectWrite text layout
    ↓
Direct2D drawing into the validated HUD presentation surface
    ↓
Composition / Presentation Manager path
```

The Phase 0 presentation backend remains authoritative: this section does **not** replace the Composition Swapchain / Presentation Manager requirement with a generic layered overlay.

The text line should be composed from separate runs, for example:

```text
[DX11] [60 FPS] [|]
[CPU] [36%] [67°C] [|]
[GPU] [98%] [72°C] [|]
[TDP] [18 W] [|]
[SYS] [24 W] [|]
[FAN] [3540 RPM] [|]
[BAT] [72%] [2.5h]
```

Separate runs allow category labels and values to use different fixed brushes without adding a UI-layout framework.

For a premultiplied-alpha/transparent composition surface, grayscale text antialiasing is a safer initial direction than relying on ClearType assumptions tied to an opaque background.

## 20.14 Avoid numeric/layout jitter

Frequently changing numbers should not make the entire line visibly shift left/right every sample.

Two useful techniques are:

1. use DirectWrite/OpenType **tabular figures** where the selected font supports them;
2. reserve small predictable widths/slots for rapidly changing values such as FPS, percentages, watts, and RPM.

Examples that should not cause distracting reflow:

```text
9% → 10% → 100%
9.8 W → 10.1 W
999 RPM → 1000 RPM
```

This does not require drawing visible boxes. It only means the renderer should use stable metric extents where practical.

## 20.15 Telemetry-source direction for this layout

The simplest desired runtime split is:

```text
PresentMon
├─ FPS / presentation metrics
├─ CPU utilization candidate
├─ GPU utilization candidate
└─ graphics API/runtime information candidate

MSI_ACPI read-only
├─ Get_Temperature(0) → CPU/GPU temperature
├─ Get_Data(221)      → CPU Package Power shown as TDP
├─ Get_Data(70/71)    → battery current
├─ Get_Data(74/75)    → battery voltage
└─ Get_Fan(0)         → Fan 1 / Fan 2 tach → average FAN value

Windows power API
└─ Battery percentage / power-source state / battery capacity data as available
```

Because FPS already makes PresentMon necessary, the first real-device PoC should test whether PresentMon's CPU/GPU utilization metrics are reliable enough to reuse. If they are, do not add a second Windows Performance Counter path merely for architectural symmetry.

If a PresentMon metric is unavailable or its meaning is unsuitable, fall back to a proven Windows/Intel metric source only for that real requirement.

The desired `DX11` / `DX12` / `VULKAN` label also needs validation. A generic Present runtime such as DXGI is not automatically proof of D3D11 vs D3D12, so this must remain a measured implementation detail rather than a guessed mapping.

## 20.16 Candidate sampling / refresh cadence

Sensor sampling and HUD redraw should be separate.

Current starting-point cadence discussed for the PoC/first implementation:

| Metric | Candidate data refresh |
|---|---:|
| FPS / frame data | ~250 ms |
| CPU usage | ~500 ms |
| GPU usage | ~500 ms |
| CPU Package Power / `TDP` | ~500 ms |
| CPU/GPU temperature | ~500–1000 ms |
| FAN average RPM | ~1000 ms |
| Battery % | ~5000 ms |
| Remaining time | ~5000–10000 ms |
| HUD redraw | ~10 FPS / 100 ms |

These are starting points, not performance requirements.

Do not poll every EC/battery metric at the renderer rate. The renderer should display the latest `TelemetrySnapshot` and redraw independently.

## 20.17 Remaining-time smoothing

Remaining battery time should not visibly oscillate with every instantaneous power sample.

Conceptually:

```text
remaining energy Wh
÷
smoothed System Power W
=
estimated remaining hours
```

A simple moving average or EMA is enough. An approximate **30–60 second smoothing window** is a reasonable experiment for stable handheld display, while the visible remaining-time string only needs to update every several seconds.

Do not turn this into a prediction framework.

## 20.18 Missing-value behavior

The HUD should prefer unavailable data over misleading data.

Examples:

- do not display fake `0 W` for DC-only system power on AC;
- do not display a synthetic `0 RPM` merely because fan telemetry failed;
- do not turn a failed temperature read into `0°C`;
- do not invent a battery remaining time when the required data is unavailable.

Whether an unavailable metric is temporarily omitted or displayed as a compact `--` placeholder can be finalized during visual testing, but synthetic zeroes should not be the default failure representation.

## 20.19 Current layout summary

The current design target can be summarized as:

```text
Position:          Top
Rows:              1
Alignment:         Left / Center / Right selectable
Background:        near-black, semitransparent
Background width:  Full Width vs Content Width selectable / final default TBD
Corner style:      square / no card
Typography:        Windows 11 built-in; Segoe UI Variable candidate
Sizing:            fixed physical-pixel target, DPI-independent
Colors:            stable category colors + white values
Warning colors:    none
Renderer:          Direct2D / DirectWrite into validated composition surface
Data model:        latest TelemetrySnapshot, independent sampling rates
```

DC example:

```text
DX11 60 FPS | CPU 36% 67°C | GPU 98% 72°C | TDP 18 W | SYS 24 W | FAN 3540 RPM | BAT 72%  2.5h
```

AC example:

```text
DX11 60 FPS | CPU 36% 67°C | GPU 98% 72°C | TDP 18 W | FAN 3540 RPM | BAT 72%
```

This section should be updated when real-device presentation, telemetry, and visual tests settle any of the remaining candidate choices.

## Main application shell

The production `ClawHUD.exe` is a lightweight tray-first native Win32 shell with lazy Settings creation. See [docs/MAIN-APP-SHELL.md](docs/MAIN-APP-SHELL.md) for the concrete startup, update, and tray lifecycle.

## EC diagnostics

The Diagnostics tab contains a user-started, bounded MSI EC read-only probe. It records ten samples from `Get_Temperature(0)`, `Get_Fan(0)`, `Get_Data(221)`, and the raw battery current/voltage selectors into `logs/diagnostics`. It does not initialize WMI at startup and never calls MSI write methods. See [docs/DIAGNOSTICS.md](docs/DIAGNOSTICS.md).

The same tab provides a separate user-started VRR orchestration test: a 30-second HUD-OFF phase followed by a 30-second `ClawHUD.VrrPoc.exe --diagnostic` HUD-ON phase. This records lifecycle evidence only; PresentMon, ETW, frame analysis, and VRR verdicts remain deferred.
