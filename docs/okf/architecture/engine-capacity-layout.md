---
type: Engine Layout Contract
title: Shadow Capacity Layout
description: Coupled formulas and invariants for local maps, queue records, embedded mapping arrays, passes, and allocation size.
tags: [shadow-engine, capacity, queue, maps, passes, layout]
status: stable
generated:
  by: codex/gpt-5
  at: 2026-08-24T10:30:00+02:00
sources:
  - id: constants
    resource: ../../../src/modules/00_shared_config_state.inc
    title: Capacity constants and relocation table
  - id: expansion
    resource: ../../../src/modules/60_engine_expansion.inc
    title: Engine expansion implementation
  - id: validator
    resource: ../../../tests/validate_refactor.py
    title: Independent capacity validator
  - id: current-status
    resource: ../current-status.md
    title: Current runtime result and v1.2.2 rejection
  - id: historical-capacity-audit
    resource: https://github.com/temdah/Shadow-Engine/blob/2082a3bec1bfc4213ae4572b0e14b4f20c17906b/docs/okf/engine-patch-validation-audit.md
    title: Historical public 16-to-24 capacity audit
---

# Native layout and coupled counts

The native engine owns 16 local maps and 17 physical queue entries. The extra
entry preserves the special long-range job. For a target of `M` local maps:

* physical queue entries = `M + 1`;
* queue index-array clear bytes = `4 + 4M`;
* external map count = `M - 16`;
* external slice-result count = `M - 17`.

Pass 16 is `LongRangeShadow`. Maps below 16 retain their native pass index;
local map `m >= 16` uses pass `m + 1`. A capacity change must update map-loop
construction, handle routing, pass routing, result routing, queue immediates,
allocation size, and all 46 recovered embedded references as one contract.
[^constants][^expansion]

# Three independently growing queue regions

One local-map record occupies `0x24C0` bytes, but the fields following the
record array do not all move by that same amount. Starting from the native
16-map offsets and adding `E = M - 16` maps:

| Region | Native offset | Formula |
|---|---:|---:|
| First mapping array | `0x270D0` | `0x270D0 + E * 0x24C0` |
| Second mapping array | `0x27114` | `0x27114 + E * 0x24C4` |
| Count and later tail fields | `0x27158` | `0x27158 + E * 0x24C8` |

The first mapping contributes four bytes per added map before the second
boundary; both mappings contribute eight bytes before count/tail/result fields.
For 30 maps (`E=14`), the authoritative offsets are first mapping `0x47350`,
second mapping `0x473CC`, count `0x47448`, tail `0x47450`, result `0x47460`, and
allocation end `0x47470`.[^constants]

v1.2.2 incorrectly applied `0x24C8` to all 46 relocations. Its queue populated,
but both native and external `SliceExecute` counters stayed at zero and all
shadows were absent. v1.2.3 assigns each old embedded value to its owning
region and the validator rejects a uniform-stride derivation.[^validator]

# Pass-table envelope

The verified shadow category is `0x0C`. Its native registry provides 64 slots
and a stored maximum key of at least `0x3F00`. Added maps require paired
`Shadow` and `ShadowAlpha` objects and therefore two physical slots per map.
The 30-map target uses passes 17-30, slots 34-61, and maximum key `0x3D0C`, all
inside that proven envelope. Do not exceed it without independently recovering
the allocator, lookup, finalization, and every consumer.[^expansion]

# Derivation rule

Never infer a capacity formula from only one old/new total. Partition the
object by every intervening variable-length region, classify each relocation
by its original field, and make the offline validator derive expected values
independently of the candidate table.

[^constants]: Capacity constants and relocation table
[^expansion]: Engine expansion implementation
[^validator]: Independent capacity validator
[^current-status]: Current runtime result and v1.2.2 rejection
[^historical-capacity-audit]: Historical public 16-to-24 capacity audit
