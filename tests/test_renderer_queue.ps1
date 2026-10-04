param([Parameter(Mandatory=$true)][string]$CompilerPath)
$ErrorActionPreference = 'Stop'
$output = Join-Path $PSScriptRoot 'renderer_queue_harness.exe'
& $CompilerPath -O2 -Wall -Werror (Join-Path $PSScriptRoot 'renderer_queue_harness.c') -o $output
if ($LASTEXITCODE -ne 0) { throw 'Renderer queue harness compilation failed.' }
& $output
if ($LASTEXITCODE -ne 0) { throw 'Renderer queue harness failed.' }
