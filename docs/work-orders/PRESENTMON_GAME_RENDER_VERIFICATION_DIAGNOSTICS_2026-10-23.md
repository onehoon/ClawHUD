# Work Order — Diagnose Intermittent Game-Render Verification Failure (PresentMon API2)

> **Repository:** onehoon/ClawHUD  
> **Date:** 2026-10-23  
> **Target branch:** integration/steamaddon ONLY  
> **Reviewed baseline:** integration/steamaddon@fb714115d35431b97315918a73bbf3c61381fe33  
> **Deliverable:** ONE narrowly scoped diagnostic implementation PR targeting integration/steamaddon, **fewer than 500 changed LOC** (prefer roughly 200–350 LOC including tests). Do not implement a speculative fix.  
> **After validation:** squash-merge the implementation PR into integration/steamaddon following normal PR review. The user will separately decide whether to cherry-pick the resulting production-code commit to main. Do **not** merge main into integration/steamaddon or modify main in this work order.  
> **Document status:** Implementation instructions only; no production source is changed by this work order.

## 1. Problem and user-visible evidence

The HUD in "In Game Only" sometimes remains hidden even while an otherwise supported game is running in the foreground. The same machine and same game may start working after Windows reboot, then fail again later. "Always" mode can still show an accurate FPS for the affected game. The problem is **not restricted to non-Steam games**.

Observed on 2026-10-23 (local timestamps):

| Case | Game / process | Observed path | Interpretation |
| --- | --- | --- | --- |
| 09:43 | Non-Steam Townfall / Townfall-Win64-Shipping.exe, PID 15272 | Foreground and executable identified; startup window briefly rejected as not fullscreen; later PresentActivity watches the game, but no renderer.first-frame or foreground.target-set | Initial window rejection alone does not explain the persistent failure |
| 12:04 | Steam Ghost of Tsushima / GhostOfTsushima.exe, PID 5808, AppID 2215430 | 12:04:20 foreground/window 1920x1200; 12:04:27 renderer.first-frame then foreground.target-set, HUD and FPS work | Positive control |
| 12:47 | Steam Death Stranding Director's Cut / ds.exe, PID 15872, AppID 1850570 | renderer.first-frame and FPS normal | Another positive control with the same installed PresentMon runtime |
| 12:51 | Death Stranding, PID 4312 | renderer.first-frame; FPS repeatedly valid | Normal after ClawHUD restart |
| 12:53 | Steam Ghost of Tsushima, PID 15868, AppID 2215430 | 1920x1200 foreground window detected; PresentActivity watch, no successful frame-confirmation; verifier stopped on foreground handoff at 12:56:23 | Failure on the **same game** that succeeded at 12:04 |
| 12:56 | Steam Ghost of Tsushima, PID 10540 | Initial non-fullscreen window rejected (reason=11); later 1920x1200 game window visible; verifier remains active until app shutdown at 12:58:45, no renderer.first-frame | Reproduces after fullscreen transition |

Sources: Google Drive / ClawHUD / logs / 1023 / 새 폴더 / clawhud.1.log and clawhud.log; earlier logs / 1023 / clawhud2.log. Preserve the successful and failed traces as the reference comparison.

**Important:** No current log proves a specific PM_STATUS failure, zero-frame delivery, nonpositive BetweenDisplayChange, an ETW outage, or interference between frame readers. Those remain distinct hypotheses, **not confirmed root causes**.

A separate diagnostic anomaly is present: thousands of HudPresentStats GetNextPresentStatistics() warnings (E_FAIL 0x80004005) appear **during successful HUD/FPS sessions too**. They come from HudPresentation.cpp / Presentation API statistics, not the PresentMon API2 game-verification query. Do **not** address them in this PR or treat their timing as proof of causation.

## 2. Review findings from the actual integration branch

