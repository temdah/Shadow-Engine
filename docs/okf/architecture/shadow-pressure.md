---
type: Architecture Reference
title: Shadow Pressure Boundaries
description: Evidence separating manager admission, cached-owner retention, and renderer face capacity.
tags: [shadow-engine, shadows, admission, cache, capacity]
status: draft
generated:
  by: codex/gpt-5
  at: 2026-08-24T17:10:00+02:00
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
  - id: historical-capability
    resource: ../../SHADOW_ENGINE_PATCH_PROVEN_CAPABILITIES_2026-08-13.md
    title: Proven Shadow Engine capabilities and historical pressure findings
---

# Boundaries

Manager admission and cached-owner reconciliation happen before renderer queue
submission. A light can lose a dynamic position or cached representation even
when the later queue remains below 24 physical faces.

The v1.2.3 release policy uses `B4=21`, approximately 20 ordinary dynamic
positions when caching is enabled, and `A8=4`, four retained cached owners.
Cached owners do not add high-resolution maps; they share the engine's coarse
refresh lane.

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

# v1.2.4 isolated residency experiment

The five-profile v1.2.3 calibration reproduced rapid spotlight/moon-shadow
eviction and reacquisition at 26 manager admissions while the renderer used
only 26 of 30 physical faces. Raising `A8` is rejected as the response because
it retains more owners only in the coarse shared-refresh lane.

v1.2.4 changes only coherent manager `B4` from 21 to 25, targeting four more
ordinary dynamic positions before manager eviction. It retains `A8=4`, all 30
maps, the 31-entry queue, passes, results, profiles, hooks, and lifecycle code.
If the manager produces more than 30 cumulative faces, the existing
priority-ordered whole-record guard remains the physical safety boundary. This
candidate tests dynamic residency stability; it does not claim that all
admitted records can render simultaneously.[^historical-capability]

# Knowledge preflight

The v1.2.4 investigation searched current source, active public/private OKF,
and archived provenance for `B4`, `A8`, admission, residency, cached owners,
owner-vector reserve, eviction, and the 26-admission capture. The retained
invariants are physical owner reserve eight, logical `A8=4`, stable owner-vector
base during admission, 30 physical faces, and priority-prefix face guarding.
An `A8` increase and another blind map increase were rejected. Runtime impact
on the remaining flicker is unresolved until Tim performs the representative
high-load test.

[^matrix]: Regional runtime matrix
[^manager-source]: Shadow manager and owner-profile module
[^renderer-source]: Renderer queue diagnostics module
[^historical-capability]: Proven Shadow Engine capabilities and historical pressure findings
