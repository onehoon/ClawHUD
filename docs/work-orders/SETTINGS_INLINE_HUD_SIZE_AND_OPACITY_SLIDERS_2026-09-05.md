# Settings UI Work Order — Inline HUD Size and HUD Opacity Sliders

Date: 2026-09-05  
Repository: `onehoon/ClawHUD`  
Baseline: `main` at `4c7068ff06db8013d96c0ea0cb85e6c29b326940` (`Fix startup re-enable, Settings lifetime, and HUD size stepper (#234)`)

## 1. Objective

Replace the current HUD-size `− / Default / +` stepper with a compact stepped slider and move both **HUD size** and **HUD opacity** to the same single-row layout.

The target presentation is:

```text
HUD size                         Default   [------●------]
...
HUD opacity                       100%     [----------●--]
```

The exact spacing can be tuned in XAML, but the two rows must share the same visual grammar:

- setting label on the left;
- current value on the right-side control group;
- slider immediately to the right of the current value;
- identical slider width, vertical alignment, and touch target;
- no second line of ticks/labels;
- no increase to the 600 × 600 Settings window height;
- no scrollbar.

This is a focused Settings-frontend UI/interaction PR. Do not combine unrelated cleanup or runtime changes.

---

## 2. Current implementation confirmed on `main`

Relevant files:

```text
src/ClawHUD.Settings/MainWindow.xaml
src/ClawHUD.Settings/MainWindow.xaml.cs
src/ClawHUD.Settings/ViewModels/MainViewModel.cs
src/ClawHUD.Settings/Styles/SettingsStyles.xaml
src/ClawHUD.Settings/Protocol/ControlProtocol.cs
tests/ClawHUD.Settings.Tests/MainViewModelTests.cs
```

Current behavior:

- HUD size is a `− / value / +` stepper.
- HUD size range is `-2..+2`.
- `0` is displayed as `Default`.
- HUD opacity is already a WPF `Slider` with range `50..100`, 5% steps, snap-to-tick, live preview while dragging, and one commit on release.
- HUD opacity currently occupies two vertical lines: label first, then slider/value below it.
- Settings uses WPF `ThemeMode="System"`; the existing standard `Slider` already participates in the current system/Fluent visual theme.
- PR #234 added custom `StepperButton`, `DecreaseStepperButton`, and `IncreaseStepperButton` styles only for HUD size.

---

## 3. Product requirements

### 3.1 HUD size row

Replace the current stepper with a WPF `Slider`.

Required slider contract:

```text
Minimum               -2
Maximum               +2
TickFrequency          1
IsSnapToTickEnabled    true
IsMoveToPointEnabled   true
```

The user must only be able to select these five values:

```text
-2  -1  0  +1  +2
```

Display text remains:

```text
-2
-1
Default
+1
+2
```

Do not add tick labels below the slider. The current value text is sufficient and keeps the row compact.

### 3.2 HUD opacity row

Keep the product name exactly:

```text
HUD opacity
```

Do **not** rename it to `Background opacity`.

The existing HUD-opacity behavior is intentional and must not be redefined by this PR. This work order changes only the Settings layout/style and preserves the existing Control IPC semantics.

Keep the existing opacity contract exactly:

```text
Minimum               50
Maximum               100
TickFrequency          5
IsSnapToTickEnabled    true
IsMoveToPointEnabled   true
```

Preserve:

- live `PreviewHudOpacity` while thumb-dragging;
- one `CommitHudOpacity` at drag completion;
- keyboard/track-click commit behavior;
- current authoritative snapshot reconciliation;
- current failure/runtime-loss behavior.

### 3.3 Shared inline-slider layout

Both rows must use the same right-side control geometry.

Recommended starting dimensions for the current 600 DIP window:

```text
current-value column:  55–65 DIP fixed width
slider width:          180–200 DIP
value/slider gap:      8–12 DIP
slider hit area:       at least ~30 DIP high
```

Exact values may be tuned visually, but the final requirements are:

1. HUD-size and HUD-opacity slider tracks start at the same X coordinate.
2. Both slider tracks have the same width.
3. Current-value text is centered/right-aligned consistently and does not cause slider movement when changing `Default` ↔ `+2` etc.
4. Controls are vertically centered in the row.
5. Card width/padding remain consistent with the rest of Settings.
6. Do not increase the fixed Settings window size.
7. Do not introduce scrolling.
8. Preserve comfortable touch interaction at the 1920 × 1200 / 150% target environment.

A reusable style such as the following is preferred over duplicating dimensions:

```xaml
<Style x:Key="InlineSettingSlider" TargetType="Slider">
    <Setter Property="Width" Value="190" />
    <Setter Property="MinHeight" Value="30" />
    <Setter Property="VerticalAlignment" Value="Center" />
</Style>
```

Do **not** create a bespoke slider ControlTemplate unless the built-in themed WPF Slider demonstrably cannot match the existing Settings UI. The current WPF system theme should be reused first.

