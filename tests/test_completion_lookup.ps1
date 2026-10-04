param([Parameter(Mandatory=$true)][string]$CompilerPath)
$ErrorActionPreference = 'Stop'
$output = Join-Path $PSScriptRoot 'completion_lookup_harness.exe'
& $CompilerPath -O2 -Wall -Werror (Join-Path $PSScriptRoot 'completion_lookup_harness.c') -o $output
if ($LASTEXITCODE -ne 0) { throw 'Completion lookup harness compilation failed.' }
& $output
if ($LASTEXITCODE -ne 0) { throw 'Completion lookup harness failed.' }
