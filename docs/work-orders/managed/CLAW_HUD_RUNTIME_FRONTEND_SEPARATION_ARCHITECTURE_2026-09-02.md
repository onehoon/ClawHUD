# ClawHUD Runtime / Frontend Separation Architecture

> **Decision date:** 2026-09-02  
> **Status:** Architecture direction selected; implementation not started.  
> **Scope:** ClawHUD runtime isolation, Control IPC, Standalone/Managed launch modes, SteamAddonforClaw integration boundary, lifecycle policy, and future standalone frontend options.  
> **Related document:** `docs/SETTINGS_WINUI3_MIGRATION_HANDOFF_2026-09-02.md`

---

## 1. Executive decision

The next ClawHUD architecture step is **not a WinUI 3 migration**.

The first priority is to separate the HUD runtime from its UI/Tray shell and expose a small, stable local control protocol.

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

The architectural decision is therefore:

> **The HUD runtime and its control contract are the product core. The standalone UI technology is a replaceable frontend decision.**

After this separation, the standalone frontend may remain Win32, move to WPF, move to WinUI 3, or use a browser-based local Web UI without redesigning the HUD runtime.

---

## 2. Why this supersedes the earlier WinUI 3 handoff

The earlier handoff selected a separate WinUI 3 Settings process because the problem was framed as modernizing the standalone Settings UI.

The product requirement has since expanded:

- ClawHUD must remain a fully independent, separately installed and separately updated application.
- SteamAddonforClaw should also be able to control an independently installed ClawHUD from its own HUD page.
- SteamAddon must not bundle or update ClawHUD.

That makes the runtime/frontend boundary more important than the UI toolkit boundary.

```text
                ClawHUD standalone frontend
                         │
                         ▼
                 stable Control IPC
                         ▲
                         │
              SteamAddon HUD frontend
```

The earlier WinUI 3 handoff is **not technically invalid**. Its process-separation reasoning remains useful. The implementation sequence is simply superseded:

```text
OLD PRIORITY
Settings protocol
-> Settings process
-> WinUI 3 migration

NEW PRIORITY
Runtime isolation
-> stable Control IPC
-> Standalone / Managed modes
-> lifecycle policy
-> SteamAddon integration
-> choose/replace standalone frontend later
```

Do not begin a WinUI 3 migration before the runtime/frontend boundary is complete.

---

## 3. Current production baseline

The large ClawHUD application refactor is already complete.

Current runtime owners include:

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

This work must **not trigger another broad refactor of the already-separated production domains**.

The current owners remain valid:

- `HudController` owns HUD runtime state and concrete HUD presentation lifecycle.
- `HudPresentation` remains the production presentation implementation.
- `ProductionTelemetryController` owns production telemetry sampling and aggregation.
- `GameSessionController` owns production game-detection/session state.
- `PresentMonTelemetryProvider` remains the shared production PresentMon API2 authority.
- `HudSettingsStore` remains the ClawHUD settings persistence authority.
- diagnostics remain outside the production app in `ClawHUD.Diag`.

The new work is a **shell/control-boundary refactor**, not another telemetry/game-detection/rendering redesign.

---

## 4. Product goals and ownership rules

### 4.1 ClawHUD remains independent

ClawHUD must continue to be:

- installed separately;
- updated separately;
- runnable without SteamAddonforClaw;
- able to expose its own Settings experience;
- able to run from Windows startup as a standalone application;
- the sole owner of its HUD implementation and settings semantics.

SteamAddonforClaw must not become ClawHUD's package manager or update authority.

### 4.2 SteamAddon does not bundle ClawHUD

```text
ClawHUD not installed
    -> HUD page reports unavailable / not installed
    -> HUD Integration controls disabled
    -> installation guidance/link is an Addon UX concern

ClawHUD installed
    -> HUD Integration controls become available
```

ClawHUD itself never probes for SteamAddon installation.

### 4.3 HUD integration is independent of existing Addon features

