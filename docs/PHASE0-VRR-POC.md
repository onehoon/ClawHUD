# Phase 0 — VRR / XeFG Compatibility PoC

This is a deliberately small Windows 11 x64 native C++20 experiment. It renders one premultiplied-alpha label surface through the Windows Composition Swapchain / Presentation Manager path. It has no game DLL, process injection, Present hook, DXGI hook, swapchain interception, telemetry, or game-specific code.

## Build and run

Use a current Windows 11 SDK (Build 22000 or later) and a Visual Studio C++ desktop toolchain:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
build\Release\ClawHUD.VrrPoc.exe
```

The program writes the console output and a `logs/vrr-poc-YYYYMMDD-HHMMSS.log` file. It prints the adapter name, Composition Swapchain initialization, and the result of `IPresentationFactory::IsPresentationSupportedWithIndependentFlip()`. A `NO` result is a Phase 0 finding; there is no layered-window or DXGI fallback.

The window is a top-level `WS_EX_NOACTIVATE | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW` popup hosted by DirectComposition. Rendering is still performed by a D3D11 displayable texture registered with `IPresentationManager`; the window is not a layered-window renderer. `F8` removes/adds the visual content and commits the DirectComposition tree, while `ESC` exits. OFF is therefore an actual content removal rather than an alpha-zero frame.

## Hardware validation

Record the exact Windows build, Intel graphics driver, panel refresh rate, game, OptiScaler revision/configuration, XeFG setting, and whether the game is borderless or fullscreen. Do not call the PoC VRR-compatible from capability output alone.

1. On the MSI Claw, enable Windows/driver VRR and select a 120 Hz mode if available. Use one test game and cap it to a non-divisor refresh target such as 73 FPS.
2. Baseline: OptiScaler OFF, XeFG OFF, VRR ON, HUD OFF. Capture 30–60 seconds with PresentMon/ETW. Record `PresentMode`, displayed timestamps/display cadence, dropped frames, and frame pacing.
3. Repeat the same baseline with HUD ON. Compare the same fields. A cadence that becomes strongly quantized to 8.33/16.67/25.00 ms is suspicious for VRR loss.
4. Final test: OptiScaler ON, XeFG ON, VRR ON. Capture HUD OFF and HUD ON separately for 30–60 seconds and record the same fields. Confirm that XeFG generated frames and the game remain functional.
5. Treat `Hardware: Independent Flip` in both states, or `Hardware Composed: Independent Flip` with no cadence regression, as a success candidate. Treat a reproducible change to `Composed: Flip`, VRR disengagement, fixed-refresh quantization, crashes, generated-frame failure, or clear pacing regression as NO-GO.

`AllowsTearing` alone does not prove VRR, and Independent Flip capability support alone does not prove runtime VRR. The OFF/ON comparison and displayed timing are required.

## Definition of Done boundary

This repository can be **PoC ready for hardware validation** after a successful build and a run that displays `ClawHUD VRR TEST`, reports the capability query, toggles ON/OFF, and leaves the game process untouched. VRR, OptiScaler, and XeFG compatibility remain unverified until the MSI Claw captures above are completed. If either GO condition fails, do not add an injection/hook or game-specific workaround; report the result and stop for a project decision.
