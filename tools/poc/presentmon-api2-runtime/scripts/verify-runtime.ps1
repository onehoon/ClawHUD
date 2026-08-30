[CmdletBinding()]
param([Parameter(Mandatory)][string]$BuildRoot)

$ErrorActionPreference = 'Stop'
$log = Join-Path $BuildRoot 'runtime-verification.txt'
Start-Transcript -LiteralPath $log -Force | Out-Null
try {
    Write-Output '=== ARTIFACTS ==='
    Get-ChildItem -LiteralPath $BuildRoot -File -Recurse | Select-Object FullName,Length
    Write-Output '=== SERVICE ==='
    sc.exe query PresentMonSharedService
    sc.exe qc PresentMonSharedService
    Write-Output '=== REGISTRY ==='
    Get-ItemProperty -LiteralPath 'HKLM:\SOFTWARE\INTEL\PresentMon\Service' -ErrorAction SilentlyContinue
    Write-Output '=== PIPE ==='
    Get-ChildItem -LiteralPath '\\.\pipe\' -ErrorAction SilentlyContinue | Where-Object Name -eq 'sharedpresentmonsvcnamedpipe'
    Write-Output '=== PROCESS ==='
    Get-Process -Name PresentMonService -ErrorAction SilentlyContinue | Select-Object Id,Path,StartTime
    Write-Output '=== SMOKE TEST ==='
    Write-Output 'Compile/run the isolated smoke client as a standard user; this script does not elevate or run it.'
} finally {
    Stop-Transcript | Out-Null
}
Write-Output "EvidenceLog=$log"
