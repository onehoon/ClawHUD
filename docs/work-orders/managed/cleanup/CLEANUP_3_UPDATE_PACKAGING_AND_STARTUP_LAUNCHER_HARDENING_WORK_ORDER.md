# Cleanup 3 — Update, PresentMon Runtime Upgrade, and Startup Launcher Hardening Work Order

> **Repository:** `onehoon/ClawHUD`  
> **Series:** Post-CH-RTF ClawHUD Standalone Cleanup  
> **Cleanup split:** 3 / 3  
> **Analyzed main HEAD:** `c6f77143ec8ad42c973c19970a8dc8e2e7b6205a`  
> **Previous cleanups:** Cleanup 1 / PR #219 merged, Cleanup 2 / PR #220 merged  
> **Pinned VeloPack:** `1.2.0`  
> **Pinned PresentMon runtime:** `2.5.1`  
> **Scope:** Bound update networking without changing the established VeloPack apply/restart policy, make the bundled PresentMon wrapper MSI safely upgradeable for future runtime revisions, make Start-with-Windows target the stable VeloPack execution stub when installed, and document shared-runtime uninstall ownership  
> **Status:** Ready for implementation

---

## 1. Objective

Cleanup 1 fixed the elevated EC helper lifetime.

Cleanup 2 fixed the PresentMon prerequisite bootstrap boundary and made bootstrap failures authoritative.

The remaining release/lifecycle cleanup is packaging and update infrastructure.

There are three concrete production issues:

```text
1. App::CheckForUpdates() uses VeloPack 1.2.0 GithubSource synchronously during startup.
   The 1.2.0 C++ GithubSource exposes no timeout configuration.
   Its underlying HTTP default is timeout=0 (no timeout).
   A stalled network path can therefore keep ClawHUD startup blocked indefinitely.

2. The ClawHUD PresentMon wrapper MSI is authored as:

       Product Id="*"
       Version="1.0.0.0"
       UpgradeCode="{4E9BE59E-7CC7-4F8F-BD00-22A44EC8B9A9}"

   but has no MajorUpgrade policy and the product version is not tied to the
   bundled PresentMon runtime. A future runtime replacement therefore does not
   yet have a complete Windows Installer upgrade story.

3. Start-with-Windows currently records the executing binary path directly.
   In a normal VeloPack install this is typically:

       <RootAppDir>\current\ClawHUD.exe

   but VeloPack deliberately creates:

       <RootAppDir>\ClawHUD.exe

   as the stable execution stub. The root stub survives `current` replacement
   and is the correct installed shortcut target.
```

The target is:

```text
Normal launch
-> Acquire single instance
-> bounded update check/download through the existing VeloPack UpdateManager
-> existing Standalone/Managed apply/restart policy
-> hardware gate
-> PresentMon prerequisite gate
-> normal runtime

Installed Start-with-Windows
-> <RootAppDir>\ClawHUD.exe execution stub
-> no --managed argument

Portable/dev Start-with-Windows
-> current executable fallback

Future PresentMon runtime revision
-> wrapper ProductVersion increases with the runtime
-> same UpgradeCode
-> MajorUpgrade replaces the older wrapper product
-> newer compatible runtime is never downgraded merely because ClawHUD ships an older pin
```

This is the final item in the originally agreed three-part lifecycle cleanup.

**F8 / Enable-HUD semantics are explicitly excluded and will be handled separately.**

---

## 2. Current baseline after Cleanup 2

### 2.1 Current startup update path

`App::Run()` currently begins:

```cpp
if (!AcquireSingleInstance()) return 0;
CheckForUpdates();
const auto hardware = CheckSupportedHardware();
...
EnsurePresentMonRuntime();
...
```

Keep the important policy properties:

```text
single-instance winner first
update before the hardware gate
PresentMon bootstrap after the hardware gate
```

Do not undo Cleanup 2.

The update-before-hardware ordering intentionally permits an older ClawHUD build to update into a build that recognizes newly supported hardware.

### 2.2 Current VeloPack update policy is already mode-correct

Current `CheckForUpdates()` uses one mode decision:

```cpp
const bool restart =
    clawhud::ShouldRestartAfterVelopackUpdate(launchMode_);
```

Required existing behavior:

