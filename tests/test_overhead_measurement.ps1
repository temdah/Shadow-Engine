param([Parameter(Mandatory=$true)][string]$CompilerPath)
$ErrorActionPreference = 'Stop'
$output = Join-Path $PSScriptRoot 'overhead_measurement_harness.exe'
try {
    & $CompilerPath -O2 -Wall -Werror (Join-Path $PSScriptRoot 'overhead_measurement_harness.c') -o $output
    if ($LASTEXITCODE -ne 0) { throw 'Overhead measurement harness compilation failed.' }
    & $output
    if ($LASTEXITCODE -ne 0) { throw 'Overhead measurement harness failed.' }
    & $CompilerPath -O2 -Wall -Werror -DOVERHEAD_WORKLOAD_INTEGRATION=1 (Join-Path $PSScriptRoot 'overhead_measurement_harness.c') -o $output
    if ($LASTEXITCODE -ne 0) { throw 'Overhead/workload integration harness compilation failed.' }
    & $output
    if ($LASTEXITCODE -ne 0) { throw 'Overhead/workload integration harness failed.' }
} finally {
    if (Test-Path -LiteralPath $output) { Remove-Item -LiteralPath $output }
}