---

## 4. HUD-size slider interaction contract

HUD size is a discrete runtime setting, not an opacity-style preview endpoint. Do not generate a flood of `SetHudSizeOffset` IPC calls while the thumb is being dragged.

### 4.1 Thumb drag

Required behavior:

```text
DragStarted
  -> begin local HUD-size interaction
  -> disable other Settings mutations

ValueChanged while dragging
  -> snap locally to -2/-1/0/+1/+2
  -> move thumb
  -> update current value label
  -> NO IPC yet

DragCompleted
  -> if target differs from authoritative snapshot:
       send exactly one SetHudSizeOffset(target)
     else:
       send nothing
  -> apply returned authoritative snapshot
  -> end interaction
```

If the runtime returns a different value, rejects the request, or the mutation fails, the UI must reconcile back to the last/returned authoritative snapshot exactly as the existing mutation model does.

### 4.2 Track click and keyboard

When there is no active thumb drag:

- track click may select the snapped target and send one `SetHudSizeOffset` mutation;
- keyboard Left/Right should change by exactly one step and send one mutation;
- Home/End, if naturally supported by WPF, must still end on a valid `-2` or `+2` value and produce at most one accepted mutation for that user action;
- never send an out-of-range value.

### 4.3 Interaction exclusion

Preserve the existing rule that Settings mutations do not overlap.

While a HUD-size drag is active:

- the HUD-size slider itself remains usable until drag completion;
- discrete controls are disabled;
- HUD-opacity interaction cannot begin;
- activation refresh must not overwrite the in-progress size gesture.

While any other discrete mutation, activation refresh, or HUD-opacity interaction is active:

- a new HUD-size interaction cannot begin.

Do not solve this by accepting clicks and silently dropping them after WPF has shown an interaction.

A minimal ViewModel state is sufficient; a large generic interaction framework is not required. For example, an implementation equivalent to the following is acceptable:

```text
HudSizeGestureActive
HudSizeGestureValue
SliderHudSizeValue
BeginHudSizeInteraction()
UpdateHudSizeGesture(...)
EndHudSizeInteractionAsync()
ChangeHudSizeAsync(...)
```

Exact names are not mandatory.

---

## 5. Programmatic-update guard

The HUD-size slider must follow authoritative snapshots without mistaking programmatic updates for user input.

Mirror the proven opacity-slider pattern in `MainWindow.xaml.cs`:

```text
ViewModel snapshot changes
  -> set suppress flag
  -> assign HudSizeSlider.Value
  -> clear suppress flag

HudSizeSlider.ValueChanged
  -> if suppress flag: return
  -> otherwise treat as user gesture/input
```

Do not use TwoWay binding that directly mutates ViewModel state independently of runtime authority.

The runtime-returned `SettingsSnapshot` remains the source of truth.

---

## 6. ViewModel changes

Refactor the existing HUD-size stepper projection only as needed for slider interaction.

The following existing members become obsolete once no button consumes them:

```text
CanDecreaseHudSizeByRange
CanIncreaseHudSizeByRange
CanDecreaseHudSize
CanIncreaseHudSize
StepHudSizeAsync(...)
```

Remove them if there are no remaining production consumers. Do not preserve dead stepper-specific properties solely for old tests.

Keep/reuse:

```text
HudSizeOffset
HudSizeLabel / FormatSizeOffset(...)
```

If the label follows the local drag gesture, `HudSizeLabel` should project the gesture value while dragging and the authoritative snapshot value otherwise.

Example behavior:

```text
authoritative = 0
user drags thumb to +2
  label immediately shows "+2"
  no IPC during drag
release
  one SetHudSizeOffset(+2)
runtime returns +2
  authoritative state becomes +2
```

If runtime instead returns `0`, the thumb and label return to `Default`.

---

## 7. XAML/layout changes

### 7.1 Card 2 — HUD size

Replace this conceptual structure:

```text
HUD size                      [−] Default [+]
```

with:

```text
HUD size                      Default [------●------]
```

The HUD-size row should be a single `DockPanel` or `Grid` row with a fixed-width right-side control group.

Do not move the slider below the `HUD size` label.

Font and Alignment controls remain below it as they are today.

### 7.2 Card 3 — HUD opacity

Replace the current two-line structure:

```text
HUD opacity
[---------------- slider ----------------] 100%
```

with the same single-row pattern as HUD size:

```text
HUD opacity                     100% [------●------]
```

`Background width` remains unchanged above it.

This should reduce or preserve vertical density; it must not increase Settings window height.

### 7.3 Remove obsolete stepper resources

If no longer used anywhere, delete:

```text
StepperButton
DecreaseStepperButton
IncreaseStepperButton
```

from `SettingsStyles.xaml`.

Also remove the old plus/minus `Path` geometries and click handlers from `MainWindow.xaml` / `.xaml.cs`.