```text
Standalone
    -> update applies
    -> restart=true
    -> normal ClawHUD restart

Managed
    -> update applies
    -> restart=false
    -> ClawHUD does not self-relaunch
```

Preserve this exactly.

Do not reintroduce `--managed` as a VeloPack restart argument.

### 2.3 Current update source

Current code constructs:

```cpp
Velopack::UpdateManager manager(
    std::make_unique<Velopack::GithubSource>(
        CLAWHUD_UPDATE_REPOSITORY));
```

with:

```text
CLAWHUD_UPDATE_REPOSITORY = https://github.com/onehoon/ClawHUD
```

The release workflow publishes a public stable VeloPack channel through:

```text
vpk pack --channel stable
vpk upload github --channel stable
```

Each public GitHub release contains, among other assets:

```text
ClawHUD-<version>-stable-full.nupkg
ClawHUD-<version>-stable-delta.nupkg    (when delta available)
releases.stable.json
ClawHUD-stable-Setup.exe
```

The release workflow retains the newest three normal releases.

### 2.4 Pinned VeloPack 1.2.0 timeout limitation

Do not assume a timeout exists on `GithubSource`.

The exact pinned 1.2.0 C++ constructor is:

```cpp
GithubSource(
    std::string const& repoUrl,
    std::string const& accessToken = "",
    bool prerelease = false);
```

It has no `HttpOptions` argument.

The timeout-capable HTTP-source work landed upstream after the 1.2.0 release. Do not solve this cleanup by silently pinning an unreleased/nightly VeloPack build.

Keep VeloPack itself at 1.2.0 in this PR unless an official stable release is deliberately adopted in a separate dependency-update decision.

### 2.5 Current startup shortcut

`App::ApplyStartupRegistration()` currently does:

```cpp
shellLink->SetPath(executablePath_.c_str());
const auto workingDirectory =
    std::filesystem::path(executablePath_).parent_path();
shellLink->SetWorkingDirectory(workingDirectory.c_str());
```

`executablePath_` comes from the currently running process.

In a normal installed VeloPack layout:

```text
<RootAppDir>
├─ ClawHUD.exe          <-- stable execution stub
├─ Update.exe
└─ current
   ├─ sq.version
   └─ ClawHUD.exe       <-- real versioned app binary
```

VeloPack updates replace `current`; the root execution stub exists specifically so launchers/shortcuts can point to a stable path.

### 2.6 Current PresentMon wrapper MSI

The wrapper source is:

```text
tools/poc/presentmon-api2-runtime/installer/
    ClawHUD.PresentMonRuntime.wxs
```

Current product metadata:

```xml
<Product Id="*"
         Name="ClawHUD PresentMon Shared Runtime"
         Language="1033"
         Version="1.0.0.0"
         Manufacturer="ClawHUD"
         UpgradeCode="{4E9BE59E-7CC7-4F8F-BD00-22A44EC8B9A9}">
```

There is no `<MajorUpgrade ... />`.

Normal ClawHUD builds do not rebuild the MSI; they copy the vetted artifact committed under:

```text
third_party/presentmon/2.5.1/ClawHUD.PresentMonRuntime.msi
```

Therefore the installer-source change and the committed vetted runtime artifact must remain synchronized.

---

## 3. Non-negotiable boundaries

1. Keep VeloPack `1.2.0` for this cleanup.
2. Keep VeloPack `VelopackApp::Build().Run()` first in `main.cpp`.
3. Keep `SetAutoApplyOnStartup(false)`.
4. Keep update-before-hardware ordering.
5. Keep CH-RTF-9 Standalone/Managed restart semantics unchanged.
6. Keep the existing VeloPack `UpdateManager` for version comparison, delta selection, download staging, pending-update detection, and apply scheduling.
7. Do not implement a second updater or manually patch `.nupkg` files.
8. Do not add a generic background-task framework just for updates.
9. Do not detach an unbounded update thread as a substitute for bounded networking.
10. Do not add a worker that must be joined while a VeloPack network call can block indefinitely.
11. Do not change the public GitHub-release distribution model.
12. Do not enable prerelease update consumption in this PR.
13. Keep the release channel `stable`.
14. Keep the release workflow's current delta generation unless an actual packaging defect is demonstrated.
15. Keep the PresentMon runtime shared-service architecture.
16. Do not uninstall the shared PresentMon runtime automatically when ClawHUD is removed.
17. Do not downgrade a newer compatible shared PresentMon runtime.
18. Do not change PresentMon provider/session behavior, game detection, EC telemetry, HUD rendering, opacity, or presentation.
19. Preserve the complete HUD/VRR presentation contract unchanged.
20. F8 / Enable-HUD semantics are out of scope.
21. Code signing is out of scope for this cleanup unless separately requested.

