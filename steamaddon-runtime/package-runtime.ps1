[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$BuildDirectory,

    [Parameter(Mandatory = $true)]
    [string]$OutputDirectory,

    [Parameter(Mandatory = $true)]
    [string]$RuntimeVersion,

    [Parameter(Mandatory = $true)]
    [string]$SourceCommit
)

$ErrorActionPreference = 'Stop'

function Normalize-RelativePath {
    param([Parameter(Mandatory = $true)][string]$Path)
    return ($Path -replace '\\', '/')
}

function Resolve-FullPath {
    param([Parameter(Mandatory = $true)][string]$Path)
    return [IO.Path]::GetFullPath($Path)
}

if ($RuntimeVersion -notmatch '^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$') {
    throw "RuntimeVersion must be MAJOR.MINOR.PATCH without a tag prefix, prerelease suffix, or build metadata: '$RuntimeVersion'."
}
if ($SourceCommit -notmatch '^[0-9a-fA-F]{40}$') {
    throw "SourceCommit must be exactly 40 hexadecimal characters: '$SourceCommit'."
}

$scriptRoot = Resolve-FullPath $PSScriptRoot
$sourceRoot = Resolve-FullPath (Join-Path $scriptRoot '..')
$buildRoot = Resolve-FullPath $BuildDirectory
$outputRoot = Resolve-FullPath $OutputDirectory
$manifestPath = Join-Path $scriptRoot 'payload.manifest.json'
$tag = "steamaddon-runtime-v$RuntimeVersion"

if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
    throw "Payload manifest is missing: $manifestPath"
}
if (-not (Test-Path -LiteralPath $buildRoot -PathType Container)) {
    throw "Build directory is missing: $buildRoot"
}
if ($outputRoot -eq $sourceRoot -or $outputRoot -eq [IO.Path]::GetPathRoot($outputRoot)) {
    throw "OutputDirectory is too broad or points at the source root: $outputRoot"
}

$payloadManifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
if ($payloadManifest.schema_version -ne 1) {
    throw "Unsupported payload manifest schema: $($payloadManifest.schema_version)"
}
$requiredFiles = @($payloadManifest.required_files | ForEach-Object { [string]$_ })
$forbiddenFiles = @($payloadManifest.forbidden_files | ForEach-Object { Normalize-RelativePath ([string]$_) })
if ($requiredFiles.Count -eq 0) {
    throw 'Payload manifest has no required files.'
}

$releaseRoot = Join-Path $buildRoot 'Release'
if (-not (Test-Path -LiteralPath (Join-Path $releaseRoot 'ClawHUD.exe') -PathType Leaf)) {
    $releaseRoot = $buildRoot
}

$stagingRoot = Join-Path $outputRoot '.staging'
$payloadRoot = Join-Path $stagingRoot 'clawhud'
$zipPath = Join-Path $outputRoot 'ClawHUDRuntime.zip'
$sidecarPath = "$zipPath.sha256"
$externalManifestPath = Join-Path $outputRoot 'runtime-manifest.json'

New-Item -ItemType Directory -Force -Path $outputRoot | Out-Null
Remove-Item -LiteralPath $outputRoot -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $payloadRoot | Out-Null

function Get-SourcePath {
    param([Parameter(Mandatory = $true)][string]$RelativePath)
    $normalized = Normalize-RelativePath $RelativePath
    if ($normalized -eq 'LICENSE' -or $normalized -eq 'THIRD-PARTY-NOTICES.md') {
        return Join-Path $sourceRoot ($normalized -replace '/', [IO.Path]::DirectorySeparatorChar)
    }
    return Join-Path $releaseRoot ($normalized -replace '/', [IO.Path]::DirectorySeparatorChar)
}

foreach ($relative in $requiredFiles) {
    $normalized = Normalize-RelativePath $relative
    if ($normalized.StartsWith('/') -or $normalized.Contains('../') -or $normalized -match '^[A-Za-z]:') {
        throw "Payload manifest contains an unsafe relative path: $relative"
    }
    $sourcePath = Get-SourcePath $relative
    if (-not (Test-Path -LiteralPath $sourcePath -PathType Leaf)) {
        throw "Required runtime file is missing: $relative (looked at $sourcePath)"
    }
    $destinationPath = Join-Path $payloadRoot ($normalized -replace '/', [IO.Path]::DirectorySeparatorChar)
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $destinationPath) | Out-Null
    Copy-Item -LiteralPath $sourcePath -Destination $destinationPath -Force
}

function Get-PayloadFiles {
    return @(Get-ChildItem -LiteralPath $payloadRoot -Recurse -File | ForEach-Object {
        [pscustomobject]@{
            Item = $_
            Relative = Normalize-RelativePath ([IO.Path]::GetRelativePath($payloadRoot, $_.FullName))
        }
    })
}

$forbiddenNames = @('coreclr.dll', 'clrjit.dll', 'hostfxr.dll', 'hostpolicy.dll', 'dotnet.exe')
$payloadFiles = Get-PayloadFiles
foreach ($entry in $payloadFiles) {
    $name = $entry.Item.Name
    if ($forbiddenNames -contains $name.ToLowerInvariant()) {
        throw "Payload contains a private .NET runtime file: $($entry.Relative)"
    }
    if ($forbiddenFiles -contains $entry.Relative -or
        $entry.Relative -match '(^|/)([^/]+)-(full|delta)\.nupkg$') {
        throw "Payload contains a forbidden file: $($entry.Relative)"
    }
}

$identityManifest = [ordered]@{
    schema_version = 1
    runtime_version = $RuntimeVersion
    tag = $tag
    source_commit = $SourceCommit.ToLowerInvariant()
    asset = 'ClawHUDRuntime.zip'
    sha256 = $null
}
$embeddedManifestPath = Join-Path $payloadRoot 'runtime-manifest.json'
$identityManifest | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $embeddedManifestPath -Encoding utf8NoBOM

$zipParent = Join-Path $outputRoot '.zip-root'
New-Item -ItemType Directory -Force -Path $zipParent | Out-Null
Copy-Item -LiteralPath (Join-Path $stagingRoot 'clawhud') -Destination $zipParent -Recurse -Force
Add-Type -AssemblyName System.IO.Compression.FileSystem
[IO.Compression.ZipFile]::CreateFromDirectory(
    $zipParent,
    $zipPath,
    [IO.Compression.CompressionLevel]::Optimal,
    $false)
$sha256 = (Get-FileHash -LiteralPath $zipPath -Algorithm SHA256).Hash.ToLowerInvariant()

$identityManifest.sha256 = $sha256
$identityManifest | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $externalManifestPath -Encoding utf8NoBOM
"$sha256  ClawHUDRuntime.zip" | Set-Content -LiteralPath $sidecarPath -Encoding ascii

$zipHashCheck = (Get-FileHash -LiteralPath $zipPath -Algorithm SHA256).Hash.ToLowerInvariant()
if ($zipHashCheck -ne $sha256) {
    throw "ZIP hash changed unexpectedly while writing release metadata."
}

Remove-Item -LiteralPath $stagingRoot, $zipParent -Recurse -Force

$finalFiles = @(Get-ChildItem -LiteralPath $outputRoot -File | Sort-Object Name)
Write-Host "SteamAddon Runtime package created"
Write-Host "Runtime version: $RuntimeVersion"
Write-Host "Tag: $tag"
Write-Host "Source commit: $($SourceCommit.ToLowerInvariant())"
Write-Host "SHA-256: $sha256"
Write-Host 'Artifacts:'
foreach ($file in $finalFiles) {
    Write-Host ("  {0} ({1} bytes)" -f $file.Name, $file.Length)
}