Do not add an icon package just for this change.

---

## 8. Do not change

This PR is Settings UI only. Do **not** modify:

- native HUD renderer;
- HUD sizing algorithm itself;
- HUD opacity renderer/compositor behavior;
- opacity meaning or range;
- native Control IPC protocol numbers or wire format;
- persistence format/defaults;
- game detection;
- telemetry;
- startup registration;
- Settings runtime-PID lifetime watcher from PR #234;
- Settings single-instance behavior.

### 8.1 HUD Presentation / VRR safety contract — mandatory

There must be zero change to:

- HUD `windowExStyle`;
- `WS_EX_TRANSPARENT`;
- `WS_EX_NOACTIVATE`;
- `WS_EX_TOPMOST`;
- existing `WS_EX_LAYERED` behavior;
- `WM_NCHITTEST -> HTTRANSPARENT`;
- `WM_MOUSEACTIVATE -> MA_NOACTIVATE`;
- `ProductionHudPresentationContract()`;
- independent-flip requirement;
- Presentation API / DirectComposition production presentation path;
- premultiplied-alpha presentation contract.

Do not use this UI change as a reason to alter the native HUD alpha/presentation path.

---

## 9. Tests

Update `tests/ClawHUD.Settings.Tests/MainViewModelTests.cs` for slider semantics rather than button-stepper semantics.

At minimum cover:

### 9.1 Projection

```text
-2 -> "-2"
-1 -> "-1"
 0 -> "Default"
+1 -> "+1"
+2 -> "+2"
```

### 9.2 Drag behavior

- begin at authoritative `0`;
- drag locally through multiple values;
- verify no `SetHudSizeOffset` IPC occurs before completion;
- complete at `+2`;
- verify exactly one `SetHudSizeOffset` call;
- verify returned snapshot becomes authoritative.

### 9.3 No-op drag

- begin at `0`;
- move away and back to `0`;
- release;
- verify no size mutation is sent.

### 9.4 Runtime rollback

- authoritative size `0`;
- user commits `+1`;
- runtime returns snapshot size `0`;
- final ViewModel value/label must be `0 / Default`.

### 9.5 Interaction exclusion

Verify HUD-size drag prevents:

- another discrete mutation;
- HUD-opacity interaction;
- activation refresh from overwriting the gesture.

Verify an existing opacity interaction or discrete mutation prevents a new HUD-size interaction.

### 9.6 Keyboard/track-style direct change

A non-drag target change should:

- snap to a legal integer offset;
- send one mutation;
- never exceed `-2..+2`.

### 9.7 Existing opacity tests

All existing opacity preview/commit tests must remain green. This PR must not alter opacity IPC behavior.

### 9.8 Existing regression suites

Run the existing Settings test project and all normal repository test/build gates required by the repo.

HUD presentation/VRR regression tests must remain unchanged and passing.

---

## 10. Manual UI acceptance check

Manual device/UI checking is useful for appearance but is **not itself a merge blocker**; the owner will perform final on-device visual verification.

Expected visual result on the target 1920 × 1200 / 150% environment:

1. Settings remains 600 × 600 DIP and does not scroll.
2. HUD size is one horizontal row.
3. HUD opacity is one horizontal row.
4. The two value columns align.
5. The two slider tracks align and have the same width.
6. `Default`, `-2`, `+2`, and `100%` do not shift the slider position.
7. HUD-size thumb is easy to drag by touch.
8. HUD size snaps cleanly to exactly five positions.
9. HUD opacity keeps its existing 5% snap behavior.
10. No button flash remains because the old `−/+` controls no longer exist.
11. No card or control is clipped.

---

## 11. Expected implementation footprint

Likely modified files:

```text
src/ClawHUD.Settings/MainWindow.xaml
src/ClawHUD.Settings/MainWindow.xaml.cs
src/ClawHUD.Settings/ViewModels/MainViewModel.cs
src/ClawHUD.Settings/Styles/SettingsStyles.xaml
tests/ClawHUD.Settings.Tests/MainViewModelTests.cs
```

A small dedicated HUD-size interaction helper/test file is acceptable if it genuinely simplifies the ViewModel, but do not introduce a broad new framework for a five-position slider.

Native ClawHUD files should not need modification.

---

## 12. Definition of done

The PR is complete when:

- HUD-size `−/+` stepper is removed;
- HUD size uses a five-position snapped slider on the right side of its label;
- HUD opacity is moved into the same inline value + slider layout;
- both sliders are visually aligned and equal width;
- window remains fixed 600 × 600 with no scrolling;
- HUD-size drag performs one IPC mutation at release rather than repeated mutations during drag;
- direct keyboard/track changes work and stay in range;
- authoritative runtime reconciliation remains intact;
- opacity preview/commit semantics are unchanged;
- obsolete stepper resources/handlers/tests are removed;
- Settings automated tests pass;
- existing HUD presentation/VRR invariants are untouched.