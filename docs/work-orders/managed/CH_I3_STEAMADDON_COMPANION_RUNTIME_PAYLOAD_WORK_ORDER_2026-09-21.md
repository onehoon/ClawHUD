# Work Order — CH-I3 SteamAddon Companion Runtime Payload

**Date:** 2026-09-21  
**Status:** Ready for implementation  
**Target repository:** `onehoon/ClawHUD`  
**Target branch:** `integration/steamaddon`  
**Reviewed baseline:** `integration/steamaddon` at `830c250d6c306d492be3a905d968a4066395f7e2`  
**Baseline note:** includes CH-I1 / PR #245 and CH-I2 / PR #246  
**Expected PR count:** 1 focused PR  
**PR base:** `integration/steamaddon`  
**PR head:** implementation branch created from `integration/steamaddon`

---

# CRITICAL BRANCH POLICY

This is an integration-branch packaging task.

It does **not** target `main`.

```text
Work only from integration/steamaddon.
Create the implementation branch from integration/steamaddon.
Open the PR against integration/steamaddon.
DO NOT target main.
DO NOT merge this PR directly into main.
DO NOT rebase onto a newer main unless explicitly requested.
```

If GitHub or local tooling defaults to:

```text
base = main
```

stop and correct it to:

```text
base = integration/steamaddon
```

before opening the PR.

The completed ClawHUD/SteamAddon integration will be hardware-validated on the integration branches before any later explicit main-branch cutover.

---

## 1. Objective

Produce a **small, explicit SteamAddon companion payload** from the existing ClawHUD build.

The payload will later be embedded under the SteamAddon package as a dedicated subdirectory.

Target product layout:

```text
SteamAddon publish root/
├─ SteamInputAddonforClaw.exe
├─ ui/
├─ qam/
├─ overlay/
├─ fse/
└─ clawhud/
   ├─ ClawHUD.exe
   ├─ ClawHUD.EcHelper.exe
   ├─ PresentMonAPI2Loader.dll
   ├─ velopack_libc.dll
   ├─ LICENSE
   ├─ THIRD-PARTY-NOTICES.md
   ├─ fonts/
   │  ├─ Unispace.otf
   │  └─ Unispace-LICENSE.txt
   └─ runtime/
      └─ ClawHUD.PresentMonRuntime.msi
```

The executable remains:

```text
ClawHUD.exe --managed
```

Do **not** create:

```text
ClawHUD.Managed.exe
ClawHUD.Runtime.exe
SteamAddon.ClawHudHost.exe
a second C++ runtime target
a source fork
a duplicated renderer
```

CH-I1/CH-I2 already made the existing Managed mode the SteamAddon-owned runtime contract.

CH-I3 only creates a clean distributable payload around that proven runtime.

---

## 2. Why this is the preferred architecture

The current ClawHUD standalone release stages all of these together:

```text
ClawHUD.exe
ClawHUD.EcHelper.exe
velopack_libc.dll
PresentMonAPI2Loader.dll
PresentMon runtime MSI
fonts
ClawHUD.Settings.exe/.dll/.deps.json/.runtimeconfig.json
THIRD-PARTY-NOTICES.md
```

The SteamAddon integration does not need the standalone Settings frontend.

It also does not need a separate ClawHUD installer, ClawHUD VeloPack package, diagnostic executable, or standalone release metadata.

At the same time, creating a second native executable target would duplicate a large and VRR-sensitive source list and increase the chance that standalone and Managed runtime behavior diverge.

Therefore:

```text
one runtime binary
+ two packaging compositions
```

is the intended design.

### Standalone composition

```text
ClawHUD VeloPack package
-> ClawHUD.exe
-> WPF Settings
-> standalone updater/startup support
-> runtime dependencies
```

### SteamAddon composition

```text
SteamAddon package
-> clawhud/ClawHUD.exe --managed
-> only Managed runtime dependencies
-> no WPF Settings frontend
-> no ClawHUD installer/package ownership
```

---

## 3. Do not change runtime behavior in CH-I3

CH-I3 is packaging/build infrastructure.

Preferred production-code diff:

```text
zero C++ runtime behavior changes
```

Do not modify:

```text
App startup
Managed exit codes
Control IPC
PresentMon bootstrap behavior
EC helper behavior
settings persistence
game detection
Intel VRR Fix
suspend/resume
HUD visibility
renderer/presentation
```

