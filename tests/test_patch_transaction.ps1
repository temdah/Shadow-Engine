param(
    [string]$CompilerPath
)

$ErrorActionPreference = 'Stop'
$project = Split-Path -Parent $PSScriptRoot
if (-not $CompilerPath) {
    $workspace = Split-Path -Parent (Split-Path -Parent $project)
    $CompilerPath = Join-Path $workspace `
        'Tools\Applications\tcc-0.9.27-win64\tcc\tcc.exe'
}
if (-not (Test-Path -LiteralPath $CompilerPath)) {
    throw 'TinyCC 0.9.27 was not found. Pass -CompilerPath.'
}

$output = Join-Path $PSScriptRoot 'patch_transaction_harness.exe'
& $CompilerPath -O2 -Wall -Werror `
    (Join-Path $PSScriptRoot 'patch_transaction_harness.c') -o $output
if ($LASTEXITCODE -ne 0) {
    throw "Transaction harness compilation failed with exit code $LASTEXITCODE."
}
& $output
if ($LASTEXITCODE -ne 0) {
    throw "Transaction harness failed with exit code $LASTEXITCODE."
}
