---
type: Experimental Findings
title: Findings and Evidence
description: Conclusions supported by in-game tests, logs, or reverse engineering.
tags: [findings, evidence, wd1]
timestamp: 2026-08-12T01:20:00+02:00
---

# v1.2.25 temporal classifier result and pivot (2026-08-13)

The user reported that v1.2.25 did not behave like a correct nearest-four
vehicle limiter. The preserved runtime log confirms that the limiter executed,
but does not validate its semantics.

- Preserved log:
  `Research/Logs/v1.2.25_temporal_classifier_test/VehicleShadowLimiter.log`
- SHA-256:
  `85E348EE1E4691F19C8453146B7D65D508CCB1FB0B30A1291FA94F0BF83BCC93`
- 409 manager samples through call 68,624.
- Maximum 143 candidates, 55 raw type-3 records, 38 motion-confirmed records,
  24 inferred nodes, 30 suppressed records, and 17 admitted records.
- Suppression was active in 64 samples, so this was not a fail-open/no-op test.
- Inferred nodes sometimes had one or three members despite the intended
  two-light vehicle cluster.

Conclusion: v1.2.25 can recognize and suppress co-moving spotlight records, but
it still lacks a true vehicle-entity-to-light ownership link. It cannot prove
that the four selected nodes are vehicles, that they are the four nearest cars,
or that suppressed moving lights are safe to remove. Retire temporal motion as
the primary production classifier. Preserve it only as diagnostic evidence.

The shared-pipeline path is now primary. A perfect four-car selector would still
allow eight independently shadow-casting headlights, while B4=8 provides only
about seven ordinary fully dynamic owners before nearby world spotlights are
counted. Classification alone therefore cannot meet the four-car coexistence
goal. Vehicle ownership can return later as a performance guard after the shared
spotlight residency system is capable enough.

# TFoWC2 developer feedback (2026-08-12)

The Fall of Windy City 2 developer independently confirmed that a WD2-style
vehicle limiter is necessary to control shadow-map count and draw calls, and that
each car has two lights which cast shadows separately. This supports selection
at a grouped vehicle-owner/node level rather than independently ranking lamps.

They also explained that TFoWC2 moon shadows use the storm lightning system:
`lightningflashgenerator.lib` has an unused illuminate-world option, but the
unfinished light source resolves to the moon. TFoWC2's moon shadows are therefore
effectively never-ending lightning. Preserve this for the later storm-lightning
restoration project; it does not change the present vehicle selector.

# Corrected v1.2.17-to-v1.2.18 regression boundary (2026-08-12)

The byte-identical v1.2.18 control produced no vehicle shadows. The user's last
confirmed shadow-present versions are v1.2.16 and v1.2.17.

Source and preserved-log evidence:

- `vehicle_shadow_engine_patch.c` is byte-identical across v1.2.16, v1.2.17,
  and v1.2.18 (SHA-256
  `9B1A77BCF4EE73485FE383B0BC91729CB03BDE1F499699DD19B399996DF373A6`).
- v1.2.16 never filters the manager candidate chain.
- v1.2.17 added selection but recorded `vehicleCandidates=0` and therefore
  reconstructed the full chain unchanged.
- v1.2.18 replaced the failed component-registry classifier with exact handle
  matches `430C0000/43430000`. It is the first build in this sequence that can
  actually remove type-3 records.
- v1.2.22-v1.2.23 telemetry later proved those handles select a distant/wrong
  subset and omit nearby candidate handles.

Therefore the leading cause of disappearance beginning at v1.2.18 is active
misclassification/filtering, not the v1.2.16 engine/pass patch.

# v1.2.18 versus v1.2.23 capacity-state difference (demoted hypothesis)

The decisive observed difference is not candidate filtering. Preserved v1.2.18
logs show `localCapacity=8, cacheCount=0` while the v1.2.23 fail-open log shows
`localCapacity=4, cacheCount=4`. Both report `dynamicBudget=8`. The latter engine
state allocates only four fully dynamic local records and moves four owners into
the cached lane, consistent with headlight shadow loss/slow-update competition.

Source comparison shows the same profile callback and scheduler patches, so an
exact original-ASl binary control is required before attributing the difference to
C logic. v1.2.24 uses ASI SHA-256
`BE2AE52D9F536CDD78FA9DEBC2B7E793EAFC46CCDA73100AB1ABD2B4F3E7E522`,
identical to v1.2.18. The exact-binary control nevertheless produced no vehicle
shadows, so this state difference is not yet causal and must be investigated
separately.

