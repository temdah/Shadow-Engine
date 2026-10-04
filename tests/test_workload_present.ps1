param(
    [Parameter(Mandatory=$true)][string]$CompilerPath,
    [switch]$RealDxgi,
    [switch]$RealPrivateTransport
)
$ErrorActionPreference = 'Stop'
$output = Join-Path $PSScriptRoot 'workload_present_harness.exe'
& $CompilerPath -O2 -Wall -Werror (Join-Path $PSScriptRoot 'workload_present_harness.c') -o $output -luser32 -ladvapi32
if ($LASTEXITCODE -ne 0) { throw 'Private presentation capture harness compilation failed.' }
& $output
if ($LASTEXITCODE -ne 0) { throw 'Private presentation capture fixtures failed.' }
if ($RealPrivateTransport) {
    & $output --real-private-transport
    if ($LASTEXITCODE -ne 0) { throw 'Real ordinary-user private transport with synthetic provider failed.' }
}
if ($RealDxgi) {
    & $output --real-dxgi
    if ($LASTEXITCODE -ne 0) { throw 'Real ordinary-user DXGI private-delivery probe failed; no game result is implied.' }
}
