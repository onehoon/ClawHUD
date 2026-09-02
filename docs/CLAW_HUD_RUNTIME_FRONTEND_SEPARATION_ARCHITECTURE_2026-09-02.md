# ClawHUD Runtime / Frontend Separation Architecture

> **Decision date:** 2026-09-02  
> **Status:** Architecture direction selected; implementation not started.  
> **Scope:** ClawHUD runtime isolation, control IPC, Standalone/Managed launch modes, SteamAddonforClaw integration boundary, and future standalone frontend options.  
> **Related document:** `docs/SETTINGS_WINUI3_MIGRATION_HANDOFF_2026-09-02.md`

---

## 1. Executive decision

The next ClawHUD architecture step is **not a WinUI 3 migration**.

The first priority is to separate the HUD runtime from its user-interface shell and expose a small, stable local control protocol.

The selected direction is:

```text
ClawHUD.exe
├─ ClawHUD Runtime
│  ├─ HUD state / rendering
│  ├─ production HUD presentation
│  ├─ PresentMon telemetry
│  ├─ EC / system / battery telemetry
│  ├─ game detection
│  ├─ tweak state
│  ├─ settings authority / persistence
│  └─ Control IPC
│
└─ launch-mode shell
   ├─ Standalone mode
   │  └─ Tray + standalone Settings entry point
   │
   └─ Managed mode
      └─ no Tray; externally controlled frontend
```

Two launch modes are required:

```text
ClawHUD.exe
    -> Standalone mode (default, always)
    -> existing standalone product behavior
    -> Tray available

ClawHUD.exe --managed
    -> Managed mode
    -> identical HUD/telemetry/game-detection runtime
    -> no ClawHUD Tray
    -> controlled through local IPC by an external frontend such as SteamAddonforClaw
```

The important architectural decision is therefore:

> **The HUD runtime and its control contract are the product core. The choice of standalone UI technology is a replaceable frontend decision.**

After this separation, the standalone frontend may remain Win32, move to WPF, move to WinUI 3, or use a browser-based local Web UI without redesigning the HUD runtime.

---

## 2. Why this decision changed from the earlier WinUI 3 handoff

The previous handoff selected a separate `ClawHUD.Settings.exe` implemented with C++/WinRT + WinUI 3.

That recommendation was reasonable when the problem was framed only as:

```text
legacy Win32 Settings
        ->
modern standalone Settings UI
```

The product requirement has since expanded.

ClawHUD must remain a fully independent standalone application, while `SteamAddonforClaw` should also be able to control an independently installed ClawHUD from a dedicated HUD page.

The two products remain separately installed and separately updated.

That creates a more important boundary than the UI toolkit boundary:

```text
                ClawHUD standalone frontend
                         │
                         │
                         ▼
                 stable control contract
                         ▲
                         │
                         │
              SteamAddon HUD frontend
```

Once this boundary exists, WinUI 3 is no longer an architectural requirement. It becomes only one possible implementation of the standalone frontend.

Therefore the earlier handoff is **not discarded as technically invalid**. Its UI/process-separation reasoning remains useful, but its implementation sequence is superseded by this document:

```text
OLD PRIORITY
Settings protocol
-> Settings process
-> WinUI 3 migration

NEW PRIORITY
Runtime isolation
-> stable Control IPC
-> Standalone / Managed modes
-> SteamAddon integration
-> choose/replace standalone frontend later
```

Do not start the WinUI 3 migration before the runtime/frontend boundary is established.

---

## 3. Current production baseline

The large ClawHUD application refactor is already complete.

The current architecture already contains useful runtime owners such as:

```text
App
├─ HudSettingsStore
├─ HudController
│  └─ HudPresentation
├─ PresentMonTelemetryProvider
├─ ProductionTelemetryController
├─ GameSessionController
├─ DebugObservationController
├─ TweakStartupCoordinator
└─ legacy SettingsWindow / Tray shell
```

The important point is that this work must **not trigger another broad refactor of the already-separated production domains**.

The current owners remain valid:

- `HudController` owns HUD runtime state and concrete HUD presentation lifecycle.
- `HudPresentation` remains the production presentation implementation.
- `ProductionTelemetryController` owns production telemetry sampling and aggregation.
- `GameSessionController` owns production game-detection/session state.
- `PresentMonTelemetryProvider` remains the shared production PresentMon API2 authority.
- `HudSettingsStore` remains the ClawHUD settings persistence authority.
- diagnostics remain outside the production app in `ClawHUD.Diag`.