If a runtime code change appears necessary to package the existing binary, stop and re-evaluate before expanding scope.

---

## 4. Payload directory must be isolated

The SteamAddon product already uses dedicated companion directories:

```text
ui/
qam/
overlay/
fse/
```

ClawHUD must follow the same model:

```text
clawhud/
```

Do not flatten ClawHUD files into the SteamAddon publish root.

This is important because ClawHUD has sibling native dependencies such as:

```text
velopack_libc.dll
PresentMonAPI2Loader.dll
ClawHUD.EcHelper.exe
```

and SteamAddon has its own dependencies/package lifecycle.

The dedicated directory prevents DLL-name collisions and keeps ClawHUD relative-path assumptions intact.

---

## 5. Exact required payload

The authoritative CH-I3 payload is:

```text
clawhud/
├─ ClawHUD.exe
├─ ClawHUD.EcHelper.exe
├─ PresentMonAPI2Loader.dll
├─ velopack_libc.dll
├─ LICENSE
├─ THIRD-PARTY-NOTICES.md
├─ fonts/
│  ├─ Unispace.otf
│  └─ Unispace-LICENSE.txt
└─ runtime/
   └─ ClawHUD.PresentMonRuntime.msi
```

### Why each binary/runtime file is required

#### ClawHUD.exe

The native HUD runtime.

SteamAddon later launches:

```text
clawhud\ClawHUD.exe --managed
```

#### ClawHUD.EcHelper.exe

Required by the existing EC telemetry client.

Current code resolves it as:

```cpp
std::filesystem::path(module).parent_path() / L"ClawHUD.EcHelper.exe"
```

Therefore it must remain beside `ClawHUD.exe`.

#### PresentMonAPI2Loader.dll

Required by the current PresentMon API2 client/runtime path.

Keep it beside `ClawHUD.exe`, matching the existing proven build layout.

#### runtime/ClawHUD.PresentMonRuntime.msi

Current bootstrap resolves the MSI relative to the running module:

```text
<ClawHUD.exe directory>\runtime\ClawHUD.PresentMonRuntime.msi
```

Preserve that exact relationship.

#### fonts/Unispace.otf

Required by the existing private-font renderer path.

Preserve the existing `fonts/` layout.

#### velopack_libc.dll

Keep it in CH-I3.

Although CH-I1 disabled ClawHUD self-update in Managed mode, the current shared `wWinMain` still executes the VeloPack fast-exit lifecycle bootstrap before launch-mode resolution.

Therefore the current binary still has a real startup dependency on `velopack_libc.dll`.

Do not attempt to compile VeloPack out of Managed in CH-I3.

That would turn a packaging PR into a second executable/build-mode refactor for no demonstrated product benefit.

If package size or embedded VeloPack behavior later proves to be a real issue, handle it as a separate focused optimization.

### LICENSE / THIRD-PARTY-NOTICES.md

The SteamAddon payload redistributes the ClawHUD binary and third-party runtime components.

Ship the existing ClawHUD GPL license and third-party notices inside the isolated `clawhud/` payload.

Do not silently rely on the standalone installer to provide them.

---

## 6. Explicitly forbidden payload files

The SteamAddon companion payload must **not** contain the standalone UI or standalone delivery artifacts.

Forbidden:

```text
ClawHUD.Settings.exe
ClawHUD.Settings.dll
ClawHUD.Settings.deps.json
ClawHUD.Settings.runtimeconfig.json

ClawHUD.Diag.exe

Setup.exe
*-full.nupkg
*-delta.nupkg
releases.stable.json

private .NET runtime:
coreclr.dll
clrjit.dll
hostfxr.dll
hostpolicy.dll
dotnet.exe
```

Do not include the WPF Settings frontend “just in case.”

Managed mode has no standalone Settings ownership, and `OpenSettings()` already guards against Managed launch.

The SteamAddon UI will later control ClawHUD over the existing Control IPC.

---

## 7. Keep the existing ClawHUD standalone package unchanged

CH-I3 must not remove or weaken the current standalone release composition.

The existing `.github/workflows/Build-Release.yml` still owns the standalone product:

```text
ClawHUD VeloPack release
+ WPF Settings frontend
+ standalone update feed
+ standalone Setup/package artifacts
```

Do not remove Settings from the standalone release.

Do not change its main executable.

Do not change its VeloPack feed semantics.

