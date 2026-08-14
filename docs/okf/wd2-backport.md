---
type: Reverse Engineering Reference
title: Watch Dogs 2 Shadow-Manager Backport Reference
description: What WD2 reveals about Ubisoft's evolved Disrupt shadow architecture.
tags: [wd2, disrupt, backport, headlight-shadows]
timestamp: 2026-08-13T20:10:00+02:00
---

# Why WD2 matters

WD2 is the successor implementation in the same engine lineage. Addresses and
layouts are not portable, but its responsibilities and budgets are an
architectural reference for a WD1 backport.

# Hard-confirmed behavior

- `HeadlightShadowsLevel` is documented in the binary as 0 off, 1 player car,
  and 2-4 cars. This proves vehicle-aware upstream admission.
- Shipped profiles use levels 0, 1 and 2; support for 3-4 exists but is not
  enabled by the extracted stock profiles.
- `MaxDynamicShadowMaps` scales from 8 to 10 to 12. Highest PC profiles use 12.
- PC profiles normally keep four static maps; static resolution scales from 256
  to 1024.
- `FUN_186df59a0` manages the static cache. It accepts 0-8 records and creates
  resources named `StaticShadowMap`.
- `FUN_186df5af0` is vegetation/wind displacement code. Its 0-4 clamp is not a
  four-vehicle selector. This corrects the earlier hypothesis.
- Named controls include `AlwaysUpdateNewStaticShadowMaps`,
  `ShadowOmniSpotMinCoverage`, `SpotsCastShadows`, priorities, shadow groups,
  maximum distance and fade distance.

# Architectural conclusion

WD2 combines vehicle-aware admission with a bounded general dynamic pool and a
separate static cache. A physically separate headlight map pool has not been
proven; selected vehicles likely submit their lamps into the shared dynamic
pool.

This complements rather than invalidates WD1 capacity expansion:

1. coherently expand owners, queues, faces, maps, passes and teardown;
2. protect persistent world-light residency;
3. recover vehicle ownership before generic type-3 admission;
4. apply player-vehicle priority and stable traffic-vehicle selection there.

# Required WD2-to-WD1 mapping

1. Trace `CHeadlightShadowsManager::Update` from the render setting to vehicle
   ranking and lamp submission.
2. Recover the allocation/pass loop behind WD2's 8/10/12 dynamic profiles.
3. Match WD1 equivalents by call graph, constants, strings and behavior, not RVA.
4. Locate WD1 vehicle-headlight registration before generic type-3 admission.
5. Backport complete responsibilities: ownership, ranking, hysteresis,
   admission, residency, eviction and lifetime.

# Key evidence

- `Research/WD2/WD2_HeadlightShadow_Xrefs_v3.txt`
- `Research/WD2/WD2_DynamicShadowManager_Decompiled.txt`
- `Research/WD2/WD2_Admission_Path_Decompiled.txt`
- `Research/WD2/WD2_Shadow_Functions_Decompiled_v5.txt`
- `Research/WD2/WD2_Headlight_TU_Decompiled.txt`
- `Research/WD2/WD2_Shadow_Manager_Callers_20260813.txt`
- `docs/WD2_SHADOW_LIGHTING_BUDGET_REPORT.md`
