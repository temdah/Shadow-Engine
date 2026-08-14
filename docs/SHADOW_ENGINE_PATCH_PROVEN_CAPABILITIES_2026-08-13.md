# Watch Dogs Shadow Engine Patch — Proven Capabilities Through v0.8.0

Date: 2026-08-13

This report contains only behavior demonstrated by saved runtime evidence or
the user's controlled in-game tests. It does not promote v0.8.0 to a stable
release and does not infer untested capability from allocated storage.

## Current tested artifact

- Artifact: `Installables\ShadowEnginePatch_Coherent12x4_v0.8.0.zip`
- ZIP SHA-256:
  `C40EAB3E464DDCF8A735DA1260EA670F3197FC21E0964DB901911AB2BAC9350A`
- ASI SHA-256:
  `9642900BA8D29B2771CAB50CF36567B4FA202B84EBD1AB340695825EBC71D4EB`
- Latest clean log SHA-256:
  `2064BE0B4AEAD8D8B355574A91F07CB3F09BAD0F3C60DB83911429D398E608C5`

## What the patch has demonstrated

### It loads early enough to extend native shadow construction

The root proxy intercepts startup before the native shadow queue and resources
are constructed. Runtime markers prove the expanded queue layout was installed
before construction, followed by the native resource and pass constructors.

### It constructs eight additional live local shadow maps

The engine originally exposes local maps 0-15. v0.8.0 created maps 16-23 during
the native construction window. All eight returned non-zero, mutually distinct
handles in the latest test.

This is stronger than merely allocating an address table: added maps were
actually consumed by rendering. The latest session reached map 17; the earlier
v0.7.9 session reached seven added maps.

### It registers the corresponding render passes

Pass 16 remains the native long-range shadow pass. The patch registered paired
Shadow and ShadowAlpha passes 17-24 before native lookup finalization. All 16
added registrations returned successfully in the latest session.

### It expands and exercises the render queue

The queue layout supports 25 entries and relocates all 46 known references to
the expanded embedded tail. v0.8.0 sampled as many as 15 entries and 18 faces.
v0.7.9 sampled 20 entries and 23 admitted faces. Thus extended entries and face
indices are proven live, although sustained safety at every possible load is not.

### It prevents the known pass-25 overrun in observed frames

The renderer counts cumulative faces rather than assuming one face per light.
The patch exposes only the largest whole-record prefix fitting 24 faces and then
restores the manager-built count for native cleanup.

In v0.7.9, seven requests exceeded the physical face budget. The largest asked
for 25 entries / 28 faces; the patch admitted 21 entries / 24 faces and logged
no physical overflow. That session later crashed in the distinct relocated-tail
add-reference fault family, so this proves the observed boundary behavior, not
overall stability of v0.7.9.

### It fixes the observed cache-owner reallocation failure

The patch uses the native reserve helper to grow the owner vector from physical
capacity four to eight before admission while keeping existing size intact.
Across the saved v0.7.7, v0.7.9 and v0.8.0 evidence, the owner base did not move
during manager admission and the former stale-owner/D3D11 failure did not recur.

### It can run a coherent 12-dynamic/four-cache scheduler profile

v0.8.0 patched the constructor, adaptive lower bound and adaptive upper bound to
the same B4 value of 12. The latest session observed B4=12 and A8=4 after profile
initialization, with no 17/8 oscillation.

This provides approximately eleven ordinary fully dynamic positions plus a
cached-owner lane. It does not mean every admitted queue entry is a separate
high-resolution dynamic light; special and cached-refresh jobs are also present.

### It sustained extended rendering in the latest controlled session

The latest log covers 258.495 seconds and ends at renderer call 41,901. It
contains 224 residency samples; 148 samples held 14 entries / 17 faces. The run
recorded:

- no physical overflow or face-budget clamp;
- no invalid queue-tail marker before or after rendering;
- no owner reserve or owner-vector invariant failure;
- no null resource guard event;
- no matching crash dump or Windows Application Error event.

The user observed no black-world state and no crash, and described shadows as
generally stable. These claims apply to this one tested session only.

### It does not suppress native light candidates

The current engine patch uses full native candidate pass-through. It contains no
active vehicle limiter, global type-3 filter, distance limiter or inferred
headlight-pair selector. This is confirmed by its runtime design markers and
source configuration.

## Remaining demonstrated failures

- During the latest two-car tree test, a world/tree shadow still demoted to a
  coarse shadow.
- One tree-test shadow briefly disappeared.
- Earlier trace evidence proves this quality transition is a fully dynamic to
  rotating-cache demotion: 4096x4096 world shadows moved to the shared 512x512
  refresh position when vehicle headlights occupied the dynamic lane.
- v0.7.9 suffered frequent black-world states and eventually crashed in the
  relocated queue-tail add-reference path. v0.8.0 did not reproduce it in one
  258.495-second logged run; the underlying late overwriter has not been found.

## What is not yet proven

- Crash-free endurance beyond the tested v0.8.0 session.
- Stable behavior in every district, weather state, time transition or traffic
  density.
- Twenty-four simultaneous high-resolution ordinary spotlight shadows.
- Guaranteed high-resolution residency for tree/pole/world spotlights under
  unrestricted headlights.
- Reliable identification of a vehicle and both of its lamps at the current
  generic shadow-manager interception point.
- A working two-to-four-car vehicle policy comparable to Watch Dogs 2.
- The exact writer or lifetime transition that corrupted the relocated queue
  tail in v0.7.9.
- A measured performance cost; the saved logs do not provide controlled FPS or
  frame-time comparisons.

## Evidence-backed next step

The latest tree failure occurred at no more than 18 of 24 faces, so adding more
physical maps is not the next fix. Raising A8 is also not a sharp-shadow fix:
A8 retains additional owners in the single coarse cached-refresh lane.

The next isolated test should retain all v0.8.0 repairs and change only the
normal adaptive B4 profile to 17 at constructor, lower and upper sites, leaving
logical A8=4 and physical owner reserve eight. Decompiled manager behavior maps
`B4-1` to the ordinary dynamic lane, making this a coherent 16-dynamic-position
test. It is a test hypothesis, not a proven result.

If that profile keeps the tree sharp without black-world states, tail corruption
or crashes, it validates dynamic-lane headroom for the tested scene. If the tree
still demotes while physical faces remain available, the patch requires explicit
protected world-light residency. A robust implementation must recover a stable
ownership/policy signal before world spotlights and vehicle headlights converge
as generic type-3 candidates.

## Primary evidence

- `Research\Logs\v0.8.0_five_minute_stable_tree_coarse_pid13908`
- `Research\Logs\v0.7.9_black_world_crash_pid27652`
- `Research\Logs\v0.7.7_owner_reserve_pass25_overrun`
- `Research\RuntimeCaptures\VehicleShadowLimiter_v1.2.16_tree_residency_trace.log`
- `docs\okf\findings-and-evidence.md`
- `docs\okf\engine-patch-validation-audit.md`
