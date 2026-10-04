param([Parameter(Mandatory=$true)][string]$CompilerPath)
$ErrorActionPreference = 'Stop'
$output = Join-Path $PSScriptRoot 'detail_window_harness.exe'
& $CompilerPath -O2 -Wall -Werror (Join-Path $PSScriptRoot 'detail_window_harness.c') -o $output
if ($LASTEXITCODE -ne 0) { throw 'Detail window harness compilation failed.' }
& $output
if ($LASTEXITCODE -ne 0) { throw 'Detail window harness failed.' }
