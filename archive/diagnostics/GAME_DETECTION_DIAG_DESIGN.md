# Game Detection Diagnostic Design

Status: Design reference for a future standalone diagnostic console application  
Date: 2026-08-31  
Repository: `onehoon/ClawHUD`

---

## 1. Purpose

The future Game Detection Diagnostic is a standalone evidence recorder for researching and validating ClawHUD game-detection policy.

It is **not** a production detector and must not decide that a process is a game. Its job is to capture raw external evidence while the operator launches games, switches between games and desktop applications, Alt+Tabs, opens launchers/overlays, and exits games.

The diagnostic should answer questions such as:

- when did a candidate process/window first appear?
- when did it first become visible?
- when did it first begin GPU 3D activity?
- when did PresentMon API2 first observe a swapchain?
- when did Presented FPS and Displayed FPS become available?
- when did the process become foreground?
- did Steam `RunningAppID` change before the game appeared?
- did Microsoft game identity identify the executable before foreground?
- when the user Alt+Tabbed away, did the game continue presenting or displaying?
- which GPU-active process would PresentMon Auto Target have selected?

The resulting logs are intended to be correlated with the normal ClawHUD application debug log. ClawHUD internal coordinator/candidate/commit state should therefore stay in the application log, not be duplicated into this standalone diagnostic.

---

## 2. Reference material

The previous in-app PresentMon API2 diagnostic was intentionally archived and removed from production. It mixed API2 capability validation with game-detection research and should **not** be restored as-is.

Useful archived references:

- `archive/diagnostics/presentmon-api2/GameDetectionProbe.cpp`
- `archive/diagnostics/presentmon-api2/GameDetectionProbe.h`
- `archive/diagnostics/presentmon-api2/PresentMonApi2Diagnostic.cpp`
- `archive/diagnostics/presentmon-api2/PresentMonApi2Diagnostic.h`
- `archive/diagnostics/presentmon-api2/assets/PresentMonAutoTargetBlockList.txt`

The new tool should reuse ideas and proven algorithms from those files, but should be rebuilt as a clean standalone diagnostic focused only on game-detection evidence.

---

## 3. Scope boundary

### Keep

The diagnostic should collect evidence from:

1. Win32 foreground state
2. top-level window lifecycle and metadata
3. DWM/window geometry
4. Windows PDH GPU Engine 3D activity
5. PresentMon-style Top GPU process ranking
6. PresentMon API2 renderer/presentation evidence for observed PIDs
7. Steam `RunningAppID` transitions
8. Microsoft/Xbox game identity metadata
9. raw Game Bar/QAM-related window/process transitions when naturally observed
10. session timeline correlation and summary

### Remove from the old API2 diagnostic

Do not include:

- API2 capability survey
- full introspection dumps
- complete metric enumeration
- static metric survey
- all-device telemetry survey
- full frame-query CSV dumps
- API2 metric health classification
- CPU/GPU power, clock, temperature, fan, memory, or other system telemetry
- EC or IGCL diagnostics
- HUD state
- VRR/presentation state
- ClawHUD internal `GameDetectionCoordinator` state
- ClawHUD candidate/committed PID state

PresentMon API2 itself has already been validated. The new tool only needs the small subset of API2 data useful for game detection.

---

## 4. Core principle: record evidence, do not produce a verdict

For each observed PID, the diagnostic should build an evidence vector rather than a `game=true/false` result.

Conceptually:

```text
PID
├─ foreground?
├─ top-level HWND?
├─ visible / ownerless / minimized / cloaked?
├─ fullscreen-like?
├─ GPU 3D activity?
├─ PresentMon Auto Target rank?
├─ API2 swapchain present?
├─ Presented FPS available?
├─ Displayed FPS available?
├─ Steam session active?
└─ Microsoft game identity evidence?
```

No single signal should be promoted into a game verdict inside the diagnostic.

Examples:

- `fullscreenLike=true` does not prove a game.
- `RunningAppID != 0` does not identify the game PID.
- Top GPU rank 1 does not prove that the user is currently viewing that process.
- a swapchain does not prove the process is a game.
- a displayed frame is strong renderer evidence but still should remain raw evidence in this tool.

The purpose of collecting multiple real-world traces is to decide the production policy later.

