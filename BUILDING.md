# Building Shadow Engine

## Requirements

- Windows x64.
- PowerShell 5.1 or newer.
- TinyCC 0.9.27 win64.

The automatic local compatibility entry point is:

```powershell
.\build-compatible.ps1 -PrepareInputs  # once, in the existing research workspace
.\build-compatible.ps1
```

It builds both profiles reproducibly, runs all native harnesses, Python and Lua
regressions, validates the five-profile matrix against private images, and
creates frozen ASIs, candidate ZIPs and a JSON report in a new
`build/compatibility/<UTC timestamp>/` directory. It never installs, launches,
downloads or publishes. Existing deliveries and calibration are preserved.
Use `-PythonPath`, `-CompilerPath`, `-Inputs` or `-Baseline` for explicit inputs.
Python 3.11+ is required. The prepared manifest is local and ignored; it references
existing research inputs, the saved source reference and desktop Lua runner.
It is not a portable substitute for obtaining those private inputs.

Exit codes: **0** calibrated offline qualification, **1** failed/incomplete
execution, **2** completed checks with missing evidence or calibration. Failed
regressions produce no installable ZIP; incomplete qualification produces clearly
named candidate ZIPs. The report lists actual feature coverage separately from
recognition of a regional binary. The local manifest now includes a verified
A4EE mapped capture. Freshly prepared manifests still leave that entry null;
register an available verified capture using its path, SHA256 and `mapped: true`.

After the user actually tests a frozen candidate, record only that observation:

```powershell
python -B tools/compatibility/record_baseline.py --report <run>/report.json --tested-profile supported-04DF --build-profile Internal --observation "<actual reported result>"
```

This creates `build/compatibility-baseline.json` once. It rejects changed source,
changed artifacts and failed gates. Later routine policy changes use the automated
regressions against that reference without requiring another full manual regional
matrix. Changes to native boundaries, compiler identity, regional feature coverage
or the gate implementation block qualification for review and focused recalibration.
This does not claim per-release visual/FPS proof or manual testing of untested
regions. Internal and Release are different products: Release omits the menu and
optional Lua repair. An Internal observation must never be called a Release playtest.

The underlying compiler entry points remain:

```powershell
.\build.ps1 -Profile Internal -VerifyReproducible
.\build.ps1 -Profile Release -VerifyReproducible
```

Internal is the default and writes `build\ShadowEnginePatch.asi`. It retains
the bounded diagnostic menu/capture instrumentation used for development.
Release writes `build_release\ShadowEnginePatch.asi` and compiles internal-only
observers, capture polling, queue probes, and the diagnostic worker out of the
runtime path. Release is not distributable until its matching Internal
candidate has passed the manual equivalence gate.

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
`Support\Tools\Applications\tcc-0.9.27-win64\tcc\tcc.exe` installation.

## Reproducibility

`-VerifyReproducible` performs two clean compilations and requires identical
SHA-256 hashes. Outputs are written to `build` and `build_repro`, both ignored by
Git.

Historical binary comparisons are recorded in the
[refactor results](../OKF/projects/shadow-engine/validation/historical-refactor-results.md).

## CLion

The CMake project exists for indexing and exposes a custom
`shadow_engine_tinycc` target that invokes `build.ps1`. CMake's selected C
compiler is not an approved replacement for the validated TinyCC research-build
pipeline.

The `.inc` files are ordered unity modules included by
`src/shadow_engine_patch.c`; do not compile them independently.

## Validation

Run the current source-contract gate against the preserved v1.2.0 reference:

```powershell
python -B tests/validate_refactor.py --baseline <tested-v1.2.0-source> --candidate . --policy-target-30-b4-21 --lifecycle-hotfix --intersection-diagnostic --vehicle-ownership-diagnostic
python -B -m unittest discover -s tests -p 'test_*.py'
python -B tests/validate_release.py --asi build/ShadowEnginePatch.asi --profile Internal
python -B tests/validate_release.py --asi build_release/ShadowEnginePatch.asi --profile Release
```

The baseline retains 30 maps, 31 queue records, B4=21 and A8=4. The contract
checks all 46 relocations, five regional identities, accepted hook topology
and transaction ownership. Retired background-distance and onset-fade code is
excluded. SESAVE3 input migrates ordinary choices in memory; subsequent edits
write SESAVE2. See [tests/README.md](tests/README.md) for native harnesses.

Regional/profile/layout changes also require the private runtime corpus. Four
profiles historically had byte-exact checks; the preserved A4EE mapped capture
now completes all five in the configured local manifest. A synthetic test
or reproducible build never substitutes for the user's in-game acceptance.

## Local packages

```powershell
./package-internal-tools.ps1
./package-internal-tools.ps1 -NexusToolsImport
```

The first packages the Internal ASI and minimal README; the second packages
only the changed Lua companion. Existing deliveries are never overwritten.
The public product uses the menu-capable profile currently named Internal:
shadow settings, traffic control, guarded Lua compatibility repair and one
staged support-report button. Individual diagnostics remain development-only.
The optional compiler preset named Release omits the menu mailbox and Lua repair;
it is not the intended public package. Both presets retain their offline gates.

## Hosted candidate checks

An authorized push to `main` runs `.github/workflows/build-mod.yml` on a GitHub-hosted
Windows runner. The workflow downloads the hash-pinned TinyCC 0.9.27 archive,
builds both profiles twice, requires byte-for-byte reproducibility, runs the
transaction fault-injection test, verifies the required exports/profile
markers, and packages the menu-capable Internal profile and separate tools:

```text
ShadowEngine-v<version>.zip
â”œâ”€â”€ bin/ShadowEnginePatch.asi
â””â”€â”€ README.md
```

The version comes directly from `PATCH_VERSION`. Hosted checks cannot access the
private regional images or local calibration, so they only compile candidates.
Automatic tagging/publication is disabled to prevent bypassing the local
compatibility gate. Publishing a qualified artifact remains a separate action.

## Local research inputs

The shared knowledge is at `../OKF/index.md`. The relocated runtime-corpus
manifest is `../Support/Inputs/runtime-corpus.json`; its raw captures remain
private. Pass this manifest to corpus validators. Universal Modder is the
workspace workflow; the canonical compiler/build gates above remain required.