The new work is primarily a **shell/control-boundary refactor**, not another telemetry/game-detection/rendering redesign.

---

## 4. Product goals

The architecture must satisfy all of the following at the same time.

### 4.1 ClawHUD remains an independent product

ClawHUD must continue to be:

- installed separately;
- updated separately;
- runnable without SteamAddonforClaw;
- able to expose its own Settings experience;
- able to run from Windows startup as a standalone application;
- the sole owner of its HUD implementation and settings semantics.

SteamAddonforClaw must **not** become ClawHUD's package manager or update authority.

### 4.2 SteamAddonforClaw does not bundle ClawHUD

SteamAddonforClaw must not include a ClawHUD build in its installer or release payload.

The Addon only determines whether ClawHUD is installed.

Conceptually:

```text
ClawHUD not installed
    -> HUD page reports unavailable / not installed
    -> HUD Integration controls disabled
    -> installation guidance/link is an Addon UX concern

ClawHUD installed
    -> HUD Integration controls become available
```

The exact installation-discovery mechanism belongs to the SteamAddon implementation and is not part of the ClawHUD runtime contract.

ClawHUD itself must never inspect whether SteamAddonforClaw is installed.

### 4.3 SteamAddon HUD functionality is separate from existing Addon functionality

The HUD integration is intentionally independent from SteamAddon's existing controller/routing/QAM/profile features.

Do not couple ClawHUD runtime state to:

- controller routing;
- OEM1 behavior;
- QAM;
- Steam controller presentation;
- profile runtime;
- Center M ownership.

SteamAddon should treat ClawHUD as a separate installed capability that happens to have a frontend inside the same application.

### 4.4 One ClawHUD settings authority

ClawHUD remains the authority for:

- runtime HUD state;
- settings validation;
- settings persistence;
- presentation recreation/rollback behavior;
- tweak state;
- runtime shutdown.

External frontends send commands. They do not own the state.

---

## 5. Final target architecture

```text
                         USER SESSION

┌───────────────────────────────────────────────────────────────┐
│ ClawHUD.exe                                                   │
│ native C++ runtime                                            │
│                                                               │
│  ClawHudRuntime / runtime composition                         │
│  ├─ HudController                                             │
│  │   └─ HudPresentation                                       │
│  ├─ PresentMonTelemetryProvider                               │
│  ├─ ProductionTelemetryController                             │
│  ├─ GameSessionController                                     │
│  ├─ HudSettingsStore                                          │
│  ├─ TweakStartupCoordinator                                   │
│  ├─ EC / system / battery telemetry                           │
│  └─ RuntimeControlServer                                      │
│                                                               │
│  Launch mode shell                                            │
│  ├─ Standalone                                                │
│  │   ├─ Tray                                                   │
│  │   └─ standalone Settings entry point                       │
│  │                                                            │
│  └─ Managed                                                   │
│      └─ no Tray                                               │
└───────────────────┬───────────────────────────────────────────┘
                    │
                    │ local versioned IPC
                    │
       ┌────────────┴──────────────┐
       │                           │
       ▼                           ▼
Standalone frontend       SteamAddonforClaw HUD page
(technology TBD)          (existing Addon WinUI frontend)
```

There is only one production HUD implementation.

Both modes use exactly the same:

```text
HudController
HudPresentation
ProductionTelemetryController
GameSessionController
PresentMonTelemetryProvider
telemetry decoders
settings persistence
```

`--managed` is **not a second HUD implementation** and must never create a different renderer/presentation path.

---

## 6. Runtime separation boundary

The goal is to make the runtime usable without any knowledge of which frontend is controlling it.

A narrow runtime-facing service/facade should expose the operations that frontends need.

Illustrative responsibility split:

```text
ClawHudRuntime
├─ Initialize / Shutdown
├─ Suspend / Resume
├─ HUD runtime state
├─ telemetry lifecycle
├─ game-session lifecycle
├─ settings state + mutation
├─ tweak state
└─ runtime snapshots

RuntimeControlServer
├─ protocol validation
├─ client connection
├─ request dispatch
└─ response serialization

StandaloneShell
├─ Standalone launch policy
├─ Tray
├─ current standalone Settings entry point
├─ startup/activation shell behavior
└─ requests into ClawHudRuntime
```

