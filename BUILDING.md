# Building Shadow Engine

## Requirements

- Windows x64.
- PowerShell 5.1 or newer.
- TinyCC 0.9.27 win64.

The verified build command is:

```powershell
tcc.exe -shared -O2 -Wall -Werror src\shadow_engine_patch.c `
  -o build\ShadowEnginePatch.asi
```

The output basename must remain `ShadowEnginePatch.asi`. TinyCC embeds it in the
PE export metadata.

## Configure TinyCC

Use any one of these methods:

1. Pass the compiler explicitly:

```powershell
.\build.ps1 -CompilerPath .\tools\tcc\tcc.exe -VerifyReproducible
```

2. Set a task-specific environment variable:

```powershell
$env:SHADOW_ENGINE_TCC = (Resolve-Path '.\tools\tcc\tcc.exe')
.\build.ps1 -VerifyReproducible
```

3. Place TinyCC at `tools\tcc\tcc.exe`. The `tools/tcc` directory is ignored by
   Git and the compiler is not redistributed by this repository.

For the original development workspace, the script also detects the sibling
`Tools\Applications\tcc-0.9.27-win64\tcc\tcc.exe` installation.

## Reproducibility

`-VerifyReproducible` performs two clean compilations and requires identical
SHA-256 hashes. Outputs are written to `build` and `build_repro`, both ignored by
Git.

The v0.9.0 reference ASI SHA-256 is:

`23334CBE5B0D69619F23CFDFE610489EA553C94EB52D788BFDA777911E8F7BE2`

## CLion

The CMake project exists for indexing and exposes a custom
`shadow_engine_tinycc` target that invokes `build.ps1`. CMake's selected C
compiler is not an approved replacement for the validated TinyCC research-build
pipeline.

The `.inc` files are ordered unity modules included by
`src/shadow_engine_patch.c`; do not compile them independently.

## Refactor contract validation

The behavior-neutral refactor gate compares the candidate with an exact tested
v1.2.0 source tree. It rejects changes to engine policy constants, RVAs, the 46
queue-tail relocations, the 77-entry Asia map, active signatures or installed
detour targets:

```powershell
python -B tests\validate_refactor.py `
  --baseline <tested-v1.2.0-source> `
  --candidate .
```
