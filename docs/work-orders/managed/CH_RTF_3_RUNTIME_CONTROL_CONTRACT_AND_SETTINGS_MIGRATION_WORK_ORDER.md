# CH-RTF-3 — Runtime Control Contract and Legacy Settings Migration Work Order

> **Repository:** `onehoon/ClawHUD`  
> **Series:** ClawHUD Runtime / Frontend Separation  
> **Architecture source:** `docs/work-orders/managed/CLAW_HUD_RUNTIME_FRONTEND_SEPARATION_ARCHITECTURE_2026-09-02.md`  
> **PR plan source:** `docs/work-orders/managed/CLAW_HUD_RUNTIME_FRONTEND_SEPARATION_PR_PLAN_2026-09-02.md`  
> **Previous PRs:** #209 CH-RTF-1 Runtime Message Window Extraction, #210 CH-RTF-2 Tray Shell Callback Decoupling  
> **Analyzed main HEAD:** `92576b35b4393f88872a10eb0e43f94a9ef35006`  
> **Scope:** Introduce the in-process runtime-control contract and migrate the existing Win32 Settings frontend to it  
> **Status:** Ready for implementation

---

## 1. Objective

Create the semantic control boundary that every future ClawHUD frontend will use, without adding IPC or changing the current Settings UI technology.

The existing Win32 Settings window must become the first client of that boundary.

Current relationship:

```text
SettingsWindow
    |
    | App&
    v
App
    |- HUD state / settings
    |- startup registration
    |- tweak state
    |- runtime orchestration
    `- Settings lifetime
```

Target relationship after this PR:

```text
SettingsWindow
    |
    | IRuntimeControl / RuntimeControl contract
    v
App (current implementation / composition root)
    |- existing HUD semantics
    |- existing persistence
    |- existing startup registration
    `- existing tweak semantics

SettingsWindow
    |
    `- narrow onDestroyed callback
           |
           v
      App::PostSettingsDestroyed()
```

The key outcome is:

> **SettingsWindow must no longer know or include the concrete `App` type for product state or mutations.**

This PR is still in-process only.

Do **not** add Named Pipe transport, protocol encoding, Managed mode, launch-mode parsing, another Settings process, WPF, WinUI 3, Web UI, or SteamAddon integration in this PR.

---

## 2. Current production baseline after PR #210

PR #209 separated the runtime hidden HWND from the Tray shell.

PR #210 removed the remaining `TrayIcon -> App` dependency.

The remaining frontend coupling is now concentrated in the legacy Win32 Settings implementation.

### 2.1 `SettingsWindow` still owns a concrete `App&`

Current `SettingsWindow.h`:

```cpp
class App;

class SettingsWindow
{
public:
    explicit SettingsWindow(App& app);
    ...

private:
    App& app_;
    ...
};
```

Current Settings implementation files include `App.h` and directly use App state/mutations.

Affected code includes:

```text
src/ClawHUD/SettingsWindow.cpp
src/ClawHUD/SettingsWindow.Settings.cpp
src/ClawHUD/SettingsWindow.Tweaks.cpp
```

`SettingsWindow.About.cpp` does not need runtime control and should not gain it unnecessarily.

### 2.2 Current App facade already contains the product semantics we need

The current public Settings-facing App API includes:

```text
StartWithWindows
SetStartWithWindows
HudEnabled
SetHudEnabled
HudSizeOffset
HudOptions
HudFont
SetHudFont
SetHudAlignment
SetHudBackgroundMode
SetHudOpacity
SetHudSizeOffset
SetHudVisibilityMode
IntelVrrRangeFixEnabled
SetIntelVrrRangeFixEnabled
IntelVrrLastResult
```

Do not reimplement these behaviors in a new service.

The new runtime-control contract should expose these product intents while `App` continues to execute the existing implementations.

### 2.3 Several setters have important side effects or rollback behavior

This is not a simple settings-DTO migration.

Examples from current production behavior:

```text
SetStartWithWindows
- updates desired state
- applies/deletes the startup shortcut
- rolls back state if registration/removal fails
- persists only after success

SetHudEnabled
- creates/shuts down HudPresentation through HudController
- starts/stops telemetry/game-session-related runtime state
- persists the enabled state
- can fail when presentation initialization fails

