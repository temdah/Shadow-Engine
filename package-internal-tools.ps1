param(
    [string]$Destination,
    [switch]$NexusToolsImport,
    [ValidateSet('Internal','Release')]
    [string]$Profile = 'Internal'
)
$ErrorActionPreference = 'Stop'
$project = $PSScriptRoot
$source = Get-Content -Raw (Join-Path $project 'src\modules\00_shared_config_state.inc')
if ($source -notmatch '#define PATCH_VERSION "([^"]+)"') { throw 'Missing ASI version.' }
$asiVersion = $Matches[1]
$manifest = Get-Content -Raw (Join-Path $project 'packaging\internal-tools\modconfig.json') | ConvertFrom-Json
if ($NexusToolsImport) {
    $name = "ShadowEngine-Tools-v$($manifest.version)-NexusTools.zip"
    $files = @{
        'shadow_engine_tools/modconfig.json' = Join-Path $project 'packaging\internal-tools\modconfig.json'
        'shadow_engine_tools/workspace/shadow_engine_tools/entrypoint.lua' = Join-Path $project 'src\internal\entrypoint.lua'
        'shadow_engine_tools/workspace/shadow_engine_tools/traffic.lua' = Join-Path $project 'src\internal\traffic.lua'
        'shadow_engine_tools/workspace/shadow_engine_tools/settings.lua' = Join-Path $project 'src\internal\settings.lua'
        'shadow_engine_tools/workspace/shadow_engine_tools/support.lua' = Join-Path $project 'src\internal\support.lua'
    }
} else {
    $name = if ($Profile -eq 'Internal') {
        "ShadowEngine-v$asiVersion.zip"
    } else {
        "ShadowEngine-v$asiVersion-Release.zip"
    }
    $buildDirectoryName = if ($Profile -eq 'Internal') { 'build' } else { 'build_release' }
    $files = @{
        'bin/ShadowEnginePatch.asi' = Join-Path (Join-Path $project $buildDirectoryName) 'ShadowEnginePatch.asi'
        'README.md' = Join-Path $project 'packaging\ASI_README.md'
    }
}
if (-not $Destination) {
    $Destination = Join-Path (Join-Path (Split-Path -Parent $project) 'Installables') $name
}
if (Test-Path -LiteralPath $Destination) { throw 'Package already exists; preserve previous deliveries.' }
foreach ($path in $files.Values) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Missing package source: $path" }
}
# Separate archives; no staging, recursive cleanup or automatic tools rebuild.
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
$archive = [IO.Compression.ZipFile]::Open($Destination, [IO.Compression.ZipArchiveMode]::Create)
try {
    foreach ($entry in ($files.Keys | Sort-Object)) {
        [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($archive, $files[$entry], $entry, [IO.Compression.CompressionLevel]::Optimal) | Out-Null
    }
} finally { $archive.Dispose() }
$archive = [IO.Compression.ZipFile]::OpenRead($Destination)
try {
    if ($archive.Entries.Count -ne $files.Count) { throw 'Unexpected package entries.' }
    foreach ($entry in $archive.Entries) {
        if (-not $files.ContainsKey($entry.FullName)) { throw 'Unexpected package file.' }
        $stream = $entry.Open()
        $digest = [Security.Cryptography.SHA256]::Create()
        try {
            $actual = [BitConverter]::ToString($digest.ComputeHash($stream)).Replace('-', '')
        } finally { $stream.Dispose(); $digest.Dispose() }
        $expected = (Get-FileHash -LiteralPath $files[$entry.FullName] -Algorithm SHA256).Hash
        if ($actual -ne $expected) { throw ('Package content differs: ' + $entry.FullName) }
    }
} finally { $archive.Dispose() }
Get-FileHash -LiteralPath $Destination -Algorithm SHA256
