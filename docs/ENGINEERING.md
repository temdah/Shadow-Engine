# Engineering the Watch Dogs Vehicle-Shadow Mod

## What the mod is trying to add

Unmodified Watch Dogs renders vehicle headlight illumination but normally does
not let ordinary traffic headlights consume dynamic shadow maps. The goal is to
allow the nearest two cars (four headlight spotlights) to cast moving shadows
without degrading or evicting shadows from streetlights, poles, characters, or
other world lights.

This is harder than turning on a `castShadow` flag. A shadow-casting light needs:

1. admission into the frame's candidate list;
2. a shadow-map resource and render-pass slot;
3. an update decision for the current frame;
4. stable ownership across frames;
5. eviction rules when more lights compete for resources;
6. a lighting pass that samples the correct map.

If only one layer is enlarged, a later layer can still truncate, alias, evict, or
index beyond its original storage.

## The WD1 pipeline we have reconstructed

At a high level:

```text
world and vehicle lights
        |
candidate construction and classification
        |
shadow-manager admission / priority
        |
dynamic or cached residency decision
        |
render queue and per-face map allocation
        |
ShadowMapN resource + ShadowN/ShadowAlphaN pass
```

The original local-map arrangement has 16 local maps. Render pass 16 is special:
it is `LongRangeShadow`, so extra local maps cannot simply continue at pass 16.
Our expansion creates 24 local maps and maps the new eight onto passes 17-24.
The render queue grows from 17 to 25 entries, and 46 embedded references to the
old queue tail were relocated.

This expansion genuinely helped: the recurring coarse/fallback shadows were
eliminated in the strongest test builds. It does not, by itself, solve admission
or stable ownership.

## The important shared-path discovery

Renderer type 3 is not synonymous with a vehicle headlight. Some pole and world
spotlights also use type 3. Filtering or toggling all type-3 records therefore
removes legitimate world shadows. This explains why an early "headlight toggle"
also disabled pole, fence, and player shadows.

Classification must happen earlier, while the engine still knows which vehicle
or light component owns the candidate. Trying to recover ownership from a
short-lived renderer candidate after submission is fragile and has crashed.

## Capacity, budget, and scheduler are different

- **Capacity** is physical storage: maps, records, queue entries, handles.
- **Admission budget** limits how many candidates become shadow work.
- **Scheduler threshold** controls when work is refreshed or moved between
  residency modes.
- **Eviction policy** chooses which existing owner loses a resource.

We initially forced two WD1 values from their native adaptive range of 4-8 to 21,
assuming they were capacity constants. Disassembly later showed they govern an
adaptive scheduler field at manager offset `B4`. That experiment produced
regressions. Current v1.2.6 restores the native 4-8 behavior while retaining the
24-map physical expansion.

Manager offset `A8` is currently treated as the local cache allocation ceiling
and is patched to 24. This interpretation is stronger than the rejected B4
interpretation, but all linked consumers still need auditing.

We now know the manager combines these fields directly. With cache enabled, its
ordinary-light allowance is approximately `(B4 - 1) + A8`. The first `B4 - 1`
eligible lights remain fully dynamic; later lights receive cache records up to
A8. Consequently, increasing B4 reduces coarse fallback but expands the expensive
dynamic lane, while increasing A8 retains more cached owners. Neither field is a
standalone physical shadow-map count.

## What the visual faults usually mean

- **High-resolution shadow becomes coarse:** a dynamic owner fell back to a
  cached/lower-resolution lane, or sampled a different residency class.
- **Shadow vanishes near a headlight:** admission or eviction changed as scene
  priority changed; it is not necessarily physical map exhaustion.
- **Black flicker on Aiden:** map ownership/pass mapping changed while lighting
  still sampled the slot, or a light switched between shadow representations.
- **Moving shadow updates at low FPS:** the light remains admitted but its map is
  refreshed on a staggered scheduler rather than every rendered frame.
- **Crash as density rises:** suspect an unpatched linked array/index, a lifetime
  error in instrumentation, or a queue/pass mismatch before assuming GPU load.

## What WD2 teaches us

Watch Dogs 2 uses a later version of the same Disrupt lineage. Reverse engineering
shows a more explicit architecture:

- dynamically resized shadow-manager storage with records around `0x230` bytes;
- an explicit configurable dynamic-map maximum;
- a separate gameplay/headlight-style budget clamped to four;
- distance, coverage, priority, fade, and shadow-group metadata;
- named settings such as `HeadlightShadowsLevel`, `MaxDynamicShadowMaps`, and
  `MaxStaticShadowMaps`.

The useful lesson is architectural, not the number four. WD2 separates vehicle or
gameplay selection from general world-light residency. Backporting that design to
WD1 means identifying the earlier ownership point, maintaining a dedicated ranked
vehicle list with hysteresis, and feeding only the selected headlights into the
shared renderer without altering the world-light chain.