# v1.2.22 wrong-classifier-subset evidence (2026-08-12)

The log at `Research\Logs\v1.2.22_wrong_classifier_subset\VehicleShadowLimiter.log`,
SHA-256
`5BFD0FEE113269D77C5E9BAF82108B176A3372B671A71CF0D8F83B93E0AFB373`,
shows valid player coordinates and four retained 430C/4343-classified sources,
but those sources were roughly 75-230 metres away. Nearby car shadows remained
absent. Therefore the distance selector worked, but the handle classifier did not
describe the intended nearby vehicle-light population.

# v1.2.21 second zero-pair confirmation (2026-08-12)

The log at
`Research\Logs\v1.2.21_spatial_pair_zero_pairs\VehicleShadowLimiter.log`,
SHA-256
`EC48007571F1BC0F72217715A66574AF2A5FD8DB80913898D3A9CA8BA8519666`,
again shows active classification with zero constructed pairs and zero retained
headlights. Both consecutive-ID and spatial/direction lamp-pair hypotheses are
rejected. v1.2.18's visible shadows came through records labeled “unpaired,”
supporting the combined-source interpretation used by v1.2.22.

# v1.2.20 zero-pair evidence (2026-08-12)

The log at
`Research\Logs\v1.2.20_world_position_zero_pairs\VehicleShadowLimiter.log`,
SHA-256
`49F242E2639135D4D033CE022AF31C928B9660D60CD4AA7ECBC4CB05FBA50B79`,
shows working player position and 1-12 positively classified vehicle candidates,
but zero complete pairs and zero retained headlights for the entire test. Thus
the absence of vehicle shadows occurred before native admission or rendering.
Consecutive light IDs are not a reliable pair identity rule.

# v1.2.19 wrong-position evidence (2026-08-12)

The rejected log is preserved at
`Research\Logs\v1.2.19_strict_four_wrong_position\VehicleShadowLimiter.log`,
SHA-256
`4777C2DA9E199A95DE088D5A8FE4E88692B085D516732C6B3A46817938BEFC56`.

`PAIR_SELECTION` repeatedly reported pair positions around `(0,0,-0.98)` while
the player was around `(1700,600-750,70)`, yielding meaningless distances near
1,800-1,900 metres. The selector was reading spotlight direction from holder
`+0x14/+0x18/+0x1C`. `WD1_ShadowManager_Caller_Function.txt` independently shows
the renderer copying holder `+0x08/+0x0C/+0x10` as the position and the later
fields as direction-dependent data. v1.2.20 uses the former offsets.

# v1.2.18 limiter-leak evidence (2026-08-12)

The preserved log at
`Research\Logs\v1.2.18_nearest_two_partial\VehicleShadowLimiter.log` has SHA-256
`E87E664B6427F594D8A048806E9C2808CDEBAE59723BB6F1EEC684A2D3154126`.

It proves that player position and handle-based classification were live, but the
limiter was not strict. Samples retained 5-7 headlight records despite a
two-pair/four-lamp target. The responsible predicate was
`!vehicle || !paired || selected`: classified-but-unpaired records bypassed
selection. This explains how a passing vehicle could cast while the visually
nearest vehicle did not—the visible set was not the selected set.

v1.2.19 uses `!vehicle || selected`, selects four complete pairs, and adds
`PAIR_SELECTION` evidence. If logged distances match the scene while a chosen
nearest pair remains invisible, the remaining fault is downstream residency or
admission rather than nearest-car classification.

# High-confidence findings

1. **Type 3 is shared.** Toggling/filtering type-3 candidates affected both vehicle
   headlights and pole/world dynamic shadows. It must not be called a vehicle type.
2. **Scheduling is a bottleneck before extended capacity.** A no-car capture used
   about 12 faces with `overflow=0` while a pole shadow still disappeared. The
   later audit found only native map handles 0-15 were live in v1.2.16.
3. **The native 4-8 values are adaptive scheduler controls.** Treating them as a
   record capacity and forcing 21 produced regressions.
4. **Pass 16 is reserved.** It is `LongRangeShadow`; extra local passes must skip it.
5. **Post-admission ownership pointers are unsafe to poll continuously.** v1.2.5
   did so even with tracing disabled. v1.2.6 gates this work, but still crashed,
   proving the observer was not the sole fault.
