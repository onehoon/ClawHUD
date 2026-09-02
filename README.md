# ClawHUD

> [!WARNING]
> **ClawHUD is under active development. Do not install or use it yet.**

ClawHUD is a lightweight performance HUD built specifically for supported **MSI Claw** handhelds on **Windows 11**.

It displays FPS, CPU/GPU telemetry, memory usage, fan speed, power, and battery information without injecting into games or hooking their rendering pipeline. The production HUD is designed around preserving **VRR** compatibility.

---

## Supported devices

ClawHUD currently supports only the following MSI Claw model:

| Device | Board ID |
| --- | --- |
| MSI Claw A2VM | `MS-1T42` / `MS-1T52` |
| MSI Claw 8 EX AI+ | `MS-1T91` |

**Windows 11 x64** and the device's **Intel Arc GPU** are required.

ClawHUD checks the baseboard ID at startup. Unsupported devices are rejected instead of running with unverified hardware behavior.

---

## Features

- Lightweight, tray-first performance HUD
- FPS and system/hardware telemetry
- `Always on` and `In-game only` display modes
- Automatic foreground game detection
- Global `F8` hotkey to show or hide the HUD
- Start ClawHUD with Windows
- HUD size adjustment
- `Unispace` and `Segoe UI Variable` fonts
- Left / Center / Right alignment
- Full-width or content-width HUD background
- HUD Opacity adjustment
- Intel VRR Range Fix for affected MSI Claw displays
- Silent application update checks at startup

ClawHUD runs from the system tray. Open **Settings** from the tray icon to configure the HUD.

---

## Telemetry

ClawHUD combines PresentMon API2, Windows telemetry, and MSI-specific EC telemetry.

| HUD item | Displayed information | Source |
| --- | --- | --- |
| **FPS** | Displayed FPS for the current target game | [PresentMon API2](https://github.com/GameTechDev/PresentMon) |
| **CPU** | CPU usage + CPU temperature | PresentMon API2 + MSI EC |
| **GPU** | GPU usage + GPU clock | PresentMon API2 |
| **TDP** | CPU package power | MSI EC |
| **RAM** | System memory usage | Windows |
| **VRAM** | GPU memory usage | PresentMon API2 |
| **FAN** | Fan speed | MSI EC |
| **BAT** | Battery level + estimated remaining time while on battery | Windows + MSI EC |

MSI EC telemetry is read through ClawHUD's narrowly scoped read-only EC helper. See [MSI EC Telemetry Reference](docs/MSI_EC_TELEMETRY_REFERENCE.md) for implementation details.

Telemetry values are displayed only when the corresponding data is available. Missing sensor data is not presented as a synthetic zero value.

### FPS and Intel XeFG

FPS is sourced from PresentMon API2. Some Intel driver-side XeFG paths can generate frames that are not fully visible to normal OS-level PresentMon observation, so the FPS shown by ClawHUD may not always represent every physically generated/output frame in those configurations.

---

## Game detection

ClawHUD automatically tries to identify the game that currently owns the foreground window. The detection path includes normal Windows games as well as Steam and Microsoft/Xbox game context.

Game detection is still under development and **may not work perfectly with every game, launcher, or unusual window configuration**.

- **Always on** does not depend on game detection to keep the HUD visible.
- **In-game only** depends on automatic game detection. If a game is not detected correctly, the HUD may not appear automatically.
- `F8` can be used to manually show or hide the HUD.

The implementation intentionally avoids continuous process polling for game detection. See [Game Detection Redesign](docs/GAME_DETECTION_REDESIGN_PR_PLAN_2026-08-31.md) for the internal design.

---

## HUD settings

The **Settings** tab provides the user-facing HUD controls:

- **Start ClawHUD with Windows**
- **Enable HUD**
- **Display mode** — `In-game only` / `Always on`
- **HUD size**
- **Font** — `Unispace` / `Segoe UI Variable`
- **Alignment** — Left / Center / Right
- **Background width** — Full width / Content width
- **HUD Opacity**

Settings are persisted between launches.

---

## Intel VRR Range Fix

The **Tweaks** tab contains **Intel VRR Range Fix**.

This tweak restores the native VRR range on affected MSI Claw displays and is applied at application startup when enabled. The UI reports the detected panel, the range before and after the operation, and the last apply result.

The tweak is enabled by default in the current development build.

---

## PresentMon runtime

ClawHUD uses **PresentMon API2** for FPS and CPU/GPU telemetry.

A compatible PresentMon API2 shared-service runtime is required. ClawHUD checks for it at startup and:

- reuses an already installed compatible runtime when available, or
- installs the bundled compatible PresentMon runtime automatically when it is missing or incompatible.

Windows may display a **UAC prompt** when the PresentMon runtime needs to be installed. Only the runtime installer is elevated; ClawHUD itself continues to run as a normal non-elevated application.

The bundled runtime contains the PresentMon shared service and matching API2 middleware used by ClawHUD. Version and licensing information is documented in [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md).

---

## VRR / XeFG-friendly presentation

ClawHUD is intentionally an **external, non-injected HUD**.

The production HUD does not use:

- game DLL injection
- `Present` hooks
- DXGI hooks
- game swapchain interception
- process-memory modification
- frame-generation interception
- an injected frame limiter

The HUD uses the Windows Presentation API / DirectComposition production path with a premultiplied-alpha presentation surface and is built around retaining an independent-flip-capable presentation path.

Real-hardware validation has shown the production HUD retaining the observed Independent Flip presentation family in tested scenarios, including dynamic HUD updates. This remains an important regression requirement as development continues.

For the detailed validation history and limitations of what software-side evidence can prove, see [HUD Presentation / VRR Decision History](docs/HUD_PRESENTATION_VRR_DECISION_HISTORY.md).

---

## Current limitations

ClawHUD is still under active development.

Current limitations include:

- Only the supported MSI Claw board IDs listed above are accepted.
- Game detection may fail with some games, launchers, or unusual foreground-window behavior.
- Individual telemetry values may be unavailable depending on the current hardware/driver state.
- PresentMon may not observe every driver-generated XeFG output frame in some Intel XeFG configurations.
- Hardware, driver, Windows, and game updates can affect presentation behavior, so VRR/XeFG compatibility continues to be regression-tested.

---

## Development

ClawHUD is a Windows-native C++ project.

Build requirements:

- Windows 11
- MSVC / Visual Studio 2026
- Windows SDK

MinGW is not supported.

Detailed implementation notes, research results, validation history, and work orders live under [`docs/`](docs/). The README intentionally focuses on the user-visible product rather than serving as the project's internal architecture specification.

Useful references:

- [HUD Presentation / VRR Decision History](docs/HUD_PRESENTATION_VRR_DECISION_HISTORY.md)
- [MSI EC Telemetry Reference](docs/MSI_EC_TELEMETRY_REFERENCE.md)
- [Game Detection Redesign](docs/GAME_DETECTION_REDESIGN_PR_PLAN_2026-08-31.md)
- [Third-party notices](THIRD-PARTY-NOTICES.md)

---

## License

ClawHUD is licensed under the **GNU General Public License v3.0**. See [LICENSE](LICENSE).

Third-party components retain their respective licenses. See [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md).
