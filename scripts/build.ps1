# Compiles the host project + plugin from the command line. Close the editor first.
#   ./scripts/build.ps1                 -> Editor target, Win64 Development
#   ./scripts/build.ps1 -Target Game -Platform Wasm   -> game target for the web (checks the fetch bridge compiles)
param(
    [ValidateSet("Editor", "Game")] [string]$Target = "Editor",
    [string]$Platform = "Win64",
    [string]$Configuration = "Development"
)
. "$PSScriptRoot\config.ps1"

$targetName = if ($Target -eq "Editor") { "${ProjectName}Editor" } else { $ProjectName }
$name = "build-$Target-$Platform".ToLower()
$log = Join-Path $OutDir "$name.log"
$buildBat = Join-Path $EngineRoot "Engine\Build\BatchFiles\Build.bat"

Write-Host "Building $targetName $Platform $Configuration ..."
& $buildBat $targetName $Platform $Configuration "-Project=$ProjectFile" -WaitMutex -NoHotReloadFromIDE *> $log
$code = $LASTEXITCODE
if (-not (Write-BuildSummary $name $log $code)) { exit 1 }
