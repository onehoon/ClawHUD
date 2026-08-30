[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$BuildRoot,
    [Parameter(Mandatory)][string]$UpstreamRoot,
    [string]$InstalledRoot = 'C:\Program Files\Intel\PresentMonSharedService'
)

$ErrorActionPreference = 'Stop'
function Write-Artifact([string]$Name, [string]$Path) {
    if (Test-Path -LiteralPath $Path -PathType Leaf) {
        $item = Get-Item -LiteralPath $Path
        $hash = (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash
        [pscustomobject]@{ Artifact=$Name; Path=$Path; SizeBytes=$item.Length; SizeMiB=[math]::Round($item.Length / 1MB, 3); SHA256=$hash }
    } else {
        [pscustomobject]@{ Artifact=$Name; Path=$Path; SizeBytes='MISSING'; SizeMiB='MISSING'; SHA256='MISSING' }
    }
}

$module = Get-ChildItem -LiteralPath $UpstreamRoot -Filter 'PresentMonSharedService.msm' -Recurse -File | Select-Object -First 1
$service = Get-ChildItem -LiteralPath $UpstreamRoot -Filter 'PresentMonService.exe' -Recurse -File | Where-Object FullName -Match "\\Release\\" | Select-Object -First 1
$middleware = Get-ChildItem -LiteralPath $UpstreamRoot -Filter 'PresentMonAPI2.dll' -Recurse -File | Where-Object FullName -Match "\\Release\\" | Select-Object -First 1
$loader = if (Test-Path -LiteralPath $BuildRoot -PathType Container) {
    Get-ChildItem -LiteralPath $BuildRoot -Filter 'PresentMonAPI2Loader.dll' -Recurse -File | Select-Object -First 1
} else { $null }
$msi = Join-Path $BuildRoot 'ClawHUD.PresentMonRuntime.msi'
$modulePath = if ($module) { $module.FullName } else { Join-Path $UpstreamRoot 'MISSING' }
$servicePath = if ($service) { $service.FullName } else { Join-Path $UpstreamRoot 'MISSING' }
$middlewarePath = if ($middleware) { $middleware.FullName } else { Join-Path $UpstreamRoot 'MISSING' }
$loaderPath = if ($loader) { $loader.FullName } else { Join-Path $BuildRoot 'MISSING' }
$rows = @()
$rows += Write-Artifact 'PresentMonSharedService.msm' $modulePath
$rows += Write-Artifact 'ClawHUD.PresentMonRuntime.msi' $msi
$rows += Write-Artifact 'PresentMonService.exe' $servicePath
$rows += Write-Artifact 'PresentMonAPI2.dll' $middlewarePath
$rows += Write-Artifact 'PresentMonAPI2Loader.dll' $loaderPath
$rows
if (Test-Path -LiteralPath $InstalledRoot -PathType Container) {
    $installedBytes = (Get-ChildItem -LiteralPath $InstalledRoot -Recurse -File | Measure-Object Length -Sum).Sum
    Write-Output ([pscustomobject]@{ Artifact='Installed runtime directory'; Path=$InstalledRoot; SizeBytes=$installedBytes; SizeMiB=[math]::Round($installedBytes / 1MB, 3); SHA256='n/a directory' })
} else {
    Write-Output ([pscustomobject]@{ Artifact='Installed runtime directory'; Path=$InstalledRoot; SizeBytes='MISSING'; SizeMiB='MISSING'; SHA256='n/a directory' })
}
