param([Parameter(Mandatory=$true)][string]$CompilerPath)
$ErrorActionPreference = 'Stop'
$output = Join-Path $PSScriptRoot 'quality_processing_q1_q2_harness.exe'
& $CompilerPath -O2 -Wall -Werror (Join-Path $PSScriptRoot 'quality_processing_harness.c') -o $output
if ($LASTEXITCODE -ne 0) { throw 'Q1/Q2 quality processing harness compilation failed.' }
& $output
if ($LASTEXITCODE -ne 0) { throw 'Q1/Q2 quality processing harness failed.' }