Do not couple ClawHUD to SteamAddon's controller routing, OEM1, QAM, profile runtime, Steam controller presentation, or Center M ownership.

### 4.4 One ClawHUD settings authority

ClawHUD remains authoritative for:

- runtime HUD state;
- settings validation;
- settings persistence;
- presentation recreation/rollback behavior;
- tweak state;
- runtime shutdown.

External frontends send commands. They do not own the state and do not write `settings.ini` directly.

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

Standalone and Managed use exactly the same:

```text
HudController
HudPresentation
ProductionTelemetryController
GameSessionController
PresentMonTelemetryProvider
telemetry decoders
settings persistence
```

`--managed` is **not a second HUD implementation** and must never create a different renderer or presentation path.

---

## 6. Runtime separation boundary

The runtime must be usable without knowing which frontend controls it.

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

Do not introduce a generic service container, DI framework, plugin system, generic RPC framework, or deep host hierarchy merely because two frontends will exist.

---

## 7. Launch modes

### 7.1 Standalone is always the default

A normal launch is always:

```text
ClawHUD.exe
```

Standalone composition:

```text
Runtime         ON
Control IPC     ON
Tray            ON
Standalone UI   available
```

Never implement:

```text
if (SteamAddonIsInstalled())
    hide ClawHUD tray;
```

ClawHUD does not know or care whether SteamAddon exists.

### 7.2 Managed mode is explicit

```text
ClawHUD.exe --managed
```

Managed composition:

```text
Runtime         ON
Control IPC     ON
Tray            OFF
Standalone UI   not automatically opened
```

Managed mode is **not persisted by ClawHUD**. A later ordinary launch always means Standalone unless an external owner explicitly supplies `--managed` again.

### 7.3 One runtime per user session

Standalone and Managed must never coexist.

A single-runtime mutex/authority should enforce one ClawHUD runtime per user session.

A normal `ClawHUD.exe` launch while Managed is already active must **not** replace Managed with Standalone. The second launch simply detects the existing runtime and exits/activates according to the final shell policy.

---

## 8. Managed lifetime and state-transition policy

This section is a product lifecycle contract, not an implementation detail.

### 8.1 Core lifetime rule

> **Standalone is owned by ClawHUD/the user. Managed is owned by the currently running SteamAddon process that launched or adopted it.**

```text
Standalone
==========
Owner: ClawHUD / user
Started by:
- direct normal launch
- ClawHUD Windows startup
Lifetime:
- independent of SteamAddon

Managed
=======
Owner: currently running SteamAddon process
Started by:
- SteamAddon only
- explicit ClawHUD.exe --managed
Lifetime:
- while Integration remains ON
- AND the owning SteamAddon process remains alive
```

Managed mode must not be left as a permanently headless orphan after its owner is gone.

### 8.2 Integration OFF -> ON

If no ClawHUD is running:

```text
Integration ON
-> launch ClawHUD.exe --managed
-> connect Control IPC
```

If Standalone is running:

```text
Integration ON
-> connect IPC
-> verify launch mode = Standalone
-> RequestShutdown
-> wait for bounded process exit
-> launch ClawHUD.exe --managed
-> reconnect IPC
```

If Managed is already running under the current owner, reuse it.

Do not hot-switch the shell in place merely to avoid a restart.

### 8.3 Integration ON -> OFF

```text
Integration OFF
-> request Managed ClawHUD shutdown
-> do not automatically relaunch Standalone
```

`Integration OFF` means the Addon stops managing ClawHUD. It does not mean "start standalone ClawHUD now".

A later ordinary user launch naturally returns to Standalone.

### 8.4 SteamAddon normal exit

If the Addon owns a Managed runtime:

```text
SteamAddon exit
-> Managed ClawHUD exits
```

Do not relaunch Standalone automatically.

### 8.5 SteamAddon crash / kill / abnormal loss

Managed must not remain as a trayless orphan.

The implementation should provide owner-loss cleanup with normal Windows process-lifetime primitives where practical. A Job Object with kill-on-close semantics is one candidate and should be evaluated during implementation rather than assumed blindly.

