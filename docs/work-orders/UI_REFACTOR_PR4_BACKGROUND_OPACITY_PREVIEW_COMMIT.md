# UI Refactor PR4 — Background Opacity Preview / Commit Interaction

**Date:** 2026-09-03  
**Status:** Ready for implementation  
**Reviewed baseline:** `main` at `be8df7f97b4b8f7007f9e5555c461a811ca8ca90`  
**Previous PR:** #225 — HUD Setting Mutations with Authoritative Snapshot Reconciliation  
**Parent plan:** `docs/UI refact/CLAW_HUD_SETTINGS_WPF_UI_REFACTOR_PLAN_2026-09-03.md`

---

## 1. Objective

Enable the WPF `Background opacity` slider while preserving the existing ClawHUD runtime semantics:

```text
tracking / drag
    -> PreviewHudOpacity
    -> live runtime apply
    -> DO NOT persist

interaction finalization
    -> CommitHudOpacity
    -> final runtime apply
    -> persist
```

PR4 is intentionally limited to this one setting because opacity is not equivalent to the ordinary click-based mutations implemented in PR3.

The final WPF frontend must provide live touch/mouse slider feedback without:

- flooding the single-instance Control Named Pipe with parallel preview requests;
- making a preview value look persisted when it is not;
- losing the final commit after a successful preview;
- allowing stale preview responses to overwrite a newer user-selected value;
- changing any native HUD presentation contract.

At the end of PR4:

- `Background opacity` is interactive;
- the supported values remain `50..100` in `5%` steps;
- slider tracking performs live runtime previews;
- preview requests are serialized and coalesced to the newest pending value;
- finishing the interaction commits the final snapped value exactly once when the interaction changed opacity;
- the runtime response snapshot remains authoritative;
- other HUD controls cannot race an active opacity interaction;
- Intel VRR Range Fix and Start with Windows remain read-only;
- the native runtime, HUD renderer/presentation, Control server, and persistence implementation remain unchanged.

---

## 2. Why PR4 needs a separate interaction model

PR3 established the correct pattern for discrete mutations:

```text
user clicks one setting
  -> disable interactive HUD controls
  -> send one IPC mutation
  -> receive authoritative snapshot
  -> apply full snapshot
  -> re-enable controls
```

That model must **not** be copied literally to a slider drag.

A WPF Slider can produce several values in a short gesture:

```text
70 -> 75 -> 80 -> 85 -> 90
```

The Control pipe is currently:

```text
one pipe instance
one request per connection
one response per connection
client closes
server disconnects/recreates
```

Sending a new Named Pipe operation for every raw `ValueChanged` without serialization would create unnecessary contention and ordering complexity.

Likewise, binding the slider to the existing `IsMutationInFlight` behavior and disabling it while each preview is running would interrupt the user's drag.

Therefore opacity needs a small dedicated interaction coordinator/state machine while continuing to use the same protocol and `RuntimeControlClient` transport.

---

## 3. Current source baseline

### 3.1 WPF slider is currently read-only

Current `MainWindow.xaml` has:

```xml
<StackPanel IsHitTestVisible="False">
    <TextBlock ... Text="Background opacity" />
    <DockPanel>
        <TextBlock Text="{Binding BackgroundOpacityText}" ... />
        <Slider Minimum="50"
                Maximum="100"
                Value="{Binding BackgroundOpacityPercent, Mode=OneWay}"
                TickFrequency="5"
                IsSnapToTickEnabled="True" ... />
    </DockPanel>
</StackPanel>
```

PR4 should enable only this opacity area.

Do not change the established one-page/card geometry merely to implement the slider.

### 3.2 Current ViewModel

`MainViewModel` currently owns:

```text
_snapshot                 latest authoritative runtime snapshot
_mutationInFlight         ordinary discrete HUD mutation guard
AreHudControlsEnabled     snapshot exists && no mutation in flight
```

and projects:

```text
BackgroundOpacityPercent
BackgroundOpacityText
```

from `_snapshot`.