SetHudVisibilityMode
- updates telemetry visibility mode
- can reevaluate current foreground game
- can trigger an FPS sample
- persists
- reconciles HUD visibility

SetHudFont / alignment / background / size / opacity
- operate through HudController
- may require presentation state changes
- persist only according to current behavior

SetIntelVrrRangeFixEnabled
- persists the startup tweak preference
- does not apply the tweak immediately
```

The contract must delegate to these existing semantics.

Do not move product logic into `SettingsWindow`.

---

## 3. Required runtime-control contract

Add one small in-process interface representing frontend-visible ClawHUD control.

A suitable location is:

```text
src/ClawHUD/RuntimeControl.h
```

A separate `.cpp` is only needed if there is real non-trivial implementation code. Do not create files just for symmetry.

Use a ClawHUD namespace such as:

```cpp
namespace clawhud
{
    ...
}
```

### 3.1 Authoritative snapshot

Define one frontend-facing in-process snapshot type.

Conceptually:

```cpp
struct RuntimeSettingsSnapshot
{
    bool startWithWindows{};
    bool hudEnabled{};
    int hudSizeOffset{};
    HudFont hudFont{};
    HudLayoutOptions hudOptions{};
    bool intelVrrRangeFixEnabled{};
    std::optional<IntelVrrRunResult> intelVrrLastResult;
};
```

The exact field order/name may vary, but the snapshot must contain everything the current Settings/Tweaks UI needs to render without reaching into `App`, `HudController`, `HudSettingsStore`, or tweak stores directly.

The snapshot is an **in-process semantic type only**.

Do not treat its raw C++ layout as the future IPC wire format. CH-RTF-4 will define explicit versioned wire structures/encoding.

### 3.2 Interface operations

Use one narrow interface, conceptually:

```cpp
class IRuntimeControl
{
public:
    virtual ~IRuntimeControl() = default;

    virtual RuntimeSettingsSnapshot GetSettingsSnapshot() const = 0;

    virtual void SetStartWithWindows(bool enabled) = 0;
    virtual bool SetHudEnabled(bool enabled) = 0;
    virtual void SetHudVisibilityMode(HudVisibilityMode mode) = 0;
    virtual void SetHudSizeOffset(int offset) = 0;
    virtual void SetHudFont(HudFont font) = 0;
    virtual void SetHudAlignment(HudAlignment alignment) = 0;
    virtual void SetHudBackgroundMode(HudBackgroundMode mode) = 0;

    virtual bool PreviewHudOpacity(float opacity) = 0;
    virtual bool CommitHudOpacity(float opacity) = 0;

    virtual void SetIntelVrrRangeFixEnabled(bool enabled) = 0;
};
```

The exact interface/type names may vary slightly, but preserve these design rules:

1. The API describes **ClawHUD runtime control**, not Win32 widgets.
2. Do not expose HWNDs, control IDs, `SettingsWindow`, `TrayIcon`, `HudController`, `HudPresentation`, PresentMon objects, EC objects, or game-detection objects.
3. Do not expose `HudSettingsStore` or allow callers to persist settings directly.
4. Do not add a generic service locator, command bus, RPC framework, property bag, `std::any`, or stringly-typed command API.
5. Keep typed domain enums for this in-process interface.
6. `RequestShutdown` is not required in this PR; shutdown IPC belongs to a later stage.

---

## 4. Preserve opacity preview / commit as explicit semantics

This is an important contract requirement.

Current Settings behavior is:

```text
TB_THUMBTRACK
    -> SetHudOpacity(value, persist=false)
    -> live visual preview
    -> do not persist every slider movement

all non-TB_THUMBTRACK completion events
    -> SetHudOpacity(value, persist=true)
    -> persist the final value
```

Do not carry a generic UI-facing `persist` boolean into the future public control protocol if it can be avoided.

Expose the product intents explicitly:

```text
PreviewHudOpacity(value)
CommitHudOpacity(value)
```

`App` may implement them by delegating to the existing method internally:

```cpp
bool App::PreviewHudOpacity(float opacity)
{
    return SetHudOpacity(opacity, false);
}