This should be a **minimal extraction from the current `App` composition root**.

Do not introduce a generic service container, DI framework, plugin system, generic RPC framework, or another deep host hierarchy simply because two frontends will exist.

The objective is a clean product boundary, not abstraction for its own sake.

---

## 7. Launch modes

## 7.1 Standalone is always the default

A normal user launch remains standalone:

```text
ClawHUD.exe
```

No installation probe or SteamAddon detection is involved.

Standalone composition includes:

```text
Runtime         ON
Control IPC     ON
Tray            ON
Standalone UI   available
```

This preserves ClawHUD as an independent application.

### Non-goal

Never implement behavior such as:

```text
if (SteamAddonIsInstalled())
    hide ClawHUD tray;
```

ClawHUD does not know or care whether SteamAddon exists.

## 7.2 Managed mode

Managed mode is entered only through an explicit launch argument:

```text
ClawHUD.exe --managed
```

Managed composition is:

```text
Runtime         ON
Control IPC     ON
Tray            OFF
Standalone UI   not automatically opened
```

Everything below the shell boundary remains identical to Standalone.

The initial Managed-mode implementation should be intentionally narrow. Do not add a different settings store, telemetry pipeline, renderer, logger, PresentMon path, or tweak implementation.

## 7.3 Switching Standalone -> Managed

When the user enables HUD Integration from SteamAddon:

```text
SteamAddon HUD page
    ↓
confirm ClawHUD is installed
    ↓
connect to existing ClawHUD Control IPC if present
    ↓
if existing process is Standalone:
    request graceful ClawHUD shutdown
    wait for bounded process exit
    launch installed ClawHUD.exe --managed
    reconnect Control IPC

if ClawHUD is not running:
    launch installed ClawHUD.exe --managed
    connect Control IPC

if ClawHUD is already Managed:
    reuse the existing runtime
```

Do not implement an in-process hot transition that dynamically destroys the tray and changes ownership in place merely to avoid a restart.

A clean controlled restart is simpler and gives a deterministic composition.

## 7.4 Integration disable

The simple initial policy is:

```text
Integration OFF
    -> request shutdown of the Managed ClawHUD runtime
    -> do not automatically relaunch Standalone
```

A later direct user launch of `ClawHUD.exe` naturally returns to Standalone because Standalone is always the default.

This avoids hidden mode persistence inside ClawHUD.

Whether SteamAddon also stops a Managed runtime when the entire Addon exits should be decided in the Addon integration work. If Managed mode is bound to Addon ownership, graceful shutdown and unexpected owner-loss behavior must avoid leaving a permanently headless runtime. Do not complicate the first runtime-isolation PR with this policy before the Addon-side lifecycle is implemented.

---

## 8. Control IPC is the stable integration boundary

The most important artifact produced by this refactor is a small versioned control contract.

It should work in both Standalone and Managed modes.

### 8.1 Why the IPC is permanent

Unlike the earlier WinUI Settings handoff, this IPC is not merely a temporary Settings-session transport.

It is the stable boundary between ClawHUD and any external frontend:

```text
Standalone native/WPF/WinUI/Web frontend
SteamAddon HUD page
future test/diagnostic client if explicitly needed
        ↓
ClawHUD Control IPC
        ↓
ClawHUD runtime authority
```

### 8.2 Recommended transport

Use a local Windows Named Pipe.

Reasons:

- same-machine desktop processes;
- tiny request volume;
- no network requirement;
- appropriate Windows ACL/security model;
- easy process/session isolation;
- works from native C++, C#, WPF, WinUI, or another local client;
- avoids making HTTP part of the external product integration contract.

The exact pipe naming scheme can be finalized during implementation, but it should be discoverable for an independently installed authorized frontend while remaining scoped to the current interactive user/session.

Recommended security properties:

- current-user-only access;
- local-only / reject remote clients;
- one ClawHUD runtime server per user session;
- fixed maximum frame/payload size;
- protocol magic + version + message type + request ID;
- strict payload validation;
- no raw C++ ABI layouts, pointers, `std::string`, or compiler-dependent enums on the wire.

### 8.3 Suggested protocol surface

The first protocol should remain small.

#### Runtime information

```text
GetRuntimeInfo
    -> ClawHUD app version
    -> protocol version
    -> launch mode: Standalone / Managed
    -> runtime readiness
```

#### Settings snapshot

