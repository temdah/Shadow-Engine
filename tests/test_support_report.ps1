param([Parameter(Mandatory=$true)][string]$CompilerPath)
$ErrorActionPreference='Stop'
$output=Join-Path $PSScriptRoot '../build/tests/support-report'
New-Item -ItemType Directory -Path $output -Force | Out-Null
$executable=Join-Path $output 'support_report_harness.exe'
& $CompilerPath -O2 -Wall -Werror (Join-Path $PSScriptRoot 'support_report_harness.c') -o $executable
if ($LASTEXITCODE -ne 0) { throw 'Support-report harness compilation failed.' }
$artifacts=Join-Path $output ([DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffffff'))
New-Item -ItemType Directory -Path $artifacts | Out-Null
& $executable ([IO.Path]::GetFullPath($artifacts))
if ($LASTEXITCODE -ne 0) { throw 'Support-report fixtures failed.' }
# Independent Windows ZIP reader verifies directory structure, byte streams and lengths.
Add-Type -AssemblyName System.IO.Compression.FileSystem
Add-Type -TypeDefinition @'
public static class SupportFixtureCrc {
    public static uint Compute(byte[] bytes) {
        uint crc = 0xffffffff;
        foreach (byte value in bytes) {
            crc ^= value;
            for (int bit = 0; bit < 8; ++bit)
                crc = (crc >> 1) ^ ((crc & 1) != 0 ? 0xedb88320 : 0);
        }
        return crc ^ 0xffffffff;
    }
}
'@
$archives=Get-ChildItem -LiteralPath $artifacts -Filter '*.zip'
if ($archives.Count -ne 6) { throw 'Expected five reports and one ZIP-format fixture.' }
foreach ($archive in $archives) {
    $raw=[IO.File]::ReadAllBytes($archive.FullName)
    $central=[BitConverter]::ToUInt32($raw,$raw.Length-6)
    $zip=[IO.Compression.ZipFile]::OpenRead($archive.FullName)
    try {
        foreach ($entry in $zip.Entries) {
            $stream=$entry.Open(); $memory=[IO.MemoryStream]::new()
            try {
                $stream.CopyTo($memory)
                if ($memory.Length -ne $entry.Length) { throw 'Entry length mismatch.' }
                if ([BitConverter]::ToUInt32($raw,$central) -ne 0x02014b50) { throw 'Invalid central directory.' }
                if ([SupportFixtureCrc]::Compute($memory.ToArray()) -ne [BitConverter]::ToUInt32($raw,$central+16)) { throw 'CRC mismatch.' }
                $central+=46+[BitConverter]::ToUInt16($raw,$central+28)+[BitConverter]::ToUInt16($raw,$central+30)+[BitConverter]::ToUInt16($raw,$central+32)
                if ($entry.FullName -eq 'binary.bin') {
                    $bytes=$memory.ToArray()
                    if ($bytes.Length -ne 65539) { throw 'Missing multichunk data.' }
                    for ($i=0;$i -lt $bytes.Length;$i++) { if ($bytes[$i] -ne (($i*37) % 256)) { throw 'Binary payload mismatch.' } }
                }
            }
            finally { $stream.Dispose(); $memory.Dispose() }
        }
        if ($archive.Name -like 'ShadowEngine-Support-*') {
            $names=@($zip.Entries | ForEach-Object { $_.FullName })
            if ($archive.Name -like '*-6.zip') {
                if ($names.Count -ne 5 -or 'intersection.log' -notin $names -or 'shadow-population.bin' -notin $names) { throw 'Current capture missing.' }
            } elseif ($names.Count -ne 3 -or 'report.txt' -notin $names -or 'shadow-population.bin' -in $names -or 'intersection.log' -in $names) {
                throw 'Unexpected report entry or leaked stale capture.'
            }
        }
    } finally { $zip.Dispose() }
}
Write-Output 'PASS independent ZIP reader: six archives; current captures included, stale captures excluded; lengths, CRCs and multichunk payload match.'
