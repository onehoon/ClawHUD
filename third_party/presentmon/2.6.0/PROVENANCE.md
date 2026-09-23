# PresentMon API2 Runtime Provenance

Project: GameTechDev/PresentMon
Version: v2.6.0
Commit: e13fce6acdb55a808fd8318175a56863e532d95f
API: 3.4

The committed `ClawHUD.PresentMonRuntime.msi` and `PresentMonAPI2Loader.dll`
were locally built from the exact pinned upstream source with the scripts in
`tools/poc/presentmon-api2-runtime/scripts/`. The service MSI wraps the
upstream `PresentMonSharedService.msm`; `PresentMonAPI2Loader.dll` is the
matching app-local loader. These locally built binaries are not described as
Intel-signed binaries.

The wrapper MSI has `ProductVersion = 2.6.0`, a generated ProductCode, and
preserves the stable UpgradeCode
`{4E9BE59E-7CC7-4F8F-BD00-22A44EC8B9A9}`. The MSI was decompiled and its embedded
cabinet extracted for validation: it contains `PresentMonService.exe`,
`PresentMonAPI2.dll`, and `ddETWExternal.xml`. The manifest comes through the
v2.6.0 upstream merge module. UCI was not enabled: `PMON_UCI_SDK_DIR` was unset,
the generated UCI component group was empty, and the MSI contains no UCI
payload.

Extracted runtime payload evidence:

| File | File version | SHA-256 |
| --- | --- | --- |
| `PresentMonService.exe` | 2.6.0.0 | `13411A6161842E9B8EDAFB646D883822D3F49EFBD227BBE240F3AEDB7BB03A34` |
| `PresentMonAPI2.dll` | 2.6.0.0 | `77ABDD2F1C1275603B06DBEA596DADC3363B5ED2F4B95AE2BFC83D980A13FB9B` |
| `ddETWExternal.xml` | n/a | `7AA7F8194A0FE5B2A713A610F7C3A22C74E82BFFDB7B13582BC97A8ED23389B7` |

`SHA256SUMS.txt` records the hashes of the two committed artifacts.
