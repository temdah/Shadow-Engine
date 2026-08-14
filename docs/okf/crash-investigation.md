---
type: Experimental Findings
title: Crash Investigation v1.2.3-v1.2.6
description: Version deltas, fault evidence, corrected hypotheses, and isolation plan.
tags: [crash, reverse-engineering, shadows, wd1]
timestamp: 2026-08-12T02:00:00+02:00
---

# Version boundary

v1.2.2 is the last build that launches and runs through the relevant test, but it
is visually wrong. Its partition routine admits special type 0/2 records, all
type-1 world records, and at most four direction-matched type-3 records. It drops
all other type-3 candidates, including legitimate pole/world dynamic lights.

That filter unintentionally limits type-3 pressure. v1.2.4-v1.2.6 pass the native
candidate chain without filtering and all crash. v1.2.5 restored native B4
scheduling; v1.2.6 additionally gated diagnostic pointer traversal. Neither
change stopped the crash.

v1.2.3 is a separate early-crash family because it introduced an unsafe component
lookup classifier and lazy ShadowMap initialization. It should not be combined
with conclusions about the no-filter builds.

# Exact fault evidence

Windows Application Error events identify:

| Build | Module RVA | Rebuilt-image interpretation |
|---|---:|---|
| v1.2.4 | `0x408A4F` | `MOV byte ptr [RCX+0x178],0`; invalid object pointer |
| v1.2.5 | `0x25DBFA` | middle of `LEA R8D,[RDX-0x30]`; runtime bytes confirm corrupted control flow |
| v1.2.6 | `0x1EFDAB` | `MOV R8D,[RAX+8]`; invalid lookup result/light descriptor |

For v1.2.6, `RAX` is returned by
`FUN_7ffc3c19ea10(&DAT_7ffc3d2a8240, param_2+0x50)`. The caller subsequently
reads fields at `+0x08`, `+0x88`, `+0xB8`, `+0xBC`, `+0xD0`, and `+0xD8`. The
fault therefore occurs while constructing or copying downstream light data from
a record that is null, stale, or otherwise invalid.

The original on-disk DLL is protected and cannot be compared directly with the
runtime disassembly. The flat captured runtime image and rebuilt DLL match around
RVA `0x25DBFA`; both place the event address inside the `LEA`. This rules out a
rebuild section-offset error and strengthens the corrupted-instruction-pointer
interpretation for v1.2.5.

The v1.2.6 fault is called from `FUN_7ffc39edd480`, a general per-light scene-data
update routine. It receives a light object, resolves a descriptor using the
handle at object `+0x50`, and uses the lookup result without a null check. Its two
known callers pass either an object stored at owner `+0x78` or one returned by a
different registry lookup. The immediate bad state is therefore a stale or
unregistered light object/handle reaching ordinary light-scene processing.

# Cache-count correction

The final v1.2.6 log reached `cacheCount=16`, but allocator function
`FUN_7ffc39ed2470` shows:

- vector base at manager `+0x98`;
- count at `+0xA0`;
- ceiling at `+0xA8`;
- element stride `0x700`;
- dynamic growth through `FUN_7ffc39eff670` while `A0 < A8`.

This invalidates the strong claim that the cache itself has a fixed 16-entry
allocation. A separate fixed loop creates ShadowMap0-15 and arrays at `+0x300`
and `+0x410`, so linked fixed-width consumers remain possible, but none is yet
proven as the crash source.

# Current hypothesis

Confidence: medium-high.

Removing the type-3 safety valve allows enough dynamic spotlight work to reach a
downstream path whose storage expansion or object lifetime handling is incomplete.
The resulting stale/invalid state manifests in different consumers, rather than
as one deterministic capacity exception. The crashes are not evidence that the
GPU simply cannot render more shadows.

