---
type: Architecture Reference
title: WD1 Shadow Engine Patch Architecture
description: Reconstructed shadow path and physical layout used by the current patch.
tags: [wd1, shadow-manager, render-queue, binary-patching]
timestamp: 2026-08-12T01:20:00+02:00
---

# Current source architecture (v0.9.0)

The runtime remains one compact `ShadowEnginePatch.asi`, but the source is now
an ordered unity build instead of a 2,903-line monolith:

1. shared configuration, signatures, types and state;
2. logging, memory and detour primitives;
3. manager profile and owner-vector handling;
4. renderer queue, residency and manual diagnostics;
5. render-record lineage, repair and sidecar lifetime;
6. resource wrapper/producer tracing and null compaction;
7. map/pass/queue/scheduler expansion;
8. signature preflight and bootstrap orchestration.

The small `src/shadow_engine_patch.c` aggregator includes these modules in
dependency order. This keeps the proven TinyCC ABI, static state and hook layout
while creating explicit ownership boundaries. A compile performed before the
version-label change was byte-identical to tested v0.8.9, proving the split did
not alter generated code. Details are in the v0.9.0 modularization audit.

# Capacity-first decision after v1.2.25 (2026-08-13)

Primary development returns to the complete-chain v1.2.16 foundation. Do not
carry v1.2.25's temporal motion filtering into the next capacity experiment.

The next bounded A/B should change shared residency rather than guess ownership:

- retain 24 physical local maps, 25 queue entries and registered passes 17-24;
- retain the complete native candidate order with no vehicle/world filtering;
- retain cached-owner ceiling A8=8 for the first control;
- raise the B4 fully dynamic residency target only from 8 to 12, yielding about
  eleven ordinary dynamic positions rather than seven;
- keep low-overhead pass/index bounds and one-shot null-key evidence;
- validate zero, one and two cars in the alley, then the tree spotlight and a
  dense-traffic endurance test.

B4 is a scheduler/residency control, not a physical capacity. Twelve is chosen
as a bounded experiment: roughly eleven dynamic plus eight cached ordinary
owners, together with the observed five special faces, fits the existing
24-local-map envelope. Do not jump to 20, 32 or 100 until every downstream
allocation, queue, pass, lifetime and teardown consumer is mapped.

# Pipeline

```text
light registration
  -> candidate construction
  -> manager admission and priority
  -> dynamic/cache residency
  -> render queue and face allocation
  -> ShadowMap resource and render pass
  -> lighting pass consumption
```

World pole spotlights and vehicle headlights converge before the renderer stage.
The generic renderer class therefore cannot reliably recover high-level ownership.

# Current physical expansion

| Item | Native | Patched |
|---|---:|---:|
| Local shadow maps | 16 | 24 |
| Render queue entries | 17 | 25 |
| Queue index-array entries | 17 | 25 |
| Profile/cache ceiling (`A8`) | 8 observed clamp | 24 |
| Reserved long-range pass | 16 | 16 |
| New local pass range | none | 17-24 |
| Relocated embedded queue-tail references | 0 | 46 |

New map indices 16-23 skip pass 16 and use passes 17-24. The ASI registers
`ShadowMap0..23` handles and additional `ShadowN` / `ShadowAlphaN` passes.
v1.2.10 proved those passes must be inserted during the native 0-16 registration
phase, before the constructor finalizes its runtime lookup tables. v1.2.11 hooks
the register-pass routine and injects 17-24 immediately after native pass 16.

# Scheduler correction

Manager field `B4` moves in the native adaptive range 4-8. Earlier builds forced
both controlling immediates to 21. Disassembly showed these values are scheduler
hysteresis thresholds, not simple storage capacity. v1.2.5 and v1.2.6 restore the
native values.

# Proven admission and residency relationship

Decompilation of `FUN_7ffc39ed9b60` shows that when caching is enabled the ordinary
light admission allowance is approximately:

`dynamic lane + cached lane = (B4 - 1) + A8`