---

## 5. Observed PID pool

The old research probe primarily followed the foreground PID. The standalone diagnostic should be broader because previous field captures showed that a real game HWND and renderer may exist several seconds, or in Microsoft/Xbox cases tens of seconds, before the game finally becomes foreground.

The diagnostic should maintain an **Observed PID Pool**.

A PID enters the pool when one or more of these are true:

1. it is the current foreground PID
2. it owns a visible ownerless top-level window
3. it appears in the PDH Top GPU candidate list
4. it creates/shows a top-level window while a Steam game session is active
5. it has positive Microsoft game identity evidence

The pool is diagnostic-only. It may contain launchers, browsers, shell processes, helpers, games, overlays, and false candidates.

For each PID in the pool, keep stable metadata and track renderer evidence while the process remains relevant/alive. A later implementation may bound the pool size or retire inactive entries, but should not lose early game-renderer evidence merely because the process is not foreground yet.

---

## 6. Win32 foreground evidence

Foreground transitions form the primary timeline reference.

Use:

```text
SetWinEventHook(EVENT_SYSTEM_FOREGROUND)
```

and record each transition immediately.

For every foreground transition log:

- wall-clock timestamp
- monotonic elapsed timestamp
- old HWND / PID
- new HWND / PID
- executable basename
- full process image path
- window title
- window class
- visible state
- minimized state
- DWM cloaked state
- owner HWND

The diagnostic may also take periodic snapshots, but explicit foreground events should be recorded separately so they are never hidden inside a sampling interval.

---

## 7. Global top-level window lifecycle

A standalone diagnostic is allowed to be much more invasive/noisy than production. Global window lifecycle evidence is valuable for reconstructing launch sequences.

Observe these WinEvent events:

```text
EVENT_OBJECT_CREATE
EVENT_OBJECT_SHOW
EVENT_OBJECT_HIDE
EVENT_OBJECT_DESTROY
EVENT_OBJECT_NAMECHANGE
```

Restrict processing to useful top-level window events, especially `OBJID_WINDOW` and root/top-level HWNDs.

For each event, capture as available:

- event type
- timestamp
- HWND
- PID
- executable basename
- process image path
- title
- class name
- visible state
- minimized state
- owner HWND
- DWM cloaked state
- window style
- extended style
- window/frame rect
- monitor rect

Destroy events should preserve cached metadata from before destruction when direct inspection is no longer possible.

This lets later analysis reconstruct sequences such as:

```text
process appears
→ game HWND CREATE
→ SHOW
→ renderer begins presenting
→ foreground
```

or launcher/helper transitions that would be invisible in foreground-only logs.

---

## 8. Window geometry / fullscreen-like evidence

For relevant windows, prefer:

```text
DwmGetWindowAttribute(..., DWMWA_EXTENDED_FRAME_BOUNDS, ...)
```

with fallback to:

```text
GetWindowRect()
```

Determine monitor geometry with:

```text
MonitorFromWindow()
GetMonitorInfo()
```

Record:

- window/frame rect
- monitor `rcMonitor`
- monitor `rcWork`
- window width/height
- monitor width/height
- derived `fullscreenLike`

A small physical-pixel tolerance such as 2 px is sufficient for the diagnostic helper.

`fullscreenLike` must remain a raw derived field. Do not treat it as a game verdict. System UI, pickers, shells, launchers, and other non-game windows can also cover an entire monitor.

---

## 9. PDH GPU Engine activity and PresentMon Auto Target parity

This source should be retained because it answers a different question from foreground tracking:

> Which visible-window process currently has the greatest Windows GPU Engine 3D activity?

Use the same core algorithm previously validated against PresentMon Auto Target.

### Counter source

Enumerate:

```text
\GPU Engine(*)\Running time
```

Keep only instances matching:

```text
pid_<PID>_..._engtype_3D
```

Reject Compute and other engine types for PresentMon parity.

### Sampling sequence

Match the pinned PresentMon collection sequence:

```text
prime PdhCollectQueryData()
first measured collect/read
sleep 100 ms
second measured collect/read
```

A delta is valid only when both measured reads are valid.

For each counter:

```text
delta = second - first
```

Use only positive deltas and sum all 3D-engine deltas belonging to the same PID.

