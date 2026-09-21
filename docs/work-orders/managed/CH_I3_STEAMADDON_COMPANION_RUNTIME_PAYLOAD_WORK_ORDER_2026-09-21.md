
# Work Order — CH-I3 SteamAddon Managed Runtime Pre-release Channel

**Date:** 2026-09-21  
**Status:** Ready for implementation  
**Target repository:** onehoon/ClawHUD  
**Target branch:** integration/steamaddon  
**Production-code baseline reviewed:** CH-I1 + CH-I2 through 830c250d6c306d492be3a905d968a4066395f7e2  
**Expected implementation PR count:** 1 focused ClawHUD PR  
**PR base:** integration/steamaddon

---

## 0. Mandatory branch rule

This is a ClawHUD-side SteamAddon integration task.

    Work from: integration/steamaddon
    PR base:   integration/steamaddon

Do not target main.  
Do not merge this PR directly into main.  
Do not change SteamAddonforClaw in this PR.

The future SteamAddon consumer, pin, downloader, and process owner are separate Addon-side work.

---

## 1. Objective

Create a dedicated immutable SteamAddon Managed Runtime distribution channel in the ClawHUD repository.

Final ownership:

    ClawHUD Standalone
    -> existing stable releases
    -> existing Velopack stable feed
    -> existing Standalone updater

    ClawHUD for SteamAddon
    -> separate GitHub Pre-release
    -> tag namespace steamaddon-runtime-vX.Y.Z
    -> ClawHUDRuntime.zip
    -> SHA-256 + machine-readable runtime manifest
    -> future SteamAddon consumes an exact pin

SteamAddon must not embed ClawHUD in its normal installer/package.

SteamAddon must not rebuild ClawHUD on every Addon release.

Publish a new SteamAddon Runtime only when the Managed ClawHUD payload itself changed and is intentionally released.

---

## 2. Current code facts reviewed

### 2.1 Existing Standalone release workflow is already a separate stable channel

Current .github/workflows/Build-Release.yml is manual workflow_dispatch and publishes the Standalone product.

It currently:

    resolves v0.1.x
    builds ClawHUD
    builds WPF Settings
    stages the Standalone composition
    packs Velopack stable
    publishes v0.1.x
    publishes releases.stable.json
    prunes old Standalone releases

Its release-number scan is limited to v0.1.*.

Its pruning logic only considers releases which are all of:

    non-draft
    non-prerelease
    tag matches v0.1.x

Therefore a Pre-release tag such as steamaddon-runtime-v1.0.0 does not participate in Standalone numbering and is not a candidate for current Standalone pruning.

Do not broaden those existing selectors in CH-I3.

### 2.2 Existing Standalone updater does not consume arbitrary releases

Current:

    src/ClawHUD/ClawHudUpdateUrl.cpp
    src/ClawHUD/ClawHudUpdateSource.cpp

reads the stable feed at:

    https://github.com/onehoon/ClawHUD/releases/latest/download/releases.stable.json

and downloads normal Standalone packages through the version-tag path.

The SteamAddon Runtime channel must not create:

    releases.stable.json
    Velopack stable nupkg
    Standalone Setup.exe

for its Pre-release.

### 2.3 Managed mode already refuses Standalone update ownership

Current RuntimeLifecyclePolicy.h defines Managed so self-update runs only in Standalone.

App::Run() skips ClawHUD self-update in Managed mode.

This remains the authority boundary:

    Standalone ClawHUD
    -> ClawHUD updater owns ClawHUD updates

    ClawHUD.exe --managed
    -> no ClawHUD self-update
    -> SteamAddon later owns which Runtime package is installed and launched

Do not add another updater inside Managed ClawHUD.

### 2.4 Existing native build already produces the required runtime dependencies

Current CMake already places beside the Release executable:

    ClawHUD.exe
    ClawHUD.EcHelper.exe
    PresentMonAPI2Loader.dll
    velopack_libc.dll
    fonts/Unispace.otf
    fonts/Unispace-LICENSE.txt
    runtime/ClawHUD.PresentMonRuntime.msi