Current production path:

    GameSessionController::EvaluateCurrentForeground()
      -> ObserveGameScreen() / ForegroundGameDetector::Evaluate()
      -> NeedsRendererVerification / RendererVerificationRequest
      -> GameSessionController::ApplyForegroundEvaluation()
      -> GameRenderVerifier::Start(pid, requestId, callback)
      -> PresentMonTelemetryProvider::BeginGameRenderVerification(pid)
           -> frameTelemetry_.Ready()
           -> client_.StartTrackingProcess(pid) [PID reference-counted]
           -> PresentMonFrameTelemetry::BeginVerification()
                -> reset detector / disarm PID
                -> client.FlushFrames(pid)
                -> arm PID
      -> GameRenderVerifier::PollLoop() [100 ms]
           -> PresentMonTelemetryProvider::PollGameRenderDisplayedFrame(pid)
           -> PresentMonFrameTelemetry::PollDisplayedFrame()
                -> client.ConsumeFrames(frameQuery, pid, max 64)
                -> DecodePresentMonFrameDouble(BETWEEN_DISPLAY_CHANGE)
                -> PresentMonDisplayedFrameDetector::Observe(value > 0)
      -> FirstDisplayedFrame message
      -> renderer.first-frame / foreground.target-set / start FPS / HUD shown

Reviewed files:

- src/ClawHUD/GameDetection/GameSessionController.cpp and GameRenderVerifier.cpp/.h
- src/ClawHUD/GameDetection/ForegroundGameDetector.cpp
- src/ClawHUD/PresentMonTelemetryProvider.cpp
- src/ClawHUD/PresentMonFrameTelemetry.cpp/.h
- src/ClawHUD/PresentMonDebugFrameTelemetry.cpp/.h
- src/ClawHUD/GameDetection/PresentActivitySource.cpp
- src/ClawHUD/PresentMonApi2Client.cpp/.h
- tests/PresentMonFrameTelemetryTests.cpp, PresentMonTelemetryProviderTests.cpp, GameRenderVerifierTests.cpp, PresentActivitySourceTests.cpp
- docs/DIAGNOSTICS.md and RuntimeLogger

Confirmed observability gaps:

1. providerReady=1, processReady=1, systemReady=1 does **not** report frameTelemetry_.Ready() or debugFrameTelemetry readiness. Both frame readers are initialized without a visible result summary.
2. "verifier.stop pid=..." means the worker was running. It does not prove ConsumeFrames succeeded or produced usable samples.
3. BeginGameRenderVerification silently loses the reason for failure at readiness, StartTrackingProcess, and FlushFrames stages (the controller only reports generic verifier.start-failed).
4. PollDisplayedFrame conflates successful polls with zero records, successful polls with nonpositive/invalid decoded values, and PM_STATUS failures in an opaque optional<bool> return. This is intentionally conservative for admission; keep that admission logic unchanged.
5. The debug-only PresentActivity path has its **own frame query** on the **same PresentMon session**. On changing the watched PID, PresentMonDebugFrameTelemetry::ReadLatest() calls client.FlushFrames(pid) and then consumes frames. The production verifier independently flushes/consumes frames for that PID. Whether the shared runtime queues permit mutual interference is **unknown**; capture correlation, do not prematurely rewrite either consumer or remove the debug probe.
6. PresentMon process FPS uses a distinct dynamic-query path; positive Always-mode FPS is not sufficient proof that the frame-event query works.

The recent October 17 PR #266 changed system dynamic-query safety checks, not PresentMonFrameTelemetry, GameRenderVerifier, or the shared provider behavior. Both main and integration have matching Git blobs for these key files as of the reviewed baseline. Do not revert system-query bounds changes without new evidence.

## 3. Scope — instrumentation first, no behavior fix

Implement a small coherent set of **DEBUG-only, bounded, per-attempt diagnostic records** using the existing RuntimeLogger. Keep existing INFO/WARN behavior and existing user-visible behavior unchanged.

### 3.1 Report frame-query initialization / readiness

In PresentMonTelemetryProvider::Initialize(), log a single DEBUG summary of the frame subsystem state after initialization. Optionally capture the actual bool return values of frameTelemetry_.Initialize() and debugFrameTelemetry_.Initialize(); do not make either result a new global initialization gate.

Suggested fields:

    [PresentMonFrameVerify] init ready=1 frameReady=1 debugFrameReady=1

For failed frame-query registration, capture the registration PM_STATUS and returned record blob size in the frame telemetry initializer, once at startup, not on every sampling iteration. Do not change plan metrics or query registration semantics.

### 3.2 Report one verification attempt's start outcome, including API failure stage

In GameSessionController::ApplyForegroundEvaluation(), when a new request is actually attempted, emit DEBUG:

    [GameDetection] verifier.begin pid=10540 requestId=2 source=window-event

Emit on **new start only** (the existing ShouldStartRendererVerification guard is authoritative), never on every foreground.evaluate.

In PresentMonTelemetryProvider::BeginGameRenderVerification(), record exactly which stage failed or succeeded, reusing the existing endpoint calls and returned statuses rather than making extra API calls:

- ready / frameReady / validPid
- StartTrackingProcess PM_STATUS (including cases where tracking already has other holders)
- FlushFrames PM_STATUS from PresentMonFrameTelemetry::BeginVerification
- successful armed PID

Use only a small additive diagnostic status or log at the existing call sites. Avoid changing return types or adding an elaborate result-state machine merely for logging. Do not change PID reference counting, flush timing, lease ownership, or failure cleanup.

Suggested lines (illustrative names; preserve accurate semantics):

    [PresentMonFrameVerify] begin pid=10540 frameReady=1 stage=start-tracking status=0
    [PresentMonFrameVerify] begin pid=10540 stage=flush status=0
    [PresentMonFrameVerify] begin pid=10540 result=armed

A PM_STATUS failure must be logged numerically; do not label every failure as "ETW lost" or another unsupported diagnosis.

### 3.3 Distinguish consume outcomes and displayed-frame evidence

Instrument PresentMonFrameTelemetry::PollDisplayedFrame() with **bounded per-attempt aggregate diagnostics**. Required distinctions:

- attempt not armed / wrong PID, when relevant;
- calls returning PM_STATUS != SUCCESS, preserving last status and count of failures;
- SUCCESS + count=0 ("no records");
- SUCCESS + count>0 and no decodable finite BetweenDisplayChange;
- SUCCESS + finite BetweenDisplayChange <=0;
- SUCCESS + finite BetweenDisplayChange >0 (first verified displayed frame);
- record layout/bounds mismatch if it occurs.

A compact attempt aggregate is enough: elapsedMs, polls, total records, emptyPolls, statusFailures, invalidSamples, nonpositiveSamples, last PM_STATUS, and first-positive value. Do not log individual frames or decode every optional metric. Optional process ID / swap-chain / present-mode fields remain debug-only context, never additional admission gates.

Produce:
- one first-outcome DEBUG line promptly (so a short failed session is diagnosable);
- one DEBUG progress summary **at most once per 5 seconds per active attempt** while still waiting; no per-100ms logs;
- one DEBUG success/stop summary when feasible without changing verification lifecycle.

The stopping summary can be owned by GameRenderVerifier or the telemetry component; use **one** authoritative per-attempt summary, not duplicate every counter across layers. Include PID and requestId/generation where available. Use the existing verifier stop reason (foreground-handoff, eligible, app-shutdown, etc.); do not reinterpret normal handoff as an error.

Suggested bounded output:

    [PresentMonFrameVerify] poll pid=10540 elapsedMs=5000 polls=50 records=0 emptyPolls=50 statusFailures=0 lastStatus=0 result=waiting
    [PresentMonFrameVerify] poll pid=10540 elapsedMs=10000 polls=100 records=63 nonpositiveSamples=63 statusFailures=0 lastStatus=0 result=waiting
    [PresentMonFrameVerify] first-displayed pid=5808 betweenDisplayChangeMs=8.7
    [GameDetection] verifier.stop pid=10540 requestId=2 reason=app-shutdown

These examples are **schema illustrations**, not claims that any of the values occurred in the captured logs.

If logging from code called while provider_.apiMutex_ is held, do not introduce blocking work or heavy per-frame string construction. Prefer recording cheap counters and formatting only on the bounded logging path. Logging must not alter ConsumeFrames return values, detector state, lock ordering, or callback scheduling.

### 3.4 Minimal, correlated diagnostics for the second frame-query consumer

The debugging PresentActivity path must remain usable to investigate possible same-session interactions. In PresentMonDebugFrameTelemetry::ReadLatest(), add DEBUG-only records at the PID transition / flush and a **bounded** summary of frame ConsumeFrames outcomes (e.g. first result plus a 5-second interval), correlated by PID:

    [PresentMonDebugFrame] retarget pid=10540 flushStatus=0
    [PresentMonDebugFrame] poll pid=10540 ... lastStatus=0 records=0 result=waiting

Keep this minimal. In particular, do **not** disable debug frame consumption, suppress FlushFrames, change its frequency (250ms), or introduce a second PresentMon session in the diagnostic PR. Those would change the experiment itself. Do not confuse "[PresentActivity] watch pid=..." (successful process tracking lease) with actual frame data delivery.

### 3.5 Preserve existing behavior and safety contract

Do not modify:

- game admission rules, fullscreen tolerance, executable exclusions, cache/evidence semantics, or Steam-vs-non-Steam eligibility;
- the positive displayed-frame requirement PM_METRIC_BETWEEN_DISPLAY_CHANGE > 0;
- polling period (100ms verifier / 250ms PresentActivity), frame-query plan, flush-before-arm, return semantics, process lease/ref-count ownership, or first-frame callback behavior;
- PM runtime/MSI version, loader, API bindings, ETW settings, existing query consumer ordering;
- auto-retries, force-allow based on Always FPS, fallback to presented FPS, or watchdog restart mechanisms;
- production HUD presentation **contract**: windowExStyle, WS_EX_TRANSPARENT, WS_EX_NOACTIVATE, WS_EX_TOPMOST, WS_EX_LAYERED, WM_NCHITTEST -> HTTRANSPARENT, WM_MOUSEACTIVATE -> MA_NOACTIVATE, ProductionHudPresentationContract(), the Presentation API / DirectComposition production path, independent flip, and premultiplied alpha.

**Background Opacity remains background-only.** Do not touch HudPresentation.cpp or HudPresentStats warning behavior in this PR.

No release-mode log flood: debug records must disappear when [Developer] DebugLoggingEnabled=false, apart from unchanged existing INFO/ERROR lines.

## 4. Small implementation illustration (adapt to local code)

Illustrative **only**, inside the existing ConsumeFrames call path. The critical point is to retain the original return behavior:

    std::uint32_t count = kFrameConsumeBatch;
    const PM_STATUS status =
        client.ConsumeFrames(query_, processId, blob.data(), &count);
    // Record cheap per-attempt counters; emit only at first outcome, every
    // >=5 seconds while waiting, and final/success. Never log per frame.
    if (status != PM_STATUS_SUCCESS)
    {
        // diagnostics.NoteConsumeFailure(status);
        return std::nullopt;  // original behavior, unchanged
    }
    // diagnostics.NoteConsumeSuccess(count);
    // Existing decode loop and positive display-change gate stay intact.

For start tracking, preserve its current outcome-based control flow rather than making a second call for diagnostics:

    const PM_STATUS status = client_.StartTrackingProcess(processId);
    // DEBUG: begin pid=... stage=start-tracking status=<numeric>
    if (status != PM_STATUS_SUCCESS)
        return {};

Avoid adding new public production APIs purely to expose logging counters. If a tiny internal diagnostics struct/helper enables reliable testing, keep it file-local or private and reset it on each BeginVerification.

## 5. Automated tests — use existing targets, branch-local CMake

Extend **existing tests** instead of creating a test framework:

1. tests/PresentMonFrameTelemetryTests.cpp already has a FakeClient overriding RegisterFrameQuery, FlushFrames, and ConsumeFrames. Extend it only as needed to fake:
   - status failure vs SUCCESS+zero records vs SUCCESS+nonpositive values vs successful positive value;
   - repeated BeginVerification resets aggregate state and does not carry over stale frame evidence;
   - status failure still returns nullopt; zero/nonpositive still returns false; first positive still returns true exactly once;
   - any testable bounded summary function/counter distinguishes those cases without changing results.
2. tests/PresentMonTelemetryProviderTests.cpp: preserve PID lease / ref-count invariants and unready-provider behavior; extend only if new stage-logging support adds genuinely testable pure logic.
3. tests/GameRenderVerifierTests.cpp / PresentActivitySourceTests.cpp: preserve start/stop semantics and existing output. Add focused assertions only if a minimal new formatter/helper merits them.
4. No production test should depend on specific timestamps or orderings between unrelated debug and verifier threads.

Required checks (on a Windows/MSVC build environment):

- Build ClawHUD Managed/integration target in Release and Debug as supported.
- Run ClawHUD.PresentMonFrameTelemetryTests, ClawHUD.PresentMonTelemetryProviderTests, ClawHUD.GameRenderVerifierTests, ClawHUD.PresentActivitySourceTests (confirm actual CTest names from the branch's CMake).
- Run the full integration-branch CTest suite, including existing HUD presentation-contract regression tests.
- git diff --check, and verify PR changed LOC remains <500.
- Verify ordinary DebugLoggingEnabled=false produces no new per-frame or repeating diagnostics.

## 6. Hardware reproduction and evidence collection

For each test, save the full log including startup and at least ~30 seconds foreground time for the candidate; do not rely only on the cropped failing section.

1. Enable debug logging in %LOCALAPPDATA%/ClawHUD/settings.ini:

       [Developer]
       DebugLoggingEnabled=true

   Restart ClawHUD so the setting is loaded.
2. Launch a **positive control** (Death Stranding Director's Cut, or Ghost of Tsushima when it succeeds). Confirm first displayed-frame verification, HUD activation, and FPS; retain begin / consume summary.
3. Without rebooting, run Ghost of Tsushima repeatedly until the intermittent failure is reproduced. Collect PID, requestId, fullscreen admission, begin status, FlushFrames status, ConsumeFrames status/count/distribution, stop reason, and the PresentActivity reader's correlated status.
4. Optionally reproduce Townfall in non-Steam mode as an additional comparison, without altering the detection rules.
5. In the same failure episode, switch to Always mode if needed to establish process FPS availability; do **not** use Always FPS as an automatic admission substitute.
6. Observe both normal failure and recovery after Windows reboot if practical. No reboot is required to verify the code; the diagnostic must also be useful before a reboot.

Classify the failure from recorded evidence, rather than guess:

| Observed diagnostics | Next investigation |
| --- | --- |
| frameReady=0 / registration failure | API2 metric introspection and frame query registration |
| StartTrackingProcess or FlushFrames fails | process-tracking / shared session / process lifetime |
| ConsumeFrames returns non-success repeatedly | frame-query handle, API status, shared session health |
| ConsumeFrames succeeds but all counts are zero | event queue delivery, PID matching, and debug-consumer interactions |
| records arrive, but BetweenDisplayChange is absent/nonpositive | frame metric semantics / display mode; verify correct PID and query element layout |
| first-displayed succeeds but no foreground.target-set | renderer completion dispatch, foreground current process generation, and eligibility re-evaluation |
| normal success and failure differ by debug-reader retarget/flush timing | reproduce with controlled debug instrumentation; design a **separate** evidence-backed fix PR |

Note: HudPresentStats read-failed / independentFlip=0 are **not** valid proxies for whether the game-render verifier received PM_METRIC_BETWEEN_DISPLAY_CHANGE. Treat VRR/presentation investigation separately.

## 7. Completion criteria / review gates

- One compact PR against **integration/steamaddon only**, below 500 changed LOC.
- Logs distinguish **not armed**, **API status error**, **no records**, **invalid/nonpositive intervals**, and **first positive displayed frame**, with PID/attempt correlation.
- Logging is DEBUG gated and bounded; no 100ms log spam, no raw frame dump, no private full executable paths.
- Existing user-visible game detection/HUD/FPS behavior and verifier return semantics are bit-for-bit logically unchanged.
- Existing tests pass, and new fake-client assertions exercise the diagnostic distinctions where practicable.
- Production HUD VRR safety invariants are preserved; no changes to the presentation path.
- PR description includes before/after log excerpts from at least one successful hardware run and, if reproducible, one failure. If failure does not recur, state that explicitly instead of claiming a fix.
- Follow-up root-cause and remedy belong to a **separate PR** after the evidence identifies the failing stage.

## 8. Handoff

Implementation branch should be cut **from integration/steamaddon**, with PR base integration/steamaddon. Keep the resulting production-source change cherry-pickable to main; no integration-specific code paths unless essential for build wiring. The user will authorize and perform any subsequent main cherry-pick separately.
