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