The function builds the eligible candidate vector, reconciles existing `0x700`-byte
cache records by light-component ID at record `+0x78`, marks unmatched records free
with `-1`, and assigns new cache records through `FUN_7ffc39ed2470`.

- `B4 - 1` candidates remain in the fully dynamic lane.
- Later eligible candidates receive cache records, up to `A8`.
- Raising B4 therefore changes update/residency policy, not physical map storage.
- Raising A8 increases how many additional light owners may be retained in the
  cached lane and directly increases total admission pressure.

This explains why native B4=4 can produce coarse fallback after roughly three
ordinary dynamic lights, while forcing B4=21 improved sharpness but destabilized
the scene. It also motivates a clean A/B control with the full candidate chain,
24 physical maps, native B4, and native A8=8.

v1.2.8 validates the render-side consequence. In the alley capture:

- special cube: queue slot 0, four faces, 8192x8192;
- three ordinary dynamic owners: queue slots 1-3, 4096x4096 each;
- cached refresh owner: queue slot 4, 512x512;
- special spot: queue slot 5, 512x512.

Eight cache records can retain eight light IDs, but they do not become eight
simultaneous high-resolution render jobs. The scheduler rotates cached owners
through the single low-resolution queue slot. Consequently, the intended 24-map
layout remains mostly unused while quality visibly changes. A later audit
established that v1.2.16 only populates the native 16 resource handles; entries
16-23 are addressable but not yet live resources.

# Instrumentation

The ASI can log manager candidate counts, admitted records, cache count, B4 state,
world/type-3 counts, and optional ownership snapshots. Candidate/component pointers
are transient. v1.2.6 only walks them when tracing or a dump is explicitly active.

# Relevant source

`Builds\Current\ShadowEngineDeveloper_v1.2.7\src\shadow_engine_extension.c`

Primary RVAs and patch tables are declared near the beginning of that source.
Never assume they match another executable build.
# Aiden-centered four-vehicle residency design (2026-08-12)

The target architecture should select stable vehicle entities before filtering
transient spotlight candidates. Aiden supplies the anchor position. Four
persistent slots hold the closest eligible entity IDs, with distance hysteresis
and minimum residency time. Each resident entity is then mapped to its current
left/right headlight candidate IDs. Only those eight candidate records may enter
vehicle-shadow admission; all non-vehicle shadows preserve native ordering.

This is superior to independently sorting raw spotlight records because candidate
pointers/IDs churn, lamps can appear without their mate in a given manager call,
and a light's direction vector cannot identify vehicle distance. The proposed
`VehicleSlot` state and replacement rule remain unimplemented private design
work, not a proven public capability.

# Shared spotlight-pipeline expansion path (2026-08-12)

If stable vehicle ownership cannot be recovered at shadow-manager time, the
alternative is to remove car-specific filtering and expand the shared type-3
pipeline. The existing work expands addressing/queue layout and registers
passes 17-24, but does not yet create live ShadowMap16-23 backing,
but admission priority, dynamic versus cached residency, redraw cadence, cache
eviction, face allocation, and downstream renderer consumers remain coupled
bottlenecks. These must be traced and extended together; a single high numeric
limit is unsafe and previously caused quality regressions, FPS collapse, or
crashes. Future expansion work must first prove live backing and every
downstream consumer at each added index.
# Adaptive B4 scheduler

The shadow manager at `Disrupt +0x2E9B60` adjusts B4 every call rather than
treating it as a static configured capacity:

```text
normal mode:  lower = 4, upper = 8
reduced mode: lower = 1, upper = 2

pressure branch: B4 = max(B4 - 1, lower)
growth branch:   B4 = min(B4 + 1, upper)
```

Any extension must patch both normal-mode bounds as one policy. Raising only the
lower bound creates contradictory branches and runtime oscillation. B4 controls
record admission; cumulative faces remain separately bounded by the 24-map
renderer prefix, and cache-owner storage remains separately bounded by the
pre-reserved A8 vector.
