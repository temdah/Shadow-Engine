param([Parameter(Mandatory=$true)][string]$CompilerPath)
$ErrorActionPreference = 'Stop'
foreach ($startupDiagnostics in @(1, 0)) {
    $startupOutput = Join-Path $PSScriptRoot "../build/tests/startup-supervisor-diag$startupDiagnostics"
    New-Item -ItemType Directory -Force -Path $startupOutput | Out-Null
    $startupExecutable = Join-Path $startupOutput 'startup_supervisor_harness.exe'
    & $CompilerPath -O2 -Wall -Werror "-DSHADOW_ENGINE_INTERNAL_DIAGNOSTICS=$startupDiagnostics" (Join-Path $PSScriptRoot 'startup_supervisor_harness.c') -o $startupExecutable
    if ($LASTEXITCODE -ne 0) { throw "Startup supervisor diagnostics=$startupDiagnostics compilation failed." }
    & $startupExecutable
    if ($LASTEXITCODE -ne 0) { throw "Startup supervisor diagnostics=$startupDiagnostics checks failed." }
}
