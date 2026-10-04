param([Parameter(Mandatory=$true)][string]$CompilerPath)
$ErrorActionPreference = 'Stop'
$outputDirectory = Join-Path (Split-Path $PSScriptRoot -Parent) 'build\tests'
New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null
$ownershipResult = $null
foreach ($diagnostics in @(1, 0)) {
    $output = Join-Path $outputDirectory "external_cycle_harness_diagnostics_$diagnostics.exe"
    & $CompilerPath -O2 -Wall -Werror "-DSHADOW_ENGINE_INTERNAL_DIAGNOSTICS=$diagnostics" (Join-Path $PSScriptRoot 'external_cycle_harness.c') -o $output
    if ($LASTEXITCODE -ne 0) { throw "External lifecycle harness compilation failed (diagnostics=$diagnostics)." }
    $modes = if ($diagnostics) { @('idle', 'recording', 'contended') } else { @('idle') }
    foreach ($mode in $modes) {
        $result = & $output $mode
        if ($LASTEXITCODE -ne 0) { throw "External lifecycle harness failed (diagnostics=$diagnostics detail=$mode)." }
        $result | Write-Output
        $trace = @($result | Where-Object { $_ -like 'OWNERSHIP_RESULT *' })
        if ($trace.Count -ne 1) { throw 'External lifecycle ownership trace missing.' }
        if ($null -ne $ownershipResult -and $ownershipResult -cne $trace[0]) {
            throw 'External lifecycle ownership differs across optional diagnostic modes.'
        }
        $ownershipResult = $trace[0]
    }
}
Write-Output 'PASS H2/H3 mandatory ownership is independent of idle, recording, contended and compiled-out optional diagnostics.'
