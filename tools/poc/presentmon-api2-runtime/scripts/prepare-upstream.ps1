[CmdletBinding()]
param(
    [string]$UpstreamRoot = 'D:\temp\PresentMon-v2.6.0-clawhud-poc',
    [switch]$Force
)

$ErrorActionPreference = 'Stop'
$repoUrl = 'https://github.com/GameTechDev/PresentMon.git'
$tag = 'v2.6.0'
$expectedCommit = 'e13fce6acdb55a808fd8318175a56863e532d95f'

if (Test-Path -LiteralPath $UpstreamRoot) {
    if (-not $Force) {
        $actual = (git -C $UpstreamRoot rev-parse HEAD).Trim()
        if ($actual -ne $expectedCommit) {
            throw "Existing upstream checkout is $actual, expected $expectedCommit. Use -Force only for this external POC checkout."
        }
    } else {
        Remove-Item -LiteralPath $UpstreamRoot -Recurse -Force
    }
}

if (-not (Test-Path -LiteralPath $UpstreamRoot)) {
    git clone --branch $tag --depth 1 $repoUrl $UpstreamRoot | Out-Host
}

$actualCommit = (git -C $UpstreamRoot rev-parse HEAD).Trim()
if ($actualCommit -ne $expectedCommit) {
    throw "Pinned tag resolved to $actualCommit, expected $expectedCommit."
}

$trackedChanges = @(git -C $UpstreamRoot status --porcelain=v1 --untracked-files=no)
if ($LASTEXITCODE -ne 0) {
    throw "Failed to verify the pinned upstream worktree at $UpstreamRoot."
}
if ($trackedChanges.Count -ne 0) {
    $trackedChangeList = $trackedChanges -join [Environment]::NewLine
    throw "Pinned upstream checkout contains tracked modifications. Recreate it with -Force before building.`n$trackedChangeList"
}

[pscustomobject]@{
    Repository = $repoUrl
    Tag = $tag
    Commit = $actualCommit
    Path = $UpstreamRoot
}