## Current bottlenecks

1. We have not yet found a safe, early WD1 ownership signal that distinguishes
   vehicle headlights from world type-3 spotlights.
2. The entire set of arrays and index consumers linked to the 24-map expansion has
   not been proven complete under dense scenes.
3. WD1 admission, cache residency, and dynamic update scheduling are intertwined;
   changing one observed number can affect another policy.
4. Post-admission diagnostic pointers are transient. v1.2.5 walked them even when
   tracing was off. v1.2.6 gated that traversal but still crashed, proving another
   fault remains in the common engine patch.
5. A truly independent vehicle map pool may require additional render passes,
   resource handles, and lighting-pass bindings—not just separate CPU storage.
6. In the v1.2.6 crash run, manager cache count rose to exactly 16 and never 17.
   This first looked like a fixed-width store, but allocator disassembly shows the
   manager cache is a dynamic vector with `A8` as its ceiling. The remaining risk
   is an incomplete downstream consumer or invalid light-record lifetime, not a
   proven 16-entry allocation.
7. Crash RVAs differ across v1.2.4-v1.2.6. Two are invalid-object dereferences and
   one maps to a mid-instruction address in both the captured runtime image and
   rebuilt DLL. That pattern is
   more consistent with pressure-triggered lifetime/state corruption than a
   single clean capacity check failing.
8. The v1.2.6 fault occurs in general per-light scene processing. A descriptor is
   looked up from the light object's handle at `+0x50` and used without a null
   check. The immediate failure is therefore a stale/unregistered light object or
   handle, although the upstream action that invalidates it remains unknown.
9. v1.2.7 proves that limiting the selected type-3 set to four avoids the reproduced
   crash even with 163 raw candidates. It does not prove four is the engine maximum;
   it proves that the unsafe state lies somewhere above or outside that controlled
   workload. Because world pole lights also use type 3, the correct repair must
   distinguish ownership before applying a vehicle budget.
10. A traffic-enabled v1.2.2 retest crashed at the same object-reset RVA as v1.2.4.
    Because v1.2.2 uses the same type-3 filter but forces B4=21, this proves the
    large fully dynamic lane can also create unsafe owner pressure. Its lack of
    coarse fallback before crashing is the expected quality/stability tradeoff.
11. v1.2.8 restores native A8=8 while retaining the full chain and stays stable.
    Its trace proves why two cars still hurt quality: three ordinary lights render
    dynamically at 4096x4096, while multiple world and vehicle-labelled owners
    rotate through one 512x512 cached refresh slot. Only 9 of 24 physical faces are
    used, so the bottleneck is residency policy rather than map allocation.
12. v1.2.9 raises B4 to 8 and removes the tested coarse fallback, but repeatedly
    reaches 17 total admitted records and crashes at `0x408A4F`. The faulting
    renderer initializer receives an unchecked null from `ff5130/ff5150`.
13. Those functions are two-level renderer table lookups, not allocators. The key
    encodes `bucket = low byte` and `index = remaining upper bits`. We therefore
    know a required renderer entry is absent, but not yet which entry or why.
    v1.2.10 logs the null key and caller without changing the failure path.
14. v1.2.10 captured the failure: shadow-pass key `0x220C`, meaning pass 17, is
    absent. The renderer calls the null lookup at RVA `0x307405` and the crashing
    reset at `0x307410`. Our frame hook registered extra passes after native table
    finalization, so the registration happened too late.
15. Instrumentation itself can perturb a real-time renderer. Logging hundreds of
    thousands of normal null probes synchronously reduced FPS from 200+ to about
    30. Future probes must be narrowly keyed, memory-buffered, rate-limited, or
    one-shot.
16. v1.2.11 validates the registration-timing fix: coarse and disappearing
    shadows were absent for about ten minutes. The same null-reset crash later
    returned, so a one-shot filtered probe is required to learn whether demand
    exceeded pass 24 or another registered pass is still absent.
17. v1.2.12 applies that diagnostic safely: ordinary nulls execute only a small
    predicate, and file I/O is atomically limited to the first missing
    Shadow/ShadowAlpha pass at index 17 or higher.
18. v1.2.12 proved the missing key is still pass 17 and the intended constructor
    injection ran zero times. NexusTools' Lua entrypoint loads the ASI after the
    pass constructor. A late-loaded ASI must repair/rebuild the live table or use
    a different earlier bootstrap; it cannot intercept an event already finished.
19. The pass-registration routine is itself the live object factory. It allocates
    `0x1B0` bytes, constructs the render-pass object, and inserts it into the
    registry's two-level table. This makes a safe late-load repair possible.
