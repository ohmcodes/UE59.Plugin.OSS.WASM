# Shared paths for the build scripts. Override any of these with environment variables.
$EngineRoot  = if ($env:OSSWASM_ENGINE) { $env:OSSWASM_ENGINE } else { "D:\UE5 Source\UE59Prev2_SRC" }
$ProjectRoot = Split-Path -Parent $PSScriptRoot
$ProjectFile = Join-Path $ProjectRoot "UE59PluginOSSWASM.uproject"
$ProjectName = "UE59PluginOSSWASM"
$OutDir      = Join-Path $PSScriptRoot "out"
New-Item -ItemType Directory -Force $OutDir | Out-Null

# Writes a short summary (result + errors + first warnings) from a UBT/UAT log. Returns $true on success.
function Write-BuildSummary([string]$Name, [string]$LogPath, [int]$ExitCode) {
    $summary = Join-Path $OutDir "$Name.summary.txt"
    $lines = Get-Content $LogPath -ErrorAction SilentlyContinue
    $errors = $lines | Where-Object { $_ -match "(?i)\berror\b|: fatal|undefined symbol|Unable to " } | Select-Object -Unique -First 40
    $warnings = $lines | Where-Object { $_ -match "(?i)\bwarning\b" -and $_ -match "OnlineSubsystemWASM|WasmOss|$ProjectName" } | Select-Object -Unique -First 15
    $result = $lines | Where-Object { $_ -match "^Result: |BUILD SUCCESSFUL|AutomationTool exiting with ExitCode" } | Select-Object -Last 1
    $out = @("$Name : " + $(if ($ExitCode -eq 0) { "PASS" } else { "FAIL (exit $ExitCode)" }), "result line: $result", "log: $LogPath")
    if ($errors) { $out += "--- errors"; $out += $errors }
    if ($warnings) { $out += "--- plugin warnings"; $out += $warnings }
    $out | Set-Content -Encoding utf8 $summary
    $out | ForEach-Object { Write-Host $_ }
    return ($ExitCode -eq 0)
}
