---
type: Validation Audit
title: Shadow Engine Patch — Claimed vs Proven
description: Evidence boundary for the 16-to-24 local-shadow extension and the next 16-dynamic test.
tags: [watch-dogs, disrupt, shadows, validation, runtime]
timestamp: 2026-08-13T12:00:00+02:00
---

# Executive correction

v1.2.16 has a **24-entry address/layout envelope**, but only the native
`ShadowMap0..15` resource handles are populated at runtime. The live initializer
copies 16 native handles, registers render passes 17-24, and explicitly reports
`resourcesCreated=0`. External handle slots 16-23 therefore remain zero.

Do not describe v1.2.16 as having 24 proven live shadow-map resources. It has the
linked storage, queue, index and pass groundwork needed for them.

# Claim audit

| Claim | Status | Evidence boundary |
|---|---|---|
| Local shadow-map storage changed from 16 to 24 | **Patched layout confirmed; live backing not confirmed** | 46 tail/layout references and three handle accesses are redirected to a 24-entry external table. Runtime only fills entries 0-15. |
| Render queue changed from 17 to 25 entries | **Static patch confirmed; extended use not observed** | Queue count/clear/tail patches install successfully. Saved v1.2.16 telemetry reached 11 entries and 14 faces, never an extended slot/index. |
| Pass 16 remains LongRangeShadow | **Implementation confirmed** | Pass-index relays leave indices below 16 unchanged and add one to indices 16+, reserving pass 16. Runtime also finds native pass 16 before added registration. |
| Shadow and ShadowAlpha passes 17-24 registered | **Runtime confirmed** | Every pass returns a non-null live pointer and the log records `PASS_RANGE_REGISTRATION_RESULT passes=17..24 resourcesCreated=0`. |
| Downstream handle/layout/face/pass references repaired | **Implemented and startup-verified; not end-to-end proven above index 15** | Signature-checked relays and layout patches install. No saved frame has consumed face/map index 16-23. |
| More high-resolution dynamic shadows coexist | **In-game and telemetry confirmed up to seven ordinary dynamic positions** | B4=8 produces approximately seven fully dynamic ordinary positions and fixed the original alley quality failure in the validated scenes. |
| Unsafe late resource creation was avoided | **Confirmed as a stabilization choice** | v1.2.13 attempted late live resource creation and regressed at startup; v1.2.16 creates zero resources and is the validated stable foundation. This does not prove late creation is impossible, only that the attempted path was unsafe. |
| Runtime diagnostics exist | **Runtime confirmed** | Logs cover admission, ownership, cache lane, resolution, queue entries/faces, expected pass and handle state. |
| Four nearest vehicles plus Aiden bypass | **Attempted, then rejected** | v1.2.25 actively suppressed records, but inferred malformed one- and three-light nodes and did not prove vehicle ownership. It must not be advertised as working. |

# Why B4=16 is not the correct number

With caching enabled, the fully dynamic ordinary lane is approximately `B4 - 1`.
To target 16 ordinary fully dynamic lights, the scheduler value must therefore be
**B4=17**, not B4=16.

Under the commonly observed frame composition, the expected physical face use is:

- five special faces (four-face cube plus one special spotlight);
- sixteen ordinary dynamic spotlight faces;
- one shared 512x512 cached-refresh face.

That is approximately **22 faces**, which fits inside the intended 24-face
envelope and roughly 19 queue jobs inside the intended 25-entry envelope. This is
an estimate, not a proof; other scene compositions need a runtime guard.

# A8/cache-owner ceiling

A8 is not a bank of simultaneous high-resolution maps. It bounds owners retained
in cache records. Those owners rotate through one 512x512 refresh position.

- Raising A8 from 8 to 16 would retain more coarse owners.
- It would make each retained owner wait longer for its refresh turn and add
  eviction/CPU pressure.
- It would not provide sixteen additional high-resolution shadows.

Keep A8=8 for the first true 16-dynamic experiment. Revisit it only after the
dynamic lane and all extra live resources are proven.

# Required proof build

Do not produce a scheduler-only B4=17 build from v1.2.16. It would be capable of
requesting external entries whose handles are zero.

The next capacity build must first solve safe creation/registration of
`ShadowMap16..23`, preferably before the first shadow manager/renderer use. Then:

1. Start from unfiltered v1.2.16 behavior.
2. Populate and log non-zero, distinct handles for all maps 0-23.
3. Keep pass 16 reserved and verify non-null pass pairs 17-24.
4. Set B4=17 and keep A8=8.
5. Add one-shot evidence for first use of map/face index 16-23, queue entry 17-24,
   and pass 17-24.
6. Guard predicted and observed faces above 24 and queue jobs above 25.
7. Test zero/one/two-car alley, the tree scene, dense traffic, then endurance.

Only a clean run that records non-zero extra handles and actual extended-index
consumption can hard-confirm the complete 24-map claim.

# v1.2.26 first proof stage

v1.2.26 implements only steps 1-3 of the proof procedure. It creates one extra
handle per 120 renderer calls after an initial 240-call delay, while keeping
B4=8/A8=8 and extended demand disabled. This deliberately separates resource
construction from extended rendering. Its final success marker proves eight
non-zero, distinct resource IDs were returned; it does not yet prove GPU backing,
render-pass consumption, or visual correctness at indices 16-23.

The Aiden/nearest-car limiter is not part of this branch and remains deferred.

## v1.2.26 result

Rejected. Pass registration completed, but the first staged call for ShadowMap16
crashed before returning. Late live resource creation is now conclusively outside
the safe lifecycle window. The 24-map proof requires an external early bootstrap
or loader-level callback that installs constructor-time hooks before native shadow
resources are built.
