param([Parameter(Mandatory = $true)][string]$DiagnosticExe)

$outputDirectory = Split-Path -Parent $DiagnosticExe
$before = @(Get-ChildItem -LiteralPath $outputDirectory -Filter 'game-detect-*.jsonl' -ErrorAction SilentlyContinue)
$diagnosticOutput = cmd.exe /d /c "(echo 1& echo 2& echo 4) | `"$DiagnosticExe`""
$created = @(Get-ChildItem -LiteralPath $outputDirectory -Filter 'game-detect-*.jsonl' | Where-Object { $_.FullName -notin $before.FullName }) | Select-Object -Last 1
if (-not $created) { throw "Diagnostic did not create a JSONL file. Output: $($diagnosticOutput -join ' ')" }
try {
    Get-Content -LiteralPath $created.FullName | ForEach-Object { $_ | ConvertFrom-Json | Out-Null }
    $records = Get-Content -LiteralPath $created.FullName | ForEach-Object { $_ | ConvertFrom-Json }
    if ($records[0].type -ne 'session_header' -or $records[-1].type -ne 'session_stop') { throw 'Unexpected diagnostic JSONL sequence.' }
}
finally {
    Remove-Item -LiteralPath $created.FullName -Force -ErrorAction SilentlyContinue
    $summary = Join-Path $created.DirectoryName ($created.BaseName + '-summary.txt')
    Remove-Item -LiteralPath $summary -Force -ErrorAction SilentlyContinue
}
