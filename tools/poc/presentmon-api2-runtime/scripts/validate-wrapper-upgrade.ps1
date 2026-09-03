<#
.SYNOPSIS
    Executes the ClawHUD PresentMon wrapper MSI upgrade matrix.

.DESCRIPTION
    Runs the wrapper packages through real elevated msiexec transactions and
    asserts only guarantees the packages can actually provide:

      1. legacy -> current : the migration Cleanup 3 must support now. Exactly
                             one wrapper product remains, at the current version,
                             and the shared service + API2 middleware stay valid.
      2. current -> current : repair / reinstall must not create a duplicate
                              product.
      3. (optional, only with -NewerCleanup3Msi) newer -> current : a genuine
         downgrade between two MajorUpgrade-authored wrappers must be rejected.

    Downgrade rejection is NOT asserted for the legacy 1.0.0.0 wrapper: that
    package predates the <MajorUpgrade> authoring and has no Upgrade table, so it
    cannot detect or reject a newer installed product. See
    third_party/presentmon/2.5.1/PROVENANCE.md.

    Run on a throwaway VM or a machine where reinstalling the PresentMon shared
    runtime is acceptable. NOT run by CI or by normal ClawHUD builds. Requires an
    elevated PowerShell session.

.PARAMETER LegacyMsi
    The already-shipped wrapper without <MajorUpgrade> (base-commit
    third_party/presentmon/<ver>/ClawHUD.PresentMonRuntime.msi, ProductVersion
    1.0.0.0).

.PARAMETER CurrentMsi
    The Cleanup-3 wrapper under test (ProductVersion = the bundled runtime pin).

.PARAMETER NewerCleanup3Msi
    Optional. A genuinely newer MajorUpgrade-authored wrapper, used only for the
    downgrade-rejection step.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string]$LegacyMsi,
    [Parameter(Mandatory)] [string]$CurrentMsi,
    [string]$ExpectedCurrentVersion = '2.5.1',
    [string]$NewerCleanup3Msi = ''
)

$ErrorActionPreference = 'Stop'

if (-not ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
        [Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'validate-wrapper-upgrade.ps1 must be run from an elevated PowerShell session.'
}

$legacy = (Resolve-Path $LegacyMsi).Path
$current = (Resolve-Path $CurrentMsi).Path
$newer = if ($NewerCleanup3Msi) { (Resolve-Path $NewerCleanup3Msi).Path } else { '' }

function Invoke-Msi([string]$path) {
    $log = [System.IO.Path]::GetTempFileName()
    $p = Start-Process msiexec.exe -Wait -PassThru -ArgumentList @(
        '/i', ('"' + $path + '"'), '/qn', '/norestart', '/l*v', ('"' + $log + '"'))
    Write-Host "  msiexec /i $([System.IO.Path]::GetFileName($path)) -> exit $($p.ExitCode)  (log: $log)"
    return $p.ExitCode
}

function Get-WrapperProducts {
    Get-ItemProperty `
        'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\*',
        'HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\*' `
        -ErrorAction SilentlyContinue |
      Where-Object DisplayName -eq 'ClawHUD PresentMon Shared Runtime' |
      Select-Object PSChildName, DisplayVersion
}

function Assert-SingleWrapperProduct([string]$version) {
    $p = @(Get-WrapperProducts)
    if ($p.Count -ne 1 -or $p[0].DisplayVersion -ne $version) {
        throw "expected exactly one wrapper product at $version, got: $($p | Out-String)"
    }
}

function Assert-RuntimeHealthy {
    $svc = Get-Service PresentMonSharedService -ErrorAction SilentlyContinue
    if (-not $svc) { throw 'PresentMonSharedService is not registered.' }
    $mw = (Get-ItemProperty 'HKLM:\SOFTWARE\INTEL\PresentMon\Service' -Name sharedMiddlewarePath -ErrorAction SilentlyContinue).sharedMiddlewarePath
    if (-not $mw -or -not (Test-Path $mw)) { throw "middleware path missing: $mw" }
    if ((Get-Item $mw).Name -ne 'PresentMonAPI2.dll') { throw "unexpected middleware file: $mw" }
    Write-Host "  runtime OK: service=$($svc.Status) middleware=$mw ($((Get-Item $mw).VersionInfo.FileVersion))"
}

Write-Host '--- baseline ---'
Get-WrapperProducts | Format-Table -AutoSize

Write-Host '--- 1) legacy -> current (major upgrade Cleanup 3 must support) ---'
if ((Invoke-Msi $legacy) -notin 0, 3010) { throw 'legacy install failed' }
if ((Invoke-Msi $current) -notin 0, 3010) { throw 'current major-upgrade failed' }
Assert-SingleWrapperProduct $ExpectedCurrentVersion
Assert-RuntimeHealthy

Write-Host '--- 2) current -> current (repair / reinstall, no duplicates) ---'
if ((Invoke-Msi $current) -notin 0, 3010) { throw 'current reinstall failed' }
Assert-SingleWrapperProduct $ExpectedCurrentVersion
Assert-RuntimeHealthy

if ($newer) {
    Write-Host '--- 3) newer -> current (downgrade between MajorUpgrade wrappers must be rejected) ---'
    if ((Invoke-Msi $newer) -notin 0, 3010) { throw 'newer install failed' }
    $downgradeExit = Invoke-Msi $current
    if ($downgradeExit -eq 0) { throw "Cleanup-3+ downgrade was not rejected (exit $downgradeExit)" }
    Assert-RuntimeHealthy
}
else {
    Write-Host '--- 3) skipped: pass -NewerCleanup3Msi to exercise downgrade rejection ---'
    Write-Host '        (the legacy 1.0.0.0 wrapper has no Upgrade table and cannot reject a newer product)'
}

Write-Host ''
Write-Host 'PASS: wrapper upgrade matrix.'