The new SteamAddon payload is an **additional packaging composition**, not a replacement for Standalone.

---

## 8. Add one authoritative staging script

Create a small reusable script, preferred path:

```text
scripts/package-steamaddon-runtime.ps1
```

The script should be the one source of truth for the SteamAddon runtime payload file list.

Do not duplicate the required/forbidden file list across multiple workflows if avoidable.

Recommended parameters:

```powershell
param(
    [Parameter(Mandatory)]
    [string]$BuildDirectory,

    [Parameter(Mandatory)]
    [string]$OutputDirectory
)
```

Expected call example:

```powershell
./scripts/package-steamaddon-runtime.ps1 `
    -BuildDirectory ./build/Release `
    -OutputDirectory ./artifacts/SteamAddonRuntime
```

The script should create:

```text
artifacts/SteamAddonRuntime/
└─ clawhud/
   └─ <exact required payload>
```

### Script responsibilities

The script must:

1. clean only its own output directory;
2. create `clawhud/fonts` and `clawhud/runtime`;
3. copy the exact required files;
4. fail if any required file is missing;
5. fail if any explicitly forbidden standalone artifact appears in the payload;
6. preserve the relative layout;
7. print the final file list and sizes.

Do not make the script:

```text
download dependencies
build C++
build WPF
publish GitHub releases
install PresentMon
run VeloPack pack
modify settings
write registry
```

It packages an already-built runtime only.

---

## 9. Use existing build output as the source

Current CMake already produces/copies the runtime dependencies into:

```text
build/Release/
├─ ClawHUD.exe
├─ ClawHUD.EcHelper.exe
├─ velopack_libc.dll
├─ PresentMonAPI2Loader.dll
├─ fonts/
└─ runtime/
```

Reuse this.

Do not duplicate the CMake source list in a second target.

Do not create a second `add_executable()` containing the same HUD/runtime implementation.

The staging script may copy:

```text
LICENSE
THIRD-PARTY-NOTICES.md
```

from the repository root because those are distribution documents rather than build outputs.

---

## 10. Add CI validation for the companion payload

Extend the existing Build Test workflow rather than creating a second full native compile pipeline unless there is a concrete CI reason not to.

Current:

```text
.github/workflows/Build-Test.yml

WPF build/test
-> CMake configure
-> native Release build
-> CTest
```

After the native build/CTest, add a step:

```text
Stage SteamAddon companion payload
```

using the new script.

The staging/validation step should run on PR CI so payload breakage blocks integration changes.

### Push coverage

Current Build Test push trigger is only:

```yaml
push:
  branches:
    - main
```

During this integration phase, add:

```yaml
    - integration/steamaddon
```

so the merged integration branch always has a validated payload artifact.

This does not make `integration/steamaddon` a release branch.

It only runs build/test packaging validation.

---

## 11. Upload an integration artifact

For:

```text
push to integration/steamaddon
PR whose base is integration/steamaddon
manual integration validation if the workflow supports it
```

upload the staged directory as a GitHub Actions artifact.

Suggested artifact name:

```text
ClawHUD-SteamAddonRuntime
```

Suggested path:

```text
artifacts/SteamAddonRuntime/clawhud
```

Suggested retention:

```text
7 days
```

The artifact is for integration testing.

It is **not** a public standalone ClawHUD release.

Do not create a GitHub Release from CH-I3.

Do not create or push a tag.

Do not publish a new stable VeloPack feed.

---

## 12. Optional zip for handoff

It is acceptable for the workflow/script to also create:

```text
ClawHUD-SteamAddonRuntime.zip
```

containing the top-level:

```text
clawhud/
```

directory.

If produced, also emit the SHA-256 in CI output or a sidecar text file.

Example:

```text
ClawHUD-SteamAddonRuntime.zip
ClawHUD-SteamAddonRuntime.zip.sha256
```

Do not invent a new signing system in CH-I3.

The future SteamAddon dependency/pin work will decide the permanent source-commit/archive-hash record.

If the zip adds unnecessary script complexity, the GitHub Actions directory artifact alone is sufficient for CH-I3.

---

## 13. CI smoke: prove the staged Managed binary actually starts

The packaging validation should test more than file existence.

On the GitHub-hosted Windows runner, launch the staged binary:

```text
artifacts/SteamAddonRuntime/clawhud/ClawHUD.exe --managed
```

