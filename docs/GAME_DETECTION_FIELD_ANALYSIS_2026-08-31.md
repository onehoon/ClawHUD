# ClawHUD Game Detection Field Analysis — 2026-08-31

Status: Field-analysis reference for the next In-Game Only redesign  
Repository: `onehoon/ClawHUD`  
Source capture: `C:\GoogleDrive\ClawHUD\Diag\gamedetection`

## 1. Purpose

This document records the conclusions from the standalone `ClawHUD.Diag` game-detection capture and the matching normal `clawhud.log` so later implementation work can continue without reconstructing the analysis from chat history.

The capture covers a long mixed session with multiple real games and normal Windows applications, including:

- Diablo IV
- Minecraft for Windows
- Dave the Diver
- Mafia: The Old Country
- Steam
- Explorer / Search / Start
- Windows Terminal
- Game Bar and Xbox helper windows
- repeated foreground changes / Alt+Tab
- Steam `RunningAppID` transitions
- standalone API2 per-PID evidence
- PDH PresentMon-style Top GPU ranking
- Microsoft game identity probing
- top-level window lifecycle and geometry

The most important conclusion is:

> In-Game Only should stop treating one globally committed game PID as the authority. The final authority should be the **current foreground window/PID**, evaluated with hard window/process admission rules and previously learned game evidence.

The detector should answer two separate questions:

1. Is there evidence that a process is a game?
2. Is the user currently looking at that game window?

These must not be collapsed into one sticky `Committed` PID.

---

## 2. Critical failure demonstrated by the field run

The existing Generic path accepted `WindowsTerminal.exe` PID 1792 as a game candidate because it was foreground and produced renderer/FPS evidence.

Observed production sequence:

```text
WindowsTerminal foreground
-> Generic candidate PID 1792
-> Verifying
-> API2 first displayed frame
-> Ready / Committed
```

The process remained alive for the rest of the test, so the existing committed-target policy ignored later real-game candidates while Diablo, Minecraft, Dave the Diver, and Mafia were launched and used.

This is not adequately solved by adding only `WindowsTerminal.exe` to the exclusion list.

The structural defect is:

```text
one false commit
-> committed process remains alive
-> new candidates are ignored
-> all later real games can be missed
```

Therefore the new design must remove the global sticky target as the final game-screen authority.

---

## 3. Renderer evidence is not game identity

The field capture shows that many normal desktop/system processes can produce Presented/Displayed FPS or other PresentMon renderer evidence, including examples such as:

- Windows Terminal
- Explorer
- Steam WebHelper
- RuntimeBroker
- Game Bar components
- DLLHost and other shell/helper processes

Therefore:

```text
RendererActive != Game
DisplayedFPS > 0 != Game
PresentedFPS > 0 != Game
```

API2 renderer evidence should mean only:

> This PID is producing graphics/presentation activity.

It may strengthen a generic game candidate, but it cannot be the game verdict by itself.

---

## 4. Strongest screen-admission combination observed

After the standalone Diag was corrected to run Per-Monitor-V2 DPI aware, the session recorded both window and monitor geometry in the same physical coordinate space.

The device used:

```text
system DPI = 144
Windows scale = 150%
monitor = 1920 x 1200 physical pixels
```

Replaying the capture with the following conditions:

```text
current foreground HWND/PID
+ top-level window
+ visible
+ not minimized
+ not DWM-cloaked
+ executable resolved
+ high-confidence non-game executable exclusion passed
+ window covers the current monitor within a small physical-pixel tolerance
```

selected only the actual game PIDs in this capture:

```text
Diablo IV.exe
Minecraft.Windows.exe
DaveTheDiver.exe
MafiaTheOldCountry.exe
```

with multiple PIDs where a game was relaunched. No ordinary desktop application in this capture passed the whole combination.

This does **not** mean fullscreen geometry alone is a game verdict. Windows shell/system UI can also own monitor-sized windows. The important finding is that fullscreen-like geometry is a strong **screen admission gate when combined with current foreground authority and hard executable/window rejection**.

---

## 5. Recommended fullscreen-like rule

The diagnostic originally used ±2 physical pixels. That is too strict.

Minecraft produced a valid fullscreen/borderless game window similar to:

```text
monitor = 0,0,1920,1200
window  = -3,-3,1923,1203
```

The window is effectively fullscreen, but a 2 px tolerance rejects it.

Recommended first production tolerance:

