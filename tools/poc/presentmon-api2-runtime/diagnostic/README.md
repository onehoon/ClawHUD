# PresentMon API2 desktop diagnostic

This is an isolated, non-production diagnostic client for the pinned PresentMon v2.5.1 runtime. It loads only `PresentMonAPI2Loader.dll`; `PresentMonAPI2.dll` must remain installed under `C:\Program Files\Intel\PresentMonSharedService` and must not be copied beside this executable.

Build the project with `PresentMonRoot` set to the pinned upstream checkout. Copy the built loader beside the diagnostic executable, then run as a normal user:

```powershell
.\PresentMonApi2DesktopDiagnostic.exe --pid <live-3d-process-id> --out .\rtx4070-results
```

The client emits a complete introspection JSON, an all-metrics text dump, and a diagnostic log. It records loader/API status codes, all exposed metric/device metadata, dynamic polling, static queries, frame-query registration/consumption, process tracking, telemetry cadence requests, and ETW flush-period result. It does not change ClawHUD production code or use Intel Graphics Software private runtime endpoints.
