param(
    [Parameter(Mandatory=$true)][string]$CompilerPath,
    [string]$LuaSourceDirectory
)
$ErrorActionPreference='Stop'
if (-not $LuaSourceDirectory) {
    $LuaSourceDirectory=Join-Path $PSScriptRoot '../../Support/Tools/Applications/FCBConverter-source/luac51/src'
}
$LuaSourceDirectory=[IO.Path]::GetFullPath($LuaSourceDirectory)
if (-not (Test-Path -LiteralPath (Join-Path $LuaSourceDirectory 'lparser.c'))) { throw 'Lua 5.1 compiler source required.' }
$output=Join-Path $PSScriptRoot '../build/tests/lua-metadata'
New-Item -ItemType Directory -Path $output -Force | Out-Null
$units=@(Get-ChildItem -LiteralPath $LuaSourceDirectory -Filter '*.c' | Where-Object Name -notin @('luac.c','print.c','wmain.c') | Sort-Object Name | Select-Object -ExpandProperty FullName)
$executable=Join-Path $output 'lua_metadata_budget.exe'
& $CompilerPath -I $LuaSourceDirectory -o $executable (Join-Path $PSScriptRoot 'lua_metadata_budget.c') @units
if ($LASTEXITCODE -ne 0) { throw 'Lua metadata inspector compilation failed.' }
# Record the actual local compiler library input, without redistributing it.
Get-ChildItem -LiteralPath $LuaSourceDirectory -File | Where-Object Extension -in @('.c','.h') | Sort-Object Name | ForEach-Object {
    [ordered]@{name=$_.Name;sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()}
} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $output 'compiler-inputs.json') -Encoding utf8
function InspectFixture([string]$name,[string]$source,[int]$expected) {
    $path=Join-Path $output ($name+'.lua')
    [IO.File]::WriteAllText($path,$source)
    & $executable $path
    if ($LASTEXITCODE -ne $expected) { throw "Wrong metadata gate result: $name" }
}
# Nested prototype captures, including the first wrapped count. All frames fit31.
foreach ($count in @(15,16)) {
    $locals=(1..$count | ForEach-Object { "local v$_=$_" }) -join "`n"
    $sum=(1..$count | ForEach-Object { "v$_" }) -join '+'
    InspectFixture "upvalues-$count" ($locals+"`nreturn function() return "+$sum+" end") ([int]($count -gt 15))
    $args=(1..$count | ForEach-Object { "v$_" }) -join ','
    InspectFixture "parameters-$count" ("return function("+$args+") return v1 end") ([int]($count -gt 15))
}
foreach ($count in @(30,31)) {
    $values=(1..$count) -join ','
    InspectFixture "registers-$($count+1)" ("return {"+$values+"}") ([int]($count+1 -gt 31))
}
$sources=@(Get-ChildItem -LiteralPath (Join-Path $PSScriptRoot '../src/internal') -Filter '*.lua' | Sort-Object Name | Select-Object -ExpandProperty FullName)
& $executable @sources
if ($LASTEXITCODE -ne 0) { throw 'Shipped Lua exceeds WD1 prototype metadata limits.' }
Write-Output 'PASS metadata boundary regressions and all shipped prototypes: registers<=31, upvalues<=15, parameters<=15.'