The GitHub runner is not a supported MSI Claw device, so CH-I2 should make the process terminate non-interactively before PresentMon installation.

Expected Managed result:

```text
21 = UnsupportedHardware
or
22 = HardwareIndeterminate
```

Accept either value in CI because runner WMI/baseboard behavior may vary.

Example shape:

```powershell
$process = Start-Process `
    -FilePath "$payload\ClawHUD.exe" `
    -ArgumentList '--managed' `
    -PassThru -Wait

if ($process.ExitCode -notin @(21, 22)) {
    throw "Unexpected staged Managed startup exit code: $($process.ExitCode)"
}
```

This smoke proves several important packaging facts at once:

```text
ClawHUD.exe can load from the staged directory
velopack_libc.dll is resolvable
the Managed argument reaches CH-I2 policy
Managed does not show a blocking ClawHUD startup MessageBox
the process exits deterministically on unsupported CI hardware
```

Do not bypass the hardware gate merely to test deeper startup in CI.

Do not install PresentMon on the GitHub runner.

---

## 14. Do not add a second Managed executable target

A tempting design is:

```text
add_executable(ClawHUD ...)
add_executable(ClawHUD.Managed ...)
```

with different source lists or preprocessor definitions.

Do not do this in CH-I3.

Reasons:

- the renderer/presentation path is production-sensitive;
- duplicated source lists drift;
- standalone and Managed would no longer prove that they run the same HUD core;
- CH-I1 already made launch-mode policy explicit;
- CH-I2 already made Managed startup non-interactive and diagnosable;
- current extra VeloPack native DLL cost is small compared with the maintenance risk of a second native target.

The target architecture remains:

```text
same executable
different launch mode
different package composition
```

---

## 15. Do not remove VeloPack from Managed yet

CH-I1 removed Managed **self-update ownership**.

That is not the same as removing the VeloPack native bootstrap dependency from the shared executable.

Current `wWinMain` performs:

```cpp
Velopack::VelopackApp::Build()
    .SetAutoApplyOnStartup(false)
    .OnBeforeUninstall(...)
    .Run();
