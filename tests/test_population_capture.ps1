param(
    [Parameter(Mandatory=$true)][string]$CompilerPath,
    [string]$RuntimeImage,
    [string]$ArtifactDirectory
)
$ErrorActionPreference = 'Stop'
if (-not $RuntimeImage) {
    $RuntimeImage = Join-Path (Split-Path (Split-Path $PSScriptRoot -Parent) -Parent) 'Support/Evidence/RuntimeCaptures/Disrupt_b64_runtime_rebuilt.dll'
}
if (-not (Test-Path -LiteralPath $RuntimeImage)) { throw 'Preserved Global runtime image is required for independent population proof fixtures.' }
$expectedImageHash = 'd8004145fb17bd6a9073466ebb9bfc19589aad7d08d8ec3fd45733a5d8ad0855'
if ((Get-FileHash -LiteralPath $RuntimeImage -Algorithm SHA256).Hash.ToLowerInvariant() -ne $expectedImageHash) {
    throw 'Population proof image does not match the independently recorded Global evidence.'
}
$output = Join-Path $PSScriptRoot 'population_capture_harness.exe'
& $CompilerPath -O2 -Wall -Werror (Join-Path $PSScriptRoot 'population_capture_harness.c') -o $output
if ($LASTEXITCODE -ne 0) { throw 'Population capture fixture compilation failed.' }
$populationFixtureArguments = @([System.IO.Path]::GetFullPath($RuntimeImage))
if ($ArtifactDirectory) {
    $populationFixtureDirectory = [System.IO.Path]::GetFullPath($ArtifactDirectory)
    New-Item -ItemType Directory -Force -Path $populationFixtureDirectory | Out-Null
    $populationFixtureArguments += $populationFixtureDirectory
}
& $output @populationFixtureArguments
if ($LASTEXITCODE -ne 0) { throw 'Population capture fixtures failed.' }
