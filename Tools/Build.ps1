param([string]$EngineRoot = "$env:ProgramFiles\Epic Games\UE_5.5", [switch]$Game)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path "$PSScriptRoot\..").Path
$target = if ($Game) { 'EchoAssault' } else { 'EchoAssaultEditor' }
$logDir = [IO.Directory]::CreateDirectory("$root\Saved\Logs").FullName
$build = Join-Path $EngineRoot 'Engine\Build\BatchFiles\Build.bat'
if (!(Test-Path $build)) { throw "Unreal Engine path missing: $EngineRoot" }
$executable = if ($Game) { "$root\Binaries\Win64\EchoAssault.exe" } else { "$EngineRoot\Engine\Binaries\Win64\UnrealEditor.exe" }
$mutexName = 'Global\LiveCoding_' + ($executable -replace '[/\\:]', '+')
$liveMutex = $null
if ([Threading.Mutex]::TryOpenExisting($mutexName, [ref]$liveMutex)) {
    $liveMutex.Dispose()
    throw "Live Coding is active for $executable. Save the EchoAssault editor, stop PIE, then close that editor and its game before retrying. Do not close other projects automatically."
}
Write-Host "Building $target from $root"
& $build $target Win64 Development "-Project=$root\EchoAssault.uproject" "-Log=$logDir\UBT-$target.log" -WaitMutex -NoUBA -NoUBALocal -NoUBTMakefiles -MaxParallelActions=3
exit $LASTEXITCODE