```cpp
constexpr LONG kFullscreenTolerancePx = 8;

bool CoversMonitor(const RECT& window, const RECT& monitor) noexcept
{
    return std::abs(window.left   - monitor.left)   <= kFullscreenTolerancePx &&
           std::abs(window.top    - monitor.top)    <= kFullscreenTolerancePx &&
           std::abs(window.right  - monitor.right)  <= kFullscreenTolerancePx &&
           std::abs(window.bottom - monitor.bottom) <= kFullscreenTolerancePx;
}
```

A normal work-area-sized window such as `1920x1128` on a `1920x1200` monitor still fails by a large margin, so the extra tolerance does not turn ordinary maximized windows into fullscreen candidates.

Use physical coordinates consistently. The production process/window code involved in this comparison must not mix DPI-virtualized and DWM physical coordinates.

---

## 6. `EVENT_OBJECT_LOCATIONCHANGE` is required

Foreground events alone are insufficient.

### Minecraft

Observed shape:

```text
foreground becomes Minecraft
window initially ~1920x1128
-> current fullscreen gate should fail

same HWND later LOCATIONCHANGE
window becomes roughly -3,-3,1923,1203
-> fullscreen gate should pass
```

No new foreground event is required for that transition.

### Mafia

A foreground transition could arrive before the real game window was fully shown/sized. Shortly afterward SHOW/LOCATIONCHANGE completed the fullscreen window.

Therefore production should re-evaluate the **current foreground window only** on:

```text
EVENT_SYSTEM_FOREGROUND
EVENT_OBJECT_SHOW
EVENT_OBJECT_LOCATIONCHANGE
EVENT_OBJECT_HIDE
EVENT_OBJECT_DESTROY
```

For object events, do not broadly evaluate every observed window. Check whether the affected HWND/PID is the current foreground target and, if so, call the same foreground evaluation path.

This remains event-driven and does not require polling.

---

## 7. Steam `RunningAppID` role confirmed

The capture showed clean Steam session transitions for games such as Diablo IV and Dave the Diver:

```text
0 -> AppID
...
AppID -> 0
```

The AppID transition often preceded the actual game foreground window by several seconds.

Therefore the correct interpretation is:

```text
RunningAppID != 0
-> Steam game session is armed/active
```

It does **not** mean:

```text
AppID identifies the game PID
AppID means HUD should show
AppID should start a short launch timeout
```

Recommended lifecycle:

```text
0 -> N      Steam session becomes active / armed
N -> M      new Steam session generation/context
N -> 0      clear Steam session context
```

The Steam session may be retained as useful positive context and for early candidate discovery, but final HUD visibility remains tied to the current foreground game-screen evaluation.

If Explorer, Steam, Search, or another non-game process becomes foreground while a Steam game remains alive, In-Game Only should hide immediately.

---

## 8. Microsoft/Xbox game identity remains valuable

The Minecraft process produced the expected strong Microsoft game evidence:

```text
MicrosoftGame.config exists
current executable exact match = true
```

This is strong positive game identity and should remain in production.

Its role should be:

```text
Microsoft game identity
-> strong evidence that this PID is a game
```

not:

```text
Microsoft identity
-> HUD show immediately regardless of foreground/window state
```

A known Microsoft game PID still needs the current foreground window to pass the screen-admission rules before the HUD is shown.

Because the game identity is strong, a current foreground fullscreen-like window from a positive Microsoft PID can bypass generic game-identity uncertainty and does not need to wait for a generic renderer-verification verdict.

---

## 9. PDH Top GPU remains diagnostic-only

PresentMon-style Top GPU ranking was useful for research but should not be a production admission requirement.

Observed behavior included:

- some real games were not Top GPU immediately
- Top GPU could arrive after foreground
- Explorer / ClawHUD / other applications could rank highly

Therefore do not require:

```text
TopGPU == current PID
```

and do not introduce production PDH polling for game detection.

Keep PDH Top GPU as a standalone Diag research source.

---

## 10. Standalone Diag API2 caveat

The standalone Game Detection Diag used one dynamic query across many tracked PIDs. In the field capture, rows self-identified the requested PID correctly after `PM_METRIC_PROCESS_ID` was added, but unrelated PIDs could still show identical cached FPS values across the same time interval.

PresentMon middleware dynamic queries maintain frame-metric cache state. When a poll has no fresh frame samples, cached frame metrics can be populated into the query output. Reusing one dynamic query across many PIDs therefore makes those numeric FPS values unsuitable as game identity evidence.

This does **not** automatically prove the production single-target `PresentMonProcessTelemetry` path has the same bug. Production currently releases/re-registers its dynamic query when the target PID changes, so its query lifecycle is narrower.

Nevertheless the product-level rule remains:

> Numeric FPS is telemetry, not game identity.

The production API2 verifier may remain a same-PID supporting verifier for an unknown generic candidate, but it must not be the only admission rule.

---

## 11. Recommended new state separation

Replace the concept of one globally authoritative committed game PID with three separate responsibilities.

### 11.1 Session Context

Platform/session evidence only.

Example:

```cpp
struct GameSessionContext
{
    std::uint32_t steamAppId{};
    std::uint64_t steamSessionGeneration{};
};
```

This object does not own the HUD target PID.

### 11.2 Verified / Known Game Process Cache

Retain useful per-process evidence so Alt+Tab return is immediate and repeated verification is unnecessary.

Key by process generation, not numeric PID alone.

Conceptual data:

```cpp
struct KnownGameProcess
{
    DWORD pid{};
    std::uint64_t processStartTime{};

    bool microsoftGameIdentity{};
    bool rendererVerified{};
    bool observedInSteamSession{};
};
```

A process may remain cached while it is backgrounded.

Remove the cache entry when that process generation exits.

Multiple known game processes may coexist without one globally blocking the others.

### 11.3 Current Foreground Target

This is the only authority for In-Game Only visibility and FPS targeting.

Conceptually:

```text
CurrentForegroundTarget
    HWND
    PID
    eligible/not eligible
```

HUD visibility and FPS target must follow this current foreground evaluation, not an older committed PID.

---

## 12. Recommended current-foreground evaluation

Conceptual production flow:

```cpp
EvaluateCurrentForeground()
{
    const HWND hwnd = GetForegroundWindow();
    const DWORD pid = ProcessIdOf(hwnd);

    if (!IsUsableTopLevelWindow(hwnd))
        return Hide;

    if (!IsWindowVisible(hwnd) || IsIconic(hwnd) || IsDwmCloaked(hwnd))
        return Hide;

    const auto process = InspectProcess(pid);
    if (!process)
        return Hide;

    if (IsKnownNonGameExecutable(process->imageName))
        return Hide;

    if (!CoversCurrentMonitor(hwnd, 8 /* physical px */))
        return Hide;

    if (HasStrongMicrosoftGameIdentity(pid))
        return Show(pid);

    if (IsKnownVerifiedGameProcess(pid))
        return Show(pid);

    BeginOrContinueSamePidRendererVerification(pid);
    return HideUntilVerified;
}
```

When renderer verification completes, do not immediately show based only on the completed verifier callback.

Perform a final foreground/screen recheck:

```text
renderer verified for PID X
-> GetForegroundWindow again
-> same current PID/window still valid?
-> still visible/not minimized/not cloaked?
-> still not excluded?
-> still fullscreen-like?

YES -> HUD SHOW, FPS target = PID X
NO  -> cache rendererVerified for process generation, HUD remains hidden
```

This avoids stale asynchronous verification changing HUD visibility after the user has already switched away.

---

## 13. Generic game path

For a normal non-Steam, non-Microsoft game such as Mafia:

```text
current foreground
+ valid top-level visible window
+ non-excluded executable
+ fullscreen-like monitor coverage
+ same-PID renderer verification
-> show HUD
```

A generic fullscreen D3D desktop application can still resemble a game. Windows does not provide one authoritative universal `IsGame(pid)` API, so this cannot be eliminated perfectly without supported-platform signals.

Mitigation should be conservative and evidence-based:

- strong executable exclusion list
- current foreground requirement
- fullscreen-like screen admission
- renderer verification for unknown generic processes
- no fuzzy title scoring
- no broad heuristic score system without field evidence

---

## 14. Exclusion policy

Keep the existing high-confidence non-game executable rejection policy and extend it only with clear field evidence.

This capture demonstrates that `WindowsTerminal.exe` must be rejected for generic detection.

Other high-confidence non-game candidates worth centralizing if not already covered include common system/helper processes such as:

```text
WindowsTerminal.exe
RuntimeBroker.exe
dllhost.exe
backgroundTaskHost.exe
WerFault.exe
CrashReportClient.exe
```

Do not use fuzzy window-title matching as the main exclusion mechanism.

Steam/Game Bar processes may still provide external context, but their own foreground executable should remain rejected as the HUD game target.

---

## 15. Alt+Tab behavior

The desired user behavior does not require a sticky committed PID.

Example:

```text
Diablo foreground
-> PID already verified / screen passes
-> HUD SHOW

Alt+Tab to Explorer
-> foreground evaluation fails/rejected
-> HUD HIDE

Diablo remains alive in KnownGameProcess cache

Alt+Tab back to Diablo
-> current foreground screen passes
-> verified cache hit
-> HUD SHOW immediately
```

