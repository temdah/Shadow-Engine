param(
    [string]$CompilerPath,
    [string]$PythonPath,
    [string]$Inputs,
    [string]$Baseline,
    [string]$OutputDirectory,
    [switch]$PrepareInputs
)
$ErrorActionPreference = 'Stop'
if (-not $PythonPath) {
    $pythonCommand = Get-Command python -ErrorAction SilentlyContinue
    if ($pythonCommand) { $PythonPath = $pythonCommand.Source }
    else { $PythonPath = Join-Path $env:USERPROFILE '.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe' }
}
if (-not (Test-Path -LiteralPath $PythonPath -PathType Leaf)) { throw 'Pass -PythonPath for Python 3.11 or newer.' }
if (-not $CompilerPath) {
    $compilerCandidates = @($env:SHADOW_ENGINE_TCC,
        (Join-Path $PSScriptRoot 'tools/tcc/tcc.exe'),
        (Join-Path (Split-Path $PSScriptRoot -Parent) 'Support/Tools/Applications/tcc-0.9.27-win64/tcc/tcc.exe'))
    $CompilerPath = $compilerCandidates | Where-Object { $_ -and (Test-Path -LiteralPath $_ -PathType Leaf) } | Select-Object -First 1
}
if (-not $Inputs) { $Inputs = Join-Path $PSScriptRoot 'build/compatibility-inputs.json' }
if (-not $Baseline) { $Baseline = Join-Path $PSScriptRoot 'build/compatibility-baseline.json' }
$arguments = @('-B', (Join-Path $PSScriptRoot 'tools/compatibility/runner.py'), '--inputs', $Inputs, '--baseline', $Baseline)
if ($PrepareInputs) { $arguments += '--prepare-inputs' }
else {
    if (-not $CompilerPath) { throw 'Pass -CompilerPath for TinyCC 0.9.27.' }
    $arguments += @('--compiler', ([IO.Path]::GetFullPath($CompilerPath)), '--powershell', (Get-Process -Id $PID).Path)
}
if ($OutputDirectory) { $arguments += @('--output', $OutputDirectory) }
& $PythonPath @arguments
exit $LASTEXITCODE
