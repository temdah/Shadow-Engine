param(
    [Parameter(Mandatory=$true)][string]$CompilerPath,
    [Parameter(Mandatory=$true)][string]$KnownHost,
    [Parameter(Mandatory=$true)][string]$UpdatedHost
)
$ErrorActionPreference = 'Stop'
$fixtures = @(
    @{ Path=$KnownHost; Hash='8120389BA4D144DC465E9FED2648B5690A6BBF0CDF5B052EF6EE3B738BE5DCFC' },
    @{ Path=$UpdatedHost; Hash='03875BACFAD447397305B9D5F19BCB07DC4473B99F7D1354604B4A2BB1DFE727' }
)
foreach ($fixture in $fixtures) {
    if ((Get-FileHash -LiteralPath $fixture.Path -Algorithm SHA256).Hash -ne $fixture.Hash) {
        throw 'Host fixture identity mismatch. Use the documented private captures.'
    }
}
$output = Join-Path $PSScriptRoot '../build/tests/nexus-compatibility'
New-Item -ItemType Directory -Force -Path $output | Out-Null
$executable = Join-Path $output 'nexus_compatibility_harness.exe'
& $CompilerPath -O2 -Wall -Werror (Join-Path $PSScriptRoot 'nexus_compatibility_harness.c') -o $executable
if ($LASTEXITCODE -ne 0) { throw 'Nexus compatibility harness compilation failed.' }
& $executable (Resolve-Path -LiteralPath $KnownHost).Path (Resolve-Path -LiteralPath $UpdatedHost).Path
if ($LASTEXITCODE -ne 0) { throw 'Nexus compatibility checks failed.' }