This preserves fast return behavior without allowing an old game PID to block later games.

Alt+Tab is therefore primarily a **visibility transition**, not a new global game-session state transition.

---

## 16. Ghost policy

Do not add special Ghost retention at this stage.

A Ghost window can carry a game-looking title/size while process identity is unreliable. The safer first policy is:

```text
Ghost/unknown foreground
-> HUD may hide briefly
-> real game foreground returns
-> normal foreground evaluation shows HUD again
```

Do not infer the old game PID from a Ghost window unless future field evidence demonstrates a concrete UX problem that warrants explicit handling.

---

## 17. Event-driven production model

No production polling is needed.

Recommended event inputs:

```text
EVENT_SYSTEM_FOREGROUND
EVENT_OBJECT_SHOW
EVENT_OBJECT_LOCATIONCHANGE
EVENT_OBJECT_HIDE
EVENT_OBJECT_DESTROY
Steam RunningAppID registry notification
Microsoft game identity one-shot probe from relevant window/process events
process lifetime wait handle for cached/verified game processes
```

The object events should only trigger lightweight re-evaluation of the current foreground target rather than global scanning.

Do not add:

- repeated EnumWindows
- foreground timers
- process-list polling
- PDH Top GPU polling
- FindWindow loops
- repeated package enumeration
- WMI WITHIN polling

---

## 18. Recommended architecture

```text
                   +---------------------------+
                   |      Session Context      |
                   | Steam RunningAppID        |
                   | Microsoft game evidence   |
                   +-------------+-------------+
                                 |
                                 | supporting evidence
                                 v
+--------------------+   +------------------------------+
| WinEvent sources   |-->| EvaluateCurrentForeground()  |
| FOREGROUND         |   | current HWND/PID authority   |
| SHOW               |   +--------------+---------------+
| LOCATIONCHANGE     |                  |
| HIDE / DESTROY     |                  v
+--------------------+       hard window/process gates
                                      |
                                      v
                             fullscreen-like coverage
                                      |
                    +-----------------+------------------+
                    |                                    |
                    v                                    v
          strong/known game PID                  unknown generic PID
          Microsoft identity or                  same-PID API2 verify
          verified process cache                          |
                    |                                    |
                    +-----------------+------------------+
                                      |
                                      v
                           final foreground recheck
                                      |
                                      v
                                   HUD SHOW
                             FPS target = same PID
```

The key invariant is:

> **Game identity evidence can be cached; HUD target authority cannot. HUD target authority is the current foreground PID/window.**

---

## 19. Keep / change / remove summary

| Area | Decision |
| --- | --- |
| `EVENT_SYSTEM_FOREGROUND` | Keep; final current-screen authority |
| Steam `RunningAppID` | Keep; session context / wake signal only |
| MicrosoftGame.config exact executable match | Keep; strong game identity |
| Same-PID API2 verifier | Keep; supporting evidence for unknown generic candidate |
| Executable exclusion | Keep and conservatively extend |
| Fullscreen-like geometry | Promote to core screen-admission gate |
| `EVENT_OBJECT_LOCATIONCHANGE` | Add to production current-foreground re-evaluation |
| Verified game process cache | Add |
| Global sticky committed PID | Remove as HUD target authority |
| Ignore all new candidates while committed PID lives | Remove |
| PDH Top GPU | Diagnostic-only |
| Numeric FPS as game verdict | Do not use |
| Window title as game verdict | Do not use |
| Ghost retention | Defer / no special handling now |
| Production polling | Do not add |

---

## 20. Implementation direction

Do not try to solve the field failure only by adding more exclusions to the current coordinator.

Recommended refactor direction:

1. Preserve the existing lightweight/event-driven Steam, Microsoft identity, foreground, and process-lifetime sources where useful.
2. Separate platform/session context from current-screen target selection.
3. Introduce a process-generation keyed verified-game cache.
4. Replace global `Committed PID is authoritative while alive` semantics with current foreground eligibility.
5. Add current-foreground re-evaluation on SHOW / LOCATIONCHANGE / HIDE / DESTROY.
6. Use fullscreen-like monitor coverage with an initial ~8 physical-pixel tolerance.
7. Use same-PID API2 renderer verification only for unknown generic candidates.
8. Always perform a final foreground/window recheck after asynchronous renderer verification.
9. Keep HUD presentation / VRR contracts completely unchanged.

This redesign should be treated as a game-detection state/policy change upstream of HUD presentation and telemetry formatting.