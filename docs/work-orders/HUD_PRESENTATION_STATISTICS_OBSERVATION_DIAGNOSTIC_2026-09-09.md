# HUD Presentation Statistics Observation Diagnostic Work Order — 2026-09-09

Status: implementation work order  
Target repository: `onehoon/ClawHUD`  
Prepared against `main` at `8ffabea03a185c2f94e9c745a0fe704e79019c29` (`Add passive HUD visibility marker diagnostic (#238)`)  
Scope: debug-only, observation-only Presentation API statistics collection for the unresolved HUD physical-visibility disappearance

---

## 1. Objective

Add **automatic, debug-only Presentation API statistics diagnostics** that reveal how Windows actually processed and displayed ClawHUD Presentation API frames while the HUD is physically visible, physically covered/disappeared, and later restored.

This is not a recovery PR.

Do **not** add another manual recovery hotkey, topmost reassert hotkey, retry loop, watchdog, DWM/MPO workaround, or automatic presentation recreation.

The purpose of this PR is to answer the remaining question that the existing diagnostics cannot answer:

> `IPresentationManager::Present()` returns `S_OK`, but did Windows actually queue, compose, scan out, independent-flip, skip, or otherwise process the ClawHUD content in a different way while the HUD was physically missing?

The existing PR #236 diagnostics establish whether ClawHUD successfully submits frames. This PR must observe what happens **after submission**, at the Presentation/DWM/display path boundary.

---

## 2. Why this is the next diagnostic

### 2.1 What has already been ruled down

The real-device reproductions established the following during the user-visible disappearance:

```text
HudPresentation logical visible = true
IsWindowVisible(HWND) = true
IsIconic(HWND) = false
WS_EX_TOPMOST = present
Presentation buffers continue to become available
SetBuffer() does not fail
Present() continues to return S_OK
successfulPresentCount continues increasing
noBufferActive = false
submissionFailureActive = false
```

PR #237 then tested two manual recovery operations:

```text
A. detach/commit/wait + reattach/commit/wait the existing DComp visual content
B. recreate the Presentation manager/surface/buffers/bitmap generation while preserving the HWND and DComp visual tree
```

Neither restored the HUD visually despite every operation succeeding.

PR #238 removed those mutation diagnostics and retained passive observation only.

Therefore, do not reintroduce those paths in this PR.

### 2.2 2026-09-09 real-device correlation

The `0909` logs added one useful correlation.

During an affected session, Minecraft became the foreground game while the HUD continued submitting normally. The game HWND appeared at approximately:

```text
01:22:37.895  Minecraft main HWND SHOW
01:22:37.901  HUD still logicalVisible=1 / isWindowVisible=1 / exTopmost=1
```

The game being foreground did not recreate the HUD presentation.

A few seconds later the first valid FPS value arrived:

```text
01:22:42.464  PresentMonFPS displayed=16.00
```

The FPS segment changed the ContentWidth measurement and immediately caused:

```text
01:22:42.480  HUD WM_WINDOWPOSCHANGED
              width 1137 -> 1231
```

Current `HudPresentation::ResizeContentWidth()` performs both:

```cpp
SetWindowPos(
    window_,
    HWND_TOPMOST,
    geometry.xPx,
    geometry.yPx,
    static_cast<int>(geometry.widthPx),
    static_cast<int>(heightPx_),
    SWP_NOACTIVATE | SWP_NOOWNERZORDER);

RECT sourceRect{
    0,
    0,
    static_cast<LONG>(geometry.widthPx),
    static_cast<LONG>(heightPx_)
};
presentationSurface_->SetSourceRect(&sourceRect);
```

This makes the resize a plausible **recomposition trigger**, but it does not prove the root cause.

Possible explanations still include:

```text
A. HWND z-order reevaluation caused by SetWindowPos(HWND_TOPMOST)
B. Presentation source-rect change causing display-path reevaluation
C. game fullscreen/presentation entry causing DWM/MPO/independent-flip plane reassignment
D. some combination of the above
```

Adding more manual mutation hotkeys would only test individual triggers and would not reveal what Windows was actually doing with the Presentation content.

The next useful evidence is therefore Presentation Statistics.

---

## 3. Current production path that must remain unchanged

