# Work Order — Standalone Game Detection Diagnostic Foundation

Status: implementation work order  
Prepared from `main` at `fbdab7cc14a02e0e444b39bec438f5f894a8efa3`  
Primary design reference: `archive/diagnostics/GAME_DETECTION_DIAG_DESIGN.md`

---

## 1. Goal

Create the foundation for a future standalone **ClawHUD Game Detection Diagnostic** console application in the existing `onehoon/ClawHUD` repository.

This work order is intentionally limited to the application/build/CI shell.

The actual diagnostic evidence collectors described in:

```text
archive/diagnostics/GAME_DETECTION_DIAG_DESIGN.md
```

must **not** be implemented in this foundation change.

The final direction is:

```text
ClawHUD production application
        |
        |  no diagnostic ownership / no shared refactor for diagnostics
        X
        |
ClawHUD.Diag standalone console application
```

The diagnostic is a developer-only tool. It does not need a Win32 settings UI, tray UI, installer integration, or polished end-user packaging.

---

## 2. Branch strategy

The main ClawHUD application is currently undergoing a large refactor.

Create the diagnostic foundation on a separate temporary feature branch, for example:

```text
feature/diag-foundation
```

The branch exists only to isolate this small foundation while production refactoring continues.

Do **not** create or maintain a permanent long-lived `diag` branch as a separate product line.

After the production refactor stabilizes:

1. rebase/update the foundation branch onto the then-current `main`;
2. resolve only build-system/workflow conflicts if any;
3. add the real diagnostic implementation in later focused work;
4. eventually merge the diagnostic target into the normal repository history.

Do not use the diagnostic branch as a reason to fork or duplicate ongoing production refactor work.

---

## 3. Non-negotiable architecture rule: no production refactor for Diag

The standalone diagnostic must **not** cause a refactor of the production application.

Do not introduce diagnostic-driven shared layers such as:

```text
ClawHUD.Core
ClawHUD.Common
Telemetry.Shared
Diagnostic.Common
GameDetection.Shared
```

Do not extract production classes into libraries merely so the diagnostic can link them.

Do not restructure `App.cpp`, production game detection, telemetry, HUD, PresentMon, EC, or renderer ownership for diagnostic reuse.

Do not change production public/internal APIs just to make them callable by `ClawHUD.Diag`.

The priority is **production isolation**, not DRY reuse.

If a later diagnostic implementation needs an algorithm that already exists in production or the archive, it may:

- study the existing implementation;
- reuse a proven algorithm concept;
- copy/adapt a small implementation into the diagnostic source tree when appropriate;
- remain independently maintainable.

That duplication is acceptable for this private developer diagnostic tool.

---

## 4. Production presentation / VRR contract must remain untouched

This work must not modify, replace, weaken, or work around any production HUD presentation behavior.

In particular, do not touch:

```text
HUD windowExStyle
WS_EX_TRANSPARENT
WS_EX_NOACTIVATE
WS_EX_TOPMOST
existing WS_EX_LAYERED behavior
WM_NCHITTEST -> HTTRANSPARENT
WM_MOUSEACTIVATE -> MA_NOACTIVATE
ProductionHudPresentationContract()
independent-flip requirement
production Presentation API / DirectComposition path
premultiplied-alpha presentation contract
```

The diagnostic foundation has no reason to interact with these components.

No HUD/VRR behavior change is acceptable as part of this work.

---

## 5. Repository layout

Add a new standalone source directory:

```text
src/
├─ ClawHUD/
│  └─ ... existing production application
├─ ClawHUD.EcHelper/
│  └─ ... existing production helper
└─ ClawHUD.Diag/
   └─ main.cpp
```

For the foundation change, keep `src/ClawHUD.Diag/` minimal.

Do not pre-create a large empty architecture.

The future implementation may eventually grow toward the source-oriented structure described by the design document, conceptually:

```text
ClawHUD.Diag/
├─ Win32/
├─ Gpu/
├─ PresentMon/
├─ Platform/
└─ Timeline/
```

