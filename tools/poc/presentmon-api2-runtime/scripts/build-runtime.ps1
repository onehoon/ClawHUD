[CmdletBinding()]
param(
    [string]$BuildRoot = 'D:\temp\ClawHUD-presentmon-api2-runtime-build',
    [string]$UpstreamRoot = 'D:\temp\PresentMon-v2.5.1-clawhud-poc',
    [string]$Configuration = 'Release'
)

$ErrorActionPreference = 'Stop'
$scriptRoot = Split-Path -Parent $PSScriptRoot
$prepare = Join-Path $PSScriptRoot 'prepare-upstream.ps1'
$upstream = & $prepare -UpstreamRoot $UpstreamRoot
$msbuild = & 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe' -version '[17.0,18.0)' -latest -products '*' -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe | Select-Object -Last 1
if (-not $msbuild) { throw 'MSBuild was not found through vswhere.' }

$wixCandidates = @(
    'C:\Program Files (x86)\WiX Toolset v3.11\bin',
    'C:\Program Files (x86)\WiX Toolset v3.14\bin',
    'D:\Program Files (x86)\WiX Toolset v3.11\bin'
)
$wixBin = $wixCandidates | Where-Object { Test-Path (Join-Path $_ 'candle.exe') } | Select-Object -First 1
if (-not $wixBin) { throw 'WiX 3.x candle.exe was not found. Install WiX Toolset 3.11+ before building this POC.' }
$wixRoot = Split-Path -Parent $wixBin

New-Item -ItemType Directory -Path $BuildRoot -Force | Out-Null
$solutionDir = "$($upstream.Path)\"
$common = @('/m', '/p:Configuration=' + $Configuration, '/p:Platform=x64', '/p:PresentMonProductVersion=2.5.1', '/p:WIX=' + "$wixRoot\", '/p:SolutionDir=' + $solutionDir)

function Invoke-MSBuild([string]$project, [string[]]$extra = @()) {
    & $msbuild $project @common @extra
    if ($LASTEXITCODE -ne 0) { throw "MSBuild failed for $project with exit code $LASTEXITCODE." }
}

# These projects are the minimum upstream graph needed by the shared module.
Invoke-MSBuild (Join-Path $upstream.Path 'IntelPresentMon\ServiceMergeModule\ServiceMergeModule.wixproj')
Invoke-MSBuild (Join-Path $upstream.Path 'IntelPresentMon\PresentMonAPI2Loader\PresentMonAPI2Loader.vcxproj')

$module = Get-ChildItem -LiteralPath $upstream.Path -Filter 'PresentMonSharedService.msm' -Recurse | Sort-Object LastWriteTime -Descending | Select-Object -First 1
$loader = Get-ChildItem -LiteralPath $upstream.Path -Filter 'PresentMonAPI2Loader.dll' -Recurse | Where-Object { $_.FullName -match "\\$Configuration\\" } | Select-Object -First 1
if (-not $module) { throw 'PresentMonSharedService.msm was not produced.' }
if (-not $loader) { throw 'PresentMonAPI2Loader.dll was not produced.' }

$wrapper = Join-Path $scriptRoot 'installer\ClawHUD.PresentMonRuntime.wixproj'
Invoke-MSBuild $wrapper @('/p:Platform=x86', '/p:PresentMonSharedServiceModule=' + $module.FullName, '/p:OutputPath=' + "$BuildRoot\")

$smokeDir = Join-Path $BuildRoot 'smoke-test'
New-Item -ItemType Directory -Path $smokeDir -Force | Out-Null
Copy-Item -LiteralPath $loader.FullName -Destination (Join-Path $smokeDir 'PresentMonAPI2Loader.dll') -Force
Copy-Item -LiteralPath (Join-Path $scriptRoot 'smoke-test\PresentMonApi2SmokeTest.cpp') -Destination $smokeDir -Force
Copy-Item -LiteralPath (Join-Path $scriptRoot 'smoke-test\PresentMonApi2SmokeTest.vcxproj') -Destination $smokeDir -Force
Invoke-MSBuild (Join-Path $smokeDir 'PresentMonApi2SmokeTest.vcxproj') @('/p:OutDir=' + "$smokeDir\")

Write-Output "BuildRoot=$BuildRoot"
Write-Output "UpstreamCommit=$($upstream.Commit)"
Write-Output "MergeModule=$($module.FullName)"
Write-Output "Loader=$($loader.FullName)"
Write-Output "RuntimeMsi=$(Join-Path $BuildRoot 'ClawHUD.PresentMonRuntime.msi')"