PR4 may refactor this frontend-only state freely because the WPF frontend has not shipped.

Do not add compatibility shims for the PR3 ViewModel shape.

### 3.3 Current Control client

`RuntimeControlClient` already has one shared transport path:

```text
ExecuteAsync
  -> create fresh NamedPipeClientStream
  -> connect
  -> write one request
  -> read one response
  -> decode correlated response
  -> dispose connection
```

Reuse this transport.

Do not create a second pipe client implementation for opacity.

### 3.4 Native opacity contract is already complete

The native runtime already exposes:

```cpp
virtual bool PreviewHudOpacity(float opacity) = 0;
virtual bool CommitHudOpacity(float opacity) = 0;
```

with the explicit contract:

```text
Preview -> live apply without persistence
Commit  -> live apply + persistence
```

`App` currently implements:

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

and:

```cpp
bool App::SetHudOpacity(float opacity, bool persist)
{
    if (!hudController_.SetOpacity(opacity))
        return false;
    if (persist) SaveHudSettings();
    return true;
}
```

Do not change this implementation for PR4.

### 3.5 Existing Win32 Settings semantics

The legacy Settings slider already distinguishes tracking from finalization:

```text
TB_THUMBTRACK
    -> PreviewHudOpacity

all non-TB_THUMBTRACK completion events
    -> CommitHudOpacity
```

The WPF implementation does not need to reproduce Win32 event names, but it must preserve the same product semantics.

---

## 4. Critical semantic rule — preview equality must never suppress commit

This is the most important PR4-specific rule.

A successful preview response contains a fresh runtime snapshot whose opacity already equals the previewed value.

Example:

```text
persisted opacity = 70

user drags to 85
    -> PreviewHudOpacity(85)
    -> runtime applies 85
    -> runtime snapshot says 85
    -> settings.ini is STILL 70
```

At drag completion, this logic would be wrong:

```text
if currentSnapshot.BackgroundOpacityPercent == finalValue
    skip Commit
```

because `currentSnapshot` represents the **current runtime value**, not proof that the value was persisted.

The correct rule is:

> If an opacity interaction changed the value / issued preview work, finalization must send `CommitHudOpacity(finalValue)` exactly once, even if the latest preview response snapshot already reports `finalValue`.

Required regression case:

```text
initial committed/runtime = 70
Preview(85) -> response snapshot = 85
final value = 85
Commit(85) MUST still be sent
```

Do not infer persistence from snapshot equality.

---

## 5. Scope

### 5.1 In scope

PR4 may modify the WPF frontend and its tests to implement:

1. protocol-v1 opacity request payload encoding;
2. `PreviewHudOpacity` response decoding;
3. `CommitHudOpacity` response decoding;
4. typed RuntimeControlClient preview/commit methods;
5. opacity-specific ViewModel interaction state;
6. mouse/touch/keyboard-compatible Slider interaction wiring;
7. preview serialization;
8. latest-value coalescing while a preview request is in flight;
9. final commit ordering;
10. authoritative snapshot reconciliation;
11. interaction exclusion between opacity and other PR3 HUD mutations;
12. automated tests for wire encoding, transport, coalescing, and commit semantics.

### 5.2 Out of scope

Do not implement in PR4:

- `SetIntelVrrRangeFixEnabled`;
- `SetStartWithWindows`;
- `RequestShutdown` from WPF;
- tray launch / production WPF cutover;
- duplicate Settings-process handling;
- VeloPack release packaging;
- .NET prerequisite changes;
- legacy Win32 Settings removal;
- polling;
- runtime event/subscription IPC;
- localization;
- About UI;
- tabs/navigation/ScrollViewer;
- native Control protocol changes;
- native pipe-server changes;
- `IRuntimeControl` changes;
- `App` opacity behavior changes;
- `HudController` opacity behavior changes;
- `HudPresentation` changes;
- renderer changes;
- telemetry/game detection/PresentMon/EC changes.

Cards 4 and 5 remain non-interactive.

---

## 6. HUD / VRR presentation safety — non-negotiable zero-touch boundary

