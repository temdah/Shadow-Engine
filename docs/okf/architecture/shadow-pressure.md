---
type: Architecture Reference
title: Shadow Pressure Boundaries
description: Evidence separating manager admission, cached-owner retention, and renderer face capacity.
tags: [shadow-engine, shadows, admission, cache, capacity]
status: draft
generated:
  by: codex/gpt-5
  at: 2026-08-24T10:30:00+02:00
sources:
  - id: manager-source
    resource: ../../../src/modules/20_manager_owner_profile.inc
    title: Shadow manager and owner-profile module
  - id: renderer-source
    resource: ../../../src/modules/30_renderer_queue_diagnostics.inc
    title: Renderer queue diagnostics module
  - id: matrix
    resource: ../validation/regional-runtime-matrix.md
    title: Regional runtime matrix
---

# Boundaries

Manager admission and cached-owner reconciliation happen before renderer queue
submission. A light can lose a dynamic position or cached representation even
when the later queue remains below 24 physical faces.

The accepted policy uses `B4=16`, approximately 15 ordinary dynamic positions
when caching is enabled, and `A8=4`, four retained cached owners. Cached owners
do not add high-resolution maps; they share the engine's coarse refresh lane.

# Current evidence and next measurement

The validated five-profile logs contain no face-budget clamp. Near-full
occupancy is evidence of pressure, not proof that the renderer guard dropped a
record.[^matrix]

The diagnostic candidate records raw candidates, final manager admissions,
dynamic-or-special records, cached bindings, and live owner records in the
existing residency and F8/F9 output. Capture F8 during visible loss and F9
after recovery. Change `A8`, `B4`, or physical maps only after those paired
measurements identify the saturated boundary.[^manager-source][^renderer-source]

# v1.2.2 rejection and v1.2.3 correction

The paired capture identified the high-quality lane and four-owner cache as
simultaneously full while the renderer remained below 24 faces. Raising `A8`
alone would retain more coarse owners rather than improve sharpness. The
capacity design therefore raises the coherent high-quality target to `B4=21`
and supplies 30
physical maps, 31 queue entries, 14 external map resources, 28 external pass
slots, and 13 external result slots. `A8` remains four so the experiment does
not lengthen the shared coarse-refresh rotation.

The first v1.2.2 runtime test rejected its implementation: all shadows were
missing and all `SliceExecute` result counters remained zero even though queue
records and faces were present. Historical patch tables plus current layout
recovery show that the record region, first mapping array, and post-mapping
fields grow by `0x24C0`, `0x24C4`, and `0x24C8` per map respectively. v1.2.2 incorrectly used the final
stride for all 46 relocations, displacing the first mapping array by 48 bytes
and the second by 24 bytes. v1.2.3 corrects those offsets and adds explicit
three-stride validation without reducing the 30-map target. The complete
formulas are maintained in the [capacity-layout contract](engine-capacity-layout.md).

The 30-map target stays inside the proven 64-slot pass registry: passes 17-30
occupy slots 34-61 and require maximum key `0x3D0C`, below stored maximum
`0x3F00`. The whole-record face guard remains active above 30 faces.

[^matrix]: Regional runtime matrix
[^manager-source]: Shadow manager and owner-profile module
[^renderer-source]: Renderer queue diagnostics module