6. **Traffic is not required for overload-like behavior.** Candidate counts rose
   sharply with moving traffic disabled, demonstrating substantial world-light work.
7. **Type-3 admission pressure is necessary for the reproduced crash.** v1.2.7
   retained the v1.2.6 engine patch but capped selected type 3 at four and survived
   35,647 manager calls, including scenes with 163 raw candidates.
8. **The safety valve is not the solution.** The same v1.2.7 run reproduced pole
   shadow disappearance and coarse fallback, because legitimate world lights share
   type 3 and are discarded by the class-wide filter.
9. **B4 and A8 form separate residency lanes.** Manager decompilation shows the
   ordinary admission allowance is approximately `(B4 - 1) + A8` when caching is
   enabled. The first `B4 - 1` records are dynamic; subsequent records use the
   `0x700`-byte cache vector up to A8.
10. **Coarse fallback in v1.2.7 is consistent with native scheduling.** With B4=4,
    only about three ordinary records stay in the dynamic lane even though 24
    physical maps exist. Extra eligible lights enter the cached lane.
11. **The expanded dynamic lane can independently trigger the crash.** v1.2.2
    shares v1.2.7's type-3 valve but forces B4=21; with traffic enabled it crashed
    at the same `0x408A4F` RVA as v1.2.4 while avoiding visible coarse fallback.
12. **The failure follows total retained-owner pressure.** Stable v1.2.7 logged up
    to 13 admitted records; crashing v1.2.6 reached 19. This brackets the observed
    test workload but does not prove a hard numeric engine limit.
13. **Native A8 stabilizes the full chain.** v1.2.8 passed 186 raw candidates and
    69 raw type-3 records without crashing while admission peaked at 13 and cache
    count stopped at the native ceiling of eight.
14. **The remaining coarse pop is a proven lane demotion.** Trace snapshots show
    three ordinary 4096x4096 dynamic queue positions and a single 512x512 cached
    refresh position at queue slot 4. Seven cached owners rotated through that slot.
15. **Native live maps were not exhausted in the alley failure.** Renderer frames
    used nine faces with `overflow=0`; scheduling alone caused the transition.
16. **The native cache refresh policy churns heavily under two cars.** The trace
    recorded 5,234 render adds and 5,222 drops. Queue slot 4 accounted for 5,222
    adds, split almost evenly between diagnostic world and vehicle labels.
17. **Seven dynamic positions solve the tested fidelity problem but exceed a linked
    renderer limit.** v1.2.9 showed no coarse fallback, then crashed after about one
    minute while repeatedly admitting 17 total records.
18. **The recurring `0x408A4F` fault follows a null renderer-table lookup.** The
    faulting reset routine receives the unchecked result of
    `FUN_7ffc39ff5130/5150`. Both functions are lookups, not allocators: they return
    `table[key & 0xff][key >> 8]`. A missing table entry is proven; its key, caller,
    ownership, and reason for being absent are not yet proven.
19. **v1.2.10 identifies the missing entry as added shadow pass 17.** The only
    one-off null was key `0x220C` at shadow-renderer call RVA `0x307405`, returning
    to `0x30740A`; the unchecked reset call follows at `0x307410`. This is the
    first added local pass, not a random light-owner pool failure.
20. **The existing extra-pass hook runs too late.** It calls the original frame
    constructor to completion and registers passes 17-24 afterward. Native code
    registers passes 0-16 and then finalizes/binds the lookup tables before the
    constructor returns. Post-return registration does not populate the renderer
    table needed by `FUN_7ffc39ef6e60`.
21. **Broad lookup logging is prohibitively hot.** v1.2.10 produced an 85.5 MB log
    and reduced observed FPS from 200+ to about 30. Most nulls were normal table
    enumeration; one caller alone produced 561,784 null records. Never ship or
    reuse this instrumentation design.
22. **Pre-finalization registration fixes the tested visual failures.** v1.2.11
    ran about ten minutes without coarse or disappearing shadows, making it the
    best functional baseline, but the later v1.2.12 log proved the intended pass
    injection executed zero times. The visual result is valid; attributing it to
    pre-finalization injection was incorrect.
23. **NexusTools loads this ASI too late to hook the native pass constructor.** The
    Lua `package.loadlib` entrypoint executes after the constructor's 0-16 loop.
    Consequently the register-pass hook never observes key `0x200C` and cannot
    create pass 17. v1.2.12 again captured missing `0x220C`, not pass >=25.