Required behavioral result:

```text
owning SteamAddon process disappears
-> owned Managed ClawHUD terminates
```

### 8.6 Managed ClawHUD unexpected exit

If:

```text
SteamAddon alive
Integration ON
Managed ClawHUD exits unexpectedly
```

then the Addon should attempt to restore the requested state by restarting:

```text
ClawHUD.exe --managed
```

However, do not create an infinite crash loop.

Use bounded/reasonable restart protection. Repeated fast failures should transition the Addon HUD page into an error/unavailable state until the user retries or conditions change.

A user killing Managed ClawHUD in Task Manager is also an unexpected runtime exit. With Integration still ON, the Addon may restart it. To intentionally stop management, the user should turn Integration OFF.

### 8.7 SteamAddon restart or update

The simple ownership model is preferred over cross-process ownership handoff.

```text
old Addon instance exits
-> its Managed ClawHUD exits

new Addon instance starts
-> Integration ON is read from Addon settings
-> launch new ClawHUD.exe --managed
```

A brief HUD gap during Addon update/restart is acceptable and is much simpler than transferring ownership of an existing Managed process between Addon generations.

### 8.8 ClawHUD update/restart while Managed

ClawHUD is separately installed and separately updated, but Managed mode must not accidentally turn into Standalone during an update restart.

Required rule:

> **A Managed ClawHUD update that requires process restart must not autonomously relaunch itself as a normal Standalone instance.**

Preferred behavior:

```text
Managed ClawHUD update requires restart
-> Managed process exits cleanly
-> owning Addon observes the exit/update transition
-> Addon launches the newly installed ClawHUD.exe --managed
```

If the existing updater architecture performs an automatic self-restart, the restart must preserve Managed semantics and owner binding. Prefer owner-driven relaunch if it avoids special updater coupling.

The implementation work must inspect the actual Velopack/update lifecycle before choosing the exact mechanism.

### 8.9 Windows shutdown / reboot

No mode persistence is required in ClawHUD.

At shutdown, normal process teardown occurs.

At the next boot, the final mode is derived again from normal startup behavior and Addon settings.

This intentionally makes boot order irrelevant.

#### Integration OFF

ClawHUD behavior depends only on its own `Start with Windows` setting:

| Addon starts | ClawHUD Start with Windows | Final ClawHUD state |
|---|---:|---|
| No | Off | Not running |
| No | On | Standalone |
| Yes | Off | Not running |
| Yes | On | Standalone |

#### Integration ON, ClawHUD startup OFF

```text
Addon starts
-> no ClawHUD runtime exists
-> Addon launches Managed
```

If the Addon itself does not auto-start, no HUD is expected until the Addon is launched.

#### Integration ON, ClawHUD startup ON

Boot order does not change the final state.

If ClawHUD starts first:

```text
ClawHUD startup -> Standalone
Addon starts -> detects Standalone
-> graceful shutdown
-> launch Managed
```

If Addon starts first:

```text
Addon -> launch Managed
later ClawHUD startup invocation -> sees existing runtime
-> does not replace Managed
-> exits
```

If both start nearly simultaneously, whichever obtains runtime authority first wins temporarily, but Addon reconciliation with Integration ON must converge the final state to Managed.

Do not introduce fixed startup delays merely to control boot order.

### 8.10 ClawHUD `Start with Windows` remains independent

SteamAddon must not rewrite, disable, or restore ClawHUD's own startup preference merely because Integration is enabled.

This avoids ownership problems on:

- Integration disable;
- Addon uninstall;
- Addon crash;
- user preference changes.

A temporary Standalone -> Managed transition during boot is acceptable when both startup mechanisms are enabled.

### 8.11 SteamAddon uninstall while Integration is enabled

Normal Addon uninstall/termination should end the owned Managed runtime.

SteamAddon uninstall must not modify ClawHUD's own startup preference or uninstall/update ClawHUD.

After the Addon is gone:

- if ClawHUD `Start with Windows` is ON, the next boot starts Standalone;
- if it is OFF, ClawHUD stays stopped until the user launches it.

### 8.12 ClawHUD uninstall while SteamAddon is running

If the installed ClawHUD disappears or its Managed process exits because of uninstall:

```text
Addon detects runtime loss
-> re-check ClawHUD installation
-> installation absent
-> do NOT enter restart loop
-> HUD integration controls become unavailable/disabled
```

The Addon's Integration preference may remain stored as a user preference so a later reinstall can be reconciled, but the runtime state is `Unavailable` until ClawHUD exists again.

### 8.13 Temporary IPC unavailability during startup/restart

IPC absence does not immediately mean the installed product is broken.

During controlled transitions such as:

- Standalone -> Managed restart;
- Addon restart;
- ClawHUD update restart;
- process startup before pipe creation;

there may be a short interval where the pipe is unavailable.

Use bounded connection/retry behavior around known transitions. Do not introduce an always-running polling subsystem merely for this.

Outside a known transition, persistent IPC failure should surface as an unavailable/error state rather than causing uncontrolled process churn.

### 8.14 Manual normal launch while Managed is active

A user may directly launch the ClawHUD shortcut while SteamAddon owns a Managed runtime.

Required result:

```text
existing runtime = Managed
normal ClawHUD.exe launched
-> do not replace Managed
-> do not create Tray
-> second process exits/activates according to shell policy
```

This preserves Managed ownership and the one-runtime invariant.

### 8.15 Lifecycle convergence summary

The desired invariant while SteamAddon is alive is:

```text
Integration ON
    -> exactly one ClawHUD runtime
    -> mode = Managed

Integration OFF
    -> Addon owns no ClawHUD runtime
```

The desired invariant without SteamAddon ownership is:

```text
normal ClawHUD launch/startup
    -> Standalone
```

---

## 9. Lifecycle state table

| Situation | Required result |
|---|---|
| Normal ClawHUD launch, no runtime | Start Standalone + Tray |
| `--managed`, no runtime | Start Managed, no Tray |
| Normal launch while Managed active | Keep Managed; second launch does not replace it |
| Integration ON, ClawHUD not running | Start Managed |
| Integration ON, Standalone running | Gracefully stop Standalone, start Managed |
| Integration OFF, Managed running | Stop Managed; do not start Standalone |
| Addon exits normally | Owned Managed stops |
| Addon crashes/is killed | Owned Managed must not remain orphaned |
| Managed crashes while Addon alive + Integration ON | Bounded restart as Managed |
| Addon restart/update | Old Managed ends; new Addon starts new Managed if Integration ON |
| ClawHUD update restart while Managed | Return to Managed through owner-driven/preserved managed restart, never accidental Standalone |
| Boot: Integration OFF | ClawHUD startup preference alone decides Standalone/not running |
| Boot: Integration ON + ClawHUD startup OFF | Addon starts Managed when it starts |
| Boot: Integration ON + ClawHUD startup ON | Any start order converges to Managed while Addon is alive |
| Addon uninstall | Managed ends; ClawHUD installation/startup preference untouched |
| ClawHUD uninstall while Addon alive | Stop restart attempts after install re-check; mark integration unavailable |
| Known restart window with missing IPC | Bounded reconnect/retry |
| Persistent unexpected IPC failure | Surface unavailable/error; do not churn processes indefinitely |

---

## 10. Control IPC is the stable integration boundary

The Control IPC works in both Standalone and Managed modes and is a permanent product boundary, not merely a temporary Settings-session transport.

```text
Standalone native/WPF/WinUI/Web frontend
SteamAddon HUD page
        ↓
ClawHUD Control IPC
        ↓
ClawHUD runtime authority
```

### 10.1 Recommended transport

Use a local Windows Named Pipe.

Recommended security properties:

- current-user-only access;
- local-only / reject remote clients;
- one ClawHUD runtime server per user session;
- fixed maximum frame/payload size;
- protocol magic + version + message type + request ID;
- strict payload validation;
- no raw C++ ABI layouts, pointers, `std::string`, or compiler-dependent enums on the wire.