Manager admission is now better understood. `FUN_7ffc39ed9b60` uses approximately
`(B4 - 1) + A8` as its eligible ordinary-light allowance. It reconciles cache
records by component ID and allocates `0x700`-byte records only after the dynamic
`B4 - 1` lane is consumed. Thus A8=24 is not merely storage expansion: it permits
substantially more cached light owners to remain admitted. The v1.2.6 invalid light
handle may therefore be a lifetime failure exposed by expanded cache retention.

Next proposed control after the v1.2.2 comparison: start from v1.2.6, keep the full
candidate chain and physical 24-map/25-queue layout, keep native B4, but restore
only the profile/A8 ceiling to native 8. Expected regression is more coarse fallback;
the test asks whether full type-3 visibility remains crash-stable when cached-owner
retention returns to its native limit.

# v1.2.2 traffic-enabled comparison

The comparison is complete: v1.2.2 crashed with traffic enabled. Windows Event
1000 recorded access violation `0xC0000005` in `Disrupt_b64.dll` at RVA
`0x408A4F`, exactly matching the v1.2.4 crash site. v1.2.2 and v1.2.7 share the
same type-3 partition, but v1.2.2 forces B4=21 while v1.2.7 retains native B4=4-8.

The test did not show the same coarse fallback before the crash. This agrees with
the decompiled residency policy: B4=21 gives roughly 20 fully dynamic ordinary
records, improving fidelity, but traffic can push unsafe dynamic-owner pressure
high enough to reproduce the invalid-object failure even with type 3 capped.

Combined conclusion: the crash is not caused solely by unfiltered type 3 or solely
by the cache vector. It correlates with too many simultaneously retained dynamic
and/or cached light owners. Stable v1.2.7 peaked at 13 admitted; crashing v1.2.6
peaked at 19. This is an observed boundary, not yet a universal threshold.

v1.2.8 implementing the native-A8 control is tested stable. ZIP
SHA-256 `2747324D031A0B154C6F15966C1DAED45F8F9AD341DA924284D841139A02F95A`;
ASI SHA-256 `8999C8B7A3F2353D263754B4293A6EE5F3955F8C5E20880CCC0B3ADAEC73B081`.

It survived 44,294 manager calls with maxima of 186 candidates, 69 raw type 3,
13 admitted, and eight cached records. This strongly supports expanded retained
owner count as the crash enabler. Native A8 prevents the crash even while passing
the full candidate chain, but does not preserve fidelity because native B4=4 gives
only three ordinary dynamic positions.

The trace recorded 5,234 render additions and 5,222 drops. Almost all were a
per-frame rotation through queue slot 4 at 512x512 among cached world/type-1 and
type-3 lights. Traced demand remained 9/24 faces. The next engineering problem is
therefore a safe residency split or moderate dynamic-lane floor, not more maps.

v1.2.9 is built to test the moderate floor: normal B4 initialization/floor 8,
native A8=8, full chain, and unchanged physical extension. This targets about
seven dynamic plus eight cached ordinary owners. ZIP SHA-256
`4557AE924E9A4F41D3C7D964CC398BCF7B7F1CD5577CD47D1FBBEAC09226627A`; ASI
SHA-256 `05346529E59A15EE7FD61DF24CDA993823557DE3F9F3994A2A5F74B773300F99`.

Test result: rejected for stability. It eliminated observed coarse shadows and
retained stable fidelity, but crashed after roughly one minute. The log repeatedly
reached 17 admitted records with B4=8 and A8=8. Event 1000 again reported
`Disrupt_b64.dll + 0x408A4F`, matching v1.2.2 and v1.2.4.

The fault function `FUN_7ffc39ff8a40` is a renderer-state reset/initializer. The
shadow-renderer caller first calls `FUN_7ffc39ff5130` or `FUN_7ffc39ff5150`, then
passes the returned pointer directly to the reset routine without checking null.

Decompilation proves that `ff5130/ff5150` are not allocation functions. Each
performs a two-level lookup equivalent to:

```text
bucket = key & 0xff
index  = key >> 8
return table[bucket][index]
```