---

# Part A — Bound VeloPack update networking

## 4. Do not use an unbounded `GithubSource` on the startup path

The current source must be replaced because the pinned 1.2.0 `GithubSource` cannot express a request timeout.

Do **not** solve this by:

```text
std::async + wait_for + abandon future
std::thread + detach around GithubSource
std::jthread joined during shutdown
TerminateThread
network watchdog that kills ClawHUD
unreleased/nightly VeloPack pin
```

Those either leave the networking unbounded, create unsafe thread lifetime, move the hang to shutdown, or introduce unnecessary dependency risk.

The update call may remain synchronous in the early startup path **provided every HTTP operation used by that path has a real bounded timeout**.

The goal is not “zero startup wait”; the goal is:

```text
normal network
-> existing silent update behavior

no update / offline / DNS failure / TLS failure / blackhole endpoint
-> bounded failure
-> log
-> continue installed ClawHUD

no path
-> indefinite startup hang
```

## 5. Add one small public-stable GitHub Release update source

Use VeloPack 1.2.0's supported custom-source interface:

```cpp
Velopack::Sources::IUpdateSource
```

The pinned 1.2.0 API explicitly provides:

```cpp
virtual const std::string GetReleaseFeed(
    const std::string releasesName) = 0;

virtual bool DownloadReleaseEntry(
    const VelopackAsset& asset,
    const std::string localFilePath,
    vpkc_progress_send_t progress) = 0;
```

Recommended production type:

```text
src/ClawHUD/ClawHudUpdateSource.h
src/ClawHUD/ClawHudUpdateSource.cpp
```

or an equivalently narrow name.

This is **not** a generic HTTP library.

It exists only to provide bounded access to ClawHUD's current public stable GitHub Release layout while continuing to let VeloPack own update semantics.

### 5.1 Release feed URL

The VeloPack callback supplies the release feed filename, expected for this product to be:

```text
releases.stable.json
```

Fetch it from the stable “latest normal GitHub release” URL:

```text
https://github.com/onehoon/ClawHUD/releases/latest/download/releases.stable.json
```

Use the actual VeloPack-supplied `releasesName` when constructing the URL. For the current stable channel it must resolve exactly to the URL above.

Recommended helper:

```cpp
std::wstring BuildReleaseFeedUrl(std::string_view releasesName);
```

Validate `releasesName` before constructing the URL.

For this repo, a conservative policy is acceptable:

```text
allow exactly `releases.stable.json`
reject path separators
reject `..`
reject arbitrary URL content
```

There is no requirement to support other channels in this PR.

### 5.2 Package URL

For a `VelopackAsset` selected by VeloPack, download the exact filename from its version tag:

```text
https://github.com/onehoon/ClawHUD/releases/download/v<asset.Version>/<asset.FileName>
```

Example:

```text
asset.Version  = 0.1.90
asset.FileName = ClawHUD-0.1.90-stable-full.nupkg

-> https://github.com/onehoon/ClawHUD/releases/download/v0.1.90/ClawHUD-0.1.90-stable-full.nupkg
```

This preserves VeloPack's own delta/full selection: the custom source only retrieves the file VeloPack asks for.

Do not reimplement update-version selection.

Validate:

```text
asset.Version is a simple version token expected by this app
asset.FileName is a filename only (no slash/backslash/..)
```

Do not concatenate unvalidated path/URL fragments.

### 5.3 Why this is compatible with release pruning

The release feed generated for the latest stable release is the authority for which assets VeloPack may request.

The existing release workflow keeps the newest three normal releases and produces deltas from the previous retained release.

The custom source's version-qualified download URL therefore continues to work for selected full/delta assets that remain referenced by the current feed.

Do not change pruning policy without evidence that the current feed references assets which the existing retention step deletes.

If implementation testing proves that the feed references more history than the retained releases provide, adjust retention in the release workflow **only as much as required by actual generated feed evidence**.

Do not speculate and retain an unlimited release history.