### 10.2 Suggested protocol surface

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
    -> other public production Settings values
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

If `Start with Windows` remains exposed through an external frontend, keep its semantics explicitly Standalone-oriented. Managed mode must not secretly own startup registration.

#### Lifecycle

```text
RequestShutdown
```

Do not expose renderer internals, PresentMon commands, EC commands, game-detection internals, or arbitrary filesystem/helper operations merely because the pipe exists.

### 10.3 Authoritative mutation responses

Mutation responses must report the authoritative post-mutation state, not just `success=true`, because existing runtime mutations may reject or roll back an attempted change.

### 10.4 Preserve opacity preview / commit

```text
SetHudOpacityPreview(value)
    -> update runtime presentation
    -> no repeated settings.ini writes while dragging

CommitHudOpacity(value)
    -> apply authoritative final value
    -> persist final value
```

### 10.5 Runtime-originated changes

Examples include F8 HUD toggling or rollback.

The first implementation does not need a generic event bus.

At minimum:

- mutation responses return authoritative state;
- frontend refreshes on open/activation;
- protocol leaves room for a narrow future `StateChanged` notification.

---

## 11. Settings ownership and persistence

Current persistence remains conceptually:

```text
%LOCALAPPDATA%\ClawHUD\settings.ini
```

External frontends must not become independent writers.

```text
Standalone frontend
       or
SteamAddon HUD page
        ↓
Control IPC
        ↓
ClawHUD runtime settings facade
        ↓
existing runtime mutation
        ↓
HudSettingsStore
        ↓
settings.ini
```

The cross-product compatibility boundary is the IPC protocol, not the physical INI schema.

---

## 12. SteamAddonforClaw integration model

### 12.1 Distribution

```text
ClawHUD installer / updater
    -> only ClawHUD

SteamAddon installer / updater
    -> only SteamAddon
    -> does NOT contain ClawHUD binaries
```

The developer may release compatible versions together, but there is no binary packaging dependency.

### 12.2 HUD page states

```text
ClawHUD not installed
----------------------
ClawHUD Integration     unavailable
HUD settings            disabled
installation guidance   Addon UX concern

ClawHUD installed
-----------------
ClawHUD Integration     available

Integration OFF
---------------
Addon does not own/manage a ClawHUD runtime

Integration ON
--------------
Addon ensures exactly one Managed ClawHUD while it is alive
Addon HUD page talks to ClawHUD Control IPC
```

### 12.3 Compatibility handling

Handshake exposes at least:

```text
ClawHUD app version
Control protocol version
```

If the installed ClawHUD is incompatible, disable unsupported controls and present an update/incompatibility state rather than guessing at semantics.

Keep protocol evolution additive where practical.

---

## 13. EC Helper decision

Shared EC-helper architecture was considered because SteamAddon already has its own MSI helper path.

Current decision:

> **Do not merge or share the EC helper as part of the initial integration work.**

```text
ClawHUD
    -> existing ClawHUD.EcHelper / EC path

SteamAddon
    -> existing SteamAddon TDP/MSI helper path
```

Reasons:

- products remain intentionally independent;
- protocols/selector allowlists are not identical;
- a shared privileged multi-client service adds ownership, recovery, security, and versioning complexity;
- no demonstrated user-impacting contention currently requires it.

Revisit only if real contention, latency, reliability, or duplicate-elevation problems are observed.

---

## 14. PresentMon and production HUD path

Managed mode must not change the PresentMon architecture.

Both modes use the existing production path:

```text
PresentMonRuntimeBootstrap
PresentMonAPI2Loader
PresentMonTelemetryProvider
ProductionTelemetryController
GameSessionController / GameRenderVerifier
```

If prerequisite handling currently lives too close to Standalone update/startup callbacks, the shell refactor may move only the invocation point so both modes can ensure readiness.

Do not create a second telemetry implementation.