```text
GetSettingsSnapshot
    -> HUD enabled
    -> Start with Windows (where applicable)
    -> visibility mode
    -> HUD size offset
    -> font
    -> alignment
    -> background width/mode
    -> background opacity
    -> Intel VRR Range Fix enabled
    -> any other currently public production Settings values
```

#### Settings mutations

```text
SetHudEnabled
SetVisibilityMode
SetHudSizeOffset
SetHudFont
SetHudAlignment
SetHudBackgroundMode
SetHudOpacityPreview
CommitHudOpacity
SetIntelVrrRangeFixEnabled
```

If `Start with Windows` remains exposed through an external frontend, keep its semantics explicitly Standalone-oriented and do not make Managed mode secretly own startup registration.

#### Lifecycle

```text
RequestShutdown
```

Do not expose renderer internals, PresentMon commands, EC commands, game-detection internals, or arbitrary file operations merely because the pipe exists.

### 8.4 Authoritative responses

Mutation responses must report the **authoritative post-mutation state**, not just `success=true`.

This matters because existing runtime mutations may reject or roll back an attempted change if presentation recreation or another required operation fails.

Conceptually:

```text
frontend requests opacity/font/etc.
        ↓
runtime validates + applies
        ↓
runtime persists only when appropriate
        ↓
runtime returns current authoritative snapshot/value
        ↓
frontend renders what actually happened
```

### 8.5 Opacity preview / commit semantics must be preserved

The current Win32 Settings implementation distinguishes live slider tracking from persistent commit.

That behavior should survive the protocol boundary:

```text
SetHudOpacityPreview(value)
    -> update runtime presentation
    -> do not write settings.ini repeatedly while dragging

CommitHudOpacity(value)
    -> apply authoritative value
    -> persist final value
```

Do not collapse this into continuous disk writes.

### 8.6 State changes that originate outside the frontend

Examples include F8 HUD toggling or runtime rollback.

The first implementation does not need a generic event bus.

At minimum:

- each mutation response returns authoritative state;
- a frontend refreshes its snapshot when opened/activated;
- protocol design leaves room for a narrow future `StateChanged` notification if exact live synchronization is later required.

Do not create a large publish/subscribe subsystem before a real need appears.

---

## 9. Settings ownership and persistence

ClawHUD remains the only settings authority.

Current persistence remains conceptually:

```text
%LOCALAPPDATA%\ClawHUD\settings.ini
```

The important rule is:

> **External frontends must not become independent writers of `settings.ini`.**

Preferred flow:

```text
Standalone frontend
       or
SteamAddon HUD page
        ↓
Control IPC
        ↓
ClawHUD runtime settings facade
        ↓
existing HudController / runtime mutations
        ↓
HudSettingsStore
        ↓
settings.ini
```

This avoids duplicating settings semantics in two repositories.

It also means an INI format change does not automatically require SteamAddon to implement the same parser/migration logic.

The cross-product compatibility boundary is the IPC protocol, not the physical settings-file schema.

---

## 10. SteamAddonforClaw integration model

SteamAddonforClaw and ClawHUD remain independent products.

### 10.1 Distribution

```text
ClawHUD installer / updater
    -> only ClawHUD

SteamAddon installer / updater
    -> only SteamAddon
    -> does NOT contain ClawHUD binaries
```

The developer may release compatible versions of both projects together, but there is no binary packaging dependency.

### 10.2 Addon HUD page behavior

Conceptual states:

```text
ClawHUD not installed
----------------------
ClawHUD Integration     unavailable
HUD settings            disabled
installation guidance   shown by Addon UX

ClawHUD installed
-----------------
ClawHUD Integration     available

Integration OFF
---------------
Addon does not manage a ClawHUD runtime

Integration ON
--------------
Addon ensures ClawHUD runs as:
    ClawHUD.exe --managed
Addon HUD page talks to ClawHUD Control IPC
```

### 10.3 Compatibility handling

Because the two applications update independently, the first handshake should expose:

```text
ClawHUD app version
Control protocol version
```

If an installed ClawHUD is too old/new for the Addon frontend contract, the Addon should disable unsupported controls and present an update/incompatibility state instead of guessing at settings semantics.

Compatibility should be additive where practical:

```text
Protocol v1
- baseline settings

Protocol v2
- baseline commands remain valid
- new setting/command added
```

Do not make every ClawHUD UI change require a breaking protocol version.

