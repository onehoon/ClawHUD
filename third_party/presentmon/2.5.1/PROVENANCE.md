# PresentMon API2 Runtime Provenance

Project: GameTechDev/PresentMon
Version: v2.5.1
Commit: 3e06c7dcb922e411bae38503b51ab501be61c37f
API: 3.3

The committed artifacts are built from the pinned upstream commit using the
isolated runtime POC build scripts in
`tools/poc/presentmon-api2-runtime/scripts/`. The runtime MSI wraps the
official `PresentMonSharedServiceModule` built from the same upstream source
and version. `PresentMonAPI2Loader.dll` is kept app-local and is not the
service middleware.

These locally built binaries are not described as Intel-signed binaries.