bool App::CommitHudOpacity(float opacity)
{
    return SetHudOpacity(opacity, true);
}
```

Equivalent factoring is acceptable.

Do not change opacity range, step, renderer behavior, background-only semantics, or persistence timing.

`Background Opacity` remains **background only** and this PR must not introduce window-wide or visual-wide opacity.

---

## 5. `App` remains the current implementation authority

For this PR, the simplest target is for `App` to implement the new runtime-control interface directly.

Conceptually:

```cpp
class App : public clawhud::IRuntimeControl
{
    ...
};
```

Do not introduce another `RuntimeControlService` object that merely forwards every call back to App unless implementation reveals a concrete ownership need.

`App` is already the valid composition root and mediator.

### 5.1 Add `GetSettingsSnapshot()`

Build the snapshot from current authoritative runtime state:

```text
startWithWindows_                 -> startWithWindows
hudController_.Enabled()          -> hudEnabled
hudController_.SizeOffset()       -> hudSizeOffset
hudController_.Font()             -> hudFont
hudController_.Options()          -> hudOptions
intelVrrRangeFixEnabled_           -> intelVrrRangeFixEnabled
IntelVrrResultStore::Load()        -> intelVrrLastResult
```

Reuse `IntelVrrLastResult()` if retained internally rather than duplicating storage access unnecessarily.

The snapshot must reflect **current effective state**, including rollback results.

Example:

```text
user checks Start with Windows
-> startup registration fails
-> App restores previous value
-> next GetSettingsSnapshot()
-> returns previous authoritative value
```

The frontend must render the snapshot, not the originally requested value.

### 5.2 Preserve existing product methods

Where possible, use the current App method bodies unchanged as interface implementations.

Do not rewrite HUD enable/disable, visibility, startup registration, telemetry coordination, or persistence solely to satisfy the interface.

This PR is about ownership boundaries, not product behavior changes.

---

## 6. Migrate `SettingsWindow` away from concrete `App`

### 6.1 Constructor and storage

Replace the concrete App dependency with:

```text
IRuntimeControl&
+
small Settings lifetime callback
```

Conceptually:

```cpp
struct SettingsWindowActions
{
    std::function<void()> onDestroyed;
};

class SettingsWindow
{
public:
    SettingsWindow(
        clawhud::IRuntimeControl& runtimeControl,
        SettingsWindowActions actions);

private:
    clawhud::IRuntimeControl& runtimeControl_;
    SettingsWindowActions actions_;
};
```

A single `std::function<void()> onDestroyed` constructor argument is also acceptable if it remains clear.

Do not create a broad Settings shell framework.

### 6.2 Remove concrete includes

After migration:

```text
SettingsWindow.cpp
SettingsWindow.Settings.cpp
SettingsWindow.Tweaks.cpp
```

must not require:

```cpp
#include "App.h"
```

`SettingsWindow.h` must not forward-declare or store `App`.

The Settings frontend may include `RuntimeControl.h` and the domain types it genuinely needs.

### 6.3 Render controls from one authoritative snapshot

Refactor control refresh functions so they read a runtime snapshot rather than making a chain of concrete App getter calls.

For example:

```cpp
void SettingsWindow::UpdateHudControls()
{
    const auto snapshot = runtimeControl_.GetSettingsSnapshot();
    ...
}
```

`UpdateGeneralControls()` and `UpdateTweaksControls()` may either fetch their own snapshot or receive one from a common refresh helper if that keeps the code simpler.

Do not add caching that can become stale.

The current UI is small enough that obtaining a fresh in-process snapshot for refresh is preferred over frontend-owned state synchronization.

---

## 7. Mutation behavior in the Win32 Settings frontend

Convert each current direct `app_.Xxx()` call to the runtime-control interface.

### 7.1 Start with Windows

Current UI behavior must remain:

```text
checkbox changed
-> request SetStartWithWindows
-> refresh from authoritative state
```

The refresh is required because the current App implementation can roll back when shortcut creation/removal fails.

### 7.2 Enable HUD

Keep:

```text
checkbox changed
-> SetHudEnabled(requested)
-> refresh controls from snapshot
```

If enable fails, the UI must show the actual resulting state rather than leaving the requested check state visible.

### 7.3 Visibility mode

Keep the same two values and same side effects:

```text
Always
InGameOnly
```

After mutation, refresh from the authoritative snapshot.

Do not alter game detection or visibility reconciliation.

### 7.4 HUD size

The current UI computes plus/minus from the live runtime value.

After removing `app_.HudSizeOffset()`, use the current snapshot:

```text
snapshot.hudSizeOffset - 1
snapshot.hudSizeOffset + 1
```

Continue to rely on existing HudController clamping/validation behavior. Do not add a second independent range policy in the UI.

### 7.5 Font, alignment, and background mode

Route through typed runtime-control methods.

After mutation, refresh the relevant controls from the authoritative snapshot.

This is especially important for font/presentation recreation failure paths.

The current `App::SetHudFont()` contains a Settings-specific refresh call. Once the Settings handler itself performs authoritative refresh, remove that unnecessary frontend-specific refresh from the runtime setter if doing so is safe.

Do not change the underlying font/presentation rollback semantics.

### 7.6 Intel VRR Range Fix

Route the toggle through:

```text
SetIntelVrrRangeFixEnabled
```

Render the checkbox and last-run result from the snapshot.

Do not change the existing policy:

> The setting is persisted and applied by startup tweak orchestration; toggling it in Settings does not immediately run the VRR tweak.

---

## 8. Preserve F8 Settings refresh behavior

Current `App::HandleHudToggleHotkey()` updates the open Win32 Settings window after F8 changes the manual HUD visibility state:

```cpp
if (settings_)
    settings_->UpdateHudControls();
```

Do not remove that observable behavior in this PR.

It is acceptable for App to continue owning the current Settings window and invoking its refresh while the legacy frontend remains in-process.

The important boundary for CH-RTF-3 is:

```text
SettingsWindow -> no concrete App dependency
```

not a complete elimination of every temporary:

```text
App -> legacy SettingsWindow
```

relationship.

Future external frontends will use IPC refresh/response semantics; that is not part of this PR.

---

## 9. Preserve asynchronous Settings destruction exactly

This is a critical lifetime rule.

Current destruction path is intentionally asynchronous:

```text
SettingsWindow receives WM_NCDESTROY
-> window_ = nullptr
-> App::PostSettingsDestroyed()
-> PostMessage(runtimeMessageWindow, kSettingsDestroyed)
-> App message loop receives it later
-> App::SettingsDestroyed()
-> settings_.reset()
```

This prevents the owning `unique_ptr<SettingsWindow>` from being destroyed synchronously from inside the window's own `WM_NCDESTROY` stack.

After removing `App&` from SettingsWindow, wire the lifetime callback like:

```cpp
[this]
{
    PostSettingsDestroyed();
}
```

and invoke that callback from `WM_NCDESTROY`.

Do **not** call `settings_.reset()` directly from the Settings window callback.

Do **not** replace `PostSettingsDestroyed()` with a direct synchronous destruction call.

Do not move the private `kSettingsDestroyed` message or change the current `RuntimeMessageWindow` destination in this PR.

---

## 10. `App::OpenSettings()` composition

Update the lazy Settings construction from the current form:

```cpp
settings_ = std::make_unique<SettingsWindow>(*this);
```

to the new contract plus lifetime callback, conceptually:

```cpp
settings_ = std::make_unique<SettingsWindow>(
    static_cast<clawhud::IRuntimeControl&>(*this),
    [this] { PostSettingsDestroyed(); });
```

Equivalent cleaner syntax is fine.

Preserve all current lazy behavior:

```text
first request
-> create SettingsWindow
-> Show

already open
-> reuse same SettingsWindow
-> Show / foreground

window closes or minimizes
-> asynchronous cleanup
-> next request creates a new SettingsWindow
```

Do not change minimize-to-destroy behavior or Settings window UX in this PR.

---

## 11. Recommended file changes

Expected primary files:

```text
src/ClawHUD/RuntimeControl.h                         [new]
src/ClawHUD/App.h
src/ClawHUD/App.cpp
src/ClawHUD/SettingsWindow.h
src/ClawHUD/SettingsWindow.cpp
src/ClawHUD/SettingsWindow.Settings.cpp
src/ClawHUD/SettingsWindow.Tweaks.cpp
```

Potential build/test files only if actually needed:

```text
CMakeLists.txt
cmake/ClawHUDTests.cmake
tests/RuntimeControl*Tests.cpp
```

Do not touch unrelated runtime domains.

`RuntimeMessageWindow`, `TrayIcon`, HUD presentation, telemetry, game detection, and updater code should remain unchanged unless a compile-only include adjustment is genuinely required.

---

## 12. Explicit out of scope

Do not implement any of the following here:

```text
Named Pipe server or client
IPC protocol structs / framing
serialization or codec
main-thread IPC dispatch
--managed parsing
Standalone/Managed launch composition
conditional tray creation
SteamAddon discovery
SteamAddon lifecycle ownership
Job Object ownership
update-mode restart policy
new standalone frontend
WinUI 3
WPF
Web UI
removal/redesign of the existing Win32 Settings UI
F8 removal
```

Do not add generic infrastructure for hypothetical future controls.

Only expose the state/mutations that current product behavior and the already-approved architecture require.

---

## 13. HUD / VRR safety contract — non-negotiable

This PR is a control-boundary refactor only.

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
- production Presentation API / DirectComposition path;
- premultiplied-alpha presentation behavior.

Do not implement Settings mutations by bypassing `HudController` or by writing presentation/window state directly.

`Background Opacity` remains background-only.

No HUD presentation source file should need a behavioral modification for this PR.

---

## 14. Verification requirements

### 14.1 Structural verification

Confirm after the change:

```text
SettingsWindow.h
- no App forward declaration
- no App& field

SettingsWindow.cpp
SettingsWindow.Settings.cpp
SettingsWindow.Tweaks.cpp
- no #include "App.h"
- no app_. product-state calls

RuntimeControl.h
- contains no Win32 control IDs or SettingsWindow knowledge
- contains no IPC/wire-layout assumptions
```

### 14.2 Build and tests

Run the repository's normal Debug and Release build/test validation.

At minimum preserve the existing CI baseline.

Run focused existing tests relevant to changed semantics where available, including HUD settings/presentation lifecycle and Intel VRR setting behavior.

Do not weaken or delete existing tests to make the refactor pass.

### 14.3 Manual Settings parity smoke

On a supported Claw device, verify the existing Settings UI still behaves identically:

```text
Tray left-click opens Settings
Tray menu -> Settings opens/reuses Settings
Start with Windows toggle
Enable HUD off/on
Always / In-game only
HUD size -2..+2 behavior
Unispace / Segoe UI Variable
Left / Center / Right
Full width / Content width
Background opacity live drag
Background opacity final persistence
Intel VRR Range Fix preference
last Intel VRR result display
minimize closes Settings as before
close and reopen Settings
F8 while Settings is open refreshes HUD-related controls as before
Tray Exit still shuts down normally
```

For startup registration failure or HUD presentation failure paths that are practical to induce, confirm the UI refreshes to the actual authoritative value rather than preserving a failed requested value.

### 14.4 Persistence parity

Verify no new settings file, key, or persistence authority is introduced.

`HudSettingsStore` remains the only ClawHUD settings persistence owner.

The refactor must not change existing settings names/defaults or write frequency, especially opacity preview vs commit.

---

## 15. Acceptance criteria

This PR is complete when all of the following are true:

1. A typed in-process runtime-control interface exists.
2. A single authoritative frontend snapshot exists.
3. `App` implements/delegates that contract using existing product semantics.
4. `SettingsWindow` no longer stores or references concrete `App` for state/mutations.
5. Settings implementation files no longer include `App.h` for product control.
6. All Settings controls render from authoritative runtime snapshot state.
7. All Settings mutations use the runtime-control contract.
8. Opacity preview and commit remain distinct and behaviorally identical.
9. Startup-registration rollback remains reflected correctly in the UI.
10. HUD enable/presentation failure remains reflected correctly in the UI.
11. Intel VRR setting remains startup-applied, not immediate-run.
12. F8 open-Settings refresh behavior remains intact.
13. Settings `WM_NCDESTROY` cleanup remains asynchronous through `PostSettingsDestroyed()`.
14. No IPC, Managed mode, or UI-framework migration is added.
15. HUD/VRR presentation invariants remain unchanged.
16. Existing builds/tests pass.

---

## 16. Handoff to CH-RTF-4

After this PR, the architecture should be:

```text
Legacy Win32 Settings
        |
        v
IRuntimeControl
        |
        v
App / existing runtime semantics
        |
        +-> HudController / HudPresentation
        +-> HudSettingsStore
        +-> telemetry / game-session orchestration
        `-> tweak preference/result
```

At that point CH-RTF-4 can define a versioned IPC wire protocol **against this semantic boundary** without depending on Win32 Settings internals or concrete App members.

Do not start transport/server work in CH-RTF-3.