### Candidate window filter

For PresentMon-style ranking, keep processes that own a visible, ownerless top-level window:

```text
IsWindowVisible(hwnd)
GetWindow(hwnd, GW_OWNER) == nullptr
```

### Logging

Do not log only the winner. Keep at least the Top 5 candidates per sample:

```text
rank
pid
exe
hwnd
title
gpu3dDelta
```

Produce two rankings:

```text
TopGPU.Raw
TopGPU.PresentMonParity
```

`TopGPU.PresentMonParity` applies the archived pinned PresentMon target blocklist. Preserve the blocklist source/version/commit in the session header so captures remain reproducible.

This enables later analysis of questions such as:

- how often was the actual game rank 1 before foreground?
- how often did a launcher or browser outrank the game?
- after Alt+Tab, did the background game remain Top GPU?
- how useful is Top GPU as discovery evidence versus visibility authority?

---

## 10. PresentMon API2 renderer evidence

The new diagnostic should use API2 only for a minimal per-PID renderer/presentation probe.

### Required API2 fields

For each tracked observed PID record:

- `pmStartTrackingProcess` status on target acquisition/retarget
- `pmPollDynamicQuery` status
- `swapChainCount`
- `SWAP_CHAIN_ADDRESS`
- `DISPLAYED_FPS`
- `PRESENTED_FPS`

Missing data must be represented as unavailable/null, never synthetic zero unless the API actually returned zero.

### Why both swapchain and FPS matter

Keep these concepts separate:

```text
swapChainCount > 0
    = renderer/swapchain evidence

PRESENTED_FPS available/positive
    = process is presenting frames

DISPLAYED_FPS available/positive
    = frames are reaching display
```

A key diagnostic case is a background game. The useful evidence may look like:

```text
foreground = false
swapChainCount > 0
presentedFps > 0
displayedFps = unavailable/0
```

or some other pattern depending on title/presentation mode. Capturing the raw distinction is the point of the diagnostic.

### Metrics intentionally excluded

Do not add capability/introspection work just to discover unrelated metrics.

For the first standalone version, omit:

- `APPLICATION_FPS`
- `GPU_BUSY`
- `GPU_TIME`
- device `GPU_UTILIZATION`
- power/temperature/frequency metrics
- frame-event CSV dumps

They can be added later only if a concrete game-detection research question requires them.

### Multiple observed PIDs

Do not bind API2 only to the current foreground PID.

The standalone diagnostic should be able to observe API2 renderer evidence for multiple PIDs in the Observed PID Pool because the real game may begin rendering before foreground.

Implementation details such as one API2 session versus multiple tracking targets are secondary to the data contract. The important requirement is that each API2 sample is explicitly associated with its PID and that target lifecycle is correct when processes appear, disappear, or are retired.

---

## 11. Steam `RunningAppID` context

The standalone diagnostic should record Steam session context directly from the external source rather than depending on ClawHUD's internal state.

Use the same registry-notification approach as production (`RegNotifyChangeKeyValue`), not polling.

Record transitions:

```text
0 -> N
N -> M
N -> 0
```

with:

- timestamp
- old AppID
- new AppID

Interpretation inside the diagnostic must stop there.

Do **not** derive a PID from the AppID and do not mark a process as a game because `RunningAppID` is active.

The reason for logging this signal is timeline correlation:

```text
RunningAppID becomes active
→ launcher/helper activity
→ game HWND appears
→ API2 renderer appears
→ game becomes foreground
```

Across multiple titles this will show how useful Steam session state is as an early wake-up signal and how variable the launch delay is.

---

## 12. Microsoft/Xbox game identity evidence

For newly observed candidate PIDs, perform a one-shot identity probe.

Keep only fields useful for game-detection policy research:

- process AUMID
- package full name
- package family name
- `MicrosoftGame.config` exists
- `MicrosoftGame.config` readable
- current executable exactly matches a configured executable

The strongest derived raw evidence to preserve is:

```text
MicrosoftGame.config exists=true
currentExecutableMatch=true
```

Do not expand this diagnostic into a complete package inventory. Publisher, architecture, version, resource IDs, mutable/external package roots, and other package metadata are not required unless a future real-world false positive/negative demonstrates a need.

Run the identity probe once per PID unless a concrete reason exists to retry.