---

## 15. HUD presentation / VRR safety contract

This work is a shell/control architecture change. It is **not** permission to alter the production HUD presentation contract.

The following remain non-negotiable:

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

Existing regression assertions/tests for these invariants must remain intact.

---

## 16. Standalone frontend technology is intentionally undecided

After runtime isolation, standalone UI technology is replaceable.

### 16.1 Keep native Win32

Advantages:

- no new framework dependency;
- lowest distribution change;
- current UI can remain as regression reference.

Disadvantages:

- manual DPI/layout/touch work;
- higher maintenance cost for modern UI polish.

### 16.2 WPF frontend

```text
ClawHUD.UI.exe
    WPF
      ↓ Named Pipe
ClawHUD.exe
```

Advantages: mature layout/binding and fast settings UI development.  
Disadvantages: .NET desktop runtime/deployment and visual stack differs from SteamAddon WinUI.

### 16.3 WinUI 3 frontend

The earlier handoff remains a viable frontend option.

Advantages: modern Windows 11 visual language, touch/DPI, similar direction to SteamAddon.  
Disadvantages: Windows App SDK packaging/runtime complexity.

WinUI 3 is optional, not the architecture itself.

### 16.4 Browser-based local Web UI — reviewed option

A browser-based standalone Settings frontend was explicitly reviewed.

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

Advantages:

- no WPF/WinUI/XAML dependency;
- browser handles DPI, scrolling, touch, responsive layout;
- simple HTML/CSS/JavaScript is sufficient for current Settings scope;
- UI memory largely belongs to the browser.

Disadvantages:

- browser tab rather than dedicated app window;
- local HTTP adds security considerations;
- loopback/session lifecycle must be implemented.

If chosen later, require at least:

- loopback-only bind;
- random ephemeral port where practical;
- unpredictable per-session token/path;
- token validation on state changes;
- appropriate Origin/Host checks;
- no remote interface;
- finite session lifetime;
- no arbitrary filesystem/process/helper exposure.

Even if Web UI is chosen, **Named Pipe remains the external application integration contract**. The Web server is only a Standalone frontend adapter.

Current decision: Web UI is viable but not selected now.

---

## 17. Existing Win32 Settings transition

Do not delete the current Win32 Settings at the start.

```text
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
Replace/remove legacy Win32 Settings only after parity
```

The protocol must describe product settings semantics, not Win32 control IDs.

---

## 18. Recommended implementation sequence

### Phase RTF-0 — lock regression baseline

- confirm current production tests pass;
- preserve HUD presentation/VRR contract tests;
- preserve settings behavior;
- record current Standalone tray/settings behavior;
- do not change rendering/presentation behavior.

### Phase RTF-1 — isolate runtime from UI shell

Goal: runtime can exist without depending on SettingsWindow/Tray implementation details.

- define a narrow runtime control/settings facade;
- move shell-only responsibilities out of the runtime-facing layer;
- keep current production domain controllers intact;
- keep existing Win32 Settings operational through the new boundary.

Acceptance: Standalone behavior remains unchanged and VRR/PresentMon/game detection behavior is identical.

### Phase RTF-2 — add stable Control IPC

- shared protocol definitions;
- current-user local Named Pipe server;
- `GetRuntimeInfo`;
- settings snapshot;
- field-level settings mutations;
- opacity preview/commit;
- graceful `RequestShutdown`;
- protocol/security tests.

Acceptance: a small client can control a running ClawHUD without touching INI directly.

### Phase RTF-3 — Standalone / Managed launch composition

- parse explicit `--managed`;
- default every ordinary launch to Standalone;
- Standalone creates Tray;
- Managed does not create Tray;
- both modes run identical runtime + Control IPC;
- preserve single-runtime policy;
- implement deterministic Standalone -> Managed restart flow;
- ensure normal launch cannot displace an active Managed runtime.

### Phase RTF-4 — Managed lifecycle ownership

