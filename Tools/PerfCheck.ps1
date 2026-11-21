param([string]$EngineRoot = "$env:ProgramFiles\Epic Games\UE_5.5")
$ErrorActionPreference = 'Stop'

$root = (Resolve-Path "$PSScriptRoot\..").Path
$uat = Join-Path $EngineRoot 'Engine\Build\BatchFiles\RunUAT.bat'
$python = (Get-Command python -CommandType Application | Select-Object -First 1).Source
if (!(Test-Path -LiteralPath $uat -PathType Leaf)) { throw "Unreal AutomationTool missing: $uat" }
$active = Get-Process UnrealEditor,UnrealEditor-Cmd,LiveCodingConsole,UnrealBuildTool,EchoAssault,EchoAssault-Win64-Development -ErrorAction SilentlyContinue
if ($active) { throw "Close active Unreal and Echo Assault processes before measuring: $($active.ProcessName -join ', ')" }

$runId = Get-Date -Format 'yyyyMMdd-HHmmss-fff'
$report = [IO.Directory]::CreateDirectory("$root\Saved\Profiles\PerfCheck-$runId").FullName
$archive = "$root\Artifacts\Windows-PerfCheck-$runId"
$manifest = [ordered]@{
    status = 'Running'
    git_head = (& git -C $root rev-parse HEAD)
    git_status = @(& git -C $root status --porcelain --untracked-files=normal)
    archive = $archive
    executable_sha256 = $null
    build_inputs = [ordered]@{ status = 'NotCompared'; before = $null; after = $null }
    runs = @()
}
if ($LASTEXITCODE -ne 0) { throw 'Could not read Git state.' }
function Save-Manifest {
    $manifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath "$report\run.json" -Encoding UTF8
}

function Save-BuildInputSnapshot([string]$Stage) {
    $json = & $python "$PSScriptRoot\PerfCheckReport.py" --build-inputs $root
    if ($LASTEXITCODE -ne 0) { throw "Could not capture build inputs: $Stage" }
    $file = "build-inputs-$Stage.json"
    $json | Set-Content -LiteralPath "$report\$file" -Encoding UTF8
    $snapshot = ($json -join "`n") | ConvertFrom-Json
    return [ordered]@{ sha256 = $snapshot.sha256; file_count = $snapshot.file_count; snapshot = $file }
}

Write-Host "PerfCheck evidence: $report"
Save-Manifest
try {
    $manifest.build_inputs.before = Save-BuildInputSnapshot 'before'
    Save-Manifest
    & $uat BuildCookRun "-project=$root\EchoAssault.uproject" -noP4 -utf8output -unattended -platform=Win64 `
        -clientconfig=Development -build -cook -stage -pak -package -prereqs -nodebuginfo -archive "-archivedirectory=$archive" |
        Tee-Object -FilePath "$report\package.log" | Out-Host
    if ($LASTEXITCODE -ne 0) { throw "Development packaging failed. See $report\package.log" }

    $manifest.build_inputs.after = Save-BuildInputSnapshot 'after'
    $manifest.build_inputs.status = 'Stable'
    if ($manifest.build_inputs.before.sha256 -ne $manifest.build_inputs.after.sha256) {
        $manifest.build_inputs.status = 'Changed'
        Write-Warning "Build inputs differ before and after packaging. See $report\build-inputs-*.json"
    }

    $exe = Join-Path $archive 'EchoAssault.exe'
    $binary = Join-Path $archive 'EchoAssault\Binaries\Win64\EchoAssault.exe'
    if (!(Test-Path -LiteralPath $exe -PathType Leaf) -or !(Test-Path -LiteralPath $binary -PathType Leaf)) {
        throw "Archived launcher or binary missing: $archive"
    }
    $manifest.executable_sha256 = (Get-FileHash -LiteralPath $binary -Algorithm SHA256).Hash
    Save-Manifest

    foreach ($repeat in 1..3) {
        $order = if ($repeat % 2 -eq 1) { 'Moving', 'Stationary' } else { 'Stationary', 'Moving' }
        foreach ($mode in $order) {
            $name = "$repeat-$mode"
            $case = [IO.Directory]::CreateDirectory("$report\$name").FullName
            $arguments = @('-EchoEnemyCrowdPreview','-EchoEnemyCrowdBenchmark','-EchoCrowdSustain','-EchoPerfCheck',
                '-unattended','-nosound','-nosplash','-RenderOffscreen','-dx12','-windowed','-ForceRes','-ResX=1920','-ResY=1080',
                '-novsync','-benchmark','-FPS=60','-seconds=62','-ExecCmds="r.GPUCsvStatsEnabled 1,Defer CSVProfile FRAMES=3660"',
                "-UserDir=`"$case\user`"", "-abslog=`"$case\run.log`"")
            if ($mode -eq 'Stationary') { $arguments += '-EchoPerfCheckStationary' }
            Write-Host "Running $name"
            $process = Start-Process -FilePath $exe -WorkingDirectory (Split-Path -Parent $exe) -WindowStyle Hidden `
                -ArgumentList $arguments -Wait -PassThru
            $manifest.runs += [ordered]@{ name = $name; mode = $mode; repeat = $repeat; exit_code = $process.ExitCode }
            Save-Manifest
            & $python "$PSScriptRoot\PerfCheckReport.py" --case $report $name
            if ($LASTEXITCODE -ne 0) { throw "Invalid capture: $case" }
        }
    }

    & $python "$PSScriptRoot\PerfCheckReport.py" --report $report
    if ($LASTEXITCODE -ne 0) { throw "Could not summarize the six captures: $report" }
    $manifest.status = 'MeasurementValid'
} catch {
    $manifest.status = 'MeasurementFailed'
    $manifest.failure = $_.Exception.Message
    throw
} finally {
    Save-Manifest
}
