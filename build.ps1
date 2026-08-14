param(
    [string]$CompilerPath,
    [switch]$VerifyReproducible
)

$ErrorActionPreference = 'Stop'
$project = $PSScriptRoot
$source = Join-Path $project 'src\shadow_engine_patch.c'

if (-not $CompilerPath) {
    $candidates = @()
    if ($env:SHADOW_ENGINE_TCC) {
        $candidates += $env:SHADOW_ENGINE_TCC
    }
    $candidates += Join-Path $project 'tools\tcc\tcc.exe'
    $candidates += Join-Path (Split-Path -Parent $project) 'Tools\Applications\tcc-0.9.27-win64\tcc\tcc.exe'
    $CompilerPath = $candidates | Where-Object {
        Test-Path -LiteralPath $_
    } | Select-Object -First 1
}

if (-not $CompilerPath -or -not (Test-Path -LiteralPath $CompilerPath)) {
    throw 'TinyCC was not found. Pass -CompilerPath or set SHADOW_ENGINE_TCC.'
}
$CompilerPath = [System.IO.Path]::GetFullPath($CompilerPath)

$buildDirectory = Join-Path $project 'build'
[System.IO.Directory]::CreateDirectory($buildDirectory) | Out-Null
$output = Join-Path $buildDirectory 'ShadowEnginePatch.asi'

& $CompilerPath -shared -O2 -Wall -Werror $source -o $output
if ($LASTEXITCODE -ne 0) {
    throw "TinyCC failed with exit code $LASTEXITCODE."
}

$hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $output).Hash
Write-Output "Built $output"
Write-Output "SHA256 $hash"

if ($VerifyReproducible) {
    $reproDirectory = Join-Path $project 'build_repro'
    [System.IO.Directory]::CreateDirectory($reproDirectory) | Out-Null
    $reproOutput = Join-Path $reproDirectory 'ShadowEnginePatch.asi'
    & $CompilerPath -shared -O2 -Wall -Werror $source -o $reproOutput
    if ($LASTEXITCODE -ne 0) {
        throw "Reproducibility build failed with exit code $LASTEXITCODE."
    }
    $reproHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $reproOutput).Hash
    if ($hash -ne $reproHash) {
        throw "Builds differ: $hash versus $reproHash."
    }
    Write-Output "Reproducible build verified: $reproHash"
}