## 6. Use WinHTTP with explicit timeouts

Implement the small source using Windows WinHTTP and link:

```text
winhttp.lib
```

Use synchronous WinHTTP requests inside the custom VeloPack callbacks. This is acceptable because the calls are now explicitly bounded.

Do not disable certificate validation.

Do not accept invalid certificates.

Do not use plain HTTP.

Follow normal GitHub redirects.

Recommended timeout policy, subject to small implementation adjustment:

```text
DNS/connect timeout       ~ 5 seconds
send timeout              ~ 10 seconds
receive inactivity timeout ~ 15 seconds
```

The exact values may be constants in the source.

The important invariant is that they are finite and intentionally small enough that a broken update endpoint cannot indefinitely hold normal ClawHUD startup.

For package downloads, a receive timeout is an **inactivity** bound; do not impose an unrealistically tiny total wall-clock cap that rejects a legitimate download merely because the user's connection is slow.

### 6.1 Feed download

For `GetReleaseFeed()`:

```text
open HTTPS request
-> bounded resolve/connect/send/receive
-> follow redirect
-> require 2xx final response
-> bound feed response size to a small sane maximum
-> read UTF-8 bytes
-> return JSON string
```

Recommended feed size ceiling:

```text
1 MiB maximum
```

The current feed is only a few KiB. Do not accept an unbounded response into memory.

On failure, throw `std::runtime_error` or the narrow exception form expected by VeloPack custom-source propagation.

`App::CheckForUpdates()` already catches update exceptions and continues with the installed version.

### 6.2 Package download

For `DownloadReleaseEntry()`:

```text
open HTTPS request
-> bounded connect/send/receive-inactivity
-> require 2xx final response
-> stream directly to `localFilePath`
-> do not buffer the entire nupkg in memory
-> report progress 0..100 when Content-Length is known
-> close all WinHTTP/file handles on every path
-> return true only after complete successful write
```

The pinned VeloPack custom progress callback contract is:

```text
call with values 0 through 100 inclusive
```

If Content-Length is unavailable, progress may remain coarse; correctness matters more than synthetic progress.

Do not manually validate VeloPack SHA hashes here unless the VeloPack `UpdateManager` contract specifically requires the source to do so. Preserve VeloPack's existing package validation ownership.

### 6.3 Partial download cleanup

If the custom download fails after creating `localFilePath`:

```text
close file
remove the incomplete local file best-effort
return false / propagate failure
```

Do not leave a file that appears complete at the exact destination expected by VeloPack.

## 7. Keep `CheckForUpdates()` lifecycle semantics otherwise unchanged

After source replacement, the existing flow remains conceptually:

```cpp
Velopack::UpdateManager manager(
    std::make_unique<ClawHudUpdateSource>());

const bool restart =
    clawhud::ShouldRestartAfterVelopackUpdate(launchMode_);

if (const auto pending = manager.UpdatePendingRestart())
{
    manager.WaitExitThenApplyUpdates(*pending, true, restart);
    std::exit(0);
}

if (const auto update = manager.CheckForUpdates())
{
    manager.DownloadUpdates(*update);
    manager.WaitExitThenApplyUpdates(*update, true, restart);
    std::exit(0);
}
```

Preserve:

```text
pending local update checked/applied first
no update -> continue
network/update exception -> log + continue installed version
Standalone restart=true
Managed restart=false
```

Do not turn update failure into a fatal application startup error.

PresentMon is a required prerequisite; network update availability is not.

---

# Part B — PresentMon runtime version and MSI upgrade policy

## 8. Tie the wrapper MSI ProductVersion to the bundled PresentMon runtime

Current CMake already has the canonical pin:

```cmake
set(PRESENTMON_VERSION "2.5.1")
set(PRESENTMON_API2_ROOT
    "${CMAKE_SOURCE_DIR}/third_party/presentmon/2.5.1")
```

The wrapper MSI source must stop hardcoding:

```xml
Version="1.0.0.0"
```

Instead, the runtime artifact build script must pass a WiX preprocessor variable derived from the actual pinned runtime version.

Conceptually:

```xml
<Product ... Version="$(var.PresentMonRuntimeVersion)" ...>
```

and the build invocation supplies:

```text
PresentMonRuntimeVersion=2.5.1
```

Do not use the ClawHUD app version (`0.1.x`) as the PresentMon wrapper ProductVersion.

