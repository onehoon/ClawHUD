# Settings Production Polish — Self-FPS Exclusion, Excluded-Window Event Suppression, Compact WPF Density, and Icon

**Date:** 2026-09-04  
**Status:** Ready for implementation  
**Reviewed baseline:** `main` at `a68f444d8d7323eb058da898a4a97ef5ca0c0633`  
**Context:** WPF Settings production cutover (#228) + startup crash hotfix (#229) are merged.

---

## 1. Objective

Polish the newly cut-over production Settings frontend in one focused PR.

This PR combines four closely related post-cutover issues found on the real MSI Claw:

1. **ClawHUD's own WPF Settings process can become the Always-mode FPS target.**
   `ClawHUD.Settings.exe` produces real WPF/D3D presents, so PresentMon reports its own UI frame rate in the HUD even though no game is running.
2. **An excluded foreground WPF Settings window produces a large LOCATIONCHANGE reevaluation storm.**
   Game detection already rejects the window, but keeps reevaluating the same permanently excluded executable on layout/composition changes.
3. **The WPF Settings visual density is much too large.**
   Touch-oriented minimum sizes from the initial shell make the five-card Settings window unnecessarily tall and make the custom toggle switch look oversized.
4. **`ClawHUD.Settings.exe` has no production ClawHUD application/window icon.**
   Reuse the existing native `src/resources/clawhud.ico`; do not introduce another artwork asset.

This is one production-polish PR. Do not split the above items unless implementation uncovers an actual architectural conflict.

Target new/modified meaningful implementation should remain roughly **300–500 LOC**. Tests and small project/XAML changes are expected on top of that.

---

## 2. Field evidence from `C:\GoogleDrive\ClawHUD\logs\0904`

The 2026-09-04 field log was reviewed end-to-end after the WPF startup crash hotfix.

### 2.1 Settings is not being classified as a game

When WPF Settings becomes foreground, game detection evaluates it and returns Hidden/NotFullscreenLike:

```text
Settings foreground
-> [GameDetection] foreground.evaluate ... pid=<ClawHUD.Settings PID>
-> [GameDetection] foreground.hidden ... reason=11
```

`GameScreenAdmissionReason::NotFullscreenLike` is `11` in the current enum.

Therefore the visible FPS is **not** caused by a false positive game-session admission.

### 2.2 Always FPS uses the current foreground PID independently

Current product behavior intentionally separates Always-mode FPS targeting from game detection:

```text
Always mode
    -> current foreground PID
    -> AlwaysModeFpsTarget
    -> PresentMon process query

In-Game Only
    -> current eligible foreground game PID
```

`AlwaysModeFpsTarget::SetForegroundProcess()` currently accepts any nonzero foreground PID without image filtering.

This is why ordinary apps usually show no FPS (`displayed=NA`) while WPF Settings does:

- Explorer and many normal desktop apps do not provide a useful sampled present stream for that foreground PID at the sampling moment.
- WPF Settings performs real Direct3D-backed composition/presents.
- The field log shows Settings around ~80 FPS immediately after opening and lower values while idle.

PresentMon is behaving correctly. The product bug is that **ClawHUD is measuring its own frontend**.

### 2.3 WPF Settings causes redundant game-detection reevaluation

The same log shows a large number of:

```text
[GameDetection] foreground.evaluate reason=window-event
[GameDetection] foreground.hidden ...
```

while Settings is foreground.

The production window source intentionally observes `EVENT_OBJECT_LOCATIONCHANGE`. WPF can emit many such events while its visual tree/layout/composition changes.

The executable is permanently non-game once it has been classified `ExcludedExecutable`, so repeating full admission/process/DWM work for every LOCATIONCHANGE on the same excluded foreground HWND/PID is unnecessary.

Do **not** globally remove LOCATIONCHANGE observation: real game windows need it for meaningful bounds/fullscreen transitions.

---

## 3. Current source state

### 3.1 Always-mode target

Current `AlwaysModeFpsTarget` behavior is intentionally simple:

```cpp
bool AlwaysModeFpsTarget::SetForegroundProcess(DWORD processId) noexcept
{
    if (processId == targetProcessId_)
        return false;
    targetProcessId_ = processId;
    published_ = {};
    return true;
}
```

Existing tests explicitly assert that Always uses the foreground PID and never falls back to the In-Game Only game PID.

Preserve that contract for all **external** applications.

### 3.2 Central production executable exclusion

`ProductionTargetPolicy.cpp` already centrally rejects known non-game images such as:

```text
explorer.exe
steam.exe
steamwebhelper.exe
gamebar.exe
browsers
MSI Center M processes
windowsterminal.exe
runtimebroker.exe
werfault.exe
...
```

but currently does not include:

```text
clawhud.settings.exe
```

The native main process is already excluded from `InspectProductionTargetProcessDetailed()` by its `ownProcessId` check, but the WPF frontend is a separate process and therefore needs explicit product identity handling.

### 3.3 Current WPF geometry/density

`MainWindow.xaml`:

```text
Window width       = 700 DIP
Window height      = 744 DIP
Min/Max width      = 700
Min/Max height     = 744
Grid margin        = 12
```

`SettingsStyles.xaml`:

```text
SettingsCard padding       = 12
SettingsCard bottom margin = 6
SegmentToggle MinHeight    = 42
SegmentToggle padding      = 14,0
StepperButton              = 46 x 40
SettingToggleSwitch min    = 96 x 42
Switch track               = 88 x 40
Switch knob                = 28 x 28
Slider MinHeight           = 40
```

These dimensions explain the excessive vertical height and oversized ON/OFF control seen on the target device.

### 3.4 Current WPF icon state

`ClawHUD.Settings.csproj` currently has an application manifest but no `ApplicationIcon`.

The repository already has the canonical product icon:

```text
src/resources/clawhud.ico
```

Native `ClawHUD.exe` uses the same asset through `ClawHUD.rc`.

Reuse it.

---

# Part A — Correctness: never show ClawHUD's own FPS

## 4. Product rule

Add a narrow, explicit product invariant:

> **ClawHUD must never use either of its own production processes as an Always-mode FPS target.**

The self-owned images are:

```text
clawhud.exe
clawhud.settings.exe
```

When either is the foreground application:

```text
Always FPS target PID = 0
latest FPS             = unavailable
stale FPS hold         = cleared immediately
PresentMon FPS query   = released/cleared through the existing target-change path
```

The HUD itself may remain visible in Always mode; only the FPS metric becomes unavailable.

Do not hide the HUD merely because Settings is foreground.

---

## 5. Do not apply the whole game reject list to Always FPS

This is important.

Do **not** change Always mode into:

```text
foreground PID
-> full ProductionTargetPolicy game eligibility
-> only game-like executable gets FPS
```

That would silently change the intended Always-mode semantics.

For example, a future non-game external application with a valid Present stream is still allowed to show foreground FPS in Always mode unless it is explicitly one of ClawHUD's own processes.

The required change is **self-exclusion**, not "game-only FPS".

Preserve:

```text
external foreground process -> existing Always behavior
```

---

## 6. Centralize ClawHUD-owned process identity

Do not scatter case-insensitive literal comparisons through App/telemetry code.

Add a small reusable policy in the existing production-target area or another equally narrow policy location.

Preferred conceptual API:

```cpp
bool IsClawHudOwnedImage(std::wstring_view image) noexcept;
bool IsClawHudOwnedProcess(DWORD processId) noexcept;
```

Exact names are implementation choice.

Required image behavior:

```text
clawhud.exe              -> true
ClawHUD.EXE              -> true
C:\...\ClawHUD.exe      -> true if the image helper accepts paths
clawhud.settings.exe     -> true
ClawHUD.Settings.EXE     -> true
unrelated.exe            -> false
```

Reuse the existing basename/lowercase conventions rather than creating inconsistent matching rules.

For a PID-based check:

- PID `0` is not a target;
- native `GetCurrentProcessId()` is always self;
- another PID should be queried with `PROCESS_QUERY_LIMITED_INFORMATION` and `QueryFullProcessImageNameW` or the existing equivalent helper;
- failure to inspect an arbitrary external process should **not** broadly suppress that PID as if it were self.

Do not add process enumeration or polling.

---

## 7. Apply self-exclusion at both Always authority entry points

There are two important ways Always can acquire a foreground PID:

1. normal foreground-change notifications;
2. entering/switching to Always mode, where the currently known foreground PID is adopted immediately.

Self-exclusion must cover both.

A good structure is a single sanitizer/policy used before `AlwaysModeFpsTarget::SetForegroundProcess()`:

```cpp
DWORD ResolveAlwaysFpsForegroundTarget(DWORD foregroundPid) noexcept
{
    return IsClawHudOwnedProcess(foregroundPid) ? 0 : foregroundPid;
}
```

Then use the same rule in:

```text
ProductionTelemetryController::OnForegroundProcessChanged(...)
ProductionTelemetryController::SetVisibilityMode(... currentForegroundProcessId)
```

or an equivalent centralized boundary.

Do not implement one path and forget the other.

### Required transition behavior

Example:

```text
external game/app pid=5000 -> valid FPS 60
foreground changes to ClawHUD.Settings.exe pid=9000
    -> target changes to 0 immediately
    -> prior 60 FPS disappears immediately
    -> stale hold cannot retain 60 FPS

foreground returns to pid=5000
    -> target becomes 5000
    -> new sample required
```

No previous self/external FPS may leak across the boundary.

The existing target-change invalidation semantics should do most of this; do not create a second stale-FPS mechanism.

---

## 8. Add WPF Settings to central game-detection executable exclusion

Add:

```text
clawhud.settings.exe
```

to the central `IsRejectedProductionTargetImage()` policy.

This is independent of the Always FPS fix.

Reason:

- the WPF Settings executable is definitively part of ClawHUD infrastructure;
- it can never be a supported game target;
- relying only on its current size causing `NotFullscreenLike` is weaker than identifying the executable correctly;
- future window-size/layout changes must not make it a renderer-verification candidate.

Do not add every arbitrary ClawHUD helper name speculatively. Add actual production process names only.

---

# Part B — Correctness/performance: suppress redundant LOCATIONCHANGE reevaluation

## 9. Preserve meaningful window-event behavior

Current `ProductionGameWindowSource` observes:

```text
CREATE
SHOW
HIDE
LOCATIONCHANGE
DESTROY
```

Keep this source contract unchanged.

LOCATIONCHANGE is meaningful for real candidates because a process can transition:

```text
windowed -> fullscreen-like
fullscreen-like -> windowed
monitor/bounds changes
```

Do not remove `EVENT_OBJECT_LOCATIONCHANGE` globally and do not debounce all window events with a timer.

---

## 10. Skip only permanently excluded current foreground LOCATIONCHANGE

Once the authoritative current foreground evaluation says:

```text
decision        = Hidden
admissionReason = ExcludedExecutable
```

then a LOCATIONCHANGE for the **same current foreground HWND/PID** cannot make that executable eligible.

Suppress only that redundant path.

Conceptually:

```cpp
if (event.type == ProductionWindowEventType::LocationChange &&
    current.decision == ForegroundGameDecision::Hidden &&
    current.admissionReason == GameScreenAdmissionReason::ExcludedExecutable &&
    event.window == current.window &&
    event.processId == current.processId &&
    event.window == foregroundWindow &&
    event.processId == foregroundProcessId)
{
    return false;
}
```

The exact helper location may be `GameSessionCutoverPolicy` or another narrow pure policy.

### Preserve these events

Even for an excluded executable, do not broadly suppress unrelated lifecycle changes in this PR:

```text
foreground change
SHOW
HIDE
DESTROY
```

They remain cheap correctness boundaries and can update authoritative foreground/window state.

### Preserve LOCATIONCHANGE for these cases

```text
Eligible game
NeedsRendererVerification candidate
Hidden because NotFullscreenLike
Hidden because Cloaked / visibility/bounds state
unknown/unavailable process inspection
```

Especially `NotFullscreenLike` must still reevaluate LOCATIONCHANGE: that is exactly how a windowed game can become fullscreen-like.

Only `ExcludedExecutable` is permanent enough for this optimization.

---

# Part C — WPF visual density

## 11. Design direction

The initial WPF shell deliberately over-optimized for touch.

That is no longer the desired visual contract.

Use a normal **Windows 11 desktop Fluent density** while keeping the controls perfectly usable by mouse and ordinary touch.

Do not try to enforce a 40–44 DIP minimum hit target everywhere.

Do not replace WPF with WinUI or add a third-party control library.

Keep:

- .NET 10 WPF;
- built-in Fluent theme;
- same five cards;
- same order;
- same labels;
- same runtime behavior;
- no scrolling;
- fixed window;
- 700 DIP width.

This PR is sizing/polish only, not a visual redesign.

---

## 12. Target geometry

Change the fixed window from:

```text
700 x 744 DIP
```

to:

```text
700 x 600 DIP
```

Use:

```xml
Width="700" Height="600"
MinWidth="700" MaxWidth="700"
MinHeight="600" MaxHeight="600"
```

Keep:

```text
ResizeMode=NoResize
WindowStartupLocation=CenterScreen
```

No `ScrollViewer`.

At the primary target:

```text
1920 x 1200 @ 150%
```

all five cards must fit in the client area without clipping or scrolling.

If implementation proves 600 DIP is short by a few DIP because of actual Fluent theme measurement, adjust **compact internal spacing first**. Do not casually restore the old 744 DIP height.

A small final height adjustment around 600 is acceptable only with measured evidence and must remain clearly compact relative to 744.

---

## 13. Concrete density targets

Use these values as the default implementation target.

### 13.1 Cards

Current:

```text
Padding = 12
Margin bottom = 6
```

Change to:

```text
Padding = 10
Margin bottom = 6
```

Keep `CornerRadius=8` and existing Fluent brushes/border.

### 13.2 Segmented controls

Current:

```text
MinHeight = 42
Padding = 14,0
```

Change to approximately:

```text
MinHeight = 32
Padding = 12,0
```

Keep the current checked/accent behavior and OneWay authoritative binding semantics.

Do not change Click handlers or mutation behavior.

### 13.3 HUD size stepper

Current:

```text
46 x 40
```

Change to:

```text
34 x 32
```

Keep the `−` / `+` content, boundary enablement, and authoritative offset logic unchanged.

The center size label margin may be reduced from `12,0` if necessary for balanced density, but do not make it visually cramped.

### 13.4 Toggle switch

The current custom switch is the most visibly oversized element:

```text
control min = 96 x 42
track       = 88 x 40
knob        = 28 x 28
```

Replace only its dimensions with a compact Windows-11-like desktop switch.

Target:

```text
control MinWidth  ~= 40
control MinHeight ~= 24
track             = 40 x 20
track radius      = 10
knob              = 12 x 12
knob side margin  = 4
```

A 42–44 x 22–24 track is also acceptable if visual testing shows better proportions.

Do **not** keep a 80+ DIP wide switch.

Keep:

- current `ToggleButton` implementation;
- Fluent default/accent brushes;
- checked-state knob movement;
- OneWay authoritative binding;
- existing Click mutation handlers.

Do not introduce a custom animated switch framework.

### 13.5 Slider

Current:

```text
MinHeight = 40
```

Change to:

```text
MinHeight = 30
```

Keep the existing PR4/PR229 interaction contract completely unchanged:

```text
50..100
step 5
Preview while dragging
Commit on completion
ValueChanged subscription only AFTER MainWindow/ViewModel/DataContext construction
programmatic authoritative updates do not become user mutations
```

**Do not move `ValueChanged` back into XAML.**

PR229 fixed a real startup crash caused by that ordering.

### 13.6 Label spacing

Current section labels frequently use:

```text
Margin="0,8,0,3"
```

Reduce repeated vertical spacing where useful, e.g.:

```text
Margin="0,6,0,2"
```

Keep `FontSize=14` unless actual layout evidence demands otherwise.

Do not make text tiny merely to hit the target height.

---

## 14. Do not regress interaction behavior while compacting

Visual XAML/style changes must not change:

- Enable HUD mutation;
- Display mode mutation;
- HUD size bounds;
- Font selection;
- Alignment selection;
- Background width;
- opacity Preview/Commit sequencing;
- Intel VRR preference;
- Start with Windows;
- discrete/opacity mutual exclusion;
- authoritative snapshot reconciliation;
- activation refresh;
- runtime-loss close;
- Settings singleton/activation relay.

This PR is not an opportunity to refactor the ViewModel or IPC layer without a concrete need.

---

# Part D — WPF executable/window icon

## 15. Reuse the canonical ClawHUD icon

Use:

```text
src/resources/clawhud.ico
```

for WPF Settings.

Do not duplicate/edit/regenerate the icon.

The Settings frontend is part of the same product and should look identical in:

```text
window title bar
taskbar / Alt-Tab representation
ClawHUD.Settings.exe file metadata/shell icon
```

---

## 16. Set both executable and Window icon deterministically

`ClawHUD.Settings.csproj` should embed the existing icon as the application icon.

A suitable shape is:

```xml
<PropertyGroup>
  ...
  <ApplicationIcon>..\resources\clawhud.ico</ApplicationIcon>
</PropertyGroup>
```

If WPF does not reliably use the embedded application icon for the Window chrome in the current publish shape, also include the same file as a WPF Resource and explicitly assign `MainWindow.Icon` using a pack/resource URI.

Preferred deterministic pattern when needed:

```xml
<ItemGroup>
  <Resource Include="..\resources\clawhud.ico"
            Link="Resources\clawhud.ico" />
</ItemGroup>
```

and in the Window:

```xml
Icon="Resources/clawhud.ico"
```

Exact resource URI syntax should follow what actually builds under this project.

Acceptance is behavior, not a specific XML spelling:

```text
published ClawHUD.Settings.exe has ClawHUD icon
MainWindow title bar has ClawHUD icon
no separate runtime .ico file is required next to the EXE unless MSBuild proves it necessary
```

Keep framework-dependent publish unchanged.

---

# Part E — Tests

## 17. ProductionTargetPolicy tests

Extend `tests/ProductionTargetPolicyTests.cpp`.

Required coverage:

```text
IsRejectedProductionTargetImage("clawhud.settings.exe") == true
case/path normalized eligibility also rejects ClawHUD.Settings.EXE
ordinary game executable remains eligible
```

If a dedicated `IsClawHudOwnedImage()` helper is introduced, test:

```text
clawhud.exe              -> true
ClawHUD.EXE              -> true
clawhud.settings.exe     -> true
ClawHUD.Settings.EXE     -> true
unrelated.exe            -> false
```

If the helper accepts full paths, test basename normalization explicitly.

---

## 18. Always FPS self-exclusion tests

Extend the existing Always-mode FPS/target policy tests rather than creating a large integration harness.

Required cases:

### 18.1 External foreground remains unchanged

```text
foreground external PID=5000
-> target PID=5000
```

Preserve the current contract.

### 18.2 Native ClawHUD foreground is excluded

Using a pure image/PID-policy seam where possible:

```text
ClawHUD.exe foreground
-> sanitized Always PID=0
```

### 18.3 WPF Settings foreground is excluded

```text
ClawHUD.Settings.exe foreground
-> sanitized Always PID=0
```

### 18.4 Self transition clears stale FPS

Test the state sequence at the nearest practical policy/controller seam:

```text
external target -> accepts 60 FPS
self target -> target becomes 0 / displayed FPS unavailable
external target again -> previous 60 cannot reappear without a new sample
```

Do not add flaky tests that depend on spawning real WPF or PresentMon processes.

---

## 19. LOCATIONCHANGE suppression tests

Extend `GameSessionCutoverPolicyTests` or the equivalent pure policy tests.

Required matrix:

```text
ExcludedExecutable + same foreground/current HWND/PID + LOCATIONCHANGE
    -> false (no reevaluation)

ExcludedExecutable + SHOW
    -> true when it otherwise affects current screen

ExcludedExecutable + HIDE
    -> true

ExcludedExecutable + DESTROY
    -> true

NotFullscreenLike + LOCATIONCHANGE
    -> true

NeedsRendererVerification/candidate + LOCATIONCHANGE
    -> true

Eligible/current game + LOCATIONCHANGE
    -> true

LOCATIONCHANGE for unrelated non-current/non-foreground window
    -> existing false behavior remains
```

The test must make clear that only **permanent executable exclusion** receives the optimization.

---

## 20. WPF regression tests

Keep all current Settings tests green, including the PR229 startup test.

### Mandatory PR229 regression

`MainWindowStartupTests` must continue to prove:

```text
real MainWindow InitializeComponent succeeds on STA
startup sends zero opacity Preview/Commit mutations
```

Visual-density changes must not reintroduce:

```xml
ValueChanged="OnOpacityValueChanged"
```

in XAML.

### Geometry assertion

Add a lightweight test if the current WPF test setup can inspect the constructed Window reliably:

```text
Width == 700
Height == 600 (or the final measured compact height)
ResizeMode == NoResize
```

Do not create pixel/screenshot tests just for fixed XAML dimensions.

### Icon validation

At minimum, the WPF project must build and framework-dependent publish must succeed with the icon configuration.

If straightforward, add a build/publish assertion that `ClawHUD.Settings.exe` exists after publish; the existing workflow already checks the framework-dependent publish closure, so do not duplicate large packaging infrastructure.

---

# Part F — Manual validation

## 21. Target-device Settings visual smoke

On MSI Claw:

```text
1920 x 1200
150% scale
```

Confirm:

1. Settings opens without the PR229 crash.
2. Window is visibly shorter than the prior 744-DIP build.
3. All five cards fit with no vertical/horizontal scrolling.
4. ON/OFF switches look like normal compact Windows controls, not large pill buttons.
5. Segment buttons are compact but readable.
6. `−` / `+` buttons remain easy to click.
7. opacity slider remains usable with mouse and touch.
8. no labels are clipped.
9. light/dark Fluent appearance remains correct.
10. title bar displays the ClawHUD icon.
11. taskbar/Alt-Tab and `ClawHUD.Settings.exe` use the ClawHUD icon.

Do not increase font size or row height simply to recover the old touch-first appearance.

---

## 22. Self-FPS field validation

Set HUD mode to `Always`.

### 22.1 Explorer/no-render baseline

With Explorer foreground, note current behavior; FPS may remain unavailable as before.

### 22.2 Settings foreground

Open Settings from tray and make it foreground.

Required result:

```text
HUD stays visible
FPS field is unavailable/hidden according to existing telemetry rendering behavior
no ClawHUD.Settings WPF FPS such as ~80 / 8 / 3 is displayed
```

Debug log should show an explicit target clear/sanitized self transition if the implementation has logging at that boundary.

Do not add high-frequency logs.

### 22.3 Return to a real game

Return foreground to a running game/application with a valid Present stream.

Required:

```text
new external foreground PID is adopted
FPS resumes from new samples
no Settings FPS leaks through stale hold
```

---

## 23. LOCATIONCHANGE field validation

With Settings foreground for a reasonable idle/interact period, compare debug log behavior.

Required:

- one normal foreground evaluation identifies Settings as `ExcludedExecutable`;
- repeated same-window WPF LOCATIONCHANGE events do **not** generate a matching full `foreground.evaluate reason=window-event` storm;
- Settings foreground/close lifecycle still reconciles correctly;
- real game windowed/fullscreen transitions still trigger appropriate reevaluation.

The goal is not zero WinEvent traffic. The goal is to stop doing full game admission work for a permanently excluded current foreground executable on every LOCATIONCHANGE.

---

# Part G — HUD / VRR safety contract

## 24. Non-negotiable zero-touch presentation boundary

Do not modify, replace, weaken, or work around:

- HUD `windowExStyle`;
- `WS_EX_TRANSPARENT`;
- `WS_EX_NOACTIVATE`;
- `WS_EX_TOPMOST`;
- existing `WS_EX_LAYERED` behavior;
- `WM_NCHITTEST -> HTTRANSPARENT`;
- `WM_MOUSEACTIVATE -> MA_NOACTIVATE`;
- `ProductionHudPresentationContract()`;
- independent-flip requirement;
- Presentation API / DirectComposition production path;
- premultiplied-alpha presentation contract.

Expected diff in:

```text
HudPresentation*
HudRenderer*
production presentation contract code
```

is **zero**.

The FPS target change is telemetry authority only.

The WPF compacting is Settings-only.

Do not use either as justification to touch HUD presentation.

All existing HUD/VRR regression tests must remain green.

---

# Part H — Explicit non-goals

## 25. Do not add

This PR must not add:

- game-only filtering to Always FPS;
- global browser/desktop FPS blocklisting in Always mode;
- polling or process enumeration;
- global LOCATIONCHANGE removal;
- timer-based game-detection debounce machinery;
- changes to renderer verification semantics;
- changes to In-Game Only targeting;
- changes to PresentMon API2 provider architecture;
- new IPC operations;
- new Settings features;
- localization;
- tabs/navigation;
- custom UI framework;
- WinUI 3;
- self-contained .NET publish;
- another icon asset;
- custom switch animations;
- HUD window-wide opacity changes;
- VRR presentation changes.

---

# Part I — Suggested implementation order

## 26. Recommended sequence

### Step 1 — product identity/self-target policy

1. add centralized ClawHUD-owned image/PID identity helper;
2. add `clawhud.settings.exe` to game-detection reject policy;
3. add pure policy tests.

### Step 2 — Always FPS self-exclusion

1. sanitize foreground PID before Always target adoption;
2. cover both foreground-change and enter-Always paths;
3. verify target=0 clears stale FPS through existing state transitions;
4. extend Always-mode tests.

### Step 3 — excluded LOCATIONCHANGE suppression

1. extend the existing pure current-screen event policy with authoritative exclusion state;
2. suppress only same-current/foreground `ExcludedExecutable + LOCATIONCHANGE`;
3. add the full event/reason matrix tests.

### Step 4 — WPF density

1. compact `SettingsStyles.xaml`;
2. compact label spacing / slider height in `MainWindow.xaml`;
3. set fixed window to 700x600 DIP;
4. preserve all event/binding semantics, especially the deferred opacity `ValueChanged` subscription.

### Step 5 — icon

1. reuse `src/resources/clawhud.ico`;
2. set WPF `ApplicationIcon`;
3. ensure MainWindow title icon resolves to the same asset;
4. verify framework-dependent publish.

### Step 6 — full regression

Run:

```text
WPF Settings Release build
WPF Settings full tests
framework-dependent WPF publish validation
native Release build
full native CTest
```

Then perform the target-device checks in sections 21–23.

---

# Part J — Acceptance criteria

## 27. Merge requirements

The PR is complete only when all of the following are true.

### FPS correctness

- `ClawHUD.exe` is never an Always FPS target.
- `ClawHUD.Settings.exe` is never an Always FPS target.
- opening Settings while no game is running no longer shows WPF Settings FPS.
- entering Settings from a valid external FPS target clears the prior FPS immediately.
- returning to an external target requires/follows new samples.
- external non-self Always-mode semantics remain unchanged.
- In-Game Only semantics remain unchanged.

### Game detection

- `clawhud.settings.exe` is centrally rejected as a production game target.
- same-current/foreground `ExcludedExecutable + LOCATIONCHANGE` does not trigger redundant full reevaluation.
- `NotFullscreenLike + LOCATIONCHANGE` still reevaluates.
- real game/candidate LOCATIONCHANGE behavior remains intact.

### WPF visual density

- fixed width remains 700 DIP.
- target fixed height is 600 DIP (or a very close measured compact value with justification).
- no ScrollViewer is introduced.
- all five cards fit at 1920x1200 @150%.
- segment controls are around 32 DIP high, not 42.
- stepper buttons are around 34x32, not 46x40.
- switch track is around 40x20 / 42–44x22–24, not 88x40.
- slider is around 30 DIP minimum height, not 40.
- font remains readable.
- all mutation/preview/commit behavior remains unchanged.
- PR229 startup crash regression remains covered and green.

### Icon

- WPF Settings uses existing `src/resources/clawhud.ico`.
- `ClawHUD.Settings.exe` shell icon is correct.
- Settings title bar icon is correct.
- taskbar/Alt-Tab icon is correct.
- no duplicate product icon is added.

### Safety/regression

- native/WPF builds pass.
- all WPF tests pass.
- full native CTest passes.
- framework-dependent publish remains valid.
- HUD presentation/VRR contract source is unchanged.
- click-through/no-activation/topmost/independent-flip/premultiplied-alpha tests remain green.

---

## 28. Final intended behavior

After this PR:

```text
ClawHUD Always HUD visible on desktop
    |
    +-- Explorer foreground
    |      -> existing behavior; usually FPS unavailable
    |
    +-- ClawHUD.Settings.exe foreground
    |      -> recognized as ClawHUD-owned + non-game
    |      -> Always FPS target = 0
    |      -> no self/WPF FPS
    |      -> redundant excluded LOCATIONCHANGE reevaluation suppressed
    |
    +-- external app/game foreground with measurable Present stream
           -> existing Always foreground FPS behavior
```

And the Settings frontend should visually be a compact, ordinary Windows 11 Fluent settings window rather than the oversized touch-first prototype, while retaining the same runtime behavior and the same VRR-safe HUD presentation contract.