24. **v1.2.16 does not impose a vehicle limit.** Its active manager hook is an
    explicit no-filtration pass-through. The older `build_partitioned_chain`
    helper is dormant and, even if re-enabled, selects the first two nearby-in-list
    direction-matched type-3 pairs. It does not sort by player distance and does
    not prove that the paired lights share a vehicle owner.
25. **The tree defect is a captured dynamic-to-cache eviction.** In the reproduced
    scene, seven vehicle headlights occupied the ordinary 4096 dynamic positions.
    World spotlights `0x1D25` and `0x1D1B` moved through queue slot 8 at 512x512.
    The trace records world `0x1D1B` replaced by vehicle `0x1F6C`, and world
    `0x1D25` replaced by vehicle `0x2012`.
26. **Coarse quality and low update cadence share one cause.** Queue slot 8 rotates
    among eight cached owners, refreshing one owner per frame. A demoted world
    spotlight therefore renders at 512 rather than 4096 and updates only on its
    turn in the cached round-robin.
27. **Vehicle demand greatly exceeds the intended budget.** The focused trace saw
    26 distinct vehicle-classified light IDs, consistent with roughly 13 headlight
    pairs. A pre-trace snapshot alone retained 14 vehicle IDs across both lanes.
    This is conclusive evidence that no 2-4-car nearest-owner policy is operating.

# v1.2.16 tree trace artifact

- Preserved log: `Research/RuntimeCaptures/VehicleShadowLimiter_v1.2.16_tree_residency_trace.log`
- SHA-256: `053C753E2CE0C12C99E9240DE37B42CC7D15E236D2D85D13CBD3164122976E5`
- Trace volume: 2,131 trace records; 573 vehicle adds, 565 vehicle drops;
  69 world adds, 69 world drops.
- Distinct traced IDs: 26 vehicle-classified and two world spotlights.

# v1.2.5 final log evidence

Immediately before the crash:

- total candidates reached 122;
- raw type-3 candidates reached 29;
- admitted records reached 10;
- cache count reached 7;
- native B4 scheduler state was 4;
- tracing produced no ownership records, yet the observer still traversed pointers.

This makes the diagnostic lifetime bug a credible crash cause. It does not yet
prove that every 24-map linked consumer is safe.

# v1.2.6 final log evidence

Tracing remained inactive, ruling out continuous ownership-pointer traversal as
the sole crash cause. Before the crash:

- candidates reached 118 and raw type-3 candidates reached 28 earlier in the run;
- admitted records peaked at 19;
- `cacheCount` rose from 3 through 4, 5, 6, 11, 13, and finally exactly 16;
- the native B4 scheduler moved between 8 and 4 as expected;
- the final recorded frame had 118 candidates, 10 admitted, and `cacheCount=16`.

The exact stop at 16 initially suggested another native-width structure. Further
disassembly of `FUN_7ffc39ed2470` shows manager `+98/+A0` is a dynamically grown
vector with a `0x700`-byte stride; it grows while `A0 < A8`. Therefore the log is
not proof of a fixed 16-entry allocation. It remains evidence of the workload at
the crash boundary, and every downstream consumer of the resulting records still
requires auditing.

# Cross-version crash evidence

- v1.2.4: `Disrupt_b64.dll + 0x408A4F`, access violation while writing through
  invalid `RCX` in an object reset/destructor-like routine.
- v1.2.5: `Disrupt_b64.dll + 0x25DBFA`; the captured flat runtime image and rebuilt
  DLL both map the RVA to the middle of a non-faulting `LEA`. This confirms the
  mapping and is consistent with corrupted control flow/return state.
- v1.2.6: `Disrupt_b64.dll + 0x1EFDAB`, access violation at
  `MOV R8D,[RAX+8]`. `RAX` is the result of a light/descriptor lookup and is then
  used for fields through at least `+0xD8`, strongly indicating a null or stale
  light record reached this downstream construction path.

The v1.2.6 caller is a general per-light scene update routine. It resolves the
descriptor via the light object's handle at `+0x50` and uses it without a null
check, placing the observed corruption at a light-object/registry lifetime boundary.

The differing fault sites rule out claiming one deterministic “slot 17” crash.
Together they support pressure-triggered invalid lifetime/state or upstream
memory corruption after more dynamic/type-3 work is admitted.