They are independent products.

## 9. Add a real MajorUpgrade policy

Keep the current stable UpgradeCode:

```text
{4E9BE59E-7CC7-4F8F-BD00-22A44EC8B9A9}
```

Add WiX major-upgrade authoring so a future higher PresentMon wrapper version replaces an older one.

Recommended basic shape:

```xml
<MajorUpgrade
    DowngradeErrorMessage="A newer ClawHUD PresentMon Shared Runtime is already installed." />
```

Use normal Windows Installer major-upgrade semantics:

```text
new wrapper ProductVersion > installed wrapper ProductVersion
    -> detect same UpgradeCode
    -> remove/replace old wrapper product

new wrapper ProductVersion < installed wrapper ProductVersion
    -> block downgrade
```

Do **not** set `AllowDowngrades="yes"`.

Do **not** set `AllowSameVersionUpgrades="yes"` merely to make repeated builds convenient.

Windows Installer compares the first three ProductVersion fields for major upgrades. Future runtime revisions must therefore increment one of those fields.

`2.5.1 -> 2.5.2` is valid.

Keep:

```xml
Product Id="*"
```

so each true major-upgrade package receives a new ProductCode.

## 10. Add a runtime-version readiness gate without downgrading newer runtimes

The current readiness check proves:

```text
service is RUNNING
registry path exists
middleware exists
name is PresentMonAPI2.dll
pmGetApiVersion succeeds
API major/minor matches the headers
```

That proves ABI compatibility, not the actual shared-runtime product revision.

Cleanup 3 must add a version floor corresponding to the bundled PresentMon pin.

Target rule:

```text
installed runtime ABI compatible
AND installed runtime version >= ClawHUD required runtime version
    -> reuse it

installed runtime ABI compatible
BUT installed runtime version < ClawHUD required runtime version
    -> run bundled wrapper MSI upgrade

installed runtime version > bundled required version
AND ABI compatible
    -> reuse it
    -> DO NOT downgrade
```

### 10.1 Preferred version evidence

Prefer Windows version-resource evidence from the installed PresentMon runtime binary, using the actual installed middleware/service artifact only after verifying which pinned 2.5.1 binary exposes a stable upstream product/file version.

`PresentMonRuntimeBootstrap.cpp` already links `version.lib`, so use normal Windows version-resource APIs if the pinned artifacts provide trustworthy version metadata.

Before committing the implementation, inspect the pinned `2.5.1` binary metadata and record which file/version field is authoritative.

Examples of acceptable evidence:

```text
PresentMonAPI2.dll FileVersion/ProductVersion
or
PresentMonService.exe FileVersion/ProductVersion
```

Do not invent a runtime version from:

```text
pmGetApiVersion()   // this is API/ABI version, not necessarily product runtime version
ClawHUD app version
wrapper MSI filename
file timestamp
```

If the pinned upstream artifact does not expose a usable product/file version, stop this subsection and use a narrowly scoped explicit installed-runtime marker owned by the wrapper MSI, while preserving compatibility with the already-shipped 2.5.1 state. Do not guess.

### 10.2 Pure comparison seam

Keep comparison testable:

```cpp
struct RuntimeVersion
{
    uint16_t major;
    uint16_t minor;
    uint16_t patch;
};

constexpr bool RuntimeVersionAtLeast(
    RuntimeVersion installed,
    RuntimeVersion required) noexcept;
```

Exact integer widths/names may differ.

Required tests:

```text
2.5.1 >= 2.5.1 -> true
2.5.2 >= 2.5.1 -> true
2.6.0 >= 2.5.1 -> true
3.0.0 >= 2.5.1 -> true  [version floor only; ABI gate still separately decides compatibility]
2.5.0 >= 2.5.1 -> false
2.4.99 >= 2.5.1 -> false
```

Keep ABI compatibility and product-version floor as separate predicates.

### 10.3 Generate required runtime version from the one CMake pin

Do not duplicate literal `2.5.1` across C++ files.

Generate a small header from CMake or compile definitions from:

```cmake
PRESENTMON_VERSION
```

so the bundled runtime path, readiness floor, and wrapper artifact process are tied to one source of truth as much as practical.

A future runtime bump should require an intentional change such as:

```cmake
set(PRESENTMON_VERSION "2.5.2")
set(PRESENTMON_API2_ROOT
    "${CMAKE_SOURCE_DIR}/third_party/presentmon/2.5.2")
```

plus the vetted replacement artifacts.

## 11. Runtime artifact reproduction requirements

Because normal builds copy the committed MSI, modifying only the `.wxs` source is insufficient.

The PR must update the reproducible build tooling/documentation so rebuilding the pinned runtime wrapper produces:

```text
ProductVersion = current PRESENTMON runtime product version
same UpgradeCode
MajorUpgrade present
new ProductCode
```

Then regenerate and commit the vetted:

```text
third_party/presentmon/2.5.1/ClawHUD.PresentMonRuntime.msi
```

if changing the wrapper package is necessary for this cleanup.

Update its recorded SHA-256 in the existing artifact manifest / reproduction documentation.

The PR description must state whether the MSI bytes changed and include the new hash if they did.

Do not modify the upstream PresentMon merge module contents merely to change wrapper upgrade metadata.

## 12. Required MSI upgrade validation

At minimum, test the wrapper packages themselves outside ClawHUD runtime logic.

Required validation matrix where reproducible in CI/local packaging tests:

```text
install old currently-shipped wrapper
-> install new Cleanup-3 wrapper
-> exactly one related ClawHUD wrapper product remains
-> PresentMon shared service/runtime is valid

install new wrapper
-> attempt older wrapper
-> downgrade is rejected

install new wrapper again / repair path
-> no duplicate product registration
-> runtime remains valid
```

If the existing committed old MSI is available as the baseline artifact, use it for the upgrade test.

Do not declare upgrade support complete solely from XML inspection.

Real Claw hardware is not required for Windows Installer product-upgrade validation.

---

# Part C — Start-with-Windows stable launcher target

## 13. Resolve the installed VeloPack root execution stub

Do not blindly change every shortcut to “parent of current exe”.

Add one small pure/path helper that distinguishes a normal installed VeloPack layout from portable/dev execution.

Recommended conceptual function:

```cpp
std::filesystem::path ResolveStartupExecutable(
    const std::filesystem::path& processExecutable);
```

For an installed layout such as:

```text
C:\Users\User\AppData\Local\ClawHUD\current\ClawHUD.exe
```

validate the expected layout before selecting:

```text
C:\Users\User\AppData\Local\ClawHUD\ClawHUD.exe
```

Recommended evidence:

```text
process executable parent directory name is `current`
root sibling `Update.exe` exists
root sibling `ClawHUD.exe` execution stub exists
current `sq.version` exists
```

Only then use the root stub.

Otherwise:

```text
portable build
local developer build
unexpected/custom layout
missing root stub
```

must safely fall back to:

```text
processExecutable
```

Do not make Start-with-Windows fail merely because the process is not in a normal installed VeloPack layout.

## 14. Shortcut content

For installed Standalone startup registration:

```text
Target:
    <RootAppDir>\ClawHUD.exe

Working directory:
    <RootAppDir>

Arguments:
    none
```

The shortcut must **never** contain:

```text
--managed
```

This remains a Standalone startup preference.

Managed launch-time reconciliation remains skipped exactly as CH-RTF-9 defines.

Explicit `SetStartWithWindows` over Control IPC may still create/remove the normal Standalone shortcut.

## 15. Startup shortcut tests

Add path-only tests covering:

```text
normal installed current path + root stub/Update.exe/sq.version present
-> root execution stub

portable/dev path
-> current executable

parent happens to be named current but no Update.exe
-> current executable

root Update.exe exists but root ClawHUD.exe missing
-> current executable

sq.version missing
-> current executable
```

Use temp directories/files; do not require an actual VeloPack installation.

Existing startup-registration behavior tests, if any, remain green.

---

# Part D — Shared PresentMon uninstall ownership documentation

## 16. Do not uninstall the shared PresentMon runtime from ClawHUD uninstall

Current ClawHUD uninstall cleanup removes the ClawHUD-owned Startup shortcut.

Keep it that way.

Do not add:

```text
msiexec /x ClawHUD.PresentMonRuntime
service deletion
Program Files\Intel\PresentMonSharedService deletion
registry deletion under HKLM\SOFTWARE\INTEL\PresentMon
```

Reason:

```text
PresentMon shared service is a machine-level shared runtime.
Another application may legitimately consume the same compatible installation.
ClawHUD cannot prove exclusive ownership at uninstall time.
```

Therefore the settled policy is:

```text
Uninstall ClawHUD
-> remove ClawHUD application files via VeloPack
-> remove ClawHUD Startup shortcut
-> leave compatible PresentMon shared runtime installed
```

## 17. Document the policy

Update the user/developer-facing PresentMon runtime section where appropriate.

A concise README note is enough, for example:

```text
ClawHUD installs the PresentMon shared-service runtime as a machine-level shared prerequisite when needed. Removing ClawHUD does not automatically remove that shared runtime because other software may use it.
```

Also put the detailed ownership rule in the relevant PresentMon runtime/reference documentation.

Do not add uninstall UI or a “remove shared runtime” checkbox in this PR.

---

## 18. Release workflow requirements

Keep the existing release topology unless changes are actually required by the custom bounded source.

The workflow must continue publishing:

```text
releases.stable.json
full nupkg
delta nupkg when available
Setup.exe
portable zip
```

The bounded update source depends on:

```text
GitHub normal latest release
releases.stable.json asset
version-tagged package assets
```

Add a release-workflow validation step that fails before publish if the output does not contain the stable feed expected by the source:

```text
release\releases.stable.json
```

The workflow already checks delta prerequisites; do not remove those checks.

If practical, add a post-pack test that parses the feed enough to verify every referenced package filename expected for the newly generated target exists in the output/retained release set.

Do not implement a second feed format.

---

## 19. Logging

Keep logs concise.

Recommended update lines:

```text
Velopack: checking stable release feed source=github-release-bounded
Velopack: no update available
Velopack: update source unavailable; continuing installed version
Velopack: applying pending update silently launchMode=Standalone restart=1
```

Do not log access tokens; none are required for this public repo.

For startup shortcut resolution, Debug-level logging is enough:

```text
Startup target=velopack-root-stub
Startup target=current-exe fallback=<reason>
```

For PresentMon runtime version:

```text
[PresentMonRuntime] installedVersion=2.5.1 requiredVersion=2.5.1
[PresentMonRuntime] installedVersion=2.5.2 requiredVersion=2.5.1 action=reuse
[PresentMonRuntime] installedVersion=2.5.0 requiredVersion=2.5.1 action=upgrade
```

Do not produce per-frame or per-sample logs.

---

## 20. Tests required

### 20.1 Bounded update-source pure tests

Test URL construction/validation without network:

```text
releases.stable.json
-> latest/download/releases.stable.json URL

invalid releases name containing `/`, `\\`, `..`
-> reject

asset 0.1.90 + ClawHUD-0.1.90-stable-full.nupkg
-> /releases/download/v0.1.90/... URL

asset filename with path traversal
-> reject
```

Test any HTTP-result classifier separately if one is introduced:

```text
2xx -> success
redirect handled by WinHTTP path
404/500 -> failure
oversized feed -> failure
short/failed file write -> failure
```

Do not require live GitHub networking in CTest.

### 20.2 VeloPack integration regression

Existing update-policy tests must remain green:

```text
Standalone -> restart after apply
Managed -> no self-restart
```

Add a focused source/manager test if possible using a local/test source so VeloPack still consumes the generated feed and chooses expected full/delta assets.

### 20.3 Runtime version tests

Test the pure version comparator and readiness composition.

Required examples are in section 10.2.

Existing API-major/minor compatibility tests remain.

### 20.4 Startup launcher tests

Required path matrix is in section 15.

### 20.5 MSI packaging tests

Validate actual old->new major upgrade and downgrade rejection as section 12 describes.

### 20.6 Full suite

Run the normal repository Release test workflow.

All existing HUD presentation/VRR contract tests must remain unchanged and pass.

---

## 21. Manual validation matrix

### Update — healthy network

```text
install an older release
launch Standalone
-> update feed check completes
-> update downloads
-> applies
-> app restarts Standalone
-> current version is new
```

### Update — Managed

```text
launch old version --managed
-> update applies
-> no ClawHUD self-restart
```

No external owner implementation is required to finish this test; verify only the ClawHUD-side no-restart behavior.

### Update — offline / bad network

Test at least:

```text
network disconnected
unresolvable endpoint in a test seam
connection accepted but response stalls in a test harness if practical
```

Expected:

```text
update fails within configured bound
ClawHUD continues installed version
normal runtime starts
```

### Startup shortcut

With installed VeloPack ClawHUD:

```text
Start with Windows = ON
-> Startup\ClawHUD.lnk target is <RootAppDir>\ClawHUD.exe
-> no --managed

perform ClawHUD update
-> root shortcut target still exists
-> next login starts updated current app through execution stub
```

### PresentMon upgrade

Using a machine/VM where safe:

```text
install current old wrapper
install Cleanup-3 wrapper
-> upgrade succeeds
-> service/runtime ready
-> no duplicate old wrapper remains
```

---

## 22. Files expected to change

Likely production files:

```text
src/ClawHUD/App.cpp
src/ClawHUD/App.h                      [only if needed]
src/ClawHUD/ClawHudUpdateSource.h      [new]
src/ClawHUD/ClawHudUpdateSource.cpp    [new]
src/ClawHUD/PresentMonRuntimeBootstrap.h/.cpp
src/ClawHUD/<startup-path helper>.h/.cpp [or equivalent small seam]
CMakeLists.txt
```

Packaging/reproduction:

```text
tools/poc/presentmon-api2-runtime/installer/ClawHUD.PresentMonRuntime.wxs
tools/poc/presentmon-api2-runtime/scripts/build-runtime.ps1
third_party/presentmon/2.5.1/ClawHUD.PresentMonRuntime.msi
third_party/presentmon/2.5.1/SHA256SUMS.txt   [or current manifest]
.github/workflows/Build-Release.yml
```

Tests, names may vary:

```text
tests/ClawHudUpdateSourceTests.cpp
tests/PresentMonRuntimeBootstrapTests.cpp
tests/StartupExecutablePathTests.cpp
cmake/ClawHUDTests.cmake
```

Docs likely:

```text
README.md
relevant PresentMon runtime/reproduction documentation
```

Do not touch HUD presentation files.

---

## 23. Explicit non-goals

Do not include any of the following:

- F8 behavior changes;
- Enable-HUD semantic changes;
- background-only opacity changes;
- HUD window style changes;
- DirectComposition/Presentation changes;
- VRR contract changes;
- game detection changes;
- PresentMon telemetry query changes;
- EC helper changes;
- Control IPC protocol changes;
- SteamAddon integration;
- Managed owner/Job Object work;
- VeloPack prerelease-channel support;
- a VeloPack nightly/development dependency pin;
- user-selectable update channels;
- update UI/progress window;
- a generic download framework;
- automatic PresentMon shared-runtime uninstall;
- code signing work unless separately requested.

---

## 24. Completion criteria

Cleanup 3 is complete when all of the following are true:

```text
[ ] Startup update networking is finite/bounded; no pinned-1.2.0 GithubSource call with unlimited timeout remains.
[ ] Existing VeloPack UpdateManager remains the authority for update selection/staging/apply.
[ ] Standalone update restart behavior is unchanged.
[ ] Managed update no-self-restart behavior is unchanged.
[ ] Offline/stalled update source cannot indefinitely prevent normal ClawHUD startup.
[ ] PresentMon wrapper MSI ProductVersion is tied to the runtime revision rather than 1.0.0.0.
[ ] Wrapper MSI has a tested MajorUpgrade policy with the existing UpgradeCode.
[ ] Downgrading a newer wrapper product is blocked.
[ ] Runtime readiness includes an actual runtime-version floor in addition to API ABI compatibility.
[ ] Newer compatible shared runtime is reused and never downgraded.
[ ] Installed Start-with-Windows shortcut targets the VeloPack root ClawHUD.exe execution stub.
[ ] Portable/dev/unexpected layouts fall back safely to the actual current executable.
[ ] Startup shortcut never includes --managed.
[ ] ClawHUD uninstall intentionally leaves the machine-level shared PresentMon runtime.
[ ] That uninstall ownership is documented.
[ ] Release workflow continues publishing a valid releases.stable.json + full/delta package set.
[ ] Full Release CTest passes.
[ ] Existing HUD/VRR presentation tests pass unchanged.
[ ] No F8/Enable-HUD work is included.
```

After this PR, the originally planned three-item lifecycle cleanup is complete.

Any remaining F8 semantics work is a separate follow-up and must not be folded back into Cleanup 3.