This PR is only a frontend Control IPC consumer change.

Do **not** modify, replace, weaken, or work around:

- HUD `windowExStyle`;
- `WS_EX_TRANSPARENT`;
- `WS_EX_NOACTIVATE`;
- `WS_EX_TOPMOST`;
- existing `WS_EX_LAYERED` behavior;
- `WM_NCHITTEST -> HTTRANSPARENT`;
- `WM_MOUSEACTIVATE -> MA_NOACTIVATE`;
- `ProductionHudPresentationContract()`;
- independent-flip requirements;
- Presentation API / DirectComposition production path;
- premultiplied-alpha behavior.

`Background opacity` remains **background only**.

Do not introduce WPF/native window-wide opacity, DirectComposition visual opacity, `SetLayeredWindowAttributes` changes, renderer alpha-policy changes, or any alternative presentation mechanism.

Expected native HUD/presentation diff: **zero**.

---

## 7. Protocol changes in the C# mirror only

The native protocol already defines:

```text
PreviewHudOpacity = 17
CommitHudOpacity  = 18
```

Both requests carry exactly:

```text
u16 little-endian opacityPercent
```

Valid values:

```text
50
55
60
65
70
75
80
85
90
95
100
```

The C# protocol mirror currently already has the bounds:

```text
MinOpacityPercent  = 50
MaxOpacityPercent  = 100
OpacityStepPercent = 5
```

### 7.1 Extend `ControlRequest`

The current record has:

```csharp
public sealed record ControlRequest(
    ControlOperation Operation,
    uint RequestId,
    bool? Flag = null,
    byte? WireEnum = null,
    int? SizeOffset = null);
```

Extend it cleanly with an opacity field, e.g.:

```csharp
ushort? OpacityPercent = null
```

No compatibility overload/alias is required.

### 7.2 Encoder

Extend the existing general `EncodeRequest(ControlRequest)`.

For both opacity operations:

```text
payload length = 2
payload        = u16 LE percent
```

Reject before transport:

- missing opacity field;
- `< 50`;
- `> 100`;
- values not divisible by `5` within the product range.

Do not encode floating point across IPC.

Do not add a second opacity-specific frame encoder.

### 7.3 Response decoder

The native runtime returns an authoritative settings snapshot for successful:

```text
PreviewHudOpacity
CommitHudOpacity
```

Extend the existing `CarriesSnapshot(...)` classification to include both operations.

Reuse the one existing snapshot decoder.

Do not create a special opacity response DTO.

All existing strict validation remains in force.

---

## 8. RuntimeControlClient additions

Add typed methods equivalent to:

```csharp
Task<ControlClientResult<SettingsSnapshot>> PreviewHudOpacityAsync(
    ushort opacityPercent,
    CancellationToken cancellationToken = default);

Task<ControlClientResult<SettingsSnapshot>> CommitHudOpacityAsync(
    ushort opacityPercent,
    CancellationToken cancellationToken = default);
```

Both must use the existing shared `Snapshot(...)` / `ExecuteAsync(...)` transport path.

Do not introduce:

- persistent pipe connections;
- a second worker transport;
- automatic mutation retry loops;
- parallel writes to the one server instance.

The existing per-call timeout/cancellation behavior stays bounded.

---

## 9. Opacity interaction state model

PR4 needs a small interaction model distinct from ordinary discrete mutation state.

The exact class layout is flexible.

It is acceptable to keep the state in `MainViewModel`, but if the async coalescing logic makes the ViewModel difficult to read, prefer a small frontend-only helper such as:

```text
ViewModels/OpacityInteractionCoordinator.cs
```

Do not create a generic command scheduler/framework.

### 9.1 Required conceptual state

Maintain enough state to distinguish at least:

```text
latest authoritative SettingsSnapshot
ordinary HUD mutation in flight
opacity interaction active
current user gesture value
whether the opacity interaction became dirty
preview request currently in flight
latest pending preview value (optional)
finalization/commit in progress
```

Names may differ.

### 9.2 Ephemeral gesture value is allowed