but those components should be introduced only when the corresponding diagnostic evidence source is actually implemented.

---

## 6. Add a dedicated console executable target

Add a separate CMake executable target:

```text
ClawHUD.Diag
```

It must be a normal console executable.

Do **not** use the `WIN32` executable subsystem for this target.

Conceptually:

```cmake
add_executable(ClawHUD.Diag
    src/ClawHUD.Diag/main.cpp)

target_compile_features(ClawHUD.Diag PRIVATE cxx_std_20)
target_compile_definitions(ClawHUD.Diag PRIVATE
    UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
set_target_properties(ClawHUD.Diag PROPERTIES CXX_EXTENSIONS OFF)
```

Adjust exact include/link settings only when the foundation actually needs them.

Do not link the production `ClawHUD` target into the diagnostic.

Do not make `ClawHUD` depend on `ClawHUD.Diag`.

Do not make `ClawHUD.EcHelper` depend on `ClawHUD.Diag`.

---

## 7. Console interaction model

No graphical UI is required.

The future diagnostic should be controlled with a simple console menu.

Target interaction model:

```text
ClawHUD Game Detection Diagnostic

1. Start capture
2. Stop capture
3. Show session status
4. Exit

Select:
```

The diagnostic is a private developer tool, so keyboard-driven numeric commands are sufficient.

Do not add:

- Win32 settings windows;
- tray integration;
- XAML/WinUI/WPF dependencies;
- GUI resources solely for diagnostics;
- notification-area lifecycle;
- user-facing settings persistence.

### Foundation behavior

Because this PR must not implement the real diagnostic yet, the console shell may be intentionally minimal.

It is acceptable for the first version to display a clear placeholder such as:

```text
ClawHUD Game Detection Diagnostic
Diagnostic capture is not implemented in this foundation build.
```

If the menu is scaffolded now, commands that would start capture must clearly report that capture is not implemented rather than pretending to collect evidence.

Do not add fake/synthetic diagnostic output.

---

## 8. Future diagnostic purpose and scope

All later implementation must use:

```text
archive/diagnostics/GAME_DETECTION_DIAG_DESIGN.md
```

as the primary functional contract.

The future tool is a **Game Detection Evidence Recorder**, not a production detector.

Its purpose is to record external raw evidence while the operator:

- launches games;
- switches between games and desktop applications;
- Alt+Tabs;
- opens launchers/overlays;
- opens Game Bar/QAM where useful;
- exits games;
- repeats the process across Steam, non-Steam, and Microsoft/Xbox titles.

It must not produce an authoritative `isGame=true/false` verdict.

The future evidence model is expected to include the design document's sources:

```text
Win32 foreground evidence
Top-level window lifecycle / DWM geometry
PDH GPU Engine 3D activity / Top GPU ranking
PresentMon API2 per-PID renderer evidence
Steam RunningAppID transitions
Microsoft/Xbox game identity evidence
Raw Game Bar/QAM-related window/process observations
Timestamped JSONL timeline
End-of-session per-PID summary
```

None of those collectors are part of this foundation implementation.

---

## 9. Explicit first-version diagnostic exclusions

Do not turn the future Game Detection Diagnostic into a generic all-system diagnostic unless a later work order explicitly expands its scope.

The referenced design intentionally excludes the old broad API2/telemetry survey behavior.

Do not add in this foundation, and do not silently plan as required first-version features:

```text
PresentMon API2 capability survey
full API2 introspection dump
all-metric enumeration
frame CSV dump
CPU/GPU power telemetry
GPU clocks
GPU temperature
fan telemetry
memory telemetry
EC diagnostic
IGCL diagnostic
HUD state
VRR/presentation-state diagnostic
production GameDetectionCoordinator state
production candidate/committed PID state
```

The previous in-app generic PresentMon/API2 diagnostic must not be restored as-is.

---

## 10. PresentMon loader handling

The current production application uses the app-local:

```text
PresentMonAPI2Loader.dll
```

The standalone diagnostic does **not** need to package this DLL in its CI artifact.

