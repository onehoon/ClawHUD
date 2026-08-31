Archived diagnostic reference only.
Not part of the production build.
Removed after the original hardware/research validation was completed.

Was the "VRR / Presentation Test" on the Settings > Diagnostics page: it
launched `PresentMon.exe`, recorded CSV files, parsed them, and compared a
HUD-OFF phase against a HUD-DYNAMIC phase. Superseded conceptually by the
PresentMon API2 diagnostic, which stays in `src/ClawHUD/`
(`PresentMonApi2Diagnostic.*`).

Contents:
  VrrDiagnostic.*            - the diagnostic worker + its F8 trigger flow
  VrrDiagnosticAnalysis.*    - PresentMon CSV parsing / independent-flip verdict
  D3dkmtVblankProbe.*        - D3DKMT vblank sampling used only by this diagnostic
  IntelVrrDiagnosticProbe.*  - Intel ctl vblank sampling used only by this diagnostic
  vrrpoc/main.cpp            - ClawHUD.VrrPoc.exe, the earlier standalone
                              PresentMon.exe layered-window VRR proof of concept

The production Intel VRR Range Fix / Tweaks implementation
(`src/ClawHUD/Tweaks/IntelVrr/`) is unrelated and untouched.
`tools/PresentMon.exe` stays packaged because Debug Logging / PresentActivitySource
still use it.

Git history is the authoritative source for deeper reconstruction.