---

## 13. Game Bar / QAM handling

Do not invent a `GameBarActive` or `QamActive` heuristic in the first standalone diagnostic.

Instead, let the global process/window evidence naturally capture Game Bar-related processes and windows, including create/show/hide/foreground/name changes.

The research question is whether Game Bar/QAM exposes a repeatable raw signal worth promoting into a future independent game-session signal.

Only after real captures show a stable contract should a dedicated Game Bar signal source be designed.

---

## 14. Per-PID stable metadata

When a PID first enters the Observed PID Pool, record stable metadata once where possible:

- PID
- executable basename
- full process image path
- process start time
- AUMID/package identity summary
- initial known top-level HWND list

Do not repeatedly query expensive stable metadata every sampling interval.

Later event/sample records should reference PID and only include fields that can change or that are necessary for independent analysis.

---

## 15. Sampling/event cadence

Recommended initial cadence:

```text
Foreground WinEvent
    immediate

Window lifecycle WinEvent
    immediate

Steam RunningAppID registry notification
    immediate

API2 observed-PID polling
    about 250 ms

PDH Top GPU ranking
    about every 500 ms
    with its own 100 ms A/B delta window

Geometry snapshot
    immediately on relevant foreground/window events
    plus about every 500 ms for currently relevant windows if useful

PID stable metadata
    once on first observation
```

This is a diagnostic tool, so the production no-polling constraint does not apply. The goal is high-quality evidence capture, not idle production efficiency.

The diagnostic should still avoid pointless work, but it is acceptable to use `EnumWindows`, PDH sampling, and API2 polling continuously during an explicit diagnostic session.

---

## 16. Log format

Use one machine-readable JSONL file as the raw authority.

Suggested name:

```text
game-detect-YYYYMMDD-HHMMSS.jsonl
```

Every record should include common timing fields:

```text
wallTime
elapsedMs
sequence
type
```

Both wall-clock and monotonic/session-relative time are required.

Wall-clock time allows correlation with the normal ClawHUD debug log. Monotonic elapsed time allows precise ordering and interval analysis even if system wall time changes.

Example foreground record:

```json
{"type":"foreground_change","wallTime":"2026-08-31T15:50:41.228+09:00","elapsedMs":17232,"sequence":88,"oldPid":4100,"pid":9124,"exe":"Diablo IV.exe","hwnd":"0x123456"}
```

Example API2 record:

```json
{"type":"api2","wallTime":"2026-08-31T15:50:41.480+09:00","elapsedMs":17484,"sequence":91,"pid":9124,"pollStatus":"SUCCESS","swapChainCount":2,"rendererActive":true,"displayedFps":119.8,"presentedFps":120.1}
```

Example Top GPU record:

```json
{"type":"top_gpu","wallTime":"2026-08-31T15:50:41.520+09:00","elapsedMs":17524,"sequence":92,"mode":"PresentMonParity","rank":1,"pid":9124,"exe":"Diablo IV.exe","gpu3dDelta":1234567.0}
```

Use explicit null/unavailable fields where appropriate rather than silently omitting ambiguous data.

---

## 17. Session header

At diagnostic start, record enough environment information to make later traces understandable and reproducible without reintroducing the old capability dump.

Recommended fields:

- diagnostic format/schema version
- tool build/version
- local start time and timezone offset
- OS build
- PresentMon API2 session open result/version if cheaply available
- PresentMon target blocklist source and pinned upstream commit/version
- sampling intervals configured for API2 and PDH

Do not dump the full API2 introspection tree.

---

## 18. End-of-session timeline summary

In addition to the JSONL raw log, generate a compact summary at stop.

For each observed PID, report first-occurrence timestamps where available:

```text
PID 9124  Diablo IV.exe

firstSeen                 12.442s
firstWindowCreate         13.102s
firstWindowShow           13.551s
firstTopGpu               14.028s
firstApi2SwapChain        14.205s
firstPresentedFps         14.455s
firstDisplayedFps         15.001s
firstForeground           18.224s

steamAppIdAtFirstSeen      2344520
microsoftGameIdentity      false
```

Also retain major last-occurrence / exit evidence where useful:

- last foreground
- last renderer evidence
- window hide/destroy
- process exit if reliably observed
- Steam session cleared