# User-validated behavior

- The base game does not show the mod-induced fence/pole disappearance.
- Some builds stabilized high-resolution shadows even with headlights active.
- One car is enough to expose world-shadow instability in the alley test; two cars
  make it much more visible.
- Headlight shadows themselves can remain full resolution while world shadows lose
  fidelity or disappear, indicating competition/policy rather than one global
  quality setting.

# v0.9.0 modular-equivalence evidence (2026-08-14)

1. **The modular source loads and sustains the repaired engine path.** PID 10316
   identified v0.9.0 in its clean header, reached `STAGE_M_COMPLETE`, and the
   user played for more than ten minutes without a crash. This is meaningful
   comparison with prior 10–60 second crash builds, not unlimited stability.
2. **The manual captures are not visual classifications.** Four F8 and three F9
   snapshots all contained 18 entries, 21 faces and maps 0–20. Rapid flicker
   prevented the user from knowing which visual state each keypress caught, so
   the labels cannot prove black versus recovered state.
3. **Sidecar lifetime stayed balanced under substantially heavier load.** The
   run recorded 20,967 sidecar acquisitions and 20,967 successful releases,
   with no observed release, context, symmetry, reset, rollback or post-write
   failure.
4. **Thirteen repairs failed closed because the cycle was unverified.** The
   saved builder value did not agree with the non-null lookup result. The events
   clustered near renderer calls 39,154–39,619 plus 60,915. Their proximity to
   one later F8 is not tight enough to prove a visible-flicker cause.
5. **Dense diagnostic I/O remains an uncontrolled performance variable.** Repair
   and release events comprise 50,900 of 53,064 lines (95.93%), while the
   sidecar adds 41,934 native wrapper submissions. A sparse-success-logging A/B
   is required before changing sidecar semantics.

# v0.9.1 sparse-logging and high-load evidence (2026-08-14)

1. **Sparse success logging removed the dominant output volume.** PID 49316
   loaded v0.9.1 and produced 1,089 lines / 377,135 bytes over about 252.154
   seconds, reductions of about 48.7x and 60.8x from v0.9.0 respectively.
2. **Sidecar work remains active.** Periodic samples bound balanced sidecars to
   9,216-10,239, equivalent to about 73.1-81.2 extra native wrapper calls per
   second. The run did not record FPS, so this does not quantify its FPS cost.
3. **The screenshot is traffic-context evidence only.** It shows roughly nine
   visible cars. It does not show the user's brief coarse/disappearing-shadow
   observation and must not be cited as visual proof of that failure.
4. **The full 24-face state exposes a new exact lifecycle boundary.** Four
   producer-side null cycles used acquire count six/index 4 and release count
   two/index 0. Static list recovery maps both positions to record `+0x110`.
   The builder obtains this field from resource lookup key `0xCCB53E0B`.
5. **More capacity is not the next safe experiment.** The nulls occurred without
   sampled face overflow. Trace and repair `+0x110` before changing B4, maps or
   passes, and keep vehicle-owner recovery separate from that diagnostic.

# B4 scheduler is an adaptive range, not a single ceiling (2026-08-13)

Targeted decompilation of manager `Disrupt +0x2E9B60` proves B4 is updated by
two opposite branches. Normal mode selects a lower bound of four and an upper
bound of eight; a reduced mode uses one and two. One branch computes
`B4 = max(B4 - 1, lowerBound)`, while the other computes
`B4 = min(B4 + 1, upperBound)`.

The inherited extension patched only the decrement-side lower bound from 4 to
17. It did not patch the increment-side upper bound 8. Consequently one branch
forces B4 to 17 while the other can immediately force it back to 8. The v0.7.8
night log observes exactly this: first manager sample B4=17, then sustained
B4=8 with only eight/nine rendered faces and zero use of maps 16-23.

This is the best current explanation for coarse world shadows returning when
headlights enter: the real high-resolution working set remained effectively
native despite 24 physical maps. The next scheduler experiment must patch the
lower and upper bounds coherently and preserve the reduced-mode branch. Do not
describe `schedulerFloor=17` as a stable 17-record capacity.

Evidence:

- `Research/RuntimeCaptures/WD1_ShadowScheduler_B4_20260813.txt`
- `Research/Logs/v0.7.8_night_transition_late_queue_layout/ShadowEnginePatch_v0.7.8.log`