The crash therefore proves a null renderer-table entry. It does not yet prove
pool exhaustion or that the absent entry is one of the new pass indices. The reset
routine has eight callers, and the tested alley workload did not require faces
above the original range. The exact encoded key and caller must be captured first.

# Current controlled experiment

v1.2.10 retains v1.2.9's B4=8/A8=8 behavior and all physical expansion patches.
It detours only `ff5130/ff5150`, logs null results with key, bucket, index, caller,
manager-call count, and thread ID, then returns the original null unchanged. A
crash is expected and intentional; suppressing the reset before understanding all
downstream uses could create corrupted renderer state.

Test result: the probe reproduced `+0x408A4F`. It also caused a severe diagnostic
performance regression because normal enumeration generated 85,547,562 bytes of
synchronous logging. The decisive last event was the only occurrence of key
`0x220C`, from lookup call RVA `0x307405` in `FUN_7ffc39ef6e60`:

```text
307405  CALL lookup_405130(key=0x220C)
30740A  MOV RCX,RAX
307410  CALL reset_408A40
408A4F  MOV byte ptr [RCX+0x178],0  ; RCX is null
```

`0x220C = (17 << 9) | 0x0C`, identifying the first added shadow pass. The current
frame hook registers passes 17-24 only after the original constructor and its
native finalization have completed. This is now the leading proven implementation
fault. Replace late registration with an injection during the native 0-16 pass
loop, before finalization. Remove both lookup probes.

v1.2.11 moved registration before finalization and eliminated both observed
visual regressions for approximately ten minutes. It still faulted at `+0x408A4F`
after about 71,306 manager calls while admitted count had repeatedly reached 17.
This indicates the first fix is real but incomplete. The next diagnostic must
log only a null key satisfying `(key & 0x1FF) == 0x0C` and `(key >> 9) >= 17`, at
most once. The result will distinguish a missing registered pass from demand
beyond pass 24 without the v1.2.10 performance distortion.

v1.2.12 implements the filtered probe. It logs only when the result is null,
`(key & 0x1ff)` is `0x00c` or `0x10c`, the decoded pass is at least 17, and the
atomic one-shot flag was previously clear. The line includes key, pass/tag,
lookup function, caller, manager-call count, and thread. Ordinary enumeration
nulls never open the log file. The original null remains unchanged.

Test result: one marker at manager call 6,745, again for `0x220C` from caller
`0x30740A`, followed by Event 1000 at `+0x408A4F`. The log contained zero pass
injection confirmations. Therefore the registration hook did not run during
startup: the ASI itself was loaded after the native constructor completed.

This invalidates the claim that v1.2.11 registered passes before finalization.
Do not iterate on that hook. Investigate the live table writer `FUN_7ffc39ff5110`,
the object creation/finalization path that supplies its value, and whether the
already-live registry exposes a supported rebuild. An earlier bootstrap loader is
an architectural alternative, but it changes installation requirements and must
not be assumed without evaluating NexusTools constraints.

Decompilation then established the supported live repair primitive:
`FUN_7ffc39eaff60` allocates a `0x1B0`-byte pass object, constructs it, and calls
`FUN_7ffc39ff5110(*(registry+8), key, object)`. The latter writes the object into
the two-level lookup table. Therefore the table does not need to be cloned and
passes must not be aliased.

v1.2.13 performs this registration exactly once from the first renderer-queue
call, where `manager+0x28` is the live registry used immediately afterward by
lookups. It also initializes the external ShadowMap handle table there because
the resource-constructor hook was equally late. Registration happens before the
original renderer call and verifies all 16 added keys (Shadow plus ShadowAlpha
for passes 17-24). This build is the first test of an actually active 24-map/pass
extension under NexusTools loading.

