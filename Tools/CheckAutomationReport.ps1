param([Parameter(Mandatory = $true)][string]$ReportDirectory)
$ErrorActionPreference = 'Stop'
$reportFile = Join-Path $ReportDirectory 'index.json'
if (!(Test-Path -LiteralPath $reportFile)) { throw "Automation report missing: $reportFile" }
$result = Get-Content -LiteralPath $reportFile -Raw -Encoding UTF8 | ConvertFrom-Json
$failedRequired = @()
foreach ($name in 'EchoAssault.Replay.Timeline','EchoAssault.Gameplay.SpawnOverlap','EchoAssault.Gameplay.CurrentWorldCombat','EchoAssault.Gameplay.ProgressionAndOutcomes') {
    $passed = @($result.tests | Where-Object { $_.fullTestPath -eq $name -and $_.state -eq 'Success' })
    if ($passed.Count -ne 1) { $failedRequired += $name }
}
foreach ($test in @($result.tests | Where-Object { $_.state -ne 'Success' })) {
    Write-Host "Automation $($test.state): $($test.fullTestPath)"
    foreach ($entry in @($test.entries | Where-Object { $_.event.type -eq 'Error' })) {
        Write-Host "  $($entry.event.message) [$($entry.filename):$($entry.lineNumber)]"
    }
}
if ($failedRequired.Count) { throw "Required automation tests did not pass: $($failedRequired -join ', '). See $ReportDirectory" }
if ($null -eq $result.failed -or $result.failed -ne 0) { throw "Automation failure summary is missing or contains failed tests. See $ReportDirectory" }
Write-Host "PASS: all four required Unreal tests. Report: $ReportDirectory"
