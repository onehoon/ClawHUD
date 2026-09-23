# Work Order — Passive PresentMon 2.6 VRR Diagnostic for ClawHUD.Diag

Repository: onehoon/ClawHUD  
Target branch: main  
Reviewed baseline: 5939e0edc106ac0e818e32effb7a817e3b4ed361  
PresentMon runtime: v2.6.0 / public API 3.4  
Date: 2026-09-23  
Target executable: ClawHUD.Diag.exe only  
Expected PR count: 4, preferably under 500 changed LOC per PR where practical

## 1. Goal

Add a standalone VRR diagnostic mode to ClawHUD.Diag.exe.

The practical question is:

Does the current live game session appear to be operating with VRR, and is the current game presentation path compatible with VRR?

This is a passive live-session diagnostic.

Intended operator flow:

    1. Launch the game first.
    2. Alt+Tab to Explorer.
    3. Run ClawHUD.Diag.exe.
    4. Select VRR Diagnostic.
    5. The diagnostic prints "Return to the game..."
    6. Alt+Tab back to the game.
    7. The diagnostic automatically identifies and verifies that foreground renderer.
    8. It waits briefly for the session to settle.
    9. It captures evidence for a fixed duration with no hotkey.
    10. It plays a completion sound.
    11. Alt+Tab back to review the result.

There must be no F8 trigger, no new hotkey, no synthetic diagnostic HUD, no automatic production HUD enable/disable, no production Control IPC mutation, and no PresentMon.exe CLI dependency.

The production HUD may be ON or OFF. The tool measures the real state the user is currently running.

If HUD OFF versus HUD ON comparison is desired, run the passive diagnostic twice as separate captures.

## 2. Current main-branch findings

Current main baseline is the PresentMon 2.6.0 squash merge:

    5939e0edc106ac0e818e32effb7a817e3b4ed361

ClawHUD.Diag is already a separate console target and is not part of production release packaging.

Current src/ClawHUD.Diag/main.cpp only exposes the Game Detection Diagnostic.

Current Api2Evidence is a game-detection dynamic-query component. It polls:

    PM_METRIC_SWAP_CHAIN_ADDRESS
    PM_METRIC_DISPLAYED_FPS
    PM_METRIC_PRESENTED_FPS
    PM_METRIC_PROCESS_ID

Do not turn Api2Evidence into the new VRR frame collector.

Current DiagPresentMonApi2Client does not expose the frame-query endpoints required for this work.

The diagnostic header is already exact PresentMon API 3.4.

Useful historical code exists under archive/diagnostics/legacy-vrr-presentmon, but the old diagnostic must not be restored wholesale.

Important historical distinction:

    ctlGetVblankTimestamp
    -> not usable in previous hardware testing
    -> do not restore

    D3DKMTWaitForVerticalBlankEvent
    -> produced useful supporting cadence evidence
    -> may be reused as a diagnostic-only supporting signal

## 3. Evidence model

Keep all evidence axes separate in code and in the report:

    Live game session
        |
        +-- PresentMon 2.6 API2 frame evidence
        |
        +-- Intel IGCL Arc Sync configuration
        |
        +-- D3DKMT target-monitor VBlank cadence
        |
        +-- Windows active display-path / nominal refresh
        |
        -> combined analysis

PresentMon is the main renderer and presentation evidence source.

IGCL is configuration evidence.

D3DKMT is cadence evidence.

Windows display configuration provides exact target identity and nominal mode refresh.

Do not describe any single one of these as direct physical VRR truth.

## 4. PresentMon 2.6 role

PresentMon 2.6 improves OS-visible display/flip tracking and completed/dropped flip handling.

The public API still does not expose a direct VRR_ACTIVE bit, current physical scanout Hz, or physical scanout interval.

Use PresentMon for:

    renderer identity
    swapchain identity
    PresentMode
    displayed/not-displayed frame evidence
    dropped-frame evidence
    display-facing timing
    frame type
    presentation-path quality

Do not infer physical VRR activity from Independent Flip alone.

## 5. Menu and UX

Change the top-level console flow to something equivalent to:

    ClawHUD Diagnostic

    1. Game Detection Diagnostic
    2. VRR Diagnostic
    3. Exit

The existing Game Detection Diagnostic may retain its existing Start / Stop / Status behavior under its submenu.

When VRR Diagnostic starts:

    === VRR Diagnostic ===

    Start the game before running this test.

    Return to the game now.
    The diagnostic will automatically lock the next verified foreground renderer.

    Waiting for game...

No hotkey is used after this point.

## 6. Passive target acquisition

Add a dedicated VrrTargetAcquisition module.

Do not reuse the entire DiagnosticSession state machine.

After VRR mode begins:

1. Record the Diag PID.
2. Record the current foreground HWND/PID as the launch context.
3. Poll GetForegroundWindow at about 100 ms.
4. Ignore the diagnostic itself and known blocked executables.
5. Candidate window must be:
   - valid top-level HWND;
   - visible;
   - ownerless;
   - not minimized.
6. Open the process using PROCESS_QUERY_LIMITED_INFORMATION.
7. Record PID + process creation FILETIME + image path + executable name.
8. Resolve HMONITOR using MonitorFromWindow with MONITOR_DEFAULTTONULL.
9. Verify the PID with PresentMon frame data.
10. Lock only after real displayed-frame evidence is observed.

Recommended target-acquisition timeout: 60 seconds.

Timeout returns cleanly to the menu.

Use the existing generated DiagBlocklist.h and kDiagPresentMonBlocklist. Do not create a second blocklist source.

Avoid refactoring DiagnosticSession.cpp merely to share a few helper functions.

## 7. Renderer verification

Do not accept a foreground process solely because it has a visible window.

Use the PresentMon frame query and require:

    PM_METRIC_BETWEEN_DISPLAY_CHANGE

to produce at least one finite value greater than zero for the PID.

Also require a non-zero PM_METRIC_SWAP_CHAIN_ADDRESS when that metric is available.

This follows the existing production meaning that BETWEEN_DISPLAY_CHANGE > 0 is displayed-frame evidence.

Do not use only dynamic FPS as the final target lock.

## 8. Process identity

The target identity is:

    PID + process creation FILETIME

Re-check creation time after capture.

A reused numeric PID is not the same session.

## 9. Target monitor

Resolve the target from the locked foreground game window:

    MonitorFromWindow(targetHwnd, MONITOR_DEFAULTTONULL)

Do not fall back to the primary monitor.

If monitor resolution fails, return INCONCLUSIVE.

Record MONITORINFOEXW information, including szDevice and monitor rectangle.

## 10. DisplayPathProbe

Add a small read-only DisplayPathProbe.

Prefer:

    GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS)
    QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS)
    DisplayConfigGetDeviceInfo(DISPLAYCONFIG_SOURCE_DEVICE_NAME)

Match MONITORINFOEXW.szDevice to DISPLAYCONFIG_SOURCE_DEVICE_NAME.viewGdiDeviceName.

Record:

    source adapter LUID
    source ID
    target adapter LUID
    target ID
    refresh numerator
    refresh denominator
    nominal refresh Hz

EnumDisplaySettingsExW may be a fallback only for nominal refresh when the rational path value is unavailable.

Nominal refresh is a mode reference, not live physical refresh.

Read display-path identity again after capture. If it changed, return INCONCLUSIVE.

## 11. Read-only IGCL probe

Add a diagnostic-only DiagIntelVrrStateProbe.

Do not reuse the production mutation-capable interface as the public diagnostic abstraction.

The diagnostic probe must never resolve or call ctlSetIntelArcSyncProfile.

Required read-only endpoints:

    ctlInit
    ctlClose
    ctlEnumerateDevices
    ctlEnumerateDisplayOutputs
    ctlGetDisplayProperties
    ctlGetIntelArcSyncInfoForMonitor
    ctlGetIntelArcSyncProfile

Do not use ctlGetVblankTimestamp.

## 12. Exact IGCL target mapping

Use ctlGetDisplayProperties.

Intel's official sample identifies:

    Os_display_encoder_handle.WindowsDisplayEncoderID

as the Display Target ID.

Correlate WindowsDisplayEncoderID with the active Windows display target ID resolved by DisplayPathProbe.

Do not select outputs by array order.

Do not select outputs only because they report 48-120 Hz.

If exactly one IGCL output matches the Windows target ID, use it.

If no output or multiple outputs match, IGCL configuration is UNKNOWN or AMBIGUOUS. Continue collecting PresentMon and D3DKMT evidence.

Do not guess.

## 13. IGCL state to capture

For the matched target:

    Arc Sync supported
    capability minimum Hz
    capability maximum Hz
    capability max frame-time increase
    capability max frame-time decrease
    current profile
    active minimum Hz
    active maximum Hz
    profile max frame-time increase
    profile max frame-time decrease

Read this once immediately before measurement and once after measurement.

If the matched target profile/range changes during the capture, overall result is INCONCLUSIVE.

## 14. DiagPresentMonApi2Client extension

Extend only src/ClawHUD.Diag/DiagPresentMonApi2Client.* with thin wrappers for:

    pmSetEtwFlushPeriod
    pmFlushFrames
    pmRegisterFrameQuery
    pmConsumeFrames
    pmFreeFrameQuery

Use the same function signatures already proven by the production PresentMonApi2Client.

Do not import production tracking reference counts or telemetry-controller ownership.

The VRR diagnostic owns one short-lived session.

## 15. API version gate

VRR mode is explicitly PresentMon 2.6 / API 3.4 based.

Require:

    version.major == PM_API_VERSION_MAJOR
    version.minor == PM_API_VERSION_MINOR

Expected value is 3.4.

Do not weaken to greater-than-or-equal.

If the API does not match, print the expected and observed versions and return to the menu.

The existing Game Detection Diagnostic does not need behavior changes.

## 16. VrrApi2FrameCapture

Add a separate VrrApi2FrameCapture module.

Responsibilities:

    initialize loader/session
    query introspection
    build frame query
    start tracking PID
    flush stale queued frames
    consume frame rows
    typed decode
    retain/write samples
    free query
    stop tracking
    close session

Set ETW flush period to 8 ms for this diagnostic session.

Do not change telemetry polling period.

## 17. Frame-query metrics

Build the query from introspection.

Required:

    PM_METRIC_BETWEEN_DISPLAY_CHANGE
    PM_METRIC_SWAP_CHAIN_ADDRESS
    PM_METRIC_PRESENT_MODE

Strongly preferred when available:

    PM_METRIC_PROCESS_ID
    PM_METRIC_DROPPED_FRAMES
    PM_METRIC_DISPLAYED_TIME
    PM_METRIC_ALLOWS_TEARING
    PM_METRIC_FRAME_TYPE
    PM_METRIC_PRESENT_RUNTIME
    PM_METRIC_PRESENT_START_QPC
    PM_METRIC_BETWEEN_PRESENTS
    PM_METRIC_UNTIL_DISPLAYED
    PM_METRIC_DISPLAY_LATENCY
    PM_METRIC_RENDER_PRESENT_LATENCY
    PM_METRIC_SYNC_INTERVAL
    PM_METRIC_PRESENT_FLAGS
    PM_METRIC_FLIP_DELAY

Optional metric absence must not invalidate the entire capture.

Do not request dynamic-only metrics through pmRegisterFrameQuery.

In PresentMon 2.6 metadata, PM_METRIC_DISPLAYED_FRAME_TIME and PM_METRIC_PRESENTED_FRAME_TIME are dynamic metrics. Exclude them from the frame query.

Use per-frame PM_METRIC_BETWEEN_DISPLAY_CHANGE and PM_METRIC_BETWEEN_PRESENTS instead.

Do not add the new PSO metrics to VRR diagnosis.

## 18. Frame query availability

Use introspection and require an AVAILABLE independent-device metric entry.

Do not hard-code support.

Use the blobSize returned by pmRegisterFrameQuery.

Do not apply the DynamicQuery 16-byte row-stride rule to frame-query blobs.

Recommended consume batch capacity: 256 frames.

Allocate blobSize * batchCapacity and drain the queue repeatedly.

## 19. Stale frame protection

After target tracking and before the real capture:

    pmFlushFrames(targetPid)

must succeed.

If it fails, do not begin measurement.

This prevents old frames from the target-verification stage from entering the real capture.

## 20. Typed VrrFrameSample

Use a typed row model containing at least:

    processId
    swapChainAddress
    presentStartQpc
    presentMode
    presentRuntime
    frameType
    allowsTearing
    dropped
    syncInterval
    presentFlags
    betweenPresentsMs
    betweenDisplayChangeMs
    displayedTimeMs
    untilDisplayedMs
    displayLatencyMs
    renderPresentLatencyMs
    flipDelayMs

Optional values use optional types.

Decode through introspected dataOffset and dataSize.

Reject invalid offset, invalid size, PID mismatch, and non-finite doubles without crashing.

A bad optional field must not discard an otherwise useful frame.

## 21. Capture timing

Initial timing:

    per-candidate renderer verification timeout: 3 s
    post-lock settle: 2 s
    measurement duration: 15 s
    foreground validation: about every 100 ms

After the 2-second settle:

1. Re-check PID generation.
2. Re-check foreground target.
3. Read display path.
4. Read IGCL state.
5. Flush PresentMon frames.
6. Start D3DKMT.
7. Start API2 frame capture.
8. Measure for 15 seconds.
9. Stop D3DKMT.
10. Drain remaining PresentMon frames.
11. Re-read IGCL and display path.
12. Analyze.
13. Write files.
14. Play completion sound.

Fifteen seconds gives enough frame samples and roughly fourteen complete 1-second cadence windows while remaining convenient.

## 22. Capture invalidation

Capture foreground integrity is **edge-triggered**, not merely sampled.

During the 15-second measurement install a diagnostic-local `SetWinEventHook` for:

    EVENT_SYSTEM_FOREGROUND

The hook must be owned by a dedicated ClawHUD.Diag thread. Seed that thread's
message queue before calling `SetWinEventHook`, register an out-of-context
hook for all processes/threads on the current desktop, and keep a
`GetMessage`/`DispatchMessage` loop running for the hook's lifetime. WinEvent
callbacks are delivered on the registering thread and are not reliable unless
that thread pumps messages.

The measurement epoch begins only after the hook thread reports successful
registration. If hook registration fails, stop any already-started capture
sources and return `INCONCLUSIVE` with `foreground_event_unavailable`; do not
fall back to polling as the only foreground-change detector. The callback must
do only bounded PID lookup and atomically publish a foreign-PID transition; it
must not perform file/console I/O, stop capture sources, join threads, or destroy
the hook owner.

On every completion or abort path, request hook-thread shutdown, unhook on the
owning thread, finish its message loop, and join it before destroying callback
state or returning to the menu. Events before the measurement epoch do not
contaminate the run.

Any foreground transition observed after measurement begins whose non-zero PID differs from the locked target PID permanently marks the run contaminated, even if the user returns to the game before the next polling interval.

A different foreground HWND owned by the same locked PID is acceptable if the resolved target monitor remains unchanged.

The approximately 100 ms `GetForegroundWindow()` poll remains as a liveness/backstop check for current state, process exit, monitor changes, and environments where a foreground event is missed. It is **not** the primary Alt+Tab detector.

Therefore:

- a transient Alt+Tab to Explorer that immediately returns to the game still invalidates the run when the WinEvent was observed;
- a different-PID foreground event invalidates the run once and the capture must stop cleanly;
- no attempt is made to "heal" the run after the game regains foreground;
- events before the actual measurement start do not contaminate the run; target acquisition and the 2-second settle phase are allowed to contain the expected Explorer -> game transition.

During capture also require:

- target process remains alive;
- PID creation FILETIME remains unchanged;
- target monitor remains unchanged;
- active Windows display path remains unchanged.

If any invariant fails:

    INCONCLUSIVE

with an explicit reason such as:

    foreground_changed
    process_exited
    process_generation_changed
    monitor_changed
    display_path_changed

Do not auto-restart the test.

## 23. D3DKMT probe

Add a diagnostic-local `DiagD3dkmtCadenceProbe`.

Use the validated target-binding/statistics concepts from `archive/diagnostics/legacy-vrr-presentmon/D3dkmtVblankProbe.*` as reference, but do not compile archive source directly.

Bind only to the target HMONITOR.

Use:

    GetMonitorInfoW
    CreateDCW
    D3DKMTOpenAdapterFromHdc

to obtain:

    adapter LUID
    VidPnSourceId

Where possible, cross-check D3DKMT adapter LUID against the Windows active display path.

If target mapping is inconsistent, mark D3DKMT unavailable rather than sampling another display.

### 23.1 Cancellation-safe wait API

The active implementation must use:

    D3DKMTWaitForVerticalBlankEvent2

rather than the legacy single-object `D3DKMTWaitForVerticalBlankEvent`.

Windows 11 is the supported ClawHUD platform, and `D3DKMTWaitForVerticalBlankEvent2` supports waiting for the VBlank condition **and** user-mode wait objects in one call.

Create one dedicated manual-reset cancellation event before starting the sampler thread.

Configure each wait with:

    target adapter handle
    target VidPnSourceId
    NumObjects = 1
    ObjectHandleArray[0] = cancellation event

Interpret results as:

    STATUS_WAIT_0
      -> VBlank occurred
      -> record QPC sample

    STATUS_WAIT_1
      -> cancellation event signaled
      -> exit sampler loop normally

    any other NTSTATUS
      -> record wait failure
      -> exit sampler loop

If `D3DKMTWaitForVerticalBlankEvent2` is unavailable, mark D3DKMT evidence unavailable for this diagnostic.

Do not fall back to an uninterruptible blocking wait.

## 24. D3DKMT sampling and shutdown contract

Use one blocking sampler thread with:

    D3DKMTWaitForVerticalBlankEvent2
    QueryPerformanceCounter

No 1 ms polling timer is needed.

`Stop()` / cancellation must follow this exact order:

1. mark stop requested;
2. `SetEvent(cancelEvent)`;
3. join the sampler thread;
4. only after the thread has returned, close the D3DKMT adapter handle;
5. close the cancellation event;
6. clear sampler state so another diagnostic run can start.

Never use:

    TerminateThread
    forced thread suspension
    adapter-handle close as the cancellation mechanism
    detach-and-leak behavior

A foreground change, process exit, monitor/display-path change, normal 15-second completion, or command teardown must all use the same cancellation path.

A normal cancellation wake is not a D3DKMT failure and must not reduce confidence by itself.

A real wait error terminates sampling and records D3DKMT as unavailable/failed supporting evidence; it must not hang shutdown.

This cancellation contract is required so every invalidated run can cleanly return to the menu and a second VRR diagnostic can run in the same process.

## 25. D3DKMT analysis

Primary cadence analysis is:

    event count / elapsed time
    non-overlapping approximately 1-second windows

Do not classify VRR from the median individual wake-to-wake interval.

Raw individual interval statistics may still be printed for debugging.

For complete windows calculate:

    count
    min Hz
    max Hz
    average Hz
    median Hz
    standard deviation
    near-nominal ratio
    range width

Use Windows nominal refresh as the reference.

### 25.1 Exact near-nominal definition

Define one shared tolerance:

    nearNominalToleranceHz = max(1.5 Hz, nominalRefreshHz * 0.015)

A complete 1-second cadence window is **near nominal** when:

    abs(windowMeasuredHz - nominalRefreshHz) <= nearNominalToleranceHz

Then:

    nearNominalRatio =
        nearNominalWindowCount / completeWindowCount

Only complete windows participate in this ratio.

Use the same `nearNominalToleranceHz` for the fixed-like median proximity check so the implementation and unit tests have one reproducible definition.

Example for a 120.000 Hz mode:

    tolerance = max(1.5, 1.8) = 1.8 Hz
    near-nominal interval = 118.2 through 121.8 Hz

Do not round the nominal rational refresh to an integer before applying this rule.

## 26. Initial cadence classifier

Do not use a universal 119-Hz threshold.

Seed rules for the currently validated 120-Hz MSI Claw class:

FIXED_LIKE requires all:

    at least 10 complete windows
    near-nominal ratio >= 0.80
    abs(medianWindowHz - nominalRefreshHz) <= nearNominalToleranceHz
    window range width <= max(5 Hz, nominal * 0.05)

VARIABLE_LIKE requires all:

    at least 10 complete windows
    near-nominal ratio <= 0.50
    window range width >= max(6 Hz, nominal * 0.05)

Otherwise:

    INDETERMINATE

Keep these thresholds in pure analysis code so later hardware calibration is easy.

These are diagnostic heuristics, not a display standard.

## 27. Dominant swapchain

Group PresentMon samples by SwapChainAddress.

Ignore zero addresses.

For each swapchain count usable displayed samples.

Choose the swapchain with the largest displayed sample count.

Require at least 70% of usable displayed samples for a high-confidence dominant swapchain.

If no swapchain dominates, return INCONCLUSIVE instead of merging unrelated swapchains.

## 28. Presentation analysis

For the dominant swapchain calculate:

    frame count
    dropped count and percentage
    PresentMode distribution
    Independent Flip family percentage
    AllowsTearing distribution
    FrameType distribution
    PresentRuntime distribution
    SyncInterval distribution

Independent Flip family is:

    PM_PRESENT_MODE_HARDWARE_INDEPENDENT_FLIP
    PM_PRESENT_MODE_HARDWARE_COMPOSED_INDEPENDENT_FLIP

Initial path classification:

VRR_SAFE:

    usable PresentMode samples >= 60
    Independent Flip family >= 90%

MARGINAL:

    samples >= 60
    Independent Flip >= 50% and < 90%

NOT_VRR_SAFE:

    samples >= 60
    Independent Flip < 50%

UNKNOWN:

    insufficient data or PresentMode unavailable

AllowsTearing is supporting evidence only. Do not make it a required gate.

## 29. Pacing statistics

For finite dominant-swapchain values calculate deterministic nearest-rank:

    P50
    P95
    P99
    Max

for:

    BetweenPresents
    BetweenDisplayChange
    DisplayedTime
    UntilDisplayed
    DisplayLatency
    RenderPresentLatency
    FlipDelay

Calculate:

    display-change rate = 1000 / P50(BetweenDisplayChange)

Label this display-change rate, not physical refresh rate.

## 30. Frame Generation

Use PM_METRIC_FRAME_TYPE when available.

Report the distribution.

Do not claim PresentMon displayed FPS equals final physical XeFG output FPS.

FG may reduce confidence in timing/FPS interpretation, but it does not invalidate:

    IGCL configuration
    PresentMode family
    target identity
    D3DKMT cadence

## 31. IGCL configuration classification

Use:

    ENABLED
    OFF
    UNKNOWN
    AMBIGUOUS

OFF if:

    profile == OFF

or active range is effectively fixed:

    abs(maxHz - minHz) <= 0.5 Hz

ENABLED if:

    Arc Sync supported
    profile != OFF
    maxHz - minHz > 0.5 Hz

Custom profiles are valid if the active range is variable.

UNKNOWN/AMBIGUOUS if the target mapping or state query is not trustworthy.

## 32. Combined status

Human-facing overall states:

    LIKELY ACTIVE
    LIKELY FIXED
    OFF
    INCONCLUSIVE

Always print the evidence axes separately.

OFF:

    explicit target-mapped IGCL OFF/fixed-range configuration

LIKELY ACTIVE, high confidence:

    IGCL ENABLED
    AND presentation VRR_SAFE
    AND D3DKMT VARIABLE_LIKE
    AND process/display identity stable

LIKELY FIXED candidate:

    IGCL ENABLED
    AND presentation VRR_SAFE
    AND D3DKMT FIXED_LIKE

Before emitting LIKELY FIXED, compare game display-change rate with nominal refresh.

If:

    displayChangeRate >= nominalRefresh * 0.90

then fixed-like cadence cannot discriminate a game naturally running near the panel maximum.

Return INCONCLUSIVE instead.

If game display-change rate is materially below nominal and D3DKMT remains fixed-like, report:

    VRR Status: LIKELY FIXED
    Confidence: MEDIUM

Do not call this VRR BROKEN.

Missing major evidence generally becomes INCONCLUSIVE.

## 33. Confidence

Use:

    HIGH
    MEDIUM
    LOW

HIGH requires:

    stable PID generation
    stable target monitor
    exact Windows display path
    exact IGCL target-ID match
    usable dominant swapchain
    usable PresentMode
    at least 10 D3DKMT windows
    major axes agree

LOW evidence should normally produce INCONCLUSIVE rather than a strong status.

## 34. Output

Create a per-run directory under the current working directory, consistent with current developer-tool behavior:

    vrr-YYYYMMDD-HHMMSS/

Write:

    report.txt
    frames.csv
    vblank.csv

No JSON requirement for the first implementation.

frames.csv suggested columns:

    ElapsedMs
    ProcessId
    SwapChainAddress
    PresentStartQpc
    PresentMode
    PresentRuntime
    FrameType
    AllowsTearing
    Dropped
    SyncInterval
    PresentFlags
    BetweenPresentsMs
    BetweenDisplayChangeMs
    DisplayedTimeMs
    UntilDisplayedMs
    DisplayLatencyMs
    RenderPresentLatencyMs
    FlipDelayMs

vblank.csv:

    EventIndex
    Qpc
    ElapsedMs

## 35. Report shape

Example summary:

    === ClawHUD VRR Diagnostic ===

    Target
      Executable                   deathstranding.exe
      PID                          12345
      HWND                         0x...
      Monitor                      \\.\DISPLAY1
      Windows target ID            0
      Nominal refresh              120.000 Hz

    PresentMon
      API                          3.4
      Capture duration             15.0 s
      Dominant swapchain           0x...
      Dominant share               99.8%
      Frames                       451
      Dropped                      0
      Independent Flip             100.0%
      Present path                 VRR_SAFE
      AllowsTearing true           100.0%

    Display pacing
      BDC P50                      33.31 ms
      BDC P95                      34.08 ms
      BDC P99                      35.20 ms
      Display-change rate          30.02 FPS

    Intel Arc Sync
      Target mapping               EXACT
      Supported                    YES
      Profile                      EXCELLENT
      Capability range             48-120 Hz
      Active range                 48-120 Hz
      Configuration                ENABLED

    D3DKMT
      Adapter LUID                 0x...
      VidPnSourceId                0
      Complete 1s windows          14
      Window range                 98-113 Hz
      Near-nominal ratio           0.0%
      Cadence                      VARIABLE_LIKE

    Overall
      VRR Status                   LIKELY ACTIVE
      Confidence                   HIGH

    Note
      This is a multi-signal diagnostic inference.
      It is not a direct physical scanout truth API.

## 36. Completion behavior

After capture:

    VRR capture complete.
    Result: LIKELY ACTIVE (HIGH)
    Report: <path>\report.txt

Use MessageBeep(MB_OK) or equivalent.

No hotkey.

## 37. Code structure

Do not put the new feature into DiagnosticSession.cpp.

Recommended files:

    src/ClawHUD.Diag/VrrDiagnosticCommand.h/.cpp
    src/ClawHUD.Diag/VrrTargetAcquisition.h/.cpp
    src/ClawHUD.Diag/VrrApi2FrameCapture.h/.cpp
    src/ClawHUD.Diag/DisplayPathProbe.h/.cpp
    src/ClawHUD.Diag/DiagIntelVrrStateProbe.h/.cpp
    src/ClawHUD.Diag/DiagD3dkmtCadenceProbe.h/.cpp
    src/ClawHUD.Diag/VrrAnalysis.h/.cpp
    src/ClawHUD.Diag/VrrReportWriter.h/.cpp

Exact names may vary, but ownership must remain separated.

## 38. CMake

Add only the new Diag sources to the ClawHUD.Diag target.

Add gdi32 if needed.

Do not link production HUD, game-session, telemetry-controller, or RuntimeControl modules into Diag for convenience.

## 39. DLL/artifact scope

Do not spend feature scope on PresentMonAPI2Loader.dll packaging.

The operator will place:

    ClawHUD.Diag.exe
    PresentMonAPI2Loader.dll

in the same directory.

Do not modify Build-Diag.yml solely for this.

Do not add the PresentMon MSI to the Diag artifact.

## 40. Production contract

Do not modify or work around:

    HUD windowExStyle
    WS_EX_TRANSPARENT
    WS_EX_NOACTIVATE
    WS_EX_TOPMOST
    existing WS_EX_LAYERED behavior
    WM_NCHITTEST -> HTTRANSPARENT
    WM_MOUSEACTIVATE -> MA_NOACTIVATE
    ProductionHudPresentationContract()
    independent-flip requirement
    Presentation API / DirectComposition production path
    premultiplied-alpha presentation contract

No production HUD source should need modification.

Also keep unchanged:

    production game detection
    F8 behavior
    Runtime Control IPC
    Settings
    production PresentMon lifecycle
    PresentMon bootstrap
    Intel VRR Range Fix mutation path
    SteamAddon integration
    VeloPack release contents

## 41. PR plan

Prefer four focused PRs.

### PR1 — API2 frame-capture foundation

Implement:

    DiagPresentMonApi2Client frame endpoints
    exact API 3.4 gate helper
    VrrApi2FrameCapture
    VrrFrameSample
    introspection-built query
    frame decode
    flush/consume/free lifecycle
    unit tests

No menu entry yet.

### PR2 — Passive target and target display state

Implement:

    VrrTargetAcquisition
    passive foreground lock
    PID generation identity
    displayed-frame verification
    DisplayPathProbe
    DiagIntelVrrStateProbe
    Windows target ID <-> IGCL WindowsDisplayEncoderID matching
    pure unit tests

Do not refactor the existing Game Detection Diagnostic.

### PR3 — D3DKMT and analysis

Implement:

    DiagD3dkmtCadenceProbe
    1-second cadence windows
    nominal-relative cadence classifier
    dominant swapchain analysis
    PresentMode classification
    IGCL classification
    combined status/confidence
    pure unit tests

### PR4 — Orchestration and report

Implement:

    main menu integration
    VrrDiagnosticCommand
    60-second target wait
    2-second settle
    15-second passive capture
    capture invalidation
    frames.csv
    vblank.csv
    report.txt
    completion sound
    complete cleanup and rerun support

## 42. Tests

Add Diag test targets in cmake/ClawHUDTests.cmake, keeping root CMakeLists production-focused.

Frame decode tests:

    uint32
    uint64
    int32
    bool
    enum
    double
    non-finite double
    bad offset
    bad size
    missing optional metric

Query-plan tests:

    required metric missing
    optional metric missing
    unavailable device
    dynamic-only metric excluded
    available independent device chosen

D3DKMT tests:

    stable 120-like cadence
    stable lower cadence
    variable 90/110/100 windows
    exact nearNominalToleranceHz boundary inclusion/exclusion
    fractional nominal refresh handling
    insufficient windows
    invalid/non-monotonic timestamps
    window boundaries
    STATUS_WAIT_0 records a sample
    STATUS_WAIT_1 performs clean cancellation
    real wait failure terminates without hanging
    cancellation followed by a second successful sampler run

Combined analysis tests:

    IGCL OFF
    enabled + VRR_SAFE + variable-like
    enabled + VRR_SAFE + fixed-like
    near-nominal game rate forcing INCONCLUSIVE
    ambiguous IGCL
    NOT_VRR_SAFE presentation
    evidence disagreement
    FG confidence reduction

## 43. Hardware validation matrix

After PR4 validate on supported MSI Claw hardware.

A. Explicit VRR OFF:

    Arc Sync profile OFF
    120 Hz
    game around 30 FPS
    FG OFF

Expected:

    IGCL OFF
    PresentMon may still be Independent Flip
    D3DKMT near-nominal fixed-like
    Overall OFF

B. VRR ON low FPS/LFC:

    30-40 FPS
    FG OFF

Expected from prior evidence:

    presentation VRR_SAFE
    D3DKMT variable-like
    LIKELY ACTIVE

C. VRR ON in-range:

    70-80 FPS
    FG OFF

D. Near upper range:

    110-120 FPS

The classifier must avoid false LIKELY FIXED when the game itself is near nominal refresh.

E. XeFG 2x:

Record FrameType, presentation path, D3DKMT, IGCL and confidence.

F. Alt+Tab during capture:

Expected INCONCLUSIVE foreground_changed.

G. Exit game during capture:

Expected INCONCLUSIVE process_exited.

H. External/ambiguous display:

Verify the tool never silently selects the wrong IGCL output.

## 44. Regression requirements

Every PR runs the full applicable CTest suite.

