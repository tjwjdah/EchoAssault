param([string]$EngineRoot = "$env:ProgramFiles\Epic Games\UE_5.5", [switch]$Play)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path "$PSScriptRoot\..").Path
$project = "$root\EchoAssault.uproject"
$editor = "$EngineRoot\Engine\Binaries\Win64\UnrealEditor-Cmd.exe"
$logs = [IO.Directory]::CreateDirectory("$root\Saved\Logs").FullName
$runId = [DateTime]::Now.ToString('yyyyMMdd-HHmmss-fff')
$importLog = "$logs\Import-$runId.log"
$report = "$root\Saved\TestReports\$runId"
function Invoke-Editor([string[]]$Arguments) {
    & $editor @Arguments | Out-Host
    if ($LASTEXITCODE -ne 0) { throw "Unreal Editor exit $LASTEXITCODE. See $logs" }
}
Start-Transcript -Path "$logs\Verify-$runId.log" | Out-Null
$stage = 'Build'
$verified = $false
$failure = $null
try {
& "$PSScriptRoot\Build.ps1" -EngineRoot $EngineRoot | Out-Host
if ($LASTEXITCODE -ne 0) { throw "Editor build failed. See $logs" }
$stage = 'NativeReplay'
& "$PSScriptRoot\ReplayCheck.cmd" | Out-Host
if ($LASTEXITCODE -ne 0) { throw "Native replay test failed (exit $LASTEXITCODE). See $root\Saved\Checks\run-*" }
$stage = 'Assets'
$missing = @('Scout.uasset','Hunter.uasset','Lancer.uasset','Crystal.uasset','Hull.uasset','Arena.umap') |
    Where-Object { !(Test-Path "$root\Content\Echo\$_") }
if ($missing) {
    Invoke-Editor @($project,'-run=pythonscript',"-script=$PSScriptRoot\ImportArena.py",'-unattended','-NullRHI','-nosound',"-abslog=$importLog")
    if (!(Test-Path $importLog) -or !(Select-String -Quiet -SimpleMatch 'ECHO_IMPORT_OK' -Path $importLog)) {
        throw "No completed Unreal asset import. Open UE 5.5 Editor, Tools > Execute Python Script > Tools\ImportArena.py, then retry. Expected log: $importLog"
    }
}
foreach ($name in 'Scout.uasset','Hunter.uasset','Lancer.uasset','Crystal.uasset','Hull.uasset','Arena.umap') {
    if (!(Test-Path "$root\Content\Echo\$name")) { throw "Missing imported asset: $name" }
}
$stage = 'UnrealAutomation'
Invoke-Editor @($project,'/Game/Echo/Arena','-unattended','-NullRHI','-nosound','-nosplash','-ExecCmds=Automation RunTests EchoAssault','-TestExit=Automation Test Queue Empty',"-ReportExportPath=$report","-abslog=$logs\Tests-$runId.log")
if (!(Test-Path "$logs\Tests-$runId.log")) { throw "Unreal returned without creating its test log. Runtime verification did not run: $logs\Tests-$runId.log" }
& "$PSScriptRoot\CheckAutomationReport.ps1" -ReportDirectory $report
Write-Host "Verified build, assets, replay and gameplay automation. Report: $report"
$stage = 'Complete'
$verified = $true
if ($Play) {
    $gui = "$EngineRoot\Engine\Binaries\Win64\UnrealEditor.exe"
    Start-Process -FilePath $gui -WorkingDirectory $root -WindowStyle Normal -ArgumentList ('"{0}" /Game/Echo/Arena -game -windowed -ResX=1280 -ResY=720' -f $project)
}
} catch {
    $failure = $_ | Out-String
    [IO.File]::WriteAllText("$logs\Verify-$runId-error.log", $failure)
    throw
} finally {
    @{ RunId = $runId; ProjectRoot = $root; Stage = $stage; Verified = $verified; Error = $failure; Report = $report } |
        ConvertTo-Json | Set-Content -LiteralPath "$logs\Verify-$runId-result.json" -Encoding UTF8
    Stop-Transcript | Out-Null
}
