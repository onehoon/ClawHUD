<#
.SYNOPSIS
    Executes the ClawHUD PresentMon wrapper MSI upgrade matrix and API smoke check.

.DESCRIPTION
    Runs the wrapper packages through real elevated msiexec transactions and
    validates the installed runtime payload and API version:

      1. v2.5.1 -> v2.6.0 : the committed wrapper is upgraded in place. Exactly
                             one wrapper product remains, at the current version,
                             and the shared service + API2 middleware stay valid.
      2. current -> current : repair / reinstall must not create a duplicate
                              product.
      3. (optional, only with -NewerCleanup3Msi) newer -> current : a genuine
         downgrade between two MajorUpgrade-authored wrappers must be rejected.

    Downgrade rejection is NOT asserted for the legacy 1.0.0.0 wrapper: that
    package predates the <MajorUpgrade> authoring and has no Upgrade table, so it
    cannot detect or reject a newer installed product. See the active runtime
    provenance for the shipped-wrapper history.

    Run on a throwaway VM or a machine where reinstalling the PresentMon shared
    runtime is acceptable. NOT run by CI or by normal ClawHUD builds. Requires an
    elevated PowerShell session.

.PARAMETER PreviousMsi
    The committed PresentMon v2.5.1 wrapper (ProductVersion 2.5.1).

.PARAMETER CurrentMsi
    The current wrapper under test (ProductVersion = the bundled runtime pin).

.PARAMETER NewerCleanup3Msi
    Optional. A genuinely newer MajorUpgrade-authored wrapper, used only for the
    downgrade-rejection step.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string]$PreviousMsi,
    [Parameter(Mandatory)] [string]$CurrentMsi,
    [string]$ExpectedCurrentVersion = '2.6.0',
    [string]$NewerCleanup3Msi = ''
)

$ErrorActionPreference = 'Stop'

if (-not ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
        [Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'validate-wrapper-upgrade.ps1 must be run from an elevated PowerShell session.'
}

$previous = (Resolve-Path $PreviousMsi).Path
$current = (Resolve-Path $CurrentMsi).Path
$newer = if ($NewerCleanup3Msi) { (Resolve-Path $NewerCleanup3Msi).Path } else { '' }
$smokeTest = Join-Path (Split-Path -Parent $current) 'smoke-test\PresentMonApi2SmokeTest.exe'
if (-not (Test-Path -LiteralPath $smokeTest -PathType Leaf)) {
    throw "The API smoke client was not found beside the current MSI build: $smokeTest"
}
$installedRoot = Join-Path $env:ProgramFiles 'Intel\PresentMonSharedService'

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
    if ($svc.Status -ne 'Running') { throw "PresentMonSharedService is not running: $($svc.Status)" }
    $mw = (Get-ItemProperty 'HKLM:\SOFTWARE\INTEL\PresentMon\Service' -Name sharedMiddlewarePath -ErrorAction SilentlyContinue).sharedMiddlewarePath
    if (-not $mw -or -not (Test-Path $mw)) { throw "middleware path missing: $mw" }
    if ((Get-Item $mw).Name -ne 'PresentMonAPI2.dll') { throw "unexpected middleware file: $mw" }
    $manifest = Join-Path $installedRoot 'ddETWExternal.xml'
    if (-not (Test-Path -LiteralPath $manifest -PathType Leaf)) { throw "NVIDIA ETW manifest is missing: $manifest" }
    $payload = @(
        (Join-Path $installedRoot 'PresentMonService.exe'),
        $mw,
        $manifest
    )
    foreach ($path in $payload) {
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "runtime payload is missing: $path" }
        $file = Get-Item -LiteralPath $path
        $version = [System.Diagnostics.FileVersionInfo]::GetVersionInfo($path).FileVersion
        $hash = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
        Write-Host "  payload: $($file.Name) version=$version sha256=$hash"
        if ($file.Name -in @('PresentMonService.exe', 'PresentMonAPI2.dll') -and
            $version -notlike "$ExpectedCurrentVersion.*") {
            throw "unexpected runtime file version for $($file.Name): $version"
        }
    }
    $uciPayload = @(Get-ChildItem -LiteralPath $installedRoot -Recurse -File |
        Where-Object { $_.Name -match '(?i)(uci|unified-collector-interface)' })
    if ($uciPayload.Count -ne 0) { throw "UCI runtime files must not be installed: $($uciPayload.FullName -join ', ')" }
    Write-Host '  UCI runtime payload: absent'
    $smokeOutput = & $smokeTest
    $smokeExitCode = $LASTEXITCODE
    $smokeOutput | ForEach-Object { Write-Host "  smoke: $_" }
    if ($smokeExitCode -ne 0) { throw "API 3.4 loader smoke test failed with exit code $smokeExitCode." }
    Write-Host "  runtime OK: service=$($svc.Status) middleware=$mw"
}

Write-Host '--- baseline ---'
Get-WrapperProducts | Format-Table -AutoSize

Write-Host '--- 1) PresentMon 2.5.1 -> 2.6.0 (major upgrade) ---'
if ((Invoke-Msi $previous) -notin 0, 3010) { throw 'previous runtime install failed' }
Assert-SingleWrapperProduct '2.5.1'
if ((Invoke-Msi $current) -notin 0, 3010) { throw 'current major-upgrade failed' }
Assert-SingleWrapperProduct $ExpectedCurrentVersion
Assert-RuntimeHealthy

Write-Host '--- 2) PresentMon 2.6.0 -> 2.6.0 (repair / reinstall, no duplicates) ---'
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