Preserve at minimum:

    ClawHUD.DiagApi2EvidenceTests
    ClawHUD.DiagProcessMetadataTests
    ClawHUD.DiagWinEventTests
    ClawHUD.DiagJsonlSmoke

and all existing production HUD/presentation regression tests.

## 45. Error handling

Supporting signal unavailable does not equal VRR failure.

Examples:

    IGCL unavailable
    -> UNKNOWN, continue

    D3DKMT unavailable
    -> continue, likely INCONCLUSIVE overall

    optional frame metric unavailable
    -> blank field, continue

    PresentMode unavailable
    -> INCONCLUSIVE

    target process exits
    -> stop immediately and clean up

    pmConsumeFrames fails
    -> INCONCLUSIVE, not VRR failure

    pmFlushFrames fails
    -> do not begin measurement

Never convert an API error into a VRR failure.

## 46. Cleanup

All success/failure/timeout paths must release:

    frame query
    PresentMon tracking
    PresentMon session
    loader
    IGCL API handle
    ControlLib.dll
    D3DKMT sampler thread
    D3DKMT adapter handle
    process handles
    files

Prefer RAII.

A second VRR Diagnostic run in the same ClawHUD.Diag process must work after success, timeout, foreground contamination, process exit, or API failure.

## 47. Do not resurrect legacy diagnostic architecture

Do not restore:

    old in-app VrrDiagnostic
    STATIC diagnostic HUD
    DYNAMIC mock HUD
    PresentMon.exe capture
    Settings Diagnostics tab
    App.cpp diagnostic ownership

Only adapt useful read-only algorithms into standalone Diag modules.

## 48. Existing design document

docs/VRR_DIAGNOSTIC_PRESENTMON_API2_DESIGN.md remains useful background.

Where it conflicts with this work order, this work order is authoritative for the new implementation.

The new direction is intentionally:

    one passive live-game capture
    no automatic HUD A/B
    no hotkey
    PresentMon 2.6 raw frame evidence
    exact target-correlated IGCL state
    target-monitor D3DKMT cadence
    combined conservative inference

## 49. Acceptance criteria

The implementation is complete when:

- VRR Diagnostic exists only in ClawHUD.Diag.
- Production release packaging is unchanged.
- Production HUD presentation code is unchanged.
- Production game detection is unchanged.
- No F8/new hotkey is used.
- No synthetic HUD is created.
- No production Control IPC mutation is used.
- No PresentMon.exe dependency is introduced.
- Game -> Diag -> Alt+Tab back to game works with automatic target lock.
- Target lock requires displayed-frame API2 evidence.
- PID + creation FILETIME is stable.
- HMONITOR resolves with no primary fallback.
- Windows active display target and nominal refresh are recorded.
- IGCL target mapping uses WindowsDisplayEncoderID and does not guess.
- ctlGetVblankTimestamp is not used.
- VRR mode requires API 3.4.
- Frame query is introspection-built.
- Dynamic-only metrics are excluded.
- Stale frames are flushed before measurement.
- D3DKMT binds only to the target monitor.
- 1-second event-count windows drive cadence classification.
- Independent Flip classification remains separate from physical-VRR inference.
- Final status is LIKELY ACTIVE / LIKELY FIXED / OFF / INCONCLUSIVE.
- Confidence is separately reported.
- Near-nominal game cadence cannot produce a naive LIKELY FIXED verdict.
- Any observed EVENT_SYSTEM_FOREGROUND transition to another PID during measurement invalidates the run, even if the game regains foreground before the next poll.
- Foreground/process/display changes invalidate the run.
- nearNominalRatio uses the exact documented nominal-relative tolerance.
- D3DKMT sampling uses D3DKMTWaitForVerticalBlankEvent2 with a dedicated cancellation event.
- D3DKMT cancellation joins cleanly without TerminateThread or adapter-close cancellation.
- frames.csv, vblank.csv and report.txt are produced.
- Completion sound works.
- The diagnostic can be run repeatedly in one process.
- New tests and existing Diag/production tests remain green.

## 50. Final principle

The diagnostic should make a useful evidence-based inference without pretending Windows or PresentMon exposes a direct physical VRR-active truth bit.

The evidence hierarchy is:

    IGCL
      -> is variable refresh configured for this exact target display?

    PresentMon 2.6
      -> is the game actually displaying through a VRR-compatible path?
      -> are displayed/drop/timing signals healthy?

    D3DKMT
      -> is the target-monitor VBlank cadence variable-like or fixed-like during the same capture?

    Combined analyzer
      -> LIKELY ACTIVE / LIKELY FIXED / OFF / INCONCLUSIVE
      -> explicit confidence and raw evidence

This keeps the diagnostic practical, explainable, and significantly stronger than a single-signal heuristic while leaving the production ClawHUD VRR presentation contract completely untouched.
