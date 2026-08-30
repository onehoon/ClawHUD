[CmdletBinding()]
param(
    [string]$UpstreamRoot = 'D:\temp\PresentMon-v2.5.1-clawhud-poc',
    [switch]$Force
)

$ErrorActionPreference = 'Stop'
$repoUrl = 'https://github.com/GameTechDev/PresentMon.git'
$tag = 'v2.5.1'
$expectedCommit = '3e06c7dcb922e411bae38503b51ab501be61c37f'

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

[pscustomobject]@{
    Repository = $repoUrl
    Tag = $tag
    Commit = $actualCommit
    Path = $UpstreamRoot
}
