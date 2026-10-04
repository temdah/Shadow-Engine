param([Parameter(Mandatory=$true)][string]$CompilerPath)
$ErrorActionPreference = 'Stop'
$driverPolicyOutput = Join-Path $PSScriptRoot '../build/tests/driver-policy'
New-Item -ItemType Directory -Force -Path $driverPolicyOutput | Out-Null
$driverPolicyExecutable = Join-Path $driverPolicyOutput 'driver_policy_harness.exe'
foreach ($driverPolicyDiagnostics in 1, 0) {
    & $CompilerPath -O2 -Wall -Werror "-DSHADOW_ENGINE_INTERNAL_DIAGNOSTICS=$driverPolicyDiagnostics" (Join-Path $PSScriptRoot 'driver_policy_harness.c') -o $driverPolicyExecutable
    if ($LASTEXITCODE -ne 0) { throw 'Driver policy harness compilation failed.' }
    & $driverPolicyExecutable
    if ($LASTEXITCODE -ne 0) { throw 'Driver policy harness failed.' }
}
