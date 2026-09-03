<#
.SYNOPSIS
    Executes the ClawHUD PresentMon wrapper MSI major-upgrade / downgrade matrix.

.DESCRIPTION
    Runs the currently shipped wrapper and a regenerated wrapper through real
    elevated msiexec transactions and asserts:

      1. old  -> new : exactly one wrapper product remains, at the new version,
                       and the shared service + API2 middleware are still valid.
      2. new  -> old : the old package is rejected (non-zero exit) and the single
                       newer product is preserved.
      3. new  -> new : repeated install / repair leaves exactly one product.

    This must be run on a throwaway VM or a machine where reinstalling the
    PresentMon shared runtime is acceptable. It is NOT run by CI or by normal
    ClawHUD builds. Requires an elevated PowerShell session.

.PARAMETER OldMsi
    The currently shipped wrapper (e.g. the base-commit
    third_party/presentmon/<ver>/ClawHUD.PresentMonRuntime.msi).

.PARAMETER NewMsi
    The regenerated wrapper under test.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string]$OldMsi,
    [Parameter(Mandatory)] [string]$NewMsi,
    [string]$ExpectedNewVersion = '2.5.1'
)

$ErrorActionPreference = 'Stop'

if (-not ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
        [Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'validate-wrapper-upgrade.ps1 must be run from an elevated PowerShell session.'
}

$old = (Resolve-Path $OldMsi).Path
$new = (Resolve-Path $NewMsi).Path

function Invoke-Msi([string]$path, [string]$verb = '/i') {
    $log = [System.IO.Path]::GetTempFileName()
    $p = Start-Process msiexec.exe -Wait -PassThru -ArgumentList @(
        $verb, ('"' + $path + '"'), '/qn', '/norestart', '/l*v', ('"' + $log + '"'))
    Write-Host "  msiexec $verb $([System.IO.Path]::GetFileName($path)) -> exit $($p.ExitCode)  (log: $log)"
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

Write-Host '--- 1) old -> new (major upgrade) ---'
if ((Invoke-Msi $old) -notin 0, 3010) { throw 'old install failed' }
if ((Invoke-Msi $new) -notin 0, 3010) { throw 'new major-upgrade failed' }
$p = @(Get-WrapperProducts)
if ($p.Count -ne 1 -or $p[0].DisplayVersion -ne $ExpectedNewVersion) {
    throw "old->new did not leave exactly one $ExpectedNewVersion product: $($p | Out-String)"
}
Assert-RuntimeHealthy

Write-Host '--- 2) new -> old (downgrade must be rejected) ---'
$oldExit = Invoke-Msi $old
$p = @(Get-WrapperProducts)
if ($oldExit -eq 0 -or $p.Count -ne 1 -or $p[0].DisplayVersion -ne $ExpectedNewVersion) {
    throw "old MSI was not rejected / did not preserve the single newer product. exit=$oldExit products=$($p | Out-String)"
}
Assert-RuntimeHealthy

Write-Host '--- 3) new -> new (repeat / repair) ---'
if ((Invoke-Msi $new) -notin 0, 3010) { throw 'new reinstall failed' }
$p = @(Get-WrapperProducts)
if ($p.Count -ne 1 -or $p[0].DisplayVersion -ne $ExpectedNewVersion) {
    throw "new->new left a duplicate product: $($p | Out-String)"
}
Assert-RuntimeHealthy

Write-Host ''
Write-Host 'PASS: wrapper major-upgrade / downgrade matrix.'
