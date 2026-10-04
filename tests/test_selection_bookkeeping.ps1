param([Parameter(Mandatory=$true)][string]$CompilerPath)
$ErrorActionPreference = 'Stop'
$directory = Join-Path $PSScriptRoot ('../build/tests/selection-bookkeeping/' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $directory -Force | Out-Null
foreach ($diagnostics in @(1, 0)) {
    # Distinct files avoid replacing a just-executed image still held by Windows.
    $output = Join-Path $directory ("selection_bookkeeping_diag$diagnostics.exe")
    & $CompilerPath -O2 -Wall -Werror "-DSHADOW_ENGINE_INTERNAL_DIAGNOSTICS=$diagnostics" (Join-Path $PSScriptRoot 'selection_bookkeeping_harness.c') -o $output
    if ($LASTEXITCODE -ne 0) { throw "Selection bookkeeping harness compilation failed (diagnostics=$diagnostics)." }
    & $output
    if ($LASTEXITCODE -ne 0) { throw "Selection bookkeeping harness failed (diagnostics=$diagnostics)." }
}
