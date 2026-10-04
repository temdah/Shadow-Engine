param([Parameter(Mandatory=$true)][string]$CompilerPath)
$ErrorActionPreference = 'Stop'
foreach ($settingsDiagnostics in @(1, 0)) {
    $settingsOutput = Join-Path $PSScriptRoot "../build/tests/saved-settings-diag$settingsDiagnostics"
    New-Item -ItemType Directory -Force -Path $settingsOutput | Out-Null
    $settingsExecutable = Join-Path $settingsOutput 'saved_settings_harness.exe'
    & $CompilerPath -O2 -Wall -Werror "-DSHADOW_ENGINE_INTERNAL_DIAGNOSTICS=$settingsDiagnostics" (Join-Path $PSScriptRoot 'saved_settings_harness.c') -o $settingsExecutable
    if ($LASTEXITCODE -ne 0) { throw "Saved-settings diagnostics=$settingsDiagnostics compilation failed." }
    & $settingsExecutable
    if ($LASTEXITCODE -ne 0) { throw "Saved-settings diagnostics=$settingsDiagnostics checks failed." }
}