---

## 11. EC Helper decision

Shared EC-helper architecture was considered because SteamAddon already has its own MSI helper path.

The current decision is:

> **Do not merge or share the EC helper as part of the initial Integration work.**

Initial architecture remains:

```text
ClawHUD
    -> existing ClawHUD.EcHelper / EC path

SteamAddon
    -> existing SteamAddon TDP/MSI helper path
```

Reasons:

- ClawHUD and SteamAddon are intentionally independent products;
- their current helper protocols and selector allowlists are not identical;
- a shared privileged multi-client service would add ownership, recovery, protocol, versioning, and security complexity;
- no real user-impacting contention has yet been demonstrated that requires this complexity.

If later measurements show real WMI/EC contention, latency, reliability, or duplicate-elevation problems, helper sharing can be designed as a separate project.

Do not preemptively couple the two products through the privileged helper.

---

## 12. PresentMon and production HUD path

Managed mode must not change the PresentMon architecture.

Both launch modes use the existing production path:

```text
PresentMonRuntimeBootstrap
PresentMonAPI2Loader
PresentMonTelemetryProvider
ProductionTelemetryController
GameSessionController / GameRenderVerifier
```

If PresentMon runtime prerequisite handling currently lives too close to standalone update/startup callbacks, the launch-shell work may move the **invocation point** so both modes can ensure readiness.

That does not justify creating a second telemetry implementation.

The runtime must remain identical after bootstrap.

---

## 13. HUD presentation / VRR safety contract

This refactor is a shell/control architecture change.

It is **not** permission to alter the production HUD presentation contract.

The following remain non-negotiable and must be preserved exactly:

- HUD `windowExStyle`;
- `WS_EX_TRANSPARENT`;
- `WS_EX_NOACTIVATE`;
- `WS_EX_TOPMOST`;
- existing `WS_EX_LAYERED` behavior;
- `WM_NCHITTEST -> HTTRANSPARENT`;
- `WM_MOUSEACTIVATE -> MA_NOACTIVATE`;
- `ProductionHudPresentationContract()`;
- independent-flip requirement;
- existing Presentation API / DirectComposition production presentation path;
- premultiplied-alpha presentation contract.

`Background Opacity` continues to mean background-only opacity.

Do not implement Managed mode, IPC, or future UI work by changing window-wide/visual-wide opacity, activation, hit testing, click-through, topmost, independent flip, or the production presentation path.

Existing regression assertions/tests for these invariants must remain intact.

---

## 14. Standalone frontend technology is intentionally undecided

After runtime isolation, the standalone frontend can be replaced without touching production HUD code.

The architecture should support all of the following options.

### 14.1 Keep native Win32

```text
Standalone Tray
    -> native Win32 Settings
    -> runtime control facade / IPC
```

Advantages:

- no new framework dependency;
- smallest distribution change;
- existing UI can be retained temporarily.

Disadvantages:

- manual DPI/layout/touch work;
- higher maintenance cost for modern UI polish.

This remains a valid low-risk option.

### 14.2 WPF frontend

```text
ClawHUD.UI.exe
    WPF
        ↓
Named Pipe
        ↓
ClawHUD.exe
```

Advantages:

- fast desktop UI development;
- mature binding/layout controls;
- easy settings-page implementation;
- UI process can exit when closed.

Disadvantages:

- .NET desktop runtime/deployment consideration;
- visual stack differs from SteamAddon WinUI.

After runtime separation there is no architectural reason to reject WPF solely because the HUD runtime is native C++.

### 14.3 WinUI 3 frontend

The earlier handoff remains a viable frontend option:

```text
ClawHUD.UI.exe / ClawHUD.Settings.exe
    WinUI 3
        ↓
Named Pipe
        ↓
ClawHUD.exe
```

Advantages:

- modern Windows 11 visual language;
- good touch/DPI controls;
- similar visual/tooling direction to SteamAddon.

Disadvantages:

- Windows App SDK packaging/runtime handling;
- more deployment complexity than a pure native shell.

The major change from the earlier handoff is that WinUI 3 is now **optional**, not the architecture itself.

### 14.4 Browser-based local Web UI — reviewed option

A browser-based standalone Settings frontend was also considered.

Conceptual model:

```text
ClawHUD.exe Standalone
├─ HUD Runtime
├─ Tray
├─ Named Pipe Control IPC     <- external app contract
└─ optional local Web adapter
       ↓
http://127.0.0.1:<ephemeral-port>/<random-session>/
       ↓
default browser
```

Tray -> Settings would open the local Settings page in the user's default browser.

#### Advantages

- no WPF/WinUI/XAML dependency;
- no standalone UI executable required if HTML resources are hosted by ClawHUD;
- browser handles DPI, scrolling, touch, responsive layout, and text rendering;
- UI can be simple HTML/CSS/JavaScript;
- easy visual iteration;
- Settings UI memory largely belongs to the browser instead of the always-running HUD process.

For the current small Settings surface, a large web framework is unnecessary. Plain HTML/CSS/JavaScript would likely be sufficient.

#### Disadvantages

- Settings opens as a browser tab rather than a dedicated app window;
- default-browser behavior varies by user environment;
- local HTTP introduces a separate security surface;
- localhost CSRF/cross-origin considerations must be handled correctly;
- a loopback listener and session lifecycle must be implemented;
- the HTTP API should not accidentally become the cross-application integration contract.

#### Required security if this option is chosen later

At minimum:

- bind only to `127.0.0.1` / loopback;
- use a random ephemeral port rather than a globally predictable fixed port where practical;
- issue a cryptographically unpredictable per-session token/path;
- validate token on state-changing requests;
- enforce appropriate Origin/Host checks;
- do not accept remote interfaces;
- stop or invalidate the Web session when no longer needed;
- do not expose arbitrary filesystem/process/helper operations.

#### Architectural rule

Even if Web UI is selected later:

> **Named Pipe remains the external application integration contract.**

The Web server is only a Standalone frontend adapter.

Do not require SteamAddon to use localhost HTTP merely because the standalone UI happens to use a browser.

#### Current decision

Web UI is **considered viable but not selected now**.

Runtime separation comes first. After that, Win32/WPF/WinUI3/Web can be compared as independent frontend choices using an already-stable runtime contract.

---

## 15. What happens to the existing Win32 Settings

Do not delete the current Win32 Settings at the start of this work.

It is the regression reference while the runtime boundary is extracted.

Recommended transition:

```text
Current
App + Runtime + Tray + Win32 Settings intertwined at shell boundary

Phase 1
Runtime responsibilities isolated behind a narrow control facade
Existing Win32 Settings still works
No user-visible behavior change

Phase 2
Control IPC added
Standalone/Managed mode added
Legacy Win32 Settings remains available for Standalone validation

Phase 3
SteamAddon HUD page consumes the same runtime contract

Phase 4
Choose standalone frontend technology
    Win32 / WPF / WinUI3 / Web

Phase 5
Replace/remove legacy Win32 Settings only after the selected frontend reaches parity
```

The existing Settings code should not dictate the new IPC message shape. The protocol should describe product settings semantics rather than Win32 control IDs or page implementation details.

---

## 16. Recommended implementation sequence

### Phase RTF-0 — lock regression baseline

Before architectural movement:

- confirm current ClawHUD production tests pass;
- preserve HUD presentation/VRR contract tests;
- preserve settings behavior tests where present;
- record current Standalone tray/settings behavior;
- do not change rendering/presentation behavior.

### Phase RTF-1 — isolate runtime from UI shell

Goal:

> runtime code can exist without depending on SettingsWindow/Tray implementation details.

Work:

- define a narrow runtime control/settings facade;
- move shell-only responsibilities out of the runtime-facing layer;
- keep current production domain controllers intact;
- keep existing Win32 Settings operational through the new boundary;
- no Managed mode yet if it would complicate proving behavioral equivalence.

Acceptance:

- Standalone behavior is unchanged;
- current Settings still controls the same authoritative state;
- HUD/PresentMon/game detection behave identically;
- VRR tests remain unchanged and passing.

### Phase RTF-2 — add stable Control IPC

Work:

- add shared protocol definitions;
- add current-user local Named Pipe server;
- add `GetRuntimeInfo`;
- add settings snapshot;
- add field-level settings mutations;
- preserve opacity preview/commit;
- add graceful `RequestShutdown`;
- add protocol/security tests.

Acceptance:

- a small test client can control a running ClawHUD without touching INI directly;
- invalid frames/versions are rejected safely;
- runtime remains authoritative.

### Phase RTF-3 — Standalone / Managed launch composition

Work:

