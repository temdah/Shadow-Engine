param([Parameter(Mandatory=$true)][string]$CompilerPath)
$ErrorActionPreference = 'Stop'
$output = Join-Path $PSScriptRoot 'driver_identity_harness.exe'
foreach ($diagnostics in @(1, 0)) {
    & $CompilerPath -O2 -Wall -Werror "-DSHADOW_ENGINE_INTERNAL_DIAGNOSTICS=$diagnostics" (Join-Path $PSScriptRoot 'driver_identity_harness.c') -o $output
    if ($LASTEXITCODE -ne 0) { throw "Driver identity harness compilation failed (diagnostics=$diagnostics)." }
    & $output
    if ($LASTEXITCODE -ne 0) { throw "Driver identity harness failed (diagnostics=$diagnostics)." }
}