The tool is for private developer use. When the future PresentMon API2 evidence collector is implemented, the operator will manually copy the loader from the main ClawHUD application beside the diagnostic executable.

Expected manual runtime layout later:

```text
ClawHUD.Diag.exe
PresentMonAPI2Loader.dll    <- manually copied from the main application
```

Therefore:

- do not embed `PresentMonAPI2Loader.dll` into the diagnostic EXE;
- do not implement temp extraction;
- do not create a special loader packaging mechanism;
- do not add the loader DLL to the Diag CI artifact;
- do not alter the production PresentMon loader/runtime arrangement.

The foundation does not need to load PresentMon at all yet.

---

## 11. Separate CI workflow

Add a dedicated workflow:

```text
.github/workflows/Build-Diag.yml
```

The diagnostic build must be independent from the existing production workflows.

Use a manual trigger initially:

```yaml
on:
  workflow_dispatch:
```

The diagnostic workflow should configure the repository and build only the diagnostic target, conceptually:

```text
cmake -S . -B build ... -DBUILD_TESTING=OFF
cmake --build build --config Release --target ClawHUD.Diag
```

Use the same Windows/MSVC/SDK generation family as the current repository unless the main branch has changed by the time this work is implemented.

Do not build all targets just to obtain the diagnostic executable.

---

## 12. Diag CI artifact contract

The `Build-Diag.yml` artifact must contain exactly the diagnostic executable:

```text
ClawHUD.Diag.exe
```

Do not include:

```text
ClawHUD.exe
ClawHUD.EcHelper.exe
PresentMonAPI2Loader.dll
PresentMon runtime MSI
Velopack files
fonts
production resources
installer/package output
```

The workflow should upload only the exact executable path rather than uploading the entire `Release` directory.

Conceptually:

```yaml
- name: Upload diagnostic executable
  uses: actions/upload-artifact@v4
  with:
    name: ClawHUD-Diag
    path: build/Release/ClawHUD.Diag.exe
    if-no-files-found: error
```

The downloadable GitHub Actions artifact container may of course be a ZIP at the Actions layer, but its payload must contain only `ClawHUD.Diag.exe`.

---

## 13. Keep production CI/release packaging unchanged

Do not add `ClawHUD.Diag.exe` to:

```text
.github/workflows/Build-Release.yml
Velopack staging
ClawHUD release assets
installed application files
production updater package
```

Do not make the production release workflow responsible for publishing the diagnostic.

`Build-Release.yml` must continue to produce the normal ClawHUD package only.

The existing `Build-Test.yml` must continue to validate the production repository normally.

Adding the CMake target must not break the ordinary all-target build used by `Build-Test.yml`.

---

## 14. Foundation validation

The foundation work must be validated at two levels.

### 14.1 Diagnostic-specific build

Configure with testing disabled and build only the new target:

```text
cmake -S . -B build-diag ... -DBUILD_TESTING=OFF
cmake --build build-diag --config Release --target ClawHUD.Diag
```

Confirm:

```text
ClawHUD.Diag.exe
```

is produced and launches as a console application.

Confirm its initial placeholder/menu behavior exits cleanly.

### 14.2 Production regression build

Run the repository's normal production validation equivalent to the current `Build-Test.yml` flow:

```text
cmake ... -DBUILD_TESTING=ON
cmake --build ... --config Release
ctest ... -C Release --output-on-failure
```

The new target must not break existing production compilation or tests.

No HUD presentation behavior should change, therefore existing HUD/VRR/presentation regression tests must remain unchanged and passing.

---

## 15. Do not implement real capture in this PR

This point is important because the production application is still being heavily refactored.

The foundation PR must stop after establishing:

```text
src/ClawHUD.Diag/main.cpp
CMake ClawHUD.Diag target
.github/workflows/Build-Diag.yml
```

plus only minimal supporting build/documentation changes required for those files.

Do not begin implementing:

- Observed PID Pool;
- WinEvent hooks;
- window lifecycle capture;
- DWM geometry capture;
- PDH Top GPU sampling;
- PresentMon API2 tracking;
- Steam registry notifications;
- Microsoft game identity probing;
- JSONL logging;
- summary generation.