20. v1.2.13 runs that repair once on the renderer thread before the original queue
    consumer. It also creates the missing resource handles there. Success must be
    proven by the explicit initialization/verification log line before visual or
    endurance results are interpreted.
21. Runtime result rejected that combined repair: it crashed before loading in
    `MSVCR100.dll`, before logging any initialization result. Constructor-era
    resource APIs cannot be assumed safe after startup merely because the render
    thread calls them. v1.2.14 returns to read-only observation and separates
    pointer validation from mutation.
22. Isolating pass 17 proved late pass allocation itself is valid. Both objects
    remained live until demand reached absent pass 18, then the same unchecked
    null reset occurred. The safe repair is therefore to populate the complete
    linked pass range independently of resource creation.
23. v1.2.16 validates that repair through more than 71,900 manager calls. The
    remaining artifact is orthogonal: vehicle demand can demote a world spotlight
    from the fully dynamic lane to a lower-resolution cached lane whose refresh
    is time-sliced. Resolution loss and low-FPS shadow motion are consequently two
    symptoms of the same residency transition, not a reduction in game FPS.
24. The focused tree trace turns that model into direct evidence. Seven vehicle
    headlights occupied the fully dynamic lane, while world spotlights `0x1D25`
    and `0x1D1B` entered the sole active 512 cached refresh slot and were replaced
    by vehicle lights. Eight cached owners share that slot over time, explaining
    why a demoted tree shadow is simultaneously coarse and animated at low FPS.
25. v1.2.16 cannot currently enforce a car budget because it is deliberately a
    full-chain pass-through. The dormant prototype only pairs type-3 records by
    similar direction and list proximity. It does not identify the owning vehicle
    or know which vehicle is closest to Aiden.

## How to reason about the next patch

### Why raising A8 caused corruption even though the vector could grow

The dynamic shadow cache does not store owners as individually stable heap
objects. It stores `0x700`-byte owner records in one contiguous vector, while
the admission pass writes raw pointers to those records into temporary candidate
descriptors. The vector is technically able to grow, but growing it at that
moment moves every owner and invalidates pointers already published earlier in
the same pass.

The old extension let the native profile reserve four owners and then changed
only the logical ceiling to eight. The fifth allocation therefore triggered a
mid-pass move. The first visible symptom could be a global lighting collapse;
depending on which stale pointer was followed next, the process later crashed
in CPU configuration lookup or inside D3D11.

The correct engine extension is not merely "raise the number." It must preserve
the engine's construction invariant: at the pre-admission boundary, reserve the
complete backing store while preserving any already-constructed owners; verify
the record count and physical capacity; and only then expose the higher logical
ceiling. v0.7.7 does this through the engine's native reserve routine. This is
the general pattern for increasing old engine limits safely: patch storage
lifetime and initialization order alongside the visible counter.

Start from evidence, not a target number. For each candidate field or constant:

1. find every writer and reader;
2. identify the containing object and constructor;
3. determine whether it controls storage, admission, scheduling, or eviction;
4. trace indices into the next structure;
5. patch the complete linked set or leave it unchanged;
6. test one variable at a time against the same scene.

The most promising long-term design is WD2-style separation: retain native world
light admission, collect vehicle headlights at registration time, rank by player
distance with hysteresis, admit at most two vehicle pairs, and reserve their
resources without stealing the world scheduler's records.

In practical terms, "limit cars" must mean suppressing only the cast-shadow
candidate for excess vehicle lights. Turning off the light itself would also
remove headlight illumination and is not the intended behavior. Ranking individual
light IDs is also insufficient because one car contributes a left/right pair.
The selector needs a common vehicle-owner identity, one distance per owner, and
an atomic decision that keeps or drops both lights together. Hysteresis (a wider
replacement threshold or short ownership hold time) is required so two similarly
distant cars do not swap shadow residency every frame.

v1.2.17 is the first controlled approximation of that design. It obtains player
coordinates through NexusTools rather than adding another engine hook, then pairs
confirmed vehicle lights using the empirical invariant visible throughout the
trace: left/right IDs are consecutive and their world positions are close. This
is safer than class-wide type-3 filtering but remains an approximation until a
common vehicle entity pointer is recovered. Therefore its failure modes are
deliberately conservative: unknowns and incomplete pairs remain eligible, and
missing position input restores native full-chain behavior.

The first test isolated a classifier failure, not a ranking failure. Player input
was valid, but the attempted component-registry query did not recognize any
candidate in this renderer state, so the safety fallback passed everything. The
render trace provides a simpler runtime discriminator: vehicle records consistently
carry component handles `430C0000` or `43430000`, while observed world spotlights
carry `43820000`. v1.2.18 uses those handles but still requires a complete
consecutive-ID/proximate pair before suppressing anything, limiting the blast
radius if another light family later shares a handle.