No second executable target is needed.

### 2.5 velopack_libc.dll remains a real shared-binary dependency

Current main.cpp executes the Velopack application bootstrap before launch-mode resolution and ClawHUD is linked with delay-load velopack_libc.dll.

Therefore the Managed payload must still contain velopack_libc.dll.

Do not introduce a second binary or Managed-only compile target solely to remove this dependency.

### 2.6 Current application-version validation is Standalone-specific

Current CMake only accepts ClawHUD versions from 0.1.0 through 0.1.999.

That value is used by:

    runtime log version
    GetRuntimeInfo.applicationVersion

A Runtime release such as 1.0.0 therefore needs a narrow build-version validation change.

---

## 3. Final release-channel model

Use the same ClawHUD GitHub repository with two intentionally separate channels.

### Standalone — unchanged

    v0.1.106
    v0.1.107
    v0.1.108

Properties:

    normal GitHub Release
    not prerelease
    Velopack stable
    releases.stable.json
    WPF Settings included
    Standalone updater consumes it

### SteamAddon Managed Runtime — new

    steamaddon-runtime-v1.0.0
    steamaddon-runtime-v1.0.1
    steamaddon-runtime-v1.1.0

Properties:

    GitHub Pre-release
    no Velopack stable feed
    no WPF Settings
    no Standalone Setup
    no nupkg
    no releases.stable.json
    ClawHUD.exe --managed runtime only
    future SteamAddon consumes exact immutable tag + SHA-256

The Pre-release flag is part of the channel contract, not only UI decoration.

---

## 4. Runtime versioning

SteamAddon Managed Runtime has an independent version identity.

Example:

    Standalone ClawHUD          = 0.1.108
    SteamAddonforClaw           = 0.3.24
    SteamAddon ClawHUD Runtime  = 1.0.2

They do not need to match.

Several Addon releases may pin the same Runtime release:

    Addon 0.3.24 ----    Addon 0.3.25 -----    Addon 0.3.26 ------> steamaddon-runtime-v1.0.2
    Addon 0.3.27 -----/

Do not rebuild or republish Runtime 1.0.2 because SteamAddon published another version.

### Immutable tags

A published Runtime tag and its assets are immutable.

Never overwrite steamaddon-runtime-v1.0.2 with new bytes.

If payload changes, publish steamaddon-runtime-v1.0.3.

Do not use a single moving runtime tag as dependency authority.

### Explicit release request

The dedicated workflow must be usable while `integration/steamaddon` is not the
repository default branch. GitHub does not expose `workflow_dispatch` for a
workflow file that exists only on a non-default branch, so publication is
requested by an explicit change to:

    steamaddon-runtime/release-request.json

The request is the only path trigger for the Runtime workflow. Its format is:

    {
      "schema_version": 1,
      "version": "1.0.0"
    }

The workflow reads and validates `version` from that file at the exact pushed
`integration/steamaddon` commit. Updating the request file to a new version is
the intentional publication action; ordinary source pushes do not publish a
Runtime.

Version format:

    MAJOR.MINOR.PATCH

Examples:

    1.0.0    valid
    1.2.17   valid

    v1.0.0   invalid
    1.0      invalid
    1.0.0-beta invalid for CH-I3

Do not infer or consume a mutable latest Runtime version.

---

## 5. Narrow CMake version-validation change

Update CLAWHUD_VERSION validation so both Standalone 0.1.x and independent Runtime versions are accepted.

Target rule:

    non-negative numeric MAJOR.MINOR.PATCH
    no leading tag prefix
    no prerelease suffix
    no build metadata suffix

Equivalent CMake regex is acceptable:

    ^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$

Do not change:

    GetRuntimeInfo wire format
    Version.h macro names
    runtime protocol
    Standalone v0.1.x release numbering
    Standalone tag policy