Those belong to later work after the current production refactor stabilizes.

---

## 16. Future implementation shape after foundation

Once the production refactor is stable, implement the design document incrementally inside `ClawHUD.Diag` without changing production ownership.

A sensible later sequence is:

```text
Phase A
    session lifecycle + JSONL writer + timestamps

Phase B
    foreground/window lifecycle + DWM geometry

Phase C
    Observed PID Pool + stable process metadata

Phase D
    PDH Top GPU / PresentMon parity ranking

Phase E
    PresentMon API2 per-PID renderer evidence

Phase F
    Steam RunningAppID + Microsoft game identity

Phase G
    end-of-session per-PID timeline summary
```

This sequence is guidance for later work only; it is not scope for the foundation PR.

At every phase, preserve the diagnostic design principle:

```text
record evidence
!=
produce game verdict
```

---

## 17. Expected final repository shape after this work

```text
ClawHUD/
├─ src/
│  ├─ ClawHUD/
│  ├─ ClawHUD.EcHelper/
│  └─ ClawHUD.Diag/
│     └─ main.cpp
├─ tests/
├─ archive/
│  └─ diagnostics/
│     └─ GAME_DETECTION_DIAG_DESIGN.md
├─ docs/
│  └─ work-orders/
│     └─ GAME_DETECTION_DIAG_FOUNDATION.md
├─ .github/
│  └─ workflows/
│     ├─ Build-Test.yml
│     ├─ Build-Release.yml
│     └─ Build-Diag.yml
└─ CMakeLists.txt
```

Production and diagnostic source remain clearly separated while sharing the same repository/revision history.

---

## 18. Acceptance criteria

The work is complete when all of the following are true:

1. `src/ClawHUD.Diag/main.cpp` exists.
2. `ClawHUD.Diag` is a standalone console CMake target.
3. `ClawHUD.Diag` can be built independently with `--target ClawHUD.Diag`.
4. The console executable starts and exits cleanly.
5. No graphical diagnostic UI is introduced.
6. No real game-detection evidence collector is implemented yet.
7. No production source is refactored for diagnostic reuse.
8. No shared Core/Common/Telemetry library is introduced for the diagnostic.
9. No production HUD presentation/VRR contract is changed.
10. `.github/workflows/Build-Diag.yml` exists and is independently runnable.
11. The Diag workflow uploads only `ClawHUD.Diag.exe` as its artifact payload.
12. `PresentMonAPI2Loader.dll` is not bundled into the Diag artifact.
13. Production `Build-Release.yml` does not package or publish the diagnostic.
14. Existing production build/tests remain passing.
15. The future implementation is explicitly anchored to `archive/diagnostics/GAME_DETECTION_DIAG_DESIGN.md`.
16. The diagnostic remains an evidence recorder and does not become a second production game detector.

---

## 19. Explicit non-goals

This foundation work must not:

- reintroduce diagnostic functionality into `ClawHUD.exe`;
- add a Diagnostics tab/page/button to the main application;
- create a GUI for the diagnostic;
- add the diagnostic to Velopack or the main release package;
- refactor production code to share it with the diagnostic;
- add EC/IGCL/VRR/system telemetry diagnostics;
- restore the archived generic PresentMon API2 diagnostic;
- implement game-detection policy or an `isGame` verdict;
- modify production game-detection behavior;
- modify HUD visibility behavior;
- modify production PresentMon sampling;
- modify the production HUD presentation path;
- modify click-through/no-activation/topmost/independent-flip/premultiplied-alpha behavior.

---

## 20. Final implementation principle

Treat `ClawHUD.Diag` as a **private standalone research instrument living beside the production application, not inside it**.

The production app must not carry diagnostic complexity merely to support the tool.

For this first work item, build only the shell:

```text
separate console EXE
+
separate CMake target
+
separate manual CI
+
EXE-only artifact
+
zero production architecture impact
```

Then stop.
