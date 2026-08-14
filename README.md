# Shadow Engine

Experimental engine-level shadow-capacity patch for the original 2014 Windows
release of *Watch Dogs*.

> **Research source only:** no public mod build or installable release exists.
> Version numbers in this repository identify private experiments and evidence,
> not downloadable releases. The source is published for technical review and
> independent compilation.

The project expands and repairs the Disrupt shadow pipeline so additional
vehicle-headlight shadows can coexist with native world lighting. It is an
ongoing reverse-engineering project, not a finished stability release.

## Verified capabilities

- Constructs 24 physical local shadow maps instead of the native 16.
- Registers eight additional paired shadow/alpha pass groups for maps 16-23.
- Expands the physical render queue from 17 to 25 entries.
- Pre-reserves storage for eight `0x700`-byte shadow-owner records.
- Admits only complete multi-face lights within the 24-face physical boundary.
- Preserves the complete native candidate chain; there is no broad spotlight or
  vehicle-type filter.
- Routes real gameplay work through added maps beyond the native map 15.
- Repairs verified same-cycle corruption of render-record fields immediately
  before renderer acquisition.
- Preserves displaced non-null resources through balanced native sidecar
  acquire/release submissions.

The v0.8.9 test recorded 8,299 accepted field repairs and 5,785 perfectly
balanced sidecar acquire/release pairs with zero recorded repair or symmetry
failures. It greatly reduced the black-world failure, but one possible instant
flicker remained unconfirmed and the build had a major FPS regression.

See [the proven-capability report](docs/SHADOW_ENGINE_PATCH_PROVEN_CAPABILITIES_2026-08-13.md)
and [OKF knowledge index](docs/okf/index.md) for the evidence boundary.

## Current source baseline

v0.9.0 reorganizes the tested v0.8.9 implementation into eight ordered unity
modules. Before changing the embedded version label, the modular tree compiled
byte-identically to the tested v0.8.9 ASI. See
[the modularization audit](MODULARIZATION_AUDIT.md).

The unity build is intentional: it preserves TinyCC ABI behavior, static state,
initialization order and detour layout while giving each subsystem an explicit
source owner.

## Open in CLion

1. Open this repository root in CLion.
2. Allow CLion to load `CMakeLists.txt`.
3. Use the `shadow_engine_sources` target for navigation and refactoring.
4. Build with the `shadow_engine_tinycc` target or run:

```powershell
.\build.ps1 -VerifyReproducible
```

CMake is present for IDE indexing. It is not the validated compiler. Do not
replace the verified TinyCC build with an MSVC or Zig-linked binary without a
separate runtime validation.

See [BUILDING.md](BUILDING.md) for toolchain setup.

## Publication status

This repository is a shared engineering record and auditable source tree. It
does not publish a finished mod version, installation package, GitHub Release,
precompiled ASI/DLL, loader binary, or test ZIP. Internal build hashes document
private test provenance; they are not download links or release endorsements.

Anyone may inspect and compile the source for research, subject to the license
status below, but a self-compiled binary remains an unsupported experimental
artifact and must not be represented as an official Shadow Engine release.

## Evidence policy

Claims in this repository are classified as runtime-observed, statically
confirmed, inferred, or untested. Raw game binaries, memory dumps and proprietary
decompilation exports are not distributed. See [docs/EVIDENCE_POLICY.md](docs/EVIDENCE_POLICY.md).

## License status

No open-source license has been selected yet. Until one is added, normal
copyright applies even though the repository is publicly readable. This must be
resolved before accepting code contributions or advertising redistribution
rights.

This project is unaffiliated with Ubisoft. *Watch Dogs* and Disrupt are Ubisoft
properties.