For Managed Runtime builds:

    release tag                  = steamaddon-runtime-v1.0.2
    CLAWHUD_VERSION              = 1.0.2
    GetRuntimeInfo app version   = 1.0.2
    runtime log version          = 1.0.2

For Standalone builds, existing 0.1.x input remains unchanged.

---

## 6. Dedicated source-controlled Runtime contract folder

Create:

    steamaddon-runtime/
      README.md
      payload.manifest.json
      package-runtime.ps1

This folder is the packaging contract and recipe.

Do not commit generated binaries, MSI files, or ClawHUDRuntime.zip into this folder.

---

## 7. payload.manifest.json

Create steamaddon-runtime/payload.manifest.json as the authoritative payload-shape definition.

Recommended schema:

    {
      "schema_version": 1,
      "launch": {
        "executable": "ClawHUD.exe",
        "arguments": ["--managed"]
      },
      "required_files": [
        "ClawHUD.exe",
        "ClawHUD.EcHelper.exe",
        "PresentMonAPI2Loader.dll",
        "velopack_libc.dll",
        "LICENSE",
        "THIRD-PARTY-NOTICES.md",
        "fonts/Unispace.otf",
        "fonts/Unispace-LICENSE.txt",
        "runtime/ClawHUD.PresentMonRuntime.msi"
      ],
      "forbidden_files": [
        "ClawHUD.Settings.exe",
        "ClawHUD.Settings.dll",
        "ClawHUD.Settings.deps.json",
        "ClawHUD.Settings.runtimeconfig.json",
        "ClawHUD.Diag.exe",
        "Setup.exe",
        "releases.stable.json"
      ]
    }

Do not put a mutable download URL here.

Do not put the current Runtime version here.

Do not duplicate Control IPC protocol constants here.

This file describes payload shape, not a release instance.

---

## 8. Exact Managed payload

The staged package must be:

    clawhud/
      ClawHUD.exe
      ClawHUD.EcHelper.exe
      PresentMonAPI2Loader.dll
      velopack_libc.dll
      LICENSE
      THIRD-PARTY-NOTICES.md
      fonts/
        Unispace.otf
        Unispace-LICENSE.txt
      runtime/
        ClawHUD.PresentMonRuntime.msi

Explicitly exclude:

    ClawHUD.Settings.exe
    ClawHUD.Settings.dll
    ClawHUD.Settings.deps.json
    ClawHUD.Settings.runtimeconfig.json
    ClawHUD.Diag.exe
    Setup.exe
    *-full.nupkg
    *-delta.nupkg
    releases.stable.json

Also reject accidental private .NET runtime files:

    coreclr.dll
    clrjit.dll
    hostfxr.dll
    hostpolicy.dll
    dotnet.exe

The repository already contains LICENSE and THIRD-PARTY-NOTICES.md; both must ship with this redistributed runtime.

---

## 9. Preserve runtime-relative paths

Do not flatten or rename dependencies.

Required relationships:

    ClawHUD.EcHelper.exe
    -> sibling of ClawHUD.exe

    PresentMonAPI2Loader.dll
    -> sibling of ClawHUD.exe

    velopack_libc.dll
    -> sibling of ClawHUD.exe

    ClawHUD.PresentMonRuntime.msi
    -> runtime/ child

    Unispace.otf
    -> fonts/ child

The zip must contain top-level clawhud/ rather than dumping payload files at zip root.

---

## 10. package-runtime.ps1

Create steamaddon-runtime/package-runtime.ps1.

Recommended inputs:

    BuildDirectory
    OutputDirectory
    RuntimeVersion
    SourceCommit

Validate:

    RuntimeVersion = strict MAJOR.MINOR.PATCH
    SourceCommit   = exact 40 hexadecimal characters

Responsibilities:

1. Read payload.manifest.json.
2. Clean only its own output directory.
3. Create OutputDirectory/clawhud.
4. Copy required runtime files from existing Release output and repo root.
5. Preserve fonts/ and runtime/ paths.
6. Fail when any required file is missing.
7. Fail when a forbidden file appears.
8. Fail when a private .NET runtime appears.
9. Generate runtime-manifest.json.
10. Create ClawHUDRuntime.zip.
11. Compute SHA-256 of the exact zip.
12. Write ClawHUDRuntime.zip.sha256.
13. Print final file list, sizes, source commit, Runtime version, and hash.

The script must not:

    compile C++
    compile WPF
    download ClawHUD
    publish GitHub releases
    install PresentMon
    write registry
    modify settings
    run Velopack pack

One script owns payload composition. Do not duplicate the entire required-file list in workflow YAML.

---

## 11. Generated runtime-manifest.json

Generate:

    artifacts/SteamAddonRuntime/runtime-manifest.json

and place the same non-hash identity manifest inside:

    clawhud/runtime-manifest.json

before zipping.

Recommended minimum:

    {
      "schema_version": 1,
      "runtime_version": "1.0.2",
      "tag": "steamaddon-runtime-v1.0.2",
      "source_commit": "0123456789abcdef0123456789abcdef01234567",
      "asset": "ClawHUDRuntime.zip",
      "sha256": "<canonical sha256>"
    }

The external release manifest and `ClawHUDRuntime.zip.sha256` sidecar contain
the canonical SHA-256 of the exact final zip. The embedded manifest must keep
the same `schema_version`, `runtime_version`, `tag`, `source_commit`, and
`asset` fields, but its `sha256` value must be `null`. An archive cannot
contain the final hash of the archive that contains that same manifest without
a self-referential hash.

The future Addon consumer verifies the exact zip against its locally pinned
SHA-256 (or the downloaded external manifest) before extraction. After
extraction it compares only the embedded manifest's non-hash identity fields
against the external manifest/lock. It must not reject a valid payload because
the embedded `sha256` is `null`.

Do not add:

    latest URL
    update-channel URL
    SteamAddon application version
    machine-local state

Runtime version is independent from Addon version.

---

## 12. Release assets

Each Runtime Pre-release should contain:

    ClawHUDRuntime.zip
    ClawHUDRuntime.zip.sha256
    runtime-manifest.json

Do not publish Runtime-channel assets such as:

    Setup.exe
    nupkg
    releases.stable.json
    WPF Settings
    ClawHUD.Diag

---

## 13. Dedicated workflow

Create:

    .github/workflows/Build-SteamAddon-Runtime.yml

Keep it separate from:

    .github/workflows/Build-Release.yml

Do not merge the two release pipelines.

### Trigger

Use an explicit release-request push only:

    push:
      branches:
        - integration/steamaddon
      paths:
        - steamaddon-runtime/release-request.json

The workflow must fail immediately unless:

    github.ref == refs/heads/integration/steamaddon

A Runtime release must not be published from main or an arbitrary branch.

### Permissions

Use only required ClawHUD-repository publication permission:

    contents: write

Do not add cross-repository pull-request permissions in CH-I3.

### Concurrency

Use a dedicated group:

    clawhud-steamaddon-runtime-release

with cancel-in-progress false.

Do not share the Standalone release concurrency group.

---

## 14. Runtime workflow sequence

The dedicated workflow should perform:

1. Verify branch is integration/steamaddon.
2. Checkout the exact pushed commit with full history/tags.
3. Read and validate `steamaddon-runtime/release-request.json`.
4. Resolve exact source commit.
5. Fail if Runtime tag already exists.
6. Fail if GitHub Runtime release already exists.
7. Configure native Release with BUILD_TESTING=ON and CLAWHUD_VERSION equal to Runtime version.
8. Build Release.
9. Run CTest Release.
10. Run package-runtime.ps1.
11. Validate generated manifest and SHA.
12. Run staged Managed startup smoke.
13. Only now create the immutable Runtime tag/Pre-release at the exact built commit.
14. Upload exactly the three Runtime release assets.

No WPF publish is required for this Runtime composition. Existing normal Build Test continues to cover the WPF frontend.

---

## 15. Pre-release creation

Tag:

    steamaddon-runtime-v<RuntimeVersion>

Example:

    steamaddon-runtime-v1.0.2

Release title:

    SteamAddon Runtime v1.0.2

Required GitHub Release state:

    draft       = false
    prerelease  = true
    target      = exact built source commit

The release description should clearly state:

    SteamAddon Managed Runtime
    not intended for standalone installation
    launch mode = ClawHUD.exe --managed
    Runtime version
    source commit

Do not create or update the Standalone stable feed.

---

## 16. Never overwrite a Runtime release

Before build/publication, fail if either exists:

    tag steamaddon-runtime-vX.Y.Z
    GitHub Release steamaddon-runtime-vX.Y.Z

Never automatically:

    delete existing runtime tag
    delete existing runtime release
    replace existing Runtime zip
    force-push Runtime tag

If new bytes are required, publish a new Runtime version.

SteamAddon will later trust tag + source commit + SHA-256, so mutability is unacceptable.

---

## 17. Standalone release cleanup must continue to ignore Runtime Pre-releases

Do not modify current Standalone pruning so it starts touching Runtime Pre-releases.

Current Stable pruning is already scoped to non-prerelease v0.1.x.

Preserve that behavior.

Do not introduce a generalized release manager.

---

## 18. Staged Managed startup smoke

Before publication, launch:

    <staged>/clawhud/ClawHUD.exe --managed

On GitHub-hosted unsupported Windows hardware, CH-I2 should return:

    21 = UnsupportedHardware
    22 = HardwareIndeterminate

Accept either.

Use a small CI-only bounded timeout. If the process does not exit, terminate the CI process and fail the workflow.

Do not add production watchdog/process supervision for this test.

This smoke proves:

    staged executable loads
    velopack_libc.dll resolves
    --managed reaches Managed policy
    no blocking startup MessageBox
    unsupported hardware exits deterministically
    PresentMon installation is not attempted before the hardware gate

---

## 19. Do not use Actions artifacts as the permanent distribution identity

The permanent downloadable Runtime is the GitHub Pre-release asset.

Do not make actions/upload-artifact IDs the pin/distribution mechanism.

Temporary runner files are fine during CI, but the product identity is:

    immutable Runtime tag
    exact source commit
    ClawHUDRuntime.zip
    SHA-256

This avoids workflow-artifact retention becoming part of product lifecycle.

---

## 20. SteamAddon package must not embed ClawHUD

Future SteamAddon Setup/nupkg must not contain:

    ClawHUDRuntime.zip
    ClawHUD.exe
    ClawHUD.EcHelper.exe
    PresentMon Runtime MSI for ClawHUD

The future Addon downloads the exact pinned Runtime only when needed.

Most naturally:

    HUD Off
    -> no ClawHUD runtime process
    -> no Runtime download requirement

    HUD first On
    -> check exact pinned Runtime
    -> download only if absent
    -> verify
    -> install
    -> launch --managed

Do not modify Addon packaging in CH-I3.

---

## 21. Future SteamAddon dependency lock — contract only

The future Addon-side lock should conceptually contain:

    {
      "schema_version": 1,
      "runtime_version": "1.0.2",
      "tag": "steamaddon-runtime-v1.0.2",
      "asset": "ClawHUDRuntime.zip",
      "source_commit": "0123456789abcdef0123456789abcdef01234567",
      "sha256": "..."
    }

The Addon must never resolve:

    latest
    latest prerelease
    highest Runtime version
    latest ClawHUD commit

at product runtime.

It uses the exact reviewed lock only.

---

## 22. Future dependency propagation to SteamAddon

The final architecture should notify SteamAddon when a new Runtime Pre-release is intentionally published.

Do not add this cross-repository write path in CH-I3 before the Addon receiver exists.

Later flow:

    ClawHUD Runtime Pre-release published
    -> trusted repository_dispatch or equivalent GitHub App event
    -> SteamAddon dependency-update workflow
    -> fetch exact tag/manifest/zip
    -> independently verify source commit + hash + payload shape
    -> create dependency update PR
    -> never auto-merge

The existing SteamAddon VIIPER dependency workflow is the architectural precedent:

    repository_dispatch
    exact immutable identity
    independent verification
    mechanical adoption
    reviewed PR
    no automatic merge

Reuse that pattern later instead of inventing a second dependency-management framework.

Prefer the existing GitHub App model over a broad long-lived classic PAT.

---

## 23. Future local runtime location — context only

Do not extract ClawHUD into SteamAddon Velopack's current app directory.

Preferred future model:

    %LOCALAPPDATA%\SteamInputAddonforClaw\Runtime\ClawHUD\<runtime-version>\

Example:

    Runtime\ClawHUD\1.0.2\
      runtime-manifest.json
      ClawHUD.exe
      ClawHUD.EcHelper.exe
      PresentMonAPI2Loader.dll
      velopack_libc.dll
      fonts\
      runtime\

Versioned directories are preferred because they avoid replacing a running ClawHUD.exe in place and keep Addon VeloPack application-directory swaps independent.

Do not implement consumer installation in CH-I3.

---

## 24. Full1902 isolation

The mandatory Full1902 controller architecture remains independent.

Future failures must be feature-local:

    ClawHUD download failure
    -> HUD unavailable
    -> controller authority unchanged

    ClawHUD SHA mismatch
    -> refuse HUD runtime
    -> controller authority unchanged

    PresentMon startup/install failure
    -> Managed HUD startup fails
    -> controller authority unchanged

    ClawHUD crash
    -> HUD stops
    -> Full1902 continues

Never make ClawHUD network/bootstrap success a gate for:

    PID1902 ownership
    HidHide
    DirectInput
    VIIPER
    Xbox360 / SteamDeck presentation
    Center M Disabled authority
    Full1902 startup / recovery

---

## 25. PresentMon ownership remains inside ClawHUD

Managed payload includes:

    runtime/ClawHUD.PresentMonRuntime.msi

ClawHUD remains responsible for:

    readiness
    compatible version/ABI checks
    reuse of compatible installed Runtime
    install/upgrade
    post-install validation
    Managed startup exit mapping

SteamAddon must not install PresentMon as part of controller startup.

Do not move this MSI into a generic Addon dependency root.

---

## 26. Settings authority remains unchanged

Do not change:

    %LOCALAPPDATA%\ClawHUD\settings.ini

Downloaded runtime location is not a new settings authority.

Future SteamAddon UI talks to ClawHUD over existing Control IPC.

ClawHUD continues to validate and persist its own HUD settings.

---

## 27. Single-instance behavior remains unchanged

Keep:

    Local\ClawHUD.SingleInstance

Do not allow Standalone and Managed ClawHUD to coexist.

CH-I2 already provides Managed startup exit 20 for AlreadyRunning.

Do not add another mutex, takeover protocol, or process-kill mechanism in CH-I3.

---

## 28. Runtime / presentation code must remain unchanged

CH-I3 is packaging/distribution infrastructure plus the narrow CMake version validation.

Preferred production behavior diff:

    none

Do not modify:

    App startup semantics
    Managed exit-code contract
    Control IPC
    PresentMon bootstrap behavior
    EC helper behavior
    game detection
    suspend/resume
    Intel VRR Fix
    HUD visibility behavior
    renderer/presentation behavior
    TopMost behavior

Do not modify HudPresentation*, HudRenderer*, HudWindowGeometry*, HudPresentationContract*, or HudPresentationLifecycle*.

Do not change D3D11, DXGI, DirectComposition, D2D/DWrite, TopMost/Z-order, independent flip, presentation create/destroy ordering, or present cadence.

---

## 29. Intel VRR Fix remains unchanged

Do not modify:

    TweakStartupCoordinator
    IntelVrrRangeTweak
    IntelArcSyncClient
    AffectedPanelDetector
    IntelVrrResultStore
    SetIntelVrrRangeFixEnabled
    retry policy
    no-rollback policy

The SteamAddon Runtime is only another distribution composition of the proven Managed binary.

---

## 30. Expected implementation files

Primary expected files:

    CMakeLists.txt
    steamaddon-runtime/README.md
    steamaddon-runtime/payload.manifest.json
    steamaddon-runtime/package-runtime.ps1
    steamaddon-runtime/release-request.json
    .github/workflows/Build-SteamAddon-Runtime.yml

A small PowerShell packaging test is acceptable if it provides useful contract coverage.

Preferred outcome: existing .github/workflows/Build-Release.yml remains unchanged.

---

## 31. Validation

### CMake version contract

Prove:

    0.1.108   accepted
    1.0.0     accepted
    12.34.56  accepted

    v1.0.0    rejected
    1.0       rejected
    1.0.0-beta rejected

Do not create a general versioning abstraction.

### Payload shape

Assert every required file in payload.manifest.json exists.

Assert forbidden files are absent.

Assert private .NET runtime files are absent.

### Generated runtime manifest

Verify:

    runtime_version == release-request.json version
    tag == steamaddon-runtime-v<version>
    source_commit == exact 40-character checkout SHA
    asset == ClawHUDRuntime.zip
    external manifest sha256 == actual zip SHA-256
    SHA sidecar == actual zip SHA-256
    embedded manifest sha256 == null
    embedded non-hash identity fields == external manifest

### Zip layout

Verify top-level clawhud/ exists and payload files are not flattened into zip root.

### Managed smoke

Require exit 21 or 22 on hosted unsupported hardware within bounded CI time.

### Existing native suite

Run CTest Release before publication.

---

## 32. Publication must be last

Do not create the Runtime tag/Pre-release until all of these pass:

    configure
    native build
    CTest
    payload staging
    payload validation
    zip generation
    SHA verification
    Managed startup smoke

A failed validation must leave:

    no new Runtime tag
    no new Runtime GitHub Release

Do not add elaborate transaction/state machinery.

---

## 33. No automatic publication on ordinary source push

Do not publish a Runtime Pre-release for every integration/steamaddon push.

HUD changes are relatively infrequent and Runtime adoption should be intentional.

Published tags are immutable, so intermediate integration commits should not
automatically become downloadable Runtime releases. The only publication
trigger is an explicit change to `steamaddon-runtime/release-request.json` on
`integration/steamaddon`; the request version is validated before any build
or publication step.

---

## 34. No separate SteamAddon build of ClawHUD

Future ordinary SteamAddon Release CI must not rebuild ClawHUD.

Final ownership:

    ClawHUD repository
    -> builds/publishes Managed Runtime when HUD changes

    SteamAddon repository
    -> pins a published Runtime version/hash
    -> ordinary Addon release does not rebuild HUD

This is the reason for the independent Runtime Pre-release channel.

---

## 35. Future Addon update behavior — context only

If Addon A and Addon B both pin Runtime 1.0.2:

    Addon A -> Addon B
    Runtime pin 1.0.2 -> 1.0.2

then:

    no ClawHUD download
    no ClawHUD replacement

If a later Addon pin changes to 1.0.3:

    download exact steamaddon-runtime-v1.0.3
    verify SHA-256
    verify runtime manifest
    install versioned runtime
    launch ClawHUD.exe --managed

The Addon never asks GitHub which Runtime is latest.

---

## 36. Overengineering guard

Do not add:

    second ClawHUD runtime executable
    generic package manager
    new updater service
    runtime update daemon
    latest-version resolver
    release database
    runtime catalog API
    new IPC protocol
    heartbeat
    watchdog
    parent PID monitor
    new settings store
    new lifecycle manager
    Windows service
    source submodule
    self-extracting binary

Required design:

    existing ClawHUD.exe --managed
    + one source-controlled payload contract
    + one package script
    + one explicit Pre-release workflow
    + immutable tag
    + SHA-256

---

## 37. PR review checklist

    [ ] PR base is integration/steamaddon
    [ ] main is not modified directly
    [ ] existing Standalone Build-Release behavior remains unchanged
    [ ] separate Build-SteamAddon-Runtime workflow exists
    [ ] Runtime publication only allowed from integration/steamaddon
    [ ] publication is triggered only by an explicit release-request file change on integration/steamaddon
    [ ] release-request version is strict MAJOR.MINOR.PATCH
    [ ] CMake still accepts Standalone 0.1.x and accepts independent Runtime version
    [ ] Runtime tag is steamaddon-runtime-vX.Y.Z
    [ ] Runtime release is GitHub Pre-release
    [ ] release target is exact built source commit
    [ ] existing Runtime tag/release cannot be overwritten
    [ ] no releases.stable.json for Runtime
    [ ] no Velopack nupkg for Runtime
    [ ] no Standalone Setup for Runtime
    [ ] no WPF Settings in Runtime payload
    [ ] no ClawHUD.Diag.exe in Runtime payload
    [ ] no private .NET runtime in Runtime payload
    [ ] existing ClawHUD.exe reused
    [ ] no second native runtime target
    [ ] EC helper is sibling of ClawHUD.exe
    [ ] PresentMonAPI2Loader.dll is sibling of ClawHUD.exe
    [ ] velopack_libc.dll is sibling of ClawHUD.exe
    [ ] PresentMon MSI remains under runtime/
    [ ] Unispace files remain under fonts/
    [ ] LICENSE included
    [ ] THIRD-PARTY-NOTICES.md included
    [ ] payload.manifest.json owns payload shape
    [ ] external runtime-manifest records Runtime version/tag/source commit/asset/exact zip hash
    [ ] embedded runtime-manifest records the same non-hash identity fields and sha256=null
    [ ] ClawHUDRuntime.zip contains top-level clawhud/
    [ ] SHA sidecar matches exact zip
    [ ] staged --managed smoke exits 21/22 on CI
    [ ] publication happens only after build/test/validation
    [ ] Managed self-update remains disabled
    [ ] single-instance contract unchanged
    [ ] PresentMon behavior unchanged
    [ ] Control IPC unchanged
    [ ] settings authority unchanged
    [ ] Intel VRR Fix unchanged
    [ ] HudPresentation/HudRenderer unchanged
    [ ] TopMost and VRR-safe presentation unchanged
    [ ] Full1902/controller code untouched
    [ ] no cross-repo auto-merge

---

## 38. Completion result

After CH-I3:

    onehoon/ClawHUD
    integration/steamaddon
            |
            | intentional release-request commit
            v
    Build-SteamAddon-Runtime.yml
            |
            +-- build with Runtime version
            +-- CTest
            +-- package-runtime.ps1
            +-- payload verification
            +-- --managed smoke
            +-- SHA-256
            |
            v
    GitHub Pre-release
    steamaddon-runtime-v1.0.2
            |
            +-- ClawHUDRuntime.zip
            +-- ClawHUDRuntime.zip.sha256
            +-- runtime-manifest.json

Existing Standalone path remains independently:

    Build-Release.yml
            |
            v
    v0.1.x normal Release
            |
            v
    Velopack stable feed
            |
            v
    Standalone ClawHUD updater

The two channels share source code but do not share release/update authority.

---

## 39. Next work after CH-I3

The next work order belongs to onehoon/SteamAddonforClaw on an integration branch, not main.

It should implement:

    ClawHUD exact dependency lock
    exact Pre-release download client
    SHA-256 + runtime-manifest verification
    versioned LocalAppData Runtime installation
    HUD On lazy bootstrap
    Managed process owner
    bounded IPC readiness / GetRuntimeInfo verification
    RequestShutdown
    CH-I2 startup-exit translation
    Full1902 isolation

After that Addon receiver/pin path exists, add the small dependency-notification bridge:

    new ClawHUD Runtime Pre-release
    -> trusted repository_dispatch
    -> SteamAddon dependency update workflow
    -> verified lock update PR
    -> never automatic merge

Use the existing Addon VIIPER dependency-automation pattern as the reference rather than creating another dependency-management architecture.
