param([Parameter(Mandatory=$true)][string]$CompilerPath)
$ErrorActionPreference = 'Stop'
$output = Join-Path $PSScriptRoot 'vehicle_capture_harness.exe'
& $CompilerPath -O2 -Wall -Werror (Join-Path $PSScriptRoot 'vehicle_capture_harness.c') -o $output
if ($LASTEXITCODE -ne 0) { throw 'Vehicle capture harness compilation failed.' }
& $output
if ($LASTEXITCODE -ne 0) { throw 'Vehicle capture harness failed.' }