Current production Presentation path:

```text
HudPresentation::Render
  -> RefreshDisplayIfNeeded
  -> FormatHud
  -> optional ResizeContentWidth
  -> TryAcquireAvailableBuffer
  -> D2D draw
  -> deviceContext_->Flush()
  -> presentationSurface_->SetBuffer(...)
  -> presentationManager_->Present()
```

Current production contract:

```cpp
constexpr HudPresentationContract ProductionHudPresentationContract() noexcept
{
    return {
        WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW | WS_EX_TRANSPARENT |
            WS_EX_LAYERED | WS_EX_TOPMOST,
        DXGI_FORMAT_B8G8R8A8_UNORM,
        1,
        3,
        D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE |
            D3D11_RESOURCE_MISC_SHARED_DISPLAYABLE,
        DXGI_ALPHA_MODE_PREMULTIPLIED,
        true,
        0.0f, 0.0f, 0.0f, 0.0f,
        true
    };
}
```

The PR must not change any part of that contract.

---

## 4. Non-negotiable HUD / VRR safety contract

Do not modify, replace, weaken, bypass, or work around any of the following:

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
existing Presentation API production path
existing DirectComposition production path
premultiplied-alpha presentation contract
```

Do not add any diagnostic behavior that changes presentation timing or flip policy.

Specifically forbidden in this PR:

```text
ForceVSyncInterrupt()
SetPreferredPresentDuration()
SetTargetTime()
CancelPresentsFrom()
SetWindowPos() as recovery
ShowWindow() as recovery
Hide()/Show() recovery
CommitVisibility() recovery
presentation recreation as recovery
buffer recreation as recovery
DComp detach/reattach recovery
MPO disable/enable
DWM reset/restart
Intel driver workaround
Edge/Explorer special casing
automatic covered/restored detection
watchdog recovery
retry loops
sleep-based recovery
```

The existing production behavior must be bit-for-bit equivalent when `DebugLoggingEnabled=false`, aside from unavoidable compile-time code presence.

---

## 5. Microsoft Presentation Statistics APIs to use

Use the existing `presentation.h` API already used by ClawHUD.

Enable these three statistics kinds:

```cpp
PresentStatisticsKind_PresentStatus
PresentStatisticsKind_CompositionFrame
PresentStatisticsKind_IndependentFlipFrame
```

The Presentation API provides:

```cpp
IPresentationManager::EnablePresentStatisticsKind(...)
IPresentationManager::GetPresentStatisticsAvailableEvent(...)
IPresentationManager::GetNextPresentStatistics(...)
IPresentationContent::SetTag(...)
```

Relevant statistics interfaces:

```cpp
IPresentStatusPresentStatistics
ICompositionFramePresentStatistics
IIndependentFlipFramePresentStatistics
```

Important Microsoft semantics:

### PresentStatus

```text
PresentStatus_Queued
PresentStatus_Skipped
PresentStatus_Canceled
```

### CompositionFrame display instance kind

```text
CompositionFrameInstanceKind_ComposedOnScreen
    content composed directly to the DWM backbuffer

CompositionFrameInstanceKind_ScanoutOnScreen
    content directly scanned out in an MPO plane

CompositionFrameInstanceKind_ComposedToIntermediate
    content composed to an intermediate surface