- define/implement owner binding from SteamAddon to Managed ClawHUD;
- normal Addon exit ends Managed;
- owner-loss cleanup prevents orphaned headless runtime;
- Managed unexpected exit can be restarted with crash-loop protection;
- Addon restart/update uses terminate-and-recreate, not ownership handoff;
- inspect ClawHUD updater behavior and preserve Managed semantics across update-required restart;
- verify boot-order convergence with both startup options;
- verify uninstall and temporary IPC-unavailability cases.

### Phase RTF-5 — SteamAddon HUD integration

Primarily SteamAddon repository work:

- detect installed ClawHUD;
- disable integration controls if absent;
- read protocol/runtime version if present;
- Integration ON reconciles to Managed;
- Integration OFF releases/stops Managed ownership;
- HUD page uses ClawHUD Control IPC;
- do not parse/write ClawHUD INI;
- do not bundle/update ClawHUD;
- handle incompatibility and not-installed states explicitly.

### Phase RTF-6 — choose standalone frontend

Only after the runtime boundary is proven, compare:

```text
keep Win32
WPF
WinUI 3
browser Web UI
```

Choose based on UX, packaging, maintenance, memory, and development effort—not runtime architecture constraints.

---

## 19. Explicit non-goals

The initial separation work must not include:

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
- turning localhost HTTP into the cross-product integration API;
- adding fixed boot delays to force launch ordering;
- persisting Managed mode in ClawHUD;
- implementing cross-generation Addon ownership handoff unless a real requirement later demands it.

---

## 20. Key design rules for future PRs

1. **Standalone is the default.** Ordinary `ClawHUD.exe` always means the independent product.
2. **Managed is explicit and not persisted.** Only `--managed` creates the no-Tray externally owned composition.
3. **One runtime implementation.** Standalone and Managed use the same HUD, telemetry, game detection, settings, tweaks, and PresentMon code.
4. **One runtime per user session.** Normal launches never displace an already-owned Managed runtime.
5. **Managed lifetime follows its Addon owner.** Owner exit/loss must not leave a permanent headless ClawHUD.
6. **Integration ON converges to Managed while Addon is alive.** Boot order is irrelevant; no fixed delays.
7. **Integration OFF ends Addon ownership.** Stop Managed; do not auto-launch Standalone.
8. **Managed crashes may be recovered only with bounded restart protection.** No infinite loops.
9. **ClawHUD updates must preserve Managed semantics.** Never accidentally self-restart into Standalone while externally owned.
10. **ClawHUD startup preference remains independent.** SteamAddon does not rewrite/restore it.
11. **ClawHUD owns settings.** Frontends never become independent INI authorities.
12. **Named Pipe is the app-integration boundary.** UI technology may change without changing runtime architecture.
13. **SteamAddon does not distribute ClawHUD.** Installation/update lifecycle remains separate.
14. **No SteamAddon knowledge in ClawHUD.** ClawHUD only knows Standalone vs explicit Managed launch mode.
15. **Do not prematurely share EC helpers.** Revisit only after a real runtime problem is demonstrated.
16. **Frontend technology is replaceable.** Win32, WPF, WinUI3, and Web remain valid post-separation options.
17. **VRR-critical presentation is untouched.** Integration/UI work stays above the renderer/presentation contract.

---

## 21. Final architecture and lifetime summary

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
     owner=user/ClawHUD       owner=SteamAddon
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

Lifecycle invariant:

```text
SteamAddon alive + Integration ON
    -> exactly one ClawHUD runtime
    -> Managed
    -> no Tray
    -> owner loss ends Managed

No SteamAddon ownership
    -> normal launch/startup = Standalone
    -> mode is never persisted by ClawHUD
```

The immediate development objective is:

> **Separate ClawHUD runtime from the UI/Tray shell, add stable Control IPC, introduce explicit Standalone/Managed composition, and then implement the owner-bound lifecycle before any standalone UI framework migration.**

This gives ClawHUD a durable architecture for independent Standalone use and SteamAddon integration while keeping the production HUD/VRR path single, native, and unchanged.