# SteamAddon Managed Runtime

This directory defines the ClawHUD payload published for SteamAddon Managed
mode. It is intentionally separate from the Standalone Velopack release
channel.

The release workflow publishes an immutable GitHub Pre-release with the tag
`steamaddon-runtime-v<MAJOR.MINOR.PATCH>`. The package contains the existing
`ClawHUD.exe --managed` binary and only its runtime dependencies; it does not
contain the WPF Settings frontend, the diagnostic executable, a Velopack
package, or the Standalone update feed.

`payload.manifest.json` is the source-controlled payload-shape contract.
`package-runtime.ps1` is the only owner of staging, validation, manifest
generation, zip creation, and SHA-256 sidecar generation. It never builds,
downloads, installs, or publishes anything.

The generated external `runtime-manifest.json` records the exact Runtime
version, immutable tag, source commit, asset name, and SHA-256 of the final
`ClawHUDRuntime.zip`. The same identity manifest is staged inside the payload
for consumers that inspect an installed Runtime.

The SHA-256 is calculated only after the archive is complete. Because a file
cannot contain the cryptographic hash of the archive that contains that same
file without a self-referential hash, the embedded manifest leaves `sha256`
null; the external manifest and `.sha256` sidecar are the authoritative exact
archive identity. All non-hash identity fields are identical in both
manifests. Consumers must verify the sidecar/external manifest before
installation and then validate the embedded identity fields.