```

### IndependentFlipFrame

Provides, among other fields:

```text
content tag
displayed time
actual present duration
output adapter LUID
output VidPn source ID
```

Microsoft reference pages:

```text
https://learn.microsoft.com/en-us/windows/win32/api/presentation/nn-presentation-ipresentationmanager
https://learn.microsoft.com/en-us/windows/win32/api/presentation/nf-presentation-ipresentationmanager-enablepresentstatisticskind
https://learn.microsoft.com/en-us/windows/win32/api/presentation/nf-presentation-ipresentationmanager-getpresentstatisticsavailableevent
https://learn.microsoft.com/en-us/windows/win32/api/presentation/nf-presentation-ipresentationmanager-getnextpresentstatistics
https://learn.microsoft.com/en-us/windows/win32/api/presentation/nf-presentation-ipresentationcontent-settag
https://learn.microsoft.com/en-us/windows/win32/api/presentation/ne-presentation-compositionframeinstancekind
https://learn.microsoft.com/en-us/windows/win32/api/presentation/ns-presentation-compositionframedisplayinstance
https://learn.microsoft.com/en-us/windows/win32/api/presentation/nn-presentation-iindependentflipframepresentstatistics
https://learn.microsoft.com/en-us/windows/win32/comp_swapchain/comp-swapchain-examples
```

Do not infer unsupported meanings beyond what the API reports.

---

## 6. Diagnostics must be explicitly gated by `DebugLoggingEnabled`

Do not rely only on `RuntimeLogger` filtering to make this cheap.

Enabling Presentation Statistics itself is extra diagnostic work, so do not enable it for normal users.

The existing startup path already loads:

```cpp
debugLoggingEnabled_ = settings.debugLoggingEnabled;
RuntimeLogger::SetDebugLogging(debugLoggingEnabled_);
```

Add an explicit diagnostic-enable path from `App` to `HudController` to `HudPresentation`.

Suggested shape:

```cpp
// HudController.h
void SetPresentationStatisticsDiagnosticsEnabled(bool enabled) noexcept
{
    presentationStatisticsDiagnosticsEnabled_ = enabled;
}
```

Call it in `App` after settings are loaded and before any `HudController::Ensure()` can initialize the presentation:

```cpp
hudController_.SetPresentationStatisticsDiagnosticsEnabled(debugLoggingEnabled_);
```

Then propagate that flag into both initial creation and `HudController::Recreate()`.

One acceptable shape is:

```cpp
HRESULT HudPresentation::Initialize(
    HINSTANCE instance,
    const HudRenderOptions& options,
    float opacityPercent,
    bool enablePresentStatisticsDiagnostics);
```

Names may vary, but the ownership must stay explicit.

Requirements:

```text
DebugLoggingEnabled=false
  -> no EnablePresentStatisticsKind calls
  -> no statistics event handle
  -> no threadpool wait
  -> no statistics queue reads
  -> no [HudPresentStats] logs

DebugLoggingEnabled=true
  -> enable the diagnostic path best-effort
```

Do not add a settings UI control. Reuse the existing developer debug flag.

---

## 7. Statistics initialization

Add a small, isolated diagnostic initialization routine in `HudPresentation`.

Suggested methods:

```cpp
HRESULT InitializePresentStatisticsDiagnostics() noexcept;
void ShutdownPresentStatisticsDiagnostics() noexcept;
void ArmPresentStatisticsWait() noexcept;
void DrainPresentStatistics() noexcept;
```

These names are suggestions, not a required ABI.

The routine must run only after both of these exist:

```text
presentationManager_
presentationSurface_
```

Recommended order inside `CreatePresentationSurface()` or immediately after it:

```cpp
presentationSurface_->SetTag(kHudPresentationStatsTag);

presentationManager_->EnablePresentStatisticsKind(
    PresentStatisticsKind_PresentStatus, TRUE);
presentationManager_->EnablePresentStatisticsKind(
    PresentStatisticsKind_CompositionFrame, TRUE);
presentationManager_->EnablePresentStatisticsKind(
    PresentStatisticsKind_IndependentFlipFrame, TRUE);

presentationManager_->GetPresentStatisticsAvailableEvent(
    &presentStatisticsAvailableEvent_);
```

Use one stable tag because ClawHUD currently has one production Presentation surface.

Example:

```cpp
static constexpr UINT_PTR kHudPresentationStatsTag = 1;
```

Do not use pointer identity as the only content identity in logs.

The log already contains `presentationEpoch_`; use `epoch + tag` to distinguish recreated generations.

### Best-effort failure policy

Statistics diagnostics are not part of production HUD correctness.

If diagnostics setup fails:

```text
log one warning/debug record with stage + HRESULT
disable only Presentation Statistics diagnostics
continue normal HUD initialization
```

Do **not** fail `HudPresentation::Initialize()` because statistics could not be enabled.

Example:

```text
[HudPresentStats] reason=init-failed stage=enable-composition-frame hr=0x........
```

Do not retry repeatedly.

---

## 8. Event-driven collection only

Do not poll Presentation Statistics once per render frame.

Do not add this to the `Render()` hot path:

```cpp
while (...) GetNextPresentStatistics(...);
```

Do not add a timer for statistics collection.

Use the event returned by:

```cpp
IPresentationManager::GetPresentStatisticsAvailableEvent()
```

Microsoft documents this event specifically for waiting until statistics are available.

### Recommended implementation

Keep the diagnostic waiter owned entirely by `HudPresentation`.

Use a one-shot Windows threadpool wait whose callback does **only** this:

```cpp
PostMessageW(window_, kHudPresentationStatsReadyMessage, 0, 0);
```

The callback must not:

```text
call IPresentationManager
read COM statistics
call D3D/D2D/DComp
render
present
log large records
mutate visibility/z-order
```

The actual queue drain then happens on the HUD/window owner thread inside `HudPresentation::WindowProc`.

A local private message is sufficient because it is sent to the HUD HWND, not `RuntimeMessageWindow`.

Example:

```cpp
constexpr UINT kHudPresentationStatsReadyMessage = WM_APP + 1;
```

The exact value may differ. Keep it local to `HudPresentation`; do not modify runtime-control message IDs unnecessarily.

### Wait lifecycle

Preferred primitive:

```cpp
PTP_WAIT presentStatisticsWait_{};
HANDLE presentStatisticsAvailableEvent_{};
```

Use one-shot arming semantics:

```text
statistics event signaled
  -> threadpool callback posts HUD private message
  -> wait is temporarily unarmed
  -> HUD WindowProc drains statistics queue to empty
  -> HUD owner thread re-arms the wait
```

This avoids callback storms while the manager-owned event remains signaled.

The callback context may reference `HudPresentation`, but shutdown must synchronize before object destruction.

---

## 9. Queue drain semantics

`GetNextPresentStatistics()` returns `S_OK` with `nullptr` when the queue is empty.

Drain until `nullptr`:

```cpp
for (;;)
{
    Microsoft::WRL::ComPtr<IPresentStatistics> stats;
    const HRESULT hr = presentationManager_->GetNextPresentStatistics(&stats);
    if (FAILED(hr))
    {
        LogPresentStatisticsFailure(L"drain", hr);
        break;
    }
    if (!stats)
        break;

    ProcessPresentStatistics(stats.Get());
}
```

Do not impose a production retry loop.

A diagnostic-only defensive upper bound is acceptable only to prevent pathological log starvation, for example:

```cpp
constexpr unsigned kMaxStatsPerDrain = 4096;
```

If such a bound is used, emit one truncation record and re-arm normally. Do not drop the presentation or recreate anything.

---

## 10. Required statistics payload

Use one prefix:

```text
[HudPresentStats]
```

Do not create many unrelated prefixes.

Every emitted record should include at least:

```text
epoch
presentId
kind
```

Include `contentTag` for content-specific statistics.

### 10.1 PresentStatus record

For `PresentStatisticsKind_PresentStatus`:

```text
status=queued|skipped|canceled
```

Example:

```text
[HudPresentStats] epoch=1 presentId=1042 kind=present-status status=queued
```

Important:

`Queued` does not itself prove the frame reached physical scanout. Treat it only as the API-defined processing status.

### 10.2 CompositionFrame record

Query `ICompositionFramePresentStatistics`.

Required fields:

```text
contentTag
compositionFrameId
instanceCount
```

For each `CompositionFrameDisplayInstance`, collect:

```text
instanceIndex
instanceKind=composed-on-screen|scanout-on-screen|composed-to-intermediate
displayAdapterLuid
displayVidPnSourceId
displayUniqueId
renderAdapterLuid
requiredCrossAdapterCopy
colorSpace
finalTransform M11/M12/M21/M22/M31/M32
```

The highest-value field for this investigation is `instanceKind`.

Example:

```text
[HudPresentStats] epoch=1 presentId=1042 kind=composition-frame contentTag=1 frameId=... instance=0 instanceKind=scanout-on-screen displayVidPnSourceId=0 requiredCrossAdapterCopy=0
```

### 10.3 IndependentFlipFrame record

Query `IIndependentFlipFramePresentStatistics`.

Required fields:

```text
contentTag
displayedTime
presentDuration
outputAdapterLuid
outputVidPnSourceId
```

Example:

```text
[HudPresentStats] epoch=1 presentId=1042 kind=independent-flip contentTag=1 displayedTime100ns=... presentDuration100ns=... outputVidPnSourceId=0
```

Do not convert timestamps into wall-clock time unless conversion is already well-tested. Raw `SystemInterruptTime` values are sufficient because ClawHUD already logs `GetTickCount64()`-based timing for correlation.

If useful, also log:

```text
observedTickMs=GetTickCount64()
```

on every statistics record.

---

## 11. Log-volume policy

Do not dump every statistics object forever at unrestricted volume if that makes a normal reproduction log unusable.

However, do not over-compress the initial diagnostic so much that the critical transition is lost.

Recommended policy:

### Always log

```text
init success/failure
statistics kind transitions
instanceKind transitions
PresentStatus skipped/canceled
IndependentFlipFrame first occurrence after a mode transition
cross-adapter-copy transition
output/displayUniqueId change
queue read failure
shutdown summary
```

### Periodic summary

At approximately the same 5-second scale as the existing `[HudPresentationState]` heartbeat, emit one compact summary containing counters since the previous summary:

```text
[HudPresentStats] reason=summary epoch=1
  queued=N
  skipped=N
  canceled=N
  composedOnScreen=N
  scanoutOnScreen=N
  composedToIntermediate=N
  independentFlip=N
  lastPresentId=...
  lastInstanceKind=...
  lastDisplayedTime100ns=...
```

Do not add a new timer just for this summary.

Use the timestamps of incoming statistics and emit the summary opportunistically while draining the event-driven statistics queue when >= 5 seconds have elapsed since the previous summary.

### Transition logging

Maintain only minimal diagnostic state such as:

```cpp
std::optional<CompositionFrameInstanceKind> lastCompositionInstanceKind_;
std::uint64_t lastSummaryTickMs_{};
```

Do not build a general display-state machine.

---

## 12. Preserve existing diagnostics

Do not remove or weaken:

```text
[HudPresentationState]
[HudWindowState]
[HudVisibilityMark]
```

PR #236 state remains important because it answers a different layer:

```text
Was ClawHUD still successfully acquiring buffers and calling SetBuffer/Present?
```

This PR answers:

```text
How did the Presentation/DWM/display path report the content after submission?
```

PR #238 marker may remain available for optional human correlation, but the new statistics diagnostic must **not depend on the user pressing it**.

The expected real-device workflow should be usable without touching the marker hotkey.

---

## 13. Shutdown / recreation safety

The statistics diagnostic owns asynchronous wait resources, so cleanup must be explicit and deterministic.

Before destroying `presentationManager_`, `presentationSurface_`, or the HUD HWND:

```text
1. mark statistics diagnostics inactive
2. disarm threadpool wait
3. wait/cancel outstanding wait callbacks
4. close the threadpool wait
5. close the caller-owned statistics event handle
6. clear statistics diagnostic state
7. continue existing HudPresentation::Shutdown() resource destruction
```

For a `PTP_WAIT` implementation the required shape is approximately:

```cpp
if (presentStatisticsWait_)
{
    SetThreadpoolWait(presentStatisticsWait_, nullptr, nullptr);
    WaitForThreadpoolWaitCallbacks(presentStatisticsWait_, TRUE);
    CloseThreadpoolWait(presentStatisticsWait_);
    presentStatisticsWait_ = nullptr;
}

if (presentStatisticsAvailableEvent_)
{
    CloseHandle(presentStatisticsAvailableEvent_);
    presentStatisticsAvailableEvent_ = nullptr;
}
```

Do not hold the wait active across a Presentation manager recreation.

Every `HudController::Recreate()` must naturally create a fresh diagnostics generation with the new Presentation manager and new epoch when debug diagnostics are enabled.

A stale callback from an old epoch must never read the new manager.

The simplest acceptable rule is: `ShutdownPresentStatisticsDiagnostics()` fully joins/cancels callbacks before the old manager is released.

---

## 14. Do not change the production Present path

The following existing sequence must remain unchanged:

```cpp
hr = presentationSurface_->SetBuffer(buffer->presentationBuffer.Get());
if (FAILED(hr))
{
    RecordSubmissionFailure(...);
    return hr;
}

hr = presentationManager_->Present();
```

Do not add:

```text
GetNextPresentId() requirements to production rendering
present ID scheduling
extra fences
blocking waits after Present()
WaitForSingleObject() in Render()
statistics queue drain in Render()
ForceVSyncInterrupt()
```

Statistics must observe the existing production stream, not alter its pacing.

---

## 15. Files expected to change

Keep the PR narrow.

Likely files:

```text
src/ClawHUD/App.cpp
src/ClawHUD/HudController.h
src/ClawHUD/HudController.cpp
src/ClawHUD/HudPresentation.h
src/ClawHUD/HudPresentation.cpp
```

Optional pure helper/test files if useful:

```text
src/ClawHUD/HudPresentationStatisticsDiagnostics.h/.cpp
tests/HudPresentationStatisticsDiagnosticsTests.cpp
```

Prefer a small helper if enum-to-string conversion, transition tracking, and summary counters would make `HudPresentation.cpp` significantly harder to review.

Do not refactor unrelated HUD rendering, game detection, PresentMon telemetry, RuntimeMessageWindow, Settings, or IPC code in this PR.

---

## 16. Suggested implementation shape

A compact implementation could look like this.

### HudPresentation members

```cpp
bool presentStatisticsDiagnosticsEnabled_{};
bool presentStatisticsDiagnosticsActive_{};
HANDLE presentStatisticsAvailableEvent_{};
PTP_WAIT presentStatisticsWait_{};

std::uint64_t presentStatisticsLastSummaryTickMs_{};
std::optional<CompositionFrameInstanceKind> lastCompositionInstanceKind_;

std::uint64_t presentStatusQueuedCount_{};
std::uint64_t presentStatusSkippedCount_{};
std::uint64_t presentStatusCanceledCount_{};
std::uint64_t composedOnScreenCount_{};
std::uint64_t scanoutOnScreenCount_{};
std::uint64_t composedToIntermediateCount_{};
std::uint64_t independentFlipCount_{};
```

### Threadpool callback

```cpp
void CALLBACK HudPresentation::PresentStatisticsWaitCallback(
    PTP_CALLBACK_INSTANCE,
    void* context,
    PTP_WAIT,
    TP_WAIT_RESULT)
{
    auto* self = static_cast<HudPresentation*>(context);
    if (!self)
        return;

    const HWND hwnd = self->window_;
    if (hwnd)
        PostMessageW(hwnd, kHudPresentationStatsReadyMessage, 0, 0);
}
```

The callback must remain this small.

### WindowProc dispatch

```cpp
if (self && message == kHudPresentationStatsReadyMessage)
{
    self->DrainPresentStatistics();
    self->ArmPresentStatisticsWait();
    return 0;
}
```

The exact code must also handle shutdown/disabled state before rearming.

### Drain

```cpp
void HudPresentation::DrainPresentStatistics() noexcept
{
    if (!presentStatisticsDiagnosticsActive_ || !presentationManager_)
        return;

    for (;;)
    {
        Microsoft::WRL::ComPtr<IPresentStatistics> stats;
        const HRESULT hr = presentationManager_->GetNextPresentStatistics(&stats);
        if (FAILED(hr))
        {
            LogPresentStatisticsReadFailure(hr);
            return;
        }
        if (!stats)
            break;

        ProcessPresentStatistics(stats.Get());
    }
}
```

Keep COM query failures diagnostic-only.

---

## 17. Unit-testable logic

Do not try to fake the whole Presentation COM stack unless it is already easy to do.

Extract and test pure logic where valuable:

```text
PresentStatus -> stable log name
CompositionFrameInstanceKind -> stable log name
summary counter accumulation
transition detection
summary due/not-due decision
```

Suggested helper model:

```cpp
struct HudPresentStatisticsSummary
{
    std::uint64_t queued{};
    std::uint64_t skipped{};
    std::uint64_t canceled{};
    std::uint64_t composedOnScreen{};
    std::uint64_t scanoutOnScreen{};
    std::uint64_t composedToIntermediate{};
    std::uint64_t independentFlip{};
};
```

Do not add elaborate mocking infrastructure only for this diagnostic PR.

---

## 18. Required validation

### 18.1 Build/test

Run the repository's normal validation for the current main baseline:

```text
Debug build
Release build
native CTest suite
Settings/WPF tests if included in normal CI
```

No new warnings.

### 18.2 Normal-user zero-path check

With:

```text
DebugLoggingEnabled=false
```

verify:

```text
no [HudPresentStats]
no statistics event handle created
no threadpool wait created
no EnablePresentStatisticsKind calls
normal HUD behavior unchanged
```

### 18.3 Debug path check

With:

```text
DebugLoggingEnabled=true
HUD Enabled=true
HUD Visibility=Always
```

verify:

```text
statistics initialization succeeds or best-effort failure is logged
PresentStatus statistics arrive
CompositionFrame and/or IndependentFlipFrame statistics arrive as supported by the active display path
queue drains without blocking the HUD
shutdown is clean
HUD recreation creates a fresh statistics generation
```

### 18.4 Stress lifecycle

Exercise at least:

```text
HUD enable -> disable -> enable
HUD size change
HUD alignment change if ContentWidth
Settings-driven HUD recreation
app exit
```

No stale callback, crash, deadlock, use-after-free, or post-shutdown stats access.

---

## 19. Real-device reproduction after implementation

The user workflow should remain simple.

Required setup:

```text
DebugLoggingEnabled=true
HUD Enabled=true
HUD Visibility=Always
```

Preferred test:

```text
1. Start ClawHUD and confirm HUD is visible.
2. Open/maximize Explorer or Edge until the HUD becomes physically covered/disappears.
3. Do not run any manual recovery diagnostic.
4. Use the machine normally for a while.
5. Launch a game as in the 2026-09-09 reproduction.
6. If the HUD becomes visible again, continue for several seconds.
7. Exit ClawHUD normally and collect the full rotated log set.
```

The `Ctrl+Alt+Shift+M` marker is optional. Do not require the user to catch an exact moment.

---

## 20. What the next log must let us decide

### Case A — composition -> scanout/MPO transition correlates with disappearance

Example pattern:

```text
before disappearance:
instanceKind=composed-on-screen

after disappearance:
instanceKind=scanout-on-screen

restoration/game transition:
instanceKind=composed-on-screen
```

Interpretation:

```text
strong evidence that the physical-visibility problem correlates with hardware composition / MPO / direct scanout path assignment
```

Do not immediately disable MPO. That would require a separate design review because the production path is VRR-sensitive.

### Case B — independent flip statistics continue while HUD is physically missing

If the HUD continues receiving valid:

```text
kind=independent-flip
displayedTime=...
presentDuration=...
```

while the user reports it is not physically visible, then ClawHUD submission plus the Presentation independent-flip path remained active.

This strongly shifts investigation toward final display-plane ordering/composition/driver behavior rather than renderer or buffer submission.

### Case C — PresentStatus changes to skipped/canceled

If disappearance correlates with a sustained or unusual rise in:

```text
status=skipped
status=canceled
```

then the existing `Present() == S_OK` heartbeat was insufficient to describe actual present processing.

Investigate Presentation scheduling/status semantics before moving further into DWM/MPO.

### Case D — stats stop entirely while Present() continues succeeding

If:

```text
[HudPresentationState] successfulPresentCount continues increasing
```

but statistics stop unexpectedly for the same epoch and manager, investigate Presentation manager/reporting/lost-state behavior.

Do not auto-recreate in this PR.

### Case E — no meaningful stats transition

If composition/scanout/independent-flip statistics remain stable across covered and restored periods, then the next investigation should move outside ClawHUD's Presentation manager:

```text
DWM ETW
DxgKrnl / hardware composition / MPO plane assignment ETW
Intel graphics driver/display diagnostics
```

Do not add those to this PR.

---

## 21. Important interpretation guardrails

Do not overclaim what an individual API field proves.

Examples:

```text
Present() == S_OK
    means the Present API call succeeded, not necessarily that the user saw the pixels.

PresentStatus_Queued
    means the frame was queued to eventually be shown; it is not by itself proof of final physical scanout.

CompositionFrameInstanceKind_ScanoutOnScreen
    means Presentation reports direct scanout in an MPO plane for that content instance.

IndependentFlipFrame displayed time
    is much stronger evidence that the Presentation API considered that independent-flip present displayed.
