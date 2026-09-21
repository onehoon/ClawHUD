# Work Order — CH-I4 ClawHUD Runtime → SteamAddon Dependency Update PR Automation

**Date:** 2026-09-21  
**Status:** Ready for implementation  
**Primary producer repository:** onehoon/ClawHUD  
**Producer branch:** integration/steamaddon  
**Consumer repository:** onehoon/SteamAddonforClaw  
**Consumer base branch:** main  
**Expected PR split:** 2 focused PRs (SteamAddon receiver first, ClawHUD sender second)  
**Auto-merge:** forbidden

---

## 1. Why this is ready now

SteamAddon CH-A1 is merged into main.

The Addon now ships the real dependency authority:

    src/SteamInputAddonforClaw/Dependencies/ClawHUD/clawhud.lock.json

Current lock schema:

    schema_version
    runtime_version
    tag
    asset
    source_commit
    sha256

CH-A1 already consumes the exact immutable tag/asset/hash and performs verified Runtime acquisition.

Therefore dependency automation must stay thin:

    verified ClawHUD Runtime release
    -> notify SteamAddon
    -> independently verify exact release
    -> mechanically update clawhud.lock.json
    -> build/test
    -> open Draft PR

Do not invent a new updater, dependency manager, Runtime catalog, latest-version resolver, or product-runtime service.

---

## 2. Architecture boundaries

Preserve:

- SteamAddon Full1902 controller ownership architecture;
- CH-I3 Managed Runtime release contract;
- SteamAddon CH-A1 exact Runtime pin/acquisition contract;
- existing SteamAddon VIIPER dependency automation as the implementation precedent.

Do not modify PID1901/PID1902 ownership, DirectInput, HidHide, VIIPER presentation, Center M authority, CH-A2 process ownership, ClawHUD IPC, PresentMon, renderer, Intel VRR Fix, or startup/recovery policy.

Dependency automation failure must never affect installed users, normal Addon startup, controller ownership, or normal Addon release CI.

---

## 3. Final flow

    ClawHUD / Build SteamAddon Runtime
      -> build/test/package/smoke
      -> publish immutable Pre-release
      -> verify published assets + tag target
      -> repository_dispatch: clawhud-runtime-ready
      -> SteamAddon dependency receiver
      -> independently verify release/manifest/zip/hash
      -> update clawhud.lock.json only
      -> build/test/publish-contract validation
      -> Draft PR to main

The dispatch payload is only a hint/input. SteamAddon independently verifies authoritative release data.

---

## 4. PR split and implementation order

### PR A — SteamAddon receiver first

Repository: onehoon/SteamAddonforClaw

Base: main

Add:

    .github/workflows/clawhud-dependency-update.yml

Implement and manually validate the receiver before any ClawHUD sender is enabled.

### PR B — ClawHUD sender second

Repository: onehoon/ClawHUD

Base: integration/steamaddon

Modify only:

    .github/workflows/Build-SteamAddon-Runtime.yml

plus a small README/work-order update if useful.

Do not add the sender first and leave dispatch events with no receiver.

---

## 5. ClawHUD sender trigger point

Dispatch only after the existing CH-I3 workflow has successfully completed:

    configure
    build
    CTest
    package
    external manifest/SHA validation
    embedded manifest validation
    Managed startup smoke
    GitHub Pre-release creation
    exact release asset verification
    exact tag target verification

Only then send repository_dispatch.

Any earlier failure sends no event.

---

## 6. Dispatch contract

Event type:

    clawhud-runtime-ready

Payload fields:

    runtime_version
    tag
    source_commit
    asset
    sha256

Example logical payload:

    runtime_version = 1.0.2
    tag             = steamaddon-runtime-v1.0.2
    source_commit   = exact 40-char source SHA
    asset           = ClawHUDRuntime.zip
    sha256          = exact 64-char ZIP SHA-256

Values must come from the exact release identity already produced and verified by the ClawHUD workflow.

Do not send mutable latest URLs, Addon version, runner paths, or Actions artifact IDs as dependency authority.

---

## 7. Cross-repository authentication

Do not use the default ClawHUD GITHUB_TOKEN for cross-repository dispatch.

Use the same GitHub App trust model already proven by the SteamAddon VIIPER dependency automation.

Prefer reusing the existing GitHub App only if its installation already covers both ClawHUD and SteamAddonforClaw with the required permissions.

If it does not, configure a narrowly scoped GitHub App credential for ClawHUD -> SteamAddon dispatch.

Do not use a broad classic PAT.

Do not commit credentials.

Missing GitHub App configuration should fail the notification step clearly.

---

## 8. SteamAddon receiver triggers

Use:

    repository_dispatch:
      types: [clawhud-runtime-ready]

Also provide a manual fallback that accepts one exact tag.

Manual mode must run the same verification path.

Do not add latest/highest Runtime lookup.

---

## 9. Receiver independent verification

For the exact requested tag, the receiver must independently:

1. Require strict steamaddon-runtime-vMAJOR.MINOR.PATCH format.
2. Query the exact GitHub Release in onehoon/ClawHUD.
3. Require non-draft + prerelease.
4. Require the exact contract assets:
       ClawHUDRuntime.zip
       ClawHUDRuntime.zip.sha256
       runtime-manifest.json
5. Read external runtime-manifest.json.
6. Require schema_version == 1.
7. Require runtime_version matches tag version.
8. Require tag matches requested tag.
9. Require source_commit is exact 40 hex.
10. Require asset == ClawHUDRuntime.zip.
11. Require sha256 is exact 64 hex.
12. Download exact ClawHUDRuntime.zip.
13. Recompute ZIP SHA-256.
14. Require recomputed SHA == external manifest SHA.
15. Require sidecar SHA == external manifest SHA.
16. Read clawhud/runtime-manifest.json inside the ZIP.
17. Require embedded schema/version/tag/source_commit/asset == external manifest.
18. Require embedded sha256 == null.

Only after all checks pass may the Addon lock be changed.

If dispatch payload fields disagree with independently verified release data, fail closed.

---

## 10. Lock adoption

Mechanically update only:

    src/SteamInputAddonforClaw/Dependencies/ClawHUD/clawhud.lock.json

Generate the new lock from independently verified release identity.

Do not edit CH-A1 acquisition code, AddonDataPaths, CH-A2 lifecycle code, UI, controller code, or installed Runtime directories.

No ClawHUD binaries belong in the dependency PR.

---

## 11. Upgrade eligibility

Keep this simple.

Exact same lock identity:

    clean no-op
    no branch
    no PR

Target Runtime version greater than current:

    eligible

Target Runtime version lower than current:

    no downgrade PR

Target Runtime version equal but source/hash/identity differs:

    fail closed

Equal-version changed bytes are incompatible with the immutable Runtime contract.

Do not infer compatibility beyond the existing package/IPC contract.

---

## 12. Automation branch / duplicate policy

Use a deterministic branch such as:

    automation/clawhud-runtime-1.0.2

Before writing:

- check existing branch;
- check open/closed PR for the same automation branch/tag;
- never force-push over an existing automation branch;
- never create duplicate PRs for the same Runtime.

Concurrency:

    group: clawhud-dependency-update
    cancel-in-progress: false

Reuse the proven VIIPER existing-automation pattern where practical.

Do not build a generic automation registry.

---

## 13. Addon main base drift

The receiver always adopts onto current SteamAddon main.

Checkout current main, capture its exact base SHA, and verify current main again immediately before push/PR creation.

If main advanced during verification/build/test:

    abort before push

Reuse the existing VIIPER base-drift pattern where practical.

Do not add a new generic base manager.

---

## 14. Required Addon validation before PR creation

After updating the lock, run at minimum:

    dotnet restore SteamInputAddonforClaw.slnx
    dotnet build SteamInputAddonforClaw.slnx -c Release --no-restore
    dotnet test SteamInputAddonforClaw.slnx -c Release --no-build

Also run the existing CH-A1 publish/dependency contract validation, including verify-publish-assets.ps1.

Do not start ClawHUD during normal Addon CI for this automation.

---

## 15. Draft PR

Open a Draft PR into main.

Suggested title:

    deps: update ClawHUD Runtime to v1.0.2

PR body should include:

- previous Runtime version/tag;
- new Runtime version/tag;
- exact source commit;
- exact ZIP SHA-256;
- release/tag link;
- validation summary;
- explicit statement that it does not auto-merge.

Never auto-merge.

---

## 16. Failure policy

ClawHUD release succeeds but dispatch fails:

    keep valid immutable release
    fail notification visibly
    recover through manual receiver run with exact tag

Receiver verification fails:

    do not modify lock
    do not create PR

Addon build/test fails:

    do not create PR

Existing branch/PR:

    no force-push
    no duplicate

Main advanced:

    abort before push

No additional production state machinery is required.

---

## 17. Full1902 isolation

This is repository/dependency infrastructure only.

Dispatch, verification, build, or PR failures must have zero effect on:

    PID1902
    DirectInput
    HidHide
    VIIPER
    X360/SteamDeck presentation
    Center M authority
    startup
    sleep/resume
    shutdown/recovery

Do not call dependency automation from product runtime.

---

## 18. Expected files

ClawHUD PR:

    .github/workflows/Build-SteamAddon-Runtime.yml

Optional:

    steamaddon-runtime/README.md

SteamAddon PR:

    .github/workflows/clawhud-dependency-update.yml

Optional narrow helpers/tests:

    scripts/
    scripts/tests/

Do not create a generic dependency framework.

---

## 19. Tests

ClawHUD sender:

- dispatch step ordered after final published-release verification;
- payload derived from verified release identity;
- exact event type clawhud-runtime-ready;
- no dispatch on earlier failure;
- no Runtime/package byte changes.

SteamAddon receiver:

- malformed tag rejected;
- draft/non-prerelease release rejected;
- missing/wrong assets rejected;
- external manifest mismatch rejected;
- recomputed ZIP SHA mismatch rejected;
- sidecar mismatch rejected;
- embedded identity mismatch rejected;
- embedded sha256 non-null rejected;
- exact current lock -> no-op;
- lower target -> no downgrade;
- equal version/different identity -> fail closed;
- newer valid version -> lock update;
- existing automation branch/PR -> no duplicate;
- stale main -> abort before push.

No live GitHub network in unit/script tests.

---

## 20. End-to-end implementation order

1. Implement SteamAddon receiver.
2. Manually validate receiver against an exact existing tag.
3. Confirm current pinned tag produces a clean no-op.
4. Confirm a newer verified Runtime can produce a Draft PR.
5. Add ClawHUD sender.
6. Publish a new Runtime using Build SteamAddon Runtime.
7. Confirm dispatch arrives.
8. Confirm receiver independently verifies.
9. Confirm one Draft dependency PR is created.

---

## 21. Acceptance criteria

1. Build SteamAddon Runtime remains one-click.
2. Successful verified Runtime release sends exactly one clawhud-runtime-ready dispatch.
3. SteamAddon has a dedicated ClawHUD dependency receiver.
4. Dispatch payload is never trusted without independent release verification.
5. Exact release state/tag/assets are verified.
6. ZIP SHA-256 is recomputed.
7. External manifest, sidecar, ZIP hash, and embedded identity agree with CH-I3/CH-A1.
8. Exact current lock is a no-op.
9. Equal-version/different-identity fails closed.
10. Downgrades are not adopted.
11. Only clawhud.lock.json is mechanically changed.
12. Addon validation passes before PR creation.
13. Draft PR targets current main.
14. Existing automation branch/PR is never overwritten.
15. No auto-merge exists.
16. No Full1902/product-runtime path depends on this automation.
17. A newly published Runtime produces one Addon dependency Draft PR end-to-end.

---

## 22. Suggested PR titles

ClawHUD:

    CH-I4: dispatch verified Runtime releases to SteamAddon

SteamAddon:

    CH-A1D: automate verified ClawHUD Runtime dependency PRs

---

## 23. Review policy

Block only realistic repository/update correctness problems:

- dispatch before release verification;
- trusting dispatch payload without re-verification;
- mutable/latest resolution;
- hash not recomputed;
- manifest mismatch accepted;
- equal version with changed bytes accepted;
- downgrade adopted;
- stale-main PR pushed;
- duplicate/force-pushed automation branch;
- lock changed without Addon validation;
- auto-merge;
- Full1902/product-runtime coupling.

Do not add theoretical race machinery, generic managers, daemons, polling, or future-proof abstractions.

Final target:

    [one click]
    ClawHUD Build SteamAddon Runtime
      -> verified immutable Pre-release
      -> repository_dispatch
      -> SteamAddon independent verification
      -> clawhud.lock.json update
      -> Draft dependency PR
      -> human review / normal merge