```

before launch-mode resolution.

Therefore CH-I3 payload includes:

```text
velopack_libc.dll
```

Do not alter `main.cpp` or introduce a compile flag just to remove this dependency.

A future optimization may revisit this only if there is a demonstrated reason.

---

## 16. Preserve settings location

Do not change:

```text
%LOCALAPPDATA%\ClawHUD\settings.ini
```

The existing location is independent of install directory.

This has a useful migration property:

```text
existing standalone ClawHUD user
-> later uses SteamAddon-bundled Managed runtime
-> same ClawHUD settings authority/data can be read
```

Do not add:

```text
%LOCALAPPDATA%\SteamAddonforClaw\ClawHUD
SteamAddon-owned duplicate settings file
settings migration copy
two-way sync
```

The future Addon UI uses Control IPC and ClawHUD remains the settings authority.

---

## 17. Standalone ClawHUD conflict is not CH-I3

Current single-instance identity remains:

```text
Local\ClawHUD.SingleInstance
```

Do not change it in this packaging PR.

If an independently installed Standalone ClawHUD is already running, a future SteamAddon Managed launch may receive CH-I2:

```text
AlreadyRunning = exit code 20
```

That migration/adoption UX belongs to the SteamAddon integration work.

Do not solve it by allowing two simultaneous ClawHUD processes.

---

## 18. PresentMon install/update contract remains unchanged

The companion payload includes:

```text
runtime/ClawHUD.PresentMonRuntime.msi
```

ClawHUD remains responsible for:

```text
readiness check
version floor
ABI compatibility
reuse of newer compatible runtime
install/upgrade when missing or old
post-install validation
Managed startup exit result
```

SteamAddon does not install PresentMon during its own controller startup.

Do not move PresentMon to the Addon root dependencies in CH-I3.

Keep it physically and logically inside:

```text
clawhud/runtime/
```

---

## 19. Full 1902 isolation

The SteamAddon Full PID1902 controller runtime is a high-criticality independent authority.

The ClawHUD payload must remain a sibling feature payload.

CH-I3 must not touch:

```text
PID1901/PID1902 ownership
DirectInput
HidHide
VIIPER
Xbox360/SteamDeck presentation
Center M authority
controller startup
controller shutdown
controller recovery
```

The future Addon package containing ClawHUD does not make ClawHUD part of controller authority.

HUD failure remains feature-local.

---

## 20. NON-NEGOTIABLE presentation / TopMost / VRR boundary

CH-I3 must have zero behavior changes in the production HUD presentation path.

Do not modify:

```text
src/ClawHUD/HudPresentation*
src/ClawHUD/HudRenderer*
src/ClawHUD/HudWindowGeometry*
src/ClawHUD/HudPresentationContract*
src/ClawHUD/HudPresentationLifecycle*
```

Do not change:

```text
D3D11
DXGI
Presentation API
DirectComposition
D2D/DWrite
TopMost / Z-order
WS_EX_TOPMOST
window styles
independent flip
premultiplied alpha
Show/Hide order
presentation create/destroy order
present cadence
resume presentation recovery
```

Preferred PR property:

```text
no src/ClawHUD production C++ diff at all
```

---

## 21. Intel VRR Fix remains unchanged

Do not modify:

```text
TweakStartupCoordinator
IntelVrrRangeTweak
IntelArcSyncClient
AffectedPanelDetector
IntelVrrResultStore
SetIntelVrrRangeFixEnabled
retry behavior
no-rollback semantics
```

The payload must simply contain the same runtime binary that already owns this feature.

---

## 22. Expected implementation scope

Preferred files:

```text
scripts/package-steamaddon-runtime.ps1        # new
.github/workflows/Build-Test.yml              # stage/validate/upload integration payload
```

Potential documentation update only if necessary:

```text
docs/work-orders/managed/CLAW_HUD_RUNTIME_FRONTEND_SEPARATION_ARCHITECTURE_2026-09-02.md
```

No CMake change should be necessary because the current native build output already contains the required runtime files.

If implementation evidence proves a small CMake packaging helper is cleaner, keep it packaging-only.

Do not add a second executable target.

---

## 23. Validation requirements

### 23.1 Exact required files

Assert all are present:

```text
clawhud/ClawHUD.exe
clawhud/ClawHUD.EcHelper.exe
clawhud/PresentMonAPI2Loader.dll
clawhud/velopack_libc.dll
clawhud/LICENSE
clawhud/THIRD-PARTY-NOTICES.md
clawhud/fonts/Unispace.otf
clawhud/fonts/Unispace-LICENSE.txt
clawhud/runtime/ClawHUD.PresentMonRuntime.msi
```

### 23.2 Forbidden files

Assert absent:

```text
ClawHUD.Settings.*
ClawHUD.Diag.exe
*.nupkg
Setup.exe
releases.stable.json
private .NET runtime files
```

### 23.3 Directory shape

Assert:

```text
EC helper is sibling of ClawHUD.exe
PresentMon MSI is runtime/ child
Unispace font is fonts/ child
```

Do not rename those runtime files in CH-I3.

### 23.4 Managed staged-start smoke

Run staged:

```text
ClawHUD.exe --managed
```

on CI and require exit:

```text
21 or 22
```

on unsupported hosted hardware.

If it fails to load because a dependency is missing, CH-I3 fails.

### 23.5 Existing test suite

Run normal CI:

```text
WPF Settings build
WPF Settings tests
WPF publish-shape validation
native Release build
CTest Release
SteamAddon companion payload validation
```

Even though WPF Settings is not in the companion payload, do not remove its existing standalone CI coverage.

---

## 24. Manual local validation

On a supported MSI Claw after building the branch:

1. Stage the payload with the new script.
2. Launch only:

```text
<stage>\clawhud\ClawHUD.exe --managed
```

3. Confirm:

```text
no tray
no standalone Settings window
no ClawHUD self-update
no startup-task mutation
PresentMon bootstrap still works
Control IPC becomes Ready
HUD renders normally
TopMost behavior unchanged
VRR remains working
Intel VRR Fix behavior unchanged
EC helper resolves from the staged sibling path
```

4. Send normal `RequestShutdown` over Control IPC.

Confirm clean process exit.

Do not perform any presentation-specific code modification based on packaging validation.

---

## 25. Release workflow policy for CH-I3

Do **not** publish a normal ClawHUD GitHub Release from `integration/steamaddon`.

Do **not** add an integration tag.

Do **not** modify release pruning.

Do **not** change stable VeloPack release numbering.

The integration artifact is temporary input for the upcoming SteamAddon integration branch.

After both products are integrated and hardware-validated, a later explicit main/release task can decide whether the standalone ClawHUD release also publishes a permanent:

```text
ClawHUD-SteamAddonRuntime-<version>.zip
```

asset.

That decision is not required for CH-I3.

---

## 26. Future SteamAddon consumption — context only

Do not implement this in the ClawHUD repository in CH-I3.

The intended future Addon publish layout is:

```text
artifacts/publish/
├─ SteamInputAddonforClaw.exe
├─ ui/
├─ qam/
├─ overlay/
├─ fse/
└─ clawhud/
   └─ <CH-I3 payload>