Test correction: v1.2.13 crashed twice before loading at `MSVCR100.dll +0x53C73`.
The only log entries were startup and hook-armed messages; neither success nor
guarded error was reached. This rejects combined late resource/pass creation.
Because resource creation occurred before registry discovery and registration,
it is the first suspect, but the fault cannot yet be assigned conclusively to a
single call. v1.2.14 removes every mutation and captures the live pointers,
native handles, pass 16, and pass 17 state once on the same callback.

v1.2.14 loaded successfully. Snapshot:
`manager=0x3F1B84B0`, `registry=0x15585740`, `table=0x1705A780`, native handles
`AE7757AF/DB6DD3C1`, valid pass-16 objects, and null pass-17 objects. The run
continued past 6,300 manager calls. This validates the pointer chain and isolates
mutation as the v1.2.13 regression. v1.2.15 now tests only pass-17 registration,
with no resource creation and before/after markers around each call.

v1.2.15 completed both registrations with non-null objects. It later reached
missing key `0x240C` (pass 18) at manager call 11,551 and faulted at the unchanged
`+0x408A4F` reset. This is strong causal proof: the live pass object survives,
and demand simply advances to the next absent pass. v1.2.16 therefore registers
passes 17-24 only. Late resource creation remains excluded because v1.2.13 tied
that operation to the immediate MSVCR100 crash.

v1.2.16 registered the complete 17-24 range successfully and completed the test
past 71,900 manager calls with no null-key marker and no corresponding crash
event. The original delayed `+0x408A4F` failure is therefore considered fixed in
this beta candidate. Do not reintroduce late ShadowMap resource creation.

The remaining tree-shadow defect is a separate scheduler/residency issue. It is
present in v1.2.11 and v1.2.16, absent with the mod disabled, and coincides with
headlight activation. Treat it as dynamic-to-cache demotion until an ownership
trace proves the exact queue transition; do not describe it as a pass-table or
physical-map crash.

# Previous controlled experiments

Create v1.2.7 from v1.2.6 and restore only the exact v1.2.2 partition/safety valve.
Keep native B4 scheduling and v1.2.6 diagnostic gating. This intentionally
reintroduces disappearing pole/world type-3 shadows and is not a release design.

- Stable result: type-3 admission pressure is necessary to trigger the crash.
- Crash result: investigate the common 24-map/queue expansion independently.

Build status: completed and tested stable through 35,647 manager calls. ZIP SHA-256
`3095F1103E6D9844A0E85398A0214A081A59C24791C6B14C5B4310E11B4A378F`; ASI
SHA-256 `87100D9FB4D9A4AA5D78825706480D0E76C3C328AD86E664D1744FF8BD3CDF50`.

Observed maxima from 218 sampled log records:

- raw candidates: 163;
- filtered chain: 118;
- candidates dropped in one sample: 61;
- admitted records: 13;
- cache count: 9;
- selected type 3: 4;
- retained type-1 world candidates: 112;
- native scheduler states: 4 and 8.

The game did not crash, while pole shadows disappeared and coarse fallback still
occurred in some areas. This validates the A/B prediction: controlling type-3
admission removes the crash trigger, but class-wide type-3 filtering damages world
lights and does not solve every quality/residency limit.

After the A/B result, trace the v1.2.6 fault caller at `7ffc39edd596` back to its
producer and capture a stable light identity before the candidate's lifetime ends.

# Evidence files

## 2026-08-13 v0.7.7 pass-25 boundary

v0.7.7 validated the native pre-reserve repair: owner-vector capacity decoded
from four to eight before admission and its base did not move. The crash moved
to `Disrupt +0x408A4F` with `RDX=0x320C`, a lookup for Shadow pass 25. Registered
local resources stop at map 23/pass 24.

The renderer decompilation at `+0x306E60` establishes separate quantities:
queue entry count and cumulative face count. It initializes the cumulative face
base by summing each entry's `+0x20A0` face count, then computes the pass key from
`faceWithinEntry + cumulativePreviousFaces`. The B4 scheduler value limits
dynamic records, not this cumulative face total. Multi-face records explain how
17 admitted positions crossed a 24-map pool.

