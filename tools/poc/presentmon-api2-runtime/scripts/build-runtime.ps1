[CmdletBinding()]
param(
    [string]$BuildRoot = 'D:\temp\ClawHUD-presentmon-api2-runtime-build',
    [string]$UpstreamRoot = 'D:\temp\PresentMon-v2.5.1-clawhud-poc',
    [string]$Configuration = 'Release',
    [string]$WixBin = ''
)

$ErrorActionPreference = 'Stop'
$scriptRoot = Split-Path -Parent $PSScriptRoot
$prepare = Join-Path $PSScriptRoot 'prepare-upstream.ps1'
$upstream = & $prepare -UpstreamRoot $UpstreamRoot
# The pinned upstream source contains UTF-8 text.  PresentMon's PresentData
# project treats C4819 as an error, so suppress only that source-encoding
# warning through the compiler environment without modifying the upstream
# checkout.
if ($env:CL -notmatch '(^|\s)/wd4819(\s|$)') {
    $env:CL = ('/wd4819 ' + $env:CL).Trim()
}
$msbuild = & 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe' -version '[17.0,18.0)' -latest -products '*' -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe | Select-Object -Last 1
if (-not $msbuild) { throw 'MSBuild was not found through vswhere.' }

$wixCandidates = @(
    $WixBin,
    'C:\Program Files (x86)\WiX Toolset v3.11\bin',
    'C:\Program Files (x86)\WiX Toolset v3.14\bin',
    'D:\Program Files (x86)\WiX Toolset v3.11\bin'
)
$wixBin = $wixCandidates | Where-Object { $_ -and (Test-Path (Join-Path $_ 'candle.exe')) } | Select-Object -First 1
if (-not $wixBin) { throw 'WiX 3.x candle.exe was not found. Install WiX Toolset 3.11+ before building this POC.' }
$wixTargets = Join-Path $wixBin 'Wix.targets'
if (-not (Test-Path -LiteralPath $wixTargets)) { throw "WiX targets were not found beside candle.exe: $wixTargets" }
$vcpkgRoot = Join-Path $upstream.Path 'build\vcpkg'
if (-not (Test-Path -LiteralPath (Join-Path $vcpkgRoot 'vcpkg.exe'))) { throw "Pinned vcpkg tool was not found at $vcpkgRoot. Install the upstream manifest dependencies at the pinned baseline first." }
$vcpkgInstalledDir = Join-Path $upstream.Path 'vcpkg_installed'
if (-not (Test-Path -LiteralPath (Join-Path $vcpkgInstalledDir 'x64-windows-static\include'))) { throw "Pinned vcpkg dependencies were not found at $vcpkgInstalledDir." }
$pocVcpkgProps = Join-Path $scriptRoot 'scripts\PresentMonVcpkg.props'
if (-not (Test-Path -LiteralPath $pocVcpkgProps)) { throw "POC vcpkg MSBuild compatibility props were not found at $pocVcpkgProps." }

New-Item -ItemType Directory -Path $BuildRoot -Force | Out-Null
$solutionDir = "$($upstream.Path)\"
$common = @(
    '/m'
    ('/p:Configuration=' + $Configuration)
    '/p:Platform=x64'
    '/p:PresentMonProductVersion=2.5.1'
    ('/p:WixToolPath=' + "$wixBin\")
    ('/p:WixInstallPath=' + "$wixBin\")
    ('/p:WixTasksPath=' + (Join-Path $wixBin 'WixTasks.dll'))
    ('/p:WixTargetsPath=' + $wixTargets)
    ('/p:VcpkgRoot=' + "$vcpkgRoot\")
    ('/p:VcpkgInstalledDir=' + "$vcpkgInstalledDir\")
    '/p:VcpkgTriplet=x64-windows-static'
    '/p:DisableSpecificWarnings=4819'
    '/p:TreatWarningAsError=false'
    ('/p:CustomVcpkgProps=' + $pocVcpkgProps)
    ('/p:ForceImportAfterCppProps=' + $pocVcpkgProps)
    ('/p:SolutionDir=' + $solutionDir)
)

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
Invoke-MSBuild $wrapper @(
    '/p:Platform=x86'
    ('/p:PresentMonSharedServiceModule=' + $module.FullName)
    ('/p:OutputPath=' + "$BuildRoot\")
)

$smokeDir = Join-Path $BuildRoot 'smoke-test'
New-Item -ItemType Directory -Path $smokeDir -Force | Out-Null
Copy-Item -LiteralPath $loader.FullName -Destination (Join-Path $smokeDir 'PresentMonAPI2Loader.dll') -Force
Copy-Item -LiteralPath (Join-Path $scriptRoot 'smoke-test\PresentMonApi2SmokeTest.cpp') -Destination $smokeDir -Force
Copy-Item -LiteralPath (Join-Path $scriptRoot 'smoke-test\PresentMonApi2SmokeTest.vcxproj') -Destination $smokeDir -Force
Invoke-MSBuild (Join-Path $smokeDir 'PresentMonApi2SmokeTest.vcxproj') @('/p:OutDir=' + "$smokeDir\")

$diagnosticDir = Join-Path $BuildRoot 'diagnostic'
New-Item -ItemType Directory -Path $diagnosticDir -Force | Out-Null
Copy-Item -LiteralPath $loader.FullName -Destination (Join-Path $diagnosticDir 'PresentMonAPI2Loader.dll') -Force
Copy-Item -LiteralPath (Join-Path $scriptRoot 'diagnostic\PresentMonApi2DesktopDiagnostic.cpp') -Destination $diagnosticDir -Force
Copy-Item -LiteralPath (Join-Path $scriptRoot 'diagnostic\PresentMonApi2DesktopDiagnostic.vcxproj') -Destination $diagnosticDir -Force
Invoke-MSBuild (Join-Path $diagnosticDir 'PresentMonApi2DesktopDiagnostic.vcxproj') @(
    ('/p:PresentMonRoot=' + $upstream.Path)
    ('/p:OutDir=' + "$diagnosticDir\")
    '/p:VcpkgEnabled=false'
)

Write-Output "BuildRoot=$BuildRoot"
Write-Output "UpstreamCommit=$($upstream.Commit)"
Write-Output "MergeModule=$($module.FullName)"
Write-Output "Loader=$($loader.FullName)"
Write-Output "RuntimeMsi=$(Join-Path $BuildRoot 'ClawHUD.PresentMonRuntime.msi')"
Write-Output "Diagnostic=$(Join-Path $diagnosticDir 'PresentMonApi2DesktopDiagnostic.exe')"