```

SteamAddon will later own:

```text
pinned ClawHUD source/artifact revision
payload integrity verification
process launch with --managed
bounded IPC readiness
CH-I2 startup exit-code translation
RequestShutdown
Addon Overlay settings tab
Addon package/update lifecycle
```

Do not pre-build those responsibilities into ClawHUD.

---

## 27. Overengineering guard

Do not add:

```text
second ClawHUD runtime executable
new host process
new IPC protocol
new service
plugin loader
generic package manager
new updater
runtime dependency resolver
dynamic download at ClawHUD startup
embedded resource extraction framework
heartbeat
watchdog
parent PID monitor
source submodule
binary self-extractor
```

CH-I3 is deliberately small:

```text
existing proven binary
+ exact dependency list
+ isolated directory
+ reusable staging script
+ CI validation artifact
```

---

## 28. PR review checklist

```text
[ ] PR base is integration/steamaddon, NOT main
[ ] implementation branch starts from current integration/steamaddon
[ ] one existing ClawHUD.exe is reused
[ ] no second native runtime target added
[ ] payload root is clawhud/
[ ] ClawHUD.exe included
[ ] ClawHUD.EcHelper.exe included beside it
[ ] PresentMonAPI2Loader.dll included beside it
[ ] velopack_libc.dll included beside it
[ ] runtime/ClawHUD.PresentMonRuntime.msi included
[ ] fonts/Unispace.otf included
[ ] fonts/Unispace-LICENSE.txt included
[ ] LICENSE included
[ ] THIRD-PARTY-NOTICES.md included
[ ] ClawHUD.Settings.* excluded
[ ] ClawHUD.Diag.exe excluded
[ ] standalone VeloPack package artifacts excluded
[ ] private .NET runtime excluded
[ ] staging script is the authoritative file-list owner
[ ] Build-Test validates the payload
[ ] integration/steamaddon push CI is enabled
[ ] integration artifact is uploaded with bounded retention
[ ] staged --managed CI smoke exits 21 or 22
[ ] no PresentMon installation is attempted on unsupported CI hardware
[ ] existing Standalone package remains unchanged
[ ] settings path remains %LOCALAPPDATA%\ClawHUD\settings.ini
[ ] single-instance contract unchanged
[ ] PresentMon bootstrap/update logic unchanged
[ ] Intel VRR Fix unchanged
[ ] Control IPC unchanged
[ ] HudPresentation* unchanged
[ ] HudRenderer* unchanged
[ ] TopMost behavior unchanged
[ ] VRR-safe presentation unchanged
[ ] Full 1902/controller code untouched
[ ] existing build/tests remain green
```

---

## 29. Completion result

After CH-I3, ClawHUD should have a reproducible integration artifact:

```text
ClawHUD source @ integration/steamaddon
        |
        v
normal Release native build
        |
        v
package-steamaddon-runtime.ps1
        |
        v
artifacts/SteamAddonRuntime/
└─ clawhud/
   ├─ ClawHUD.exe
   ├─ ClawHUD.EcHelper.exe
   ├─ PresentMonAPI2Loader.dll
   ├─ velopack_libc.dll
   ├─ LICENSE
   ├─ THIRD-PARTY-NOTICES.md
   ├─ fonts/...
   └─ runtime/ClawHUD.PresentMonRuntime.msi
        |
        v
CI shape validation
        |
        v
staged --managed startup smoke
        |
        v
GitHub Actions integration artifact
```

No runtime/presentation behavior changes are required.

The next step after CH-I3 is the SteamAddon-side integration branch work:

```text
ClawHUD payload pin/verification
+ Managed process owner
+ Control IPC client
+ HUD On/Off process lifetime
+ Addon Overlay settings UI
```

That future work belongs in the SteamAddon repository, not this PR.
