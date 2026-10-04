param([Parameter(Mandatory=$true)][string]$CompilerPath)
$ErrorActionPreference = 'Stop'
$outputDirectory = Join-Path (Split-Path $PSScriptRoot -Parent) 'build\tests'
New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null
$output = Join-Path $outputDirectory 'periodic_summary_harness.exe'
& $CompilerPath -O2 -Wall -Werror (Join-Path $PSScriptRoot 'periodic_summary_harness.c') -o $output
if ($LASTEXITCODE -ne 0) { throw 'Periodic summary harness compilation failed.' }
& $output
if ($LASTEXITCODE -ne 0) { throw 'Periodic summary harness failed.' }
