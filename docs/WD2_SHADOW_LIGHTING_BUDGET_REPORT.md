# Watch Dogs 2 shadow and headlight budget comparison

Date: 2026-08-13

## Executive answer

Watch Dogs 2 does not solve vehicle-headlight shadows with one unlimited shadow
pool. It combines three controls:

1. a vehicle-aware headlight admission policy;
2. a configurable general dynamic-shadow budget;
3. a distinct static-shadow cache with its own count, resolution and refresh
   policy.

This does not invalidate the Watch Dogs 1 engine expansion. Raising physical
capacity beyond the original limit remains a valid goal. It does show that
capacity must be expanded coherently and paired with residency policy. More maps
alone cannot guarantee important world spotlights remain high-resolution when
unrestricted headlights enter the same generic queue.

## Confirmed WD2 behavior

### Vehicle-level headlight policy

The WD2 binary registers `HeadlightShadowsLevel` with the description:

`0 = OFF / 1 = Only your car / 2..4 = 2..4 cars`

The wording is cars, not individual spotlight records. The player vehicle is a
special policy tier. This proves WD2 makes a headlight decision while vehicle
ownership is still available.

| Render profile | HeadlightShadowsLevel |
| --- | ---: |
| pc_0, pc_1, pc_2 | 0 |
| pc_3 | 1 |
| pc_4 | 2 |

The executable accepts levels 3 and 4, but the extracted shipped profiles stop
at level 2. This is a deliberate scalability control.

Evidence: `Research/WD2/WD2_HeadlightShadow_Xrefs_v3.txt` and
`Research/WD2/Watch_Dogs2_Shadow_Analysis/patch2/engine/settings/defaultrenderconfig.xml`.

### Independent dynamic and static budgets

| Shadow profile | Dynamic maps | Static maps | Static-map size | Minimum spot coverage |
| --- | ---: | ---: | ---: | ---: |
| pc_0 | 8 | 4 | 256 | 0.001 |
| pc_1 | 8 | 4 | 256 | 0.005 |
| pc_2 | 8 | 4 | 256 | 0.0001 |
| pc_3 | 10 | 4 | 512 | 0.00005 |
| pc_4, pc_5, pc_6 | 12 | 4 | 1024 | 0.00001 |

WD2 independently registers `MaxDynamicShadowMaps`, `MaxStaticShadowMaps`,
`StaticShadowMapSize`, `AlwaysUpdateNewStaticShadowMaps`,
`ShadowOmniSpotMinCoverage`, and `SpotsCastShadows`. Ubisoft exposed dynamic
capacity, persistent/static capacity, cache resolution and coverage admission as
separate controls.

Evidence: `Research/WD2/WD2_Shadow_Functions_Decompiled_v5.txt` and the extracted
configuration above.

### Static-shadow cache manager

`FUN_186df59a0` reads a count clamped to 0-8 and a size clamped to 32-4096. It
rebuilds a vector of approximately `0x230`-byte records. The allocator/creator
names the resource `StaticShadowMap`. Stock PC profiles request four, although
the manager accepts up to eight.

This corrects an earlier hypothesis: the 0-8 manager is a static-shadow cache,
not the vehicle selector.

Evidence: `Research/WD2/WD2_DynamicShadowManager_Decompiled.txt` and
`Research/WD2/WD2_Shadow_Manager_Callers_20260813.txt`.

### Corrected false lead

`FUN_186df5af0`, previously suspected because it clamps a value to 0-4, is
reached from a constructor loading `Mesh_WD2Vegetation`, `VEGETATION_ANIM`,
`VEGETATION_ANIM_LEAF`, `USE_GLOBAL_WIND`, and displacement controls. It is
vegetation/wind code, not the four-car selector.

## Confirmed WD1 behavior

At our current shadow-manager interception point, vehicle headlights and world
dynamic spotlights are already generic type-3/`SpotLight3` candidates. Pole,
fence, tree and vehicle tests prove globally filtering this type removes both
classes.

Classifiers based on renderer type, transient IDs, guessed positions, direction
pairing and neighbouring records did not recover a reliable vehicle-to-lamps
relationship. Some removed world lights; others removed every vehicle shadow.

A direct search of the WD1 files and preserved runtime image found no
`CHeadlightShadowsManager`, `HeadlightShadowsLevel`, or WD2 help-string match.
This suggests the named subsystem was added later or stripped from WD1. It does
not prove vehicle identity never exists in WD1. Vehicle/headlight ownership must
exist earlier, before those lights are flattened into generic candidates.

## Unproven details

- The exact WD2 function that ranks eligible traffic vehicles.
- Whether ranking is strictly player distance or includes camera relevance,
  coverage and hysteresis.
- Whether both lamps share one physical map. Current evidence proves vehicle-
  level admission, not combined lamp rendering.
- Whether headlights have a physically separate resource pool. No dedicated
  `HeadlightShadowMap` name has been recovered. The safer model is selected
  vehicles feeding the general dynamic pool.
- The complete WD2 pass-registration loop. A targeted read-only Ghidra export
  timed out and produced no partial result.

## Meaning for the WD1 engine patch

The 24-map direction is not inherently wrong. Earlier failures came from
incomplete coupling and inconsistent policy, not from the ambition to exceed
native limits.

Extra maps have already rendered and improved retention. We also found concrete
linked failures:

- raising an owner ceiling without pre-reserving moved live records after raw
  pointers were captured;
- record count did not equal face count, so multi-face lights exceeded the
  physical pass range;
- late queue-tail relocation exposed unconstructed memory;
- changing the scheduler's lower branch to 17 while leaving its other branch at
  8 produced contradictory 17/8 behavior.

All stages must agree:

`policy -> admission -> face cost -> owner residency -> queue -> maps -> passes -> lifetime -> teardown`

## Recommended architecture

### Track A: coherent engine expansion

1. Repair both scheduler branches as one coherent target.
2. Validate WD2's shipped high-end value of 12 dynamic maps first. This is a
   known successor-engine reference, not our final ambition.
3. Then validate 16 and 24 as explicit extended profiles.
4. Construct/reserve every owner, queue, map, pass and teardown consumer before
   use.
5. Budget cumulative faces, not only admitted light records.
6. Protect persistent world-spotlight residency so headlights cannot demote all
   pole/tree lights into the coarse rotating cache.

### Track B: vehicle-aware admission

Do not classify vehicles after they become generic type-3 candidates. Locate
WD1's vehicle-headlight registration path and attach ownership or decide there.

The WD2-like policy should give the player vehicle a priority bypass, select
traffic as stable vehicle nodes, make both lamps inherit vehicle selection, and
use hysteresis so ownership does not thrash every frame. Visible illumination
must remain independent of shadow admission.

If no practical WD1 owner link is recoverable, protected world-versus-moving-
light residency is still useful. It will be less precise than WD2 but can stop
headlights evicting all nearby world shadows.

## Immediate consequence

v0.7.9 remains useful only as a queue lifecycle control. It is not expected to
fix coarse shadows: runtime already showed the scheduler returning to eight and
leaving maps 16-23 unused.

The next quality build should coherently repair the scheduler and validate 12
maps while retaining all proven 24-map research for later extended profiles. In
parallel, trace WD1 vehicle lights before generic `SpotLight3` admission.