```

Correlate statistics with:

```text
[HudPresentationState]
[HudWindowState]
[HudVisibilityMark] if used
[WindowLifecycle]
[GameDetection]
[PresentMonFPS]
```

---

## 22. Explicitly out of scope

Do not include any of the following in this implementation PR:

```text
fixing the HUD disappearance
TOPMOST reassert diagnostic hotkey
new global hotkeys
removing the existing M marker
A/B recovery code
DWM ETW
DxgKrnl ETW
MPO registry changes
MPO disable workaround
Intel driver API calls
browser-specific logic
Explorer-specific logic
game-launch special casing
ResizeContentWidth behavior changes
SetSourceRect behavior changes
HUD geometry redesign
PresentMon FPS changes
opacity changes
Settings UI changes
```

---

## 23. PR size / split

This should be **one implementation PR** if the event-driven diagnostics stay narrow.

Target roughly:

```text
~150-350 LOC production diagnostic code
small pure tests if useful
no unrelated refactor
```

If safe event-wait lifecycle integration makes the PR materially larger than that, split only the pure statistics helper/tests from the HudPresentation integration. Do not create multiple speculative diagnostic PRs.

---

## 24. PR description requirements

The implementation PR description must state:

```text
- debug-only Presentation Statistics diagnostics were added
- enabled only when DebugLoggingEnabled=true
- PresentStatus, CompositionFrame, and IndependentFlipFrame are observed
- collection is event-driven through GetPresentStatisticsAvailableEvent
- no Render() polling or blocking wait was added
- no recovery behavior was added
- no SetWindowPos/Show/Hide/DComp mutation is performed by diagnostics
- ForceVSyncInterrupt is not used
- PR #236 diagnostics are preserved
- PR #238 passive marker is preserved
- HudPresentationContract.h has zero diff
- independent flip / premultiplied alpha / click-through / no-activation / topmost contracts remain unchanged
```

---

## 25. Final completion checklist

Before marking the PR ready:

```text
[ ] DebugLoggingEnabled=false has zero Presentation Statistics runtime path
[ ] DebugLoggingEnabled=true enables PresentStatus
[ ] DebugLoggingEnabled=true enables CompositionFrame
[ ] DebugLoggingEnabled=true enables IndependentFlipFrame
[ ] Presentation surface has a stable statistics content tag
[ ] GetPresentStatisticsAvailableEvent is used
[ ] no per-frame statistics polling exists
[ ] no blocking wait exists in Render()
[ ] callback only posts a private HUD message
[ ] statistics queue drains on HUD/window owner thread
[ ] queue drains until GetNextPresentStatistics returns nullptr
[ ] diagnostics failure does not fail HUD initialization
[ ] shutdown synchronizes and closes diagnostic wait/event resources
[ ] presentation recreation creates a clean new diagnostic generation
[ ] [HudPresentationState] preserved
[ ] [HudWindowState] preserved
[ ] [HudVisibilityMark] preserved
[ ] no recovery path added
[ ] no ForceVSyncInterrupt
[ ] no topmost/watchdog retry
[ ] HudPresentationContract.h zero diff
[ ] click-through tests pass
[ ] no-activation tests pass
[ ] topmost tests pass
[ ] transparent hit-test tests pass
[ ] independent-flip tests pass
[ ] premultiplied-alpha tests pass
[ ] production presentation contract tests pass
[ ] Debug build clean
[ ] Release build clean
[ ] native CTest clean
```

---

## 26. Expected result of this PR

After this PR, the next field log should no longer leave us with only:

```text
"Present() kept returning S_OK while the HUD was invisible."
```

It should allow us to say something materially stronger, such as:

```text
"The HUD switched from DWM composition to direct MPO scanout when it disappeared, then returned to composition when the game started."
```

or:

```text
"The HUD continued receiving independent-flip displayed-time statistics while physically invisible, so renderer, buffer submission, and Presentation display confirmation all remained active."
```

or:

```text
"PresentStatus changed to sustained skipped/canceled while Present() itself still returned S_OK."
```

or:

```text
"Presentation statistics remained completely stable, so the next diagnostic must move outside the ClawHUD Presentation manager into DWM/DxgKrnl/hardware-plane observation."
```

That is the decision-quality evidence this work order is intended to produce.