PR3 correctly forbids requested Settings values from becoming the frontend source of truth.

A Slider is slightly different because its thumb must visually follow the user's finger immediately.

PR4 may therefore maintain a **temporary interaction-only opacity value** while tracking.

This value is permitted only for:

- Slider thumb position during the active gesture;
- the visible `%` label during the active gesture;
- selecting the next preview/final commit value.

It is **not** persisted Settings state and must not replace the runtime snapshot as the frontend authority.

Conceptually:

```text
not interacting:
    slider value = authoritative snapshot opacity

interacting:
    slider value = current snapped finger/keyboard value
    runtime state = latest successful Preview response snapshot

interaction ends:
    Commit(final gesture value)
    -> apply returned authoritative snapshot
    -> discard temporary gesture value
```

This is not an exception allowing optimistic state for other Settings controls.

---

## 10. Preview request policy — serialized latest-value coalescing

Do not send parallel preview requests.

Do not create one queued request for every intermediate WPF `ValueChanged`.

Use a **latest-value wins** preview queue.

Example user gesture:

```text
75
80
85
90
```

while `Preview(75)` is still in flight.

Desired behavior:

```text
Preview(75) begins
80 arrives -> pending = 80
85 arrives -> pending = 85
90 arrives -> pending = 90
Preview(75) completes
Preview(90) begins
```

`80` and `85` do not need their own pipe round trip.

This preserves live preview while avoiding stale queue buildup.

### 10.1 At most one preview IPC in flight

Invariant:

```text
number of concurrent PreviewHudOpacity IPC calls <= 1
```

### 10.2 Only valid snapped values enter the queue

The UI already uses:

```text
Minimum = 50
Maximum = 100
TickFrequency = 5
IsSnapToTickEnabled = True
```

Still validate/normalize the interaction value before calling the protocol layer.

The protocol encoder remains the final validation boundary.

### 10.3 Avoid duplicate previews

If the latest pending value is identical to:

- the preview value currently in flight; or
- the most recently successful preview value,

there is no need to enqueue another identical preview.

This duplicate suppression applies to **Preview only**.

It must never be used to suppress the required final `Commit` described in section 4.

---

## 11. Finalization / commit ordering

When the user finishes one opacity interaction:

1. stop accepting additional values for that interaction;
2. capture the final snapped slider value;
3. mark finalization in progress;
4. discard stale queued intermediate previews;
5. allow any already-in-flight preview request to complete;
6. send `CommitHudOpacity(finalValue)` exactly once if the interaction was dirty;
7. wait for the commit response;
8. on success, apply the **whole returned snapshot**;
9. clear temporary gesture state;
10. restore normal control availability.

Do not send the final Commit in parallel with an older Preview request.

### 11.1 Do not require a final Preview first

If the last pointer value arrived while an older preview was still running, it is acceptable to discard the pending preview and go directly to:

```text
wait current preview
-> Commit(finalValue)
```

because Commit also applies the final runtime opacity before persisting it.

The final value does not need two round trips.

### 11.2 Dirty interaction

If an interaction begins and ends without any actual opacity change, it may complete without sending Preview or Commit.

Once the gesture changes opacity, however, final Commit is required even if the final value later matches a preview response snapshot.

---

## 12. Authoritative snapshot handling during preview

Every successful Preview response still contains an authoritative runtime snapshot.

Use it.

However, while the user is still actively dragging, applying the response must not yank the visible Slider thumb backward to an older preview value while the finger has already moved ahead.

Recommended model:

```text
_snapshot = successful Preview response snapshot

if opacity interaction active:
    visible slider/value text = temporary latest gesture value
else:
    visible slider/value text = _snapshot.BackgroundOpacityPercent
```

Other projected fields may continue to come from `_snapshot`.

Because PR4 blocks other HUD mutations during opacity tracking, no legitimate frontend mutation should be changing other snapshot fields concurrently.

Do not add polling/event infrastructure to solve external out-of-band changes in this PR.

---

## 13. Failure behavior

### 13.1 Preview failure