v0.7.8 clamps only the render-visible queue prefix at whole-record boundaries
when cumulative faces would exceed 24, calls the native renderer, then restores
the manager-built entry count for native ownership/cleanup. It retains the
active 24-map extension and does not restore the old vehicle/type-3 filter.

- Clean log: `Research/Logs/v0.7.7_owner_reserve_pass25_overrun/ShadowEnginePatch_v0.7.7.log`
- Dump: `Research/Logs/v0.7.7_owner_reserve_pass25_overrun/Watch_Dogs.exe.47828.dmp`
- Targeted decompile: `Research/RuntimeCaptures/WD1_RenderQueue_Targeted_20260813.txt`

- `Research/RuntimeCaptures/VehicleShadowLimiter_v1.2.6_crash.log`
- `Research/RuntimeCaptures/WD1_DynamicShadowCacheAllocator.txt`
- `Research/RuntimeCaptures/WD1_v1.2.6_Fault_1EFDAB_Disassembly.txt`
- `Research/RuntimeCaptures/WD1_v1.2.5_Fault_25DBFA_Disassembly.txt`
- `Research/RuntimeCaptures/WD1_v1.2.5_v1.2.6_FaultFunctions_Decompiled.txt`
- `Research/RuntimeCaptures/WD1_v1.2.6_FaultCallerChain.txt`
- `Research/RuntimeCaptures/WD1_v1.0_Crash_408A4F_Disassembly.txt`
- `Research/RuntimeCaptures/VehicleShadowLimiter_v1.2.7_stable_safety_valve.log`
- `Research/RuntimeCaptures/WD1_ShadowAdmissionAndCacheReconciliation.txt`
- `Research/RuntimeCaptures/VehicleShadowLimiter_v1.2.8_nativeA8_alley_trace.log`
- `Research/RuntimeCaptures/VehicleShadowLimiter_v1.2.9_B4_8_crash.log`

## 2026-08-13 v0.7.9 black-world crash

The clean PID 27652 session validates the v0.7.9 early-layout lifecycle but
rejects the build for long-run stability. It ran for roughly 171 seconds and
reached 20 queue entries, 23 admitted faces and seven extra maps without a
numeric face overflow, owner-vector movement or reserve failure. Seven larger
requests were safely clamped at whole-record boundaries.

The matching dump faults at `Disrupt +0x1300A0` from queue builder
`+0x30D1CC`. The relocated tail reference at queue `+0x397A0` is
`0xBE491D453E93813E`, again invalid during add-ref. The renderer queue base was
`0x3E7CDEC0` and manager was `0x0840B2D0`. This is the same fault family as the
v0.7.8 night-transition crash, now occurring after successful early native
construction and much heavier use.

Static decompilation confirms the allocation site uses `0x397C0`, the sole
direct writer at `+0x3CA76F` targets relocated `+0x397A0`, and the builder reads
the same relocated field at `+0x30D1BD`. Therefore the old unconstructed-tail
explanation is resolved, but the later corrupting writer or lifetime transition
is not yet identified.

The attached black-world capture is consistent with a failed world-lighting or
shadow render state rather than total rendering loss: UI, rain, emissives and
lamp sprites remain visible while world geometry is nearly black. The user's
observation that pitching the camera upward restores lighting suggests a
view-dependent queue rebuild/reset path. This is a strong diagnostic clue, not
yet proof that the relocated tail causes the visual state.

- Evidence directory: `Research/Logs/v0.7.9_black_world_crash_pid27652`
- Dump SHA-256: `2590C97FABA21D60C4F652DE725BF0754682193227184BD5DA216A32568022B8`
- Original JXR SHA-256: `095FBE680EE1B8929FDFF5E1E33CDBB01969A4741B3F6EB14DCED90BF5D1FDD2`
- Detailed findings: `Research/Logs/v0.7.9_black_world_crash_pid27652/RESULT.md`