- parse explicit `--managed`;
- default to Standalone for every ordinary launch;
- Standalone creates Tray;
- Managed does not create Tray;
- both modes start identical runtime + Control IPC;
- preserve single-runtime-instance policy;
- support graceful Standalone -> Managed restart flow.

Acceptance:

```text
ClawHUD.exe
    -> Standalone + Tray

ClawHUD.exe --managed
    -> same HUD runtime
    -> no Tray
```

HUD presentation output and game/telemetry behavior must be identical between modes.

### Phase RTF-4 — SteamAddon integration

This phase belongs primarily to the SteamAddon repository.

Work conceptually:

- determine whether ClawHUD is installed;
- disable HUD integration controls if absent;
- connect and read protocol/runtime version if present;
- Integration ON performs graceful restart into `--managed` when needed;
- expose HUD controls through the Addon HUD page;
- send settings commands over ClawHUD Control IPC;
- do not parse/write ClawHUD INI directly;
- do not bundle/update ClawHUD;
- handle protocol incompatibility explicitly.

### Phase RTF-5 — choose standalone frontend

Only after the above is proven, compare:

```text
keep Win32
WPF
WinUI 3
browser Web UI
```

The choice should be based on UX, packaging, maintenance, memory, and development effort—not on runtime architecture constraints.

---

## 17. Explicit non-goals

The first runtime separation work must **not** include:

- rewriting HUD renderer/presentation;
- changing the VRR presentation contract;
- changing PresentMon query semantics merely for integration;
- moving game detection into SteamAddon;
- merging ClawHUD and SteamAddon repositories;
- bundling ClawHUD inside SteamAddon;
- making SteamAddon the ClawHUD updater;
- making ClawHUD detect SteamAddon installation;
- creating a shared privileged EC daemon without demonstrated need;
- replacing all existing Settings UI in the first runtime refactor;
- selecting WinUI3/WPF/Web before the runtime boundary is proven;
- introducing a generic service/DI/plugin/RPC framework;
- turning localhost HTTP into the cross-product integration API.

---

## 18. Key design rules for future PRs

1. **Standalone is the default.** `ClawHUD.exe` without an explicit integration argument always behaves as the independent product.
2. **Managed mode is explicit.** Only `--managed` suppresses the ClawHUD Tray for external management.
3. **One runtime implementation.** Standalone and Managed use the same production HUD, telemetry, game detection, settings, tweaks, and PresentMon code.
4. **ClawHUD owns settings.** Frontends never become independent `settings.ini` authorities.
5. **Named Pipe is the app-integration boundary.** UI technology may change without changing the runtime architecture.
6. **SteamAddon does not distribute ClawHUD.** Installation/update lifecycle remains separate.
7. **No SteamAddon knowledge in ClawHUD.** ClawHUD only knows Standalone vs explicit Managed launch mode.
8. **Do not prematurely share EC helpers.** Revisit only after a real runtime problem is demonstrated.
9. **Frontend technology is replaceable.** Win32, WPF, WinUI3, and Web UI remain valid post-separation options.
10. **VRR-critical presentation is untouched.** Integration/UI work must remain above the renderer/presentation contract.

---

## 19. Final architecture summary

```text
                         ClawHUD INSTALL
                         ============

                    ClawHUD.exe
                         │
              ┌──────────┴──────────┐
              │                     │
        Standalone mode        Managed mode
         (default)             (--managed)
              │                     │
           Tray ON                Tray OFF
              │                     │
              └──────────┬──────────┘
                         │
                  SAME HUD RUNTIME
                         │
        ┌────────────────┼────────────────┐
        │                │                │
 HUD presentation    telemetry       game detection
 PresentMon          EC/system       settings/tweaks
        │                │                │
        └────────────────┼────────────────┘
                         │
                  Control IPC
                         │
             ┌───────────┴────────────┐
             │                        │
     Standalone frontend       SteamAddon HUD page
     technology TBD            separately installed
             │                        │
     Win32 / WPF /              existing Addon UI
     WinUI3 / Web
```

The immediate development objective is therefore:

> **Separate ClawHUD runtime from the UI/Tray shell first, add a stable Control IPC, and introduce an explicit Managed no-Tray mode. Do not commit to a standalone UI framework until that boundary is complete and proven.**

This gives ClawHUD a durable architecture for both independent Standalone use and SteamAddon integration while keeping the production HUD/VRR path single, native, and unchanged.
