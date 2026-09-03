# Cleanup 1 — EC Elevated Helper Lifetime and UAC Retry Hardening Work Order

> **Repository:** `onehoon/ClawHUD`  
> **Series:** Post-CH-RTF ClawHUD Standalone Cleanup  
> **Cleanup split:** 1 / 3  
> **Analyzed main HEAD:** `2c7212ad3a435d4c7197cefd7bdafcee26a78eee`  
> **Scope:** Keep the current elevated read-only EC helper architecture, but remove unnecessary re-elevation and automatic repeated UAC prompts across normal HUD sampling lifecycle transitions and helper failures  
> **Status:** Ready for implementation

---

## 1. Objective

The current MSI EC read path has one product constraint that is now settled:

```text
MSI EC reads require administrator privilege on the supported hardware.
```

Therefore this cleanup must **not** try to make production EC telemetry unelevated.

The existing privilege boundary remains correct in principle:

```text
ClawHUD.exe
    normal-user process
        ↓ private named pipe
ClawHUD.EcHelper.exe
    launched with runas / elevated
        ↓
read-only MSI WMI / EC access
```

The problem is lifecycle, not privilege.

Today, the helper is coupled too tightly to the production sampling session. `ProductionTelemetryController::ResetSamplingState()` closes and destroys `ecClient_`, and `EcHelperClient::Close()` resets `attempted_`.

This creates realistic repeated-elevation paths:

```text
In-game only
    game foreground -> HUD visible -> EC helper UAC -> connected
    leave game       -> HUD hidden  -> ResetSamplingState -> helper released
    enter game again -> HUD visible -> a new helper is launched -> another UAC
```

and:

```text
system suspend
    -> sampling reset
    -> helper released
resume
    -> sampling restarts
    -> another UAC
```

and on failure:

```text
first EC sample
    -> runas
    -> user cancels UAC / helper cannot launch / pipe session fails
    -> Close() resets attempted_=false
next 1-second EC sample
    -> launch attempted again
    -> another UAC / another failed launch
```

The target behavior is:

```text
Normal ClawHUD process lifetime
    -> first production EC need may request elevation once
    -> connected elevated helper may remain idle while sampling is temporarily paused
    -> normal HUD hide/show and suspend/resume do not re-prompt
    -> automatic failure recovery must never create a UAC prompt loop

Explicit HUD disable
    -> release the elevated helper

Explicit HUD re-enable
    -> a new EC helper lifetime may begin
    -> one new elevation request is acceptable because the user explicitly re-enabled HUD telemetry

App shutdown / update shutdown
    -> release helper
```

This PR is a lifecycle cleanup only. It must not redesign EC transport or privilege architecture.

---

## 2. Current production behavior

### 2.1 Production sampling starts EC synchronously

`ProductionTelemetryController::StartBaseSampling()` currently does:

```cpp
samplingActive_ = true;
SampleSystemEc();
SetTimer(messageWindow_, kEcHudTimerId, 1000, nullptr);
SampleBattery();
...
```

The first `SampleSystemEc()` reaches `ReadEcTelemetry()`, which lazily constructs `EcHelperClient` and performs the first helper connection.

Therefore the first visible production HUD session is also the first normal elevation point.

Keep that lazy behavior. Do not move elevation to installer startup, application startup, or tray startup in this PR.

### 2.2 Helper launch requires elevation

Current `EcHelperClient::StartHelper()` uses:

```cpp
info.lpVerb = L"runas";
info.lpFile = helper.c_str();
ShellExecuteExW(&info);
```

This is required and remains production behavior.

Do not remove `runas`.

Do not elevate `ClawHUD.exe` itself.

### 2.3 The helper is already narrow and read-only

The helper protocol permits only:

```text
Get_Temperature(0)
Get_Fan(0)
Get_Data(70)
Get_Data(71)
Get_Data(74)
Get_Data(75)
Get_Data(221)
```

The elevated helper must stay limited to this whitelist.

Do not add write/control operations.

### 2.4 Sampling reset currently destroys helper ownership

Current `ProductionTelemetryController::ResetSamplingState()` includes:

```cpp
if (ecClient_)
{
    ecClient_->Close();
    ecClient_.reset();
}
```

But this reset is used for several very different reasons:

```text
hud-hidden
suspend
hud-disabled
app-shutdown
```

Those reasons must no longer have identical EC-helper lifetime semantics.

### 2.5 `Close()` currently re-arms automatic launch

Current `EcHelperClient::Close()` ends with:

```cpp
attempted_ = false;
```

Internal failure paths also call `Close()`.

As a result, a failed launch/connection is immediately eligible for another automatic `runas` attempt on the next EC sample.

The existing runtime log suppression can make repeated attempts look quieter in logs, but it does not itself prevent the actual launch attempt.

---

## 3. Non-negotiable rules

1. EC access remains administrator-only through `ClawHUD.EcHelper.exe`.
2. `ClawHUD.exe` remains a normal-user process.
3. Keep the existing private per-process named-pipe design.
4. Keep the EC helper protocol version and read-only whitelist unchanged.
5. Do not install a Windows service in this PR.
6. Do not create a scheduled task, persistent broker, shared EC daemon, or cross-application helper.
7. Do not add privilege escalation to the main ClawHUD process.
8. Do not add retry timers, exponential-backoff frameworks, generic connection-state machines, or service-manager abstractions.
9. Do not poll EC while HUD production telemetry is inactive.
10. Do not change PresentMon, game detection, HUD rendering, HUD opacity, HUD presentation, Control IPC, or Managed/Standalone launch composition.
11. Preserve all VRR-critical presentation contracts unchanged.
12. A helper that remains alive while sampling is paused must remain idle: no hidden/background EC polling.

---

## 4. Required EC helper lifetime model

Separate these two concepts explicitly:

```text
production sampling lifetime
EC elevated helper lifetime
```

They are not the same thing.

### 4.1 Transient sampling stop — preserve helper

The following are temporary sampling gates and must **not** release a healthy elevated helper:

```text
HUD hidden because In-game only has no eligible foreground game
F8/manual visibility hide while HUD remains enabled
system suspend / resume-recovery sampling pause
ordinary visibility reconciliation that stops production sampling temporarily
```

Required behavior:

```text
sampling timers stop
telemetry snapshots / estimator state may reset exactly as needed today
EC helper connection stays owned by ProductionTelemetryController
helper performs no reads while sampling is stopped
sampling restarts later
same helper connection is reused
no new UAC prompt
```

The important invariant is:

> `ResetSamplingState()` must no longer automatically mean `release EC elevation`.

### 4.2 Explicit HUD disable — release helper

When the user explicitly turns **Enable HUD** off:

```text
StopHud()
    -> stop production sampling
    -> stop EC polling
    -> release EC helper
    -> elevated helper exits when its private pipe closes
```

This is an explicit user action and is a reasonable boundary for ending the elevated helper lifetime.

If the same user later explicitly enables HUD again in the same ClawHUD process:

```text
Enable HUD
    -> production telemetry becomes active when visible
    -> a new helper lifetime may start
    -> one new UAC elevation request is acceptable
```

Do not preserve an elevated helper indefinitely after the user explicitly disables the HUD.

### 4.3 App shutdown / update shutdown — release helper

Normal app exit, `RequestShutdown`, and VeloPack-driven runtime shutdown must release the helper before ClawHUD fully exits.

Do not rely only on process termination to kill the helper.

The current private-pipe ownership rule remains:

```text
main process closes pipe
-> helper read loop terminates
-> helper shuts down MSI reader and exits
```

### 4.4 Suspend/resume — preserve helper, pause polling

Suspend currently calls:

```cpp
productionTelemetry_.StopSamplingTimersAndFps();
...
productionTelemetry_.ResetSamplingState(L"suspend");
```

After this cleanup:

```text
suspend
    -> no EC polling
    -> preserve existing helper ownership/connection object

resume
    -> resume normal sampling
    -> reuse same helper when still connected
```

If Windows suspend/resume invalidates the pipe/helper in practice, the first post-resume read may fail normally. That failure must follow the automatic-relaunch suppression rules below; do not pop repeated UAC dialogs trying to self-heal.

---

## 5. Required failure / UAC retry behavior

### 5.1 Automatic launch attempts are sticky within one `EcHelperClient` lifetime

Once an `EcHelperClient` has attempted to establish its elevated helper session, a failed automatic attempt must not immediately re-arm itself just because transport handles are closed.

Conceptually separate:

```text
close transport/resources
```

from:

```text
allow a brand-new elevated launch attempt
```

Recommended shape:

```cpp
void CloseTransport() noexcept;   // close pipe/process handles, keep attempted_ state
void Close() noexcept;            // explicit lifetime release; may reset full session state
```

Exact names may differ.

The important rule is that **internal failure cleanup must use the preserve-attempt path**, not the full explicit-reset path.

### 5.2 UAC cancellation

When `ShellExecuteExW(..., L"runas", ...)` fails with `ERROR_CANCELLED`:

```text
record helper launch failure
close transient pipe resources
keep this EcHelperClient launch attempt consumed
return EC unavailable
next 1-second sample
    -> no new runas call
    -> no new UAC prompt
```

The application continues normally with EC-derived values unavailable.

Do not exit ClawHUD because the user declined elevation.

### 5.3 Other helper launch failures

The same no-loop policy applies to failures such as:

```text
helper executable missing
runas launch failure
helper starts but cannot satisfy the elevated/session bootstrap
initial private-pipe session fails
```

Do not automatically create repeated elevation attempts every second.

A ClawHUD process restart or explicit HUD disable -> re-enable may start a new helper lifetime and therefore retry.

### 5.4 Connected helper method failures

Not every MSI method failure requires destroying the elevated helper.

Preserve the existing metric-level failure behavior where possible:

```text
Get_Temperature failure
Get_Fan failure
Get_Data failure
    -> affected metric/sample unavailable
    -> no synthetic zero
```

Do not add a new elevation request merely because a single read method failed.

For session-level/transport failures, close the broken transport but keep the automatic launch attempt consumed for that `EcHelperClient` lifetime.

### 5.5 No automatic UAC-based self-healing loop

The product rule after this PR is:

```text
Automatic EC recovery may not repeatedly ask for administrator consent.
```

If recovery would require a fresh `runas` launch, stop automatic recovery for the current helper lifetime.

This intentionally prefers:

```text
EC metrics temporarily unavailable
```

over:

```text
surprise/repeated UAC prompts
```

---

## 6. Recommended production API cleanup

Keep the change small and explicit.

### 6.1 `ProductionTelemetryController`

Recommended API shape:

```cpp
void ResetSamplingState(const wchar_t* reason);
void ReleaseEcHelper();
```

with semantics:

```text
ResetSamplingState
    -> reset sampling/aggregator/battery state
    -> DO NOT destroy a healthy/suppressed EcHelperClient

ReleaseEcHelper
    -> Close()
    -> ecClient_.reset()
```

Then call `ReleaseEcHelper()` only at real helper-lifetime boundaries.

Expected call-site policy:

```text
hud-hidden      -> ResetSamplingState only
suspend         -> ResetSamplingState only
hud-disabled    -> ResetSamplingState + ReleaseEcHelper
app-shutdown    -> ResetSamplingState + ReleaseEcHelper
```

A small explicit boolean/enum parameter is also acceptable if clearer, for example:

```cpp
enum class EcHelperLifetime
{
    Preserve,
    Release,
};

void ResetSamplingState(const wchar_t* reason, EcHelperLifetime ecLifetime);
```

Do not encode behavior by parsing the `reason` string.

Do not write logic like:

```cpp
if (wcscmp(reason, L"hud-hidden") == 0) ...
```

The reason remains logging text, not policy input.

### 6.2 `EcHelperClient`

Refactor only enough to distinguish failure cleanup from explicit lifetime reset.

Conceptual target:

```cpp
bool EnsureConnected()
{
    if (Connected())
        return true;

    if (attempted_)
        return false;

    attempted_ = true;
    ... launch/connect ...

    if (failure)
    {
        CloseTransport(); // does NOT clear attempted_
        return false;
    }
}
```

and:

```cpp
void EcHelperClient::Close()
{
    CloseTransport();
    attempted_ = false; // only because caller is explicitly ending this helper lifetime
    ... reset explicit-session state ...
}
```

Do not let `WriteAll` / `ReadAll` / `EnsureConnected` failure paths call a full reset that immediately permits another elevated launch.

### 6.3 Remove controller-side forced retry re-arming

`ReadEcTelemetry()` currently calls `ecClient_->Close()` after several session-level failures.

Audit those calls carefully.

If `Close()` becomes the explicit "release and permit a future helper lifetime" operation, failure paths in `ReadEcTelemetry()` must not use it merely to clean broken transport.

Prefer that `EcHelperClient` itself leave its connection state correct after a failed send/session, and let `ReadEcTelemetry()` simply abort the current sample.

Do not make `ProductionTelemetryController` understand Win32 pipe internals.

---

## 7. User-visible behavior after the fix

### Scenario A — default Always-on startup

```text
launch ClawHUD
-> HUD becomes visible
-> first EC sample requests UAC once
-> approve
-> EC helper stays connected
-> normal sampling continues
```

### Scenario B — In-game only, multiple games

```text
first detected game
-> UAC once
-> helper connected

leave game
-> HUD hidden
-> sampling stops
-> helper stays idle

start another game
-> sampling resumes
-> same helper reused
-> NO UAC
```

### Scenario C — F8 temporary hide/show

```text
HUD visible + helper connected
F8 hide
-> no EC polling
-> helper preserved
F8 show
-> same helper reused
-> NO UAC
```

### Scenario D — suspend/resume

```text
helper connected
sleep
-> EC polling stops
-> helper ownership preserved
resume
-> sampling resumes
-> NO UAC if session survived
```

If the session did not survive, EC may become unavailable for that helper lifetime; do not generate an automatic UAC loop.

### Scenario E — UAC cancelled

```text
first EC need
-> UAC
-> Cancel
-> HUD/app stay alive
-> EC values unavailable
-> subsequent timer samples do NOT prompt again
```

### Scenario F — explicit HUD disable / re-enable

```text
helper connected
Disable HUD
-> sampling stops
-> helper released

Enable HUD
-> when EC telemetry is needed again
-> a new UAC request is allowed
```

This is user-driven, not an automatic retry loop.

### Scenario G — app exit / update

```text
app shutdown begins
-> timers stop
-> helper pipe closes
-> elevated helper exits
-> ClawHUD exits
```

No orphan helper should remain after normal ClawHUD shutdown.

---

## 8. Logging requirements

Keep production logging quiet and useful.

Useful events:

```text
EC Helper launch requested
EC Helper connected
EC Helper elevation cancelled; automatic retry suppressed
EC Helper session unavailable; automatic relaunch suppressed
EC Helper released reason=hud-disabled
EC Helper released reason=app-shutdown
```

Exact wording may differ.

Do not log one failure line per 1-second telemetry sample after retry has been suppressed.

Do not log UAC cancellation as a fatal application error.

The existing "first failure transition only" logging behavior can be retained, but tests must verify the **actual launch-attempt policy**, not only the log count.

---

## 9. Tests required

Add focused deterministic tests for the new lifetime rules.

### 9.1 Connection-attempt gate

Prove that within one `EcHelperClient` lifetime:

```text
first failed launch/connect attempt
-> later ReadTemperature / ReadFan / ReadData calls
-> do not start another helper launch attempt
```

The existing runtime logging test is insufficient by itself because `runtimeFailureActive_` can suppress duplicate log messages even if the underlying launch path is retried.

Use the smallest practical test seam to count real helper-launch attempts.

Acceptable examples:

```text
test-only launch callback
small injected StartHelper function
#ifdef test-only attempt counter
```

Do not introduce a production DI framework just for this test.

### 9.2 Explicit close resets lifetime

Prove:

```text
failed attempt
-> automatic second read does not relaunch
-> explicit Close()
-> next read is allowed to begin one new launch attempt
```

This protects the explicit HUD disable -> re-enable behavior.

### 9.3 Sampling-stop helper policy

Add a focused test/pure policy seam proving:

```text
hud-hidden   -> preserve helper
suspend      -> preserve helper
hud-disabled -> release helper
app-shutdown -> release helper
```

Do not use reason-string matching in the test or implementation.

### 9.4 No-polling invariant

Preserve existing behavior that when production sampling is inactive:

```text
no EC timer-driven reads occur
```

Keeping the helper process alive must not mean keeping EC polling alive.

### 9.5 Existing tests

All existing EC protocol, decoder, telemetry, runtime logging, App lifecycle, Release CTest, and diagnostic tests must remain passing.

Do not weaken existing read-only helper protocol assertions.

---

## 10. Documentation update in this PR

Update the active EC documentation to match the real production constraint and the new lifecycle.

At minimum review:

```text
docs/MSI_EC_TELEMETRY_REFERENCE.md
```

Correct stale language that implies production EC should first attempt to operate unelevated or that the helper is only diagnostic-owned.

The active documentation should state clearly:

```text
- Supported MSI Claw EC reads used by ClawHUD require elevation.
- ClawHUD.exe remains unelevated.
- ClawHUD.EcHelper.exe is the narrow read-only elevated boundary.
- The helper is launched lazily on first production EC demand.
- A healthy helper is reused across transient HUD sampling pauses.
- No EC polling occurs while production sampling is stopped.
- UAC cancellation / failed helper bootstrap does not automatically re-prompt every sample.
- Explicit HUD disable releases the helper; re-enable may request elevation again.
```

Do not rewrite historical investigation documents unless a stale statement is presented as current production guidance.

---

## 11. Explicit non-goals

Do **not** include any of the following in Cleanup 1:

```text
persistent Windows EC service
scheduled-task broker
shared EC helper between ClawHUD / OptiSensor / other apps
helper installation redesign
helper write/control operations
main-process elevation
PresentMon runtime bootstrap sequencing
VeloPack update networking/background update work
startup shortcut changes
F8 master-switch semantics changes
HUD opacity implementation changes
HUD presentation changes
Managed IPC changes
SteamAddon integration
```

Those are independent concerns.

---

## 12. Completion criteria

Cleanup 1 is complete when all of the following are true:

```text
[ ] EC still requires and uses elevated ClawHUD.EcHelper.exe.
[ ] ClawHUD.exe remains unelevated.
[ ] First production EC demand may show one UAC prompt.
[ ] In-game-only hide/show does not release a healthy helper.
[ ] F8 temporary hide/show does not release a healthy helper.
[ ] Suspend/resume does not intentionally release a healthy helper.
[ ] Hidden/suspended HUD performs no EC polling.
[ ] UAC cancellation does not automatically prompt again on the next telemetry sample.
[ ] Failed helper/pipe session does not enter an automatic re-elevation loop.
[ ] Explicit HUD disable releases the elevated helper.
[ ] Explicit HUD re-enable may begin one new helper lifetime.
[ ] App/update shutdown releases the helper.
[ ] No helper remains orphaned after normal app exit.
[ ] EC protocol/whitelist is unchanged.
[ ] No EC write path is added.
[ ] Active EC documentation describes elevation and helper lifetime accurately.
[ ] Release build and full CTest pass.
```

The intended user-visible result is simple:

> **ClawHUD may ask for administrator consent when an EC helper lifetime begins, because EC reads require elevation, but normal HUD visibility changes, suspend/resume, or a cancelled/failed automatic attempt must not turn that requirement into repeated surprise UAC prompts.**