If a preview returns:

```text
ProtocolError
MalformedResponse
TransportUnavailable
TimedOut
```

then:

- do not fabricate a snapshot;
- retain the last successfully known authoritative snapshot;
- keep the gesture responsive;
- do not launch parallel retries;
- allow finalization to make one normal Commit attempt for the final value.

If transport is gone, that Commit may also fail; it remains bounded.

### 13.2 Commit failure

If final Commit fails:

- do not claim that the final gesture value was persisted;
- restore the visible Slider/value text to the last authoritative runtime snapshot known to the frontend;
- clear interaction/finalization state;
- re-enable controls;
- keep the window responsive.

If the last successful operation was a Preview, that snapshot may contain the live preview value even though persistence still contains the previous committed value. That is acceptable because it is the last known **runtime** state.

Do not invent a persistence-success state.

A separate automatic retry/reconciliation loop is not required.

### 13.3 Exceptions

No `.Result`, `.Wait()`, dispatcher blocking, or unhandled async exception may be introduced.

---

## 14. Interaction exclusion with PR3 mutations

Opacity tracking must not race the discrete HUD mutations introduced in PR3.

Use simple mutual exclusion at the ViewModel/interaction level.

### 14.1 While an ordinary HUD mutation is in flight

Do not start a new opacity interaction.

The opacity slider should be disabled until that mutation completes.

### 14.2 While opacity interaction/finalization is active

Disable the other interactive HUD controls in cards 1-3.

The opacity Slider itself must remain enabled during preview IPC activity so the user's drag is not interrupted.

This means PR3's current single property:

```text
AreHudControlsEnabled = snapshot exists && !mutationInFlight
```

may need to be split/refined, for example conceptually:

```text
AreDiscreteHudControlsEnabled
IsOpacityControlEnabled
```

Exact names are implementation choice.

Do not disable the whole window.

Close/window chrome must remain usable.

---

## 15. WPF event semantics

The exact event wiring may use code-behind because this is a small single-page utility.

Do not add a third-party MVVM/event framework solely for Slider gestures.

The implementation must recognize:

- pointer/touch tracking;
- final pointer/touch release;
- keyboard changes when the Slider has focus;
- direct track interactions as reasonably supported by the stock WPF Slider.

A practical implementation may use a named Slider plus WPF routed input / Thumb drag events, e.g. some combination of:

```text
Slider.ValueChanged
Thumb.DragStarted
Thumb.DragCompleted
PreviewMouseLeftButtonDown / Up
PreviewTouchDown / Up
PreviewKeyDown / Up
```

The exact event set is not prescribed.

What matters is externally observable behavior:

```text
continuous tracking -> preview semantics
interaction completion -> one commit
```

### 15.1 Programmatic binding updates must not emit mutations

Applying an authoritative snapshot changes the bound Slider value.

That programmatic update must **not** be interpreted as new user input and must not send Preview/Commit.

Include an explicit guard/design for this.

Do not rely on accidental current WPF event ordering.

---

## 16. Touch requirements

The MSI Claw primary target remains:

```text
1920 x 1200
150% scale
```

The opacity control must be comfortably usable by touch.

Current Slider `MinHeight="28"` was sufficient only as a read-only visual placeholder.

PR4 may increase the Slider/Thumb effective touch area without changing overall card/window geometry materially.

Target a practical touch hit area around the previously agreed `40-44 DIP` interactive-control scale.

Do not create a custom rendering framework.

A small local WPF style/template adjustment is acceptable if the stock Fluent Slider touch target is demonstrably too small.

The no-scroll, fixed-size window requirement remains.

---

## 17. Background-only opacity contract

The frontend label `Background opacity` is literal.

PR4 must only send the existing runtime background-opacity control operation.

Do not implement the feature by changing:

```text
WPF window Opacity
HUD window-wide alpha
DirectComposition visual opacity
text/foreground opacity
separator opacity
outline opacity
```

No frontend code should know how the HUD renderer realizes background opacity internally.

---

## 18. Tests

Extend the existing `ClawHUD.Settings.Tests` suite.

### 18.1 Protocol encoder tests

Add golden/field tests for:

```text
PreviewHudOpacity(50)
PreviewHudOpacity(75)
PreviewHudOpacity(100)
CommitHudOpacity(50)
CommitHudOpacity(75)
CommitHudOpacity(100)
```

At least one should assert the entire exact frame, including:

```text
operation id 17 or 18
payloadSize = 2
u16 little-endian opacity payload
```

Reject:

```text
49
53
101
missing OpacityPercent
```

### 18.2 Response decode tests

Both:

```text
PreviewHudOpacity Ok
CommitHudOpacity Ok
```

must decode the existing authoritative `SettingsSnapshot` payload.

Non-Ok response must surface typed error and no fabricated snapshot.

### 18.3 RuntimeControlClient pipe tests

Use the existing local message-pipe test style.

Verify at least:

- Preview sends operation `17` and correct u16 payload;
- Commit sends operation `18` and correct u16 payload;
- returned snapshot is surfaced unchanged.

### 18.4 Interaction/coalescing tests

Use a fake/gated client to prove serialization.

Required scenario:

```text
begin at 70
user -> 75     Preview(75) held in flight
user -> 80     pending=80
user -> 85     pending=85
user -> 90     pending=90
release Preview(75)

expected:
- no parallel preview call
- stale 80/85 are not individually dispatched
- next preview is at most latest 90, OR finalization may go directly to Commit(90)
- final Commit(90) exactly once
```

Either allowed implementation path is acceptable as long as stale preview queue buildup cannot occur.

### 18.5 Mandatory preview-equality / commit regression

Required test:

```text
initial snapshot opacity = 70
Preview(85) succeeds
Preview response snapshot opacity = 85
interaction ends at 85

assert Commit(85) was still sent exactly once
```

This is a merge-critical PR4 test.

### 18.6 Authoritative commit rollback test

Example:

```text
final requested commit = 85
runtime Commit response snapshot = 80
```

After finalization:

```text
slider = 80
label  = 80%
```

The requested 85 must not remain displayed as authoritative state.

### 18.7 Commit failure test

Example:

```text
last successful Preview snapshot = 80
final Commit(85) -> OperationFailed / transport failure
```

After interaction cleanup:

```text
slider returns to last known authoritative 80
interaction not active
other controls re-enabled
```

### 18.8 Programmatic snapshot update test

`ApplySnapshot(...)` changing opacity must not call Preview or Commit.

### 18.9 Mutual exclusion tests

Verify:

- ordinary PR3 mutation in flight -> opacity interaction does not start;
- active opacity interaction -> discrete HUD mutation does not dispatch;
- preview in flight does not disable/cancel the Slider itself;
- interaction completion restores normal controls.

---

## 19. Manual smoke validation

On a Claw device / Windows runtime:

### 19.1 Initial state

1. Start normal `ClawHUD.exe`.
2. Open the WPF Settings executable manually during development.
3. Confirm slider and `%` text match current runtime snapshot.

### 19.2 Mouse drag

1. Drag slowly through multiple 5% values.
2. Confirm HUD background changes live.
3. Confirm HUD text/foreground does not fade with background opacity.
4. Release.
5. Close/reopen Settings or inspect persisted settings through the existing product path.
6. Confirm final value was committed.

### 19.3 Touch drag

At `1920x1200 @ 150%`:

1. Move slider by touch.
2. Confirm the thumb can be acquired comfortably.
3. Confirm movement remains responsive while IPC previews occur.
4. Confirm final release commits.

### 19.4 Fast drag

Drag quickly from one end toward the other.

Confirm:

- UI does not freeze;
- preview does not produce a long delayed replay of every crossed value after release;
- final background state matches the released value;
- final commit wins.

### 19.5 Other HUD controls during drag

While opacity interaction is active:

- other HUD controls should be unavailable;
- window Close remains usable;
- Slider remains operable.

### 19.6 Runtime unavailable

Stop ClawHUD while WPF Settings remains open, then interact with opacity.

Confirm:

- no crash;
- no permanent UI hang;
- operation returns within the existing bounded timeout;
- Settings remains closable.

---

## 20. Files expected to change

Likely frontend changes:

```text
src/ClawHUD.Settings/MainWindow.xaml
src/ClawHUD.Settings/MainWindow.xaml.cs
src/ClawHUD.Settings/Protocol/ControlProtocol.cs
src/ClawHUD.Settings/Protocol/ControlCodec.cs
src/ClawHUD.Settings/Services/RuntimeControlClient.cs
src/ClawHUD.Settings/ViewModels/MainViewModel.cs
```

Optional if it keeps the ViewModel simpler:

```text
src/ClawHUD.Settings/ViewModels/OpacityInteractionCoordinator.cs
```

Tests likely:

```text
tests/ClawHUD.Settings.Tests/ControlCodecTests.cs
tests/ClawHUD.Settings.Tests/RuntimeControlClientTests.cs
tests/ClawHUD.Settings.Tests/MainViewModelTests.cs
FakeRuntimeControlClient.cs or equivalent test helper
```

Native source changes are not expected.

Do not modify CMake/Control server/HUD presentation simply because opacity is now interactive in WPF.

---

## 21. CI / verification

Required before PR completion:

```text
dotnet build src/ClawHUD.Settings/ClawHUD.Settings.csproj --configuration Release

dotnet test tests/ClawHUD.Settings.Tests/ClawHUD.Settings.Tests.csproj --configuration Release
```

Existing GitHub Actions native Release build and CTest must remain green.

No Release/VeloPack packaging change is required in PR4.

No self-contained .NET publish change is required.

---

## 22. Completion criteria

PR4 is complete only when all of the following are true:

- [ ] Background opacity Slider is interactive.
- [ ] Valid values remain exactly `50..100`, step `5`.
- [ ] C# protocol encoder supports `PreviewHudOpacity` u16 payload.
- [ ] C# protocol encoder supports `CommitHudOpacity` u16 payload.
- [ ] Invalid opacity values are rejected before transport.
- [ ] Preview/Commit successful responses decode the standard authoritative snapshot.
- [ ] RuntimeControlClient exposes typed Preview/Commit methods using the existing transport.
- [ ] Tracking produces live preview behavior.
- [ ] Preview IPC calls are never parallel.
- [ ] Intermediate preview values are latest-value coalesced rather than fully queued.
- [ ] Final commit waits for any older preview in flight.
- [ ] Dirty interaction sends exactly one final Commit.
- [ ] Commit is **not** suppressed because a Preview response snapshot already equals the final value.
- [ ] Programmatic snapshot application sends no opacity mutation.
- [ ] During drag, the thumb/label can remain responsive using temporary interaction state.
- [ ] After final Commit success, the whole returned snapshot replaces projected runtime state.
- [ ] Commit rollback/failure does not leave requested local state displayed as authoritative.
- [ ] Other HUD mutations cannot race an opacity interaction.
- [ ] Slider remains enabled while its own preview IPC is in flight.
- [ ] Intel VRR / Start with Windows remain read-only.
- [ ] No horizontal/vertical scrollbar is introduced.
- [ ] Touch usability is validated at `1920x1200 @ 150%`.
- [ ] No native HUD/presentation contract is changed.
- [ ] Background opacity still affects background only.
- [ ] WPF Settings Release build passes.
- [ ] WPF Settings tests pass.
- [ ] Existing native Build/CTest remains green.

---

## 23. Explicitly deferred after PR4

The planned remaining sequence is:

```text
PR5
  Intel VRR Range Fix mutation
  Start with Windows mutation
  final Settings-state interaction cleanup

PR6
  tray -> WPF Settings production launch
  one Settings process / bring-existing-window behavior
  framework-dependent VeloPack packaging
  Settings close/lifetime finalization
  real tray Exit behavior verification
  legacy Win32 Settings removal
```

Do not pull those changes into PR4 merely because the WPF page is now almost fully interactive.
