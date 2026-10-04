param([Parameter(Mandatory=$true)][string]$CompilerPath,[string]$EvidenceDirectory)
$ErrorActionPreference = 'Stop'
$output = Join-Path $PSScriptRoot 'intersection_harness.exe'
& $CompilerPath -O2 -Wall -Werror (Join-Path $PSScriptRoot 'intersection_harness.c') -o $output
if ($LASTEXITCODE -ne 0) { throw 'Intersection harness compilation failed.' }
if ($EvidenceDirectory) {
    $captureDirectory = (Resolve-Path -LiteralPath $EvidenceDirectory).Path.TrimEnd('\') + '\'
    & $output $captureDirectory
} else {
    & $output
}
if ($LASTEXITCODE -ne 0) { throw 'Intersection harness failed.' }