The timeline summary is derived convenience output. The JSONL raw log remains authoritative.

This summary is expected to be particularly useful for comparing multiple titles and deriving production rules from actual timing distributions.

---

## 19. Intended manual capture scenarios

A single diagnostic session should support repeated transitions and multiple games.

Recommended field test sequence:

1. start diagnostic on normal Windows desktop
2. interact briefly with Explorer / browser / Steam
3. launch Steam game A
4. observe launcher/splash/startup delay
5. play game A
6. Alt+Tab to Explorer or another application
7. leave game A rendering in background
8. return to game A
9. open Game Bar/QAM if relevant
10. exit game A
11. launch non-Steam game B
12. repeat foreground/background transitions
13. launch Microsoft/Xbox title where available
14. observe Gaming Services / picker / security UI transitions
15. return to desktop
16. stop diagnostic

The design should allow the operator to run for as long as necessary. Do not impose the old fixed ~15-second API2 diagnostic duration.

---

## 20. Source/data matrix

| Source | Data | Include |
|---|---|---:|
| Win32 | foreground changes | Yes |
| Win32/DWM | top-level window lifecycle + metadata | Yes |
| Win32/DWM | fullscreen-like geometry | Yes |
| PDH | per-PID 3D Running Time delta | Yes |
| PDH | Top 5 GPU candidates | Yes |
| PresentMon parity | blocklist-filtered Top 5 | Yes |
| PresentMon API2 | tracking/poll status | Yes |
| PresentMon API2 | swapchain count/address | Yes |
| PresentMon API2 | `DISPLAYED_FPS` | Yes |
| PresentMon API2 | `PRESENTED_FPS` | Yes |
| Steam registry | `RunningAppID` transitions | Yes |
| AppModel | AUMID / package identity summary | Yes |
| MicrosoftGame.config | exact executable match | Yes |
| Game Bar/QAM | raw window/process events | Yes |
| PresentMon API2 | capability/introspection dump | No |
| PresentMon API2 | all metrics survey | No |
| PresentMon API2 | frame CSV | No |
| PresentMon API2 | system CPU/GPU telemetry | No |
| ClawHUD | coordinator/candidate/committed state | No |
| HUD/VRR | presentation state | No |
| EC/IGCL | telemetry | No |

---

## 21. Suggested internal source separation

The future console application can be organized around evidence sources rather than production detection policy:

```text
Game Detection Diagnostic
│
├─ Win32 Evidence
│   ├─ Foreground events
│   ├─ Window lifecycle
│   └─ Geometry/DWM metadata
│
├─ GPU Discovery Evidence
│   └─ PDH PresentMon-style Top GPU
│
├─ Renderer Evidence
│   └─ PresentMon API2 per observed PID
│
├─ Platform Context
│   ├─ Steam RunningAppID
│   └─ Microsoft game identity
│
└─ Timeline Correlator
    ├─ JSONL writer
    └─ end-of-session PID summary
```

This is intentionally different from the production game-detection architecture. The diagnostic gathers facts; production later consumes validated conclusions.

---

## 22. Non-goals

The first standalone Game Detection Diagnostic must not:

- implement or replace production game detection
- output a final `isGame` verdict
- modify ClawHUD production state
- modify HUD visibility
- alter PresentMon production sampling
- alter HUD presentation/VRR behavior
- implement focus stealing or bring games to foreground
- infer a game PID from Steam AppID
- infer a game PID from Game Bar process identity
- treat fullscreen geometry as authoritative
- treat Top GPU as foreground authority
- restore the old generic API2 capability diagnostic

---

## 23. Design conclusion

The standalone diagnostic should be treated as a **Game Detection Evidence Recorder**, not as a smaller version of the old API2 test.

The essential data model is:

```text
Window/Foreground Evidence
        +
GPU Discovery Evidence
        +
API2 Renderer Evidence
        +
Platform Session/Identity Evidence
        ↓
Timestamped raw timeline
        ↓
Cross-game comparison
        ↓
Production game-detection policy design
```

The archived diagnostic code is reference material only. Reuse the proven foreground/geometry/PDH algorithms and the minimal API2 query ideas, but rebuild the standalone console diagnostic around this narrower evidence contract.