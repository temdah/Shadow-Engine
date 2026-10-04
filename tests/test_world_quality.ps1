param([Parameter(Mandatory=$true)][string]$CompilerPath)
$ErrorActionPreference = 'Stop'
$output = Join-Path $PSScriptRoot 'world_quality_harness.exe'
& $CompilerPath -O2 -Wall -Werror (Join-Path $PSScriptRoot 'world_quality_harness.c') -o $output
if ($LASTEXITCODE -ne 0) { throw 'World quality harness compilation failed.' }
& $output
if ($LASTEXITCODE -ne 0) { throw 'World quality harness failed.' }
