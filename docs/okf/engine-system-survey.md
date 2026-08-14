---
type: Engine System Survey
title: WD1 Disrupt Engine Opportunity Map
description: Evidence-based survey of LOD, lighting, weather, physics, debris, UI, reflections, crowds, destruction, and E3-era effects.
tags: [watch-dogs, disrupt, engine, lod, weather, cloth, debris, ultrawide, optics, e3]
timestamp: 2026-08-13T00:00:00+02:00
status: deferred-until-vehicle-shadows-stable
confidence_model: [confirmed-runtime, confirmed-data-model, observed, historical, hypothesis]
---

# Purpose and evidence standard

This is the parallel engine-research lane. It does not change the validated
vehicle-shadow build or authorize a combined all-in-one patch.

**Project-order decision (2026-08-13): implementation is deferred until the
vehicle-shadow pipeline is stable.** Preserve this report for later research;
the active engineering lane has returned to shared spotlight capacity and
residency.

Evidence labels used here:

- **Runtime-confirmed**: the rebuilt WD1 runtime contains a live registration or
  a Ghidra cross-reference from executable code.
- **Data-model-confirmed**: an extracted WD1 binary-object schema or active mod
  resource exposes the field; the renderer consumer may still need tracing.
- **Observed**: reproduced in game or documented by the current project.
- **Historical**: supported by an original developer, vendor, or contemporary
  mod source.
- **Hypothesis**: plausible next target, not yet proven.

# Executive conclusion

The retail engine is not an empty shell stripped of every reveal-era feature.
It still contains substantial systems for rain/fog light interaction, light
beams and flares, wind-driven rigid debris, sliced cloth/civilian updates,
dual-paraboloid and screen-space reflections, environment lighting and fog,
traffic/crowd budgets and reactions, and road-destruction events.

That does **not** mean an untouched E3 preset can simply be enabled. Only four
schema/resource names explicitly contain `E3`, and none proves that the complete
E3 2012 content set survived. The defensible strategy is subsystem restoration:
prove each surviving path, compare it under a repeatable scene, then package it
as a separate optional module.

# Opportunity matrix

| System | Strongest local evidence | Present conclusion | Feasibility | First safe experiment |
|---|---|---|---|---|
| Distant objects / geometry LOD | Live `LodDithering`; active `LodScale`, `KillLodScale`, building/terrain/vegetation controls | Multiple independent gates, not one draw-distance value | High for tuning; medium for engine policy | Read-only LOD reason probe in fixed route |
| Distant lamp illumination | Live `fTurnOffDistance`, spotlight coverage and attenuation registrations | Source existence, illumination, and shadow have separate cull paths | High | Log source ID, distance, screen coverage, priority, light-active, shadow-active and residency |
| Weather / storms | Weather clips, wind, fog, clouds, environment lighting, lightning generator | Rich retail system survives; storm lightning can be restored separately from moon shadows | High for data; medium for corrected lightning origin | Finite-flash A/B preset with moon-shadow option disabled |
| Cloth FPS | Live `ClothSizeLimitOnScreen` and `ClothMaxModulo`; historical FPS-dependent physics report | Cadence slicing and timestep dependence are distinct possible faults | Medium | Record cloth update count and displacement at 30/60/120/240 FPS |
| Trash / leaves / wind | Live debris-manager budget and simulation-radius registration; positional-wind schema | Concrete spawn, fluid-sim, wind and radius controls survive | High | Count active/simulated debris while changing one budget or radius only |
| Ultrawide UI | Live `Force16_9`, `WidescreenFOV`, output scaling and safe-area registration; 13 modified FEUs in current mod | Engine projection is tunable, but ActionScript anchors still require FEU work | High | Aspect/safe-area snapshot plus audit of every unmodified FEU screen |
| Sun/camera corona | Sun sprite, sun lens-flare visibility, bloom artifact, exposure and lens texture | Screenshot is a compound optical stack, not one corona flag | High for sun; medium for headlights | Isolate bloom artifact, sun flare and exposure one at a time |
| Headlight rain/fog/flare | Light-effect rain/fog cone, rain-light and beam fields survive | E3-like interaction is represented in retail data; vehicle attachment and renderer execution remain unproven | Medium-high | Player-car-only light-effect prototype, no extra shadow demand |
| Water / wet reflections | Live SSR and water-quality registrations; active 1024 reflection config | Tuning path already works; improvement is quality/range/selection, not resurrection from zero | High | Fixed wet street/water route with SSR, paraboloid and water changes isolated |
| Crowds / traffic | Live density, budgets, wake-up counts and sliced controller/animation/panic work | Density and update cadence are tunable; exact E3 AI is not proven recoverable | Medium | Raise one budget while logging awake objects and per-frame update slices |
| Reactions / emergent AI | Large traffic reaction graph: threats, panic, flee, help, gather, rescue, collision | Many retail behaviors survive; content/routine differences may be authored rather than capped | Medium-low | Reaction telemetry before altering probabilities or radii |
| Destruction | Six runtime occurrences of `RoadDestructionHole`; live parameters for hole shape, particles, force and aftermath | A real scripted road-destruction system survives; generic micro-destruction is not proven | Medium for existing events; low for broad restoration | Trace event registration and spawn calls in one known road-hack scene |

# 1. Distance pop-in and distant lighting

## What is confirmed

WD1 has no runtime occurrence of WD2's `ExtendDrawDistance`. The successor's
render config exposes that switch, but a direct one-flag backport is not available.

WD1 instead exposes layered controls:

1. **Entity/streaming presence** - whether the world item is loaded.
2. **Geometry LOD and kill distance** - active render config contains geometry,
   building, terrain, vegetation and cluster LOD controls.
3. **Light contribution** - `Dynamiclightprefab` includes radius, attenuation,
   priority, `fTurnOffDistance` and `fTurnOffFallOff`.
4. **Shadow contribution** - the same prefab has `fCastShadowMaxDistance`,
   `fCastShadowFadeDistance`, resolution filtering and shadow groups.
5. **Renderer eligibility** - live `ShadowOmniSpotMinCoverage` and
   `LightOmniQuadraticCutoff` registrations can reject a small/faint light or
   shadow even while the pole mesh remains visible.
6. **Residency** - the current shadow project proved that an eligible world
   spotlight can still be demoted into the 512 cache lane.

This explains the user's lamp case: a visible lamp, its emissive bulb, its
illumination of the road, and its cast shadow are four different states.

## Required probe

Create a read-only `LightLOD` record keyed by stable light ID:

`frame, playerDistance, cameraDistance, prefab, priority, radius,
turnOffDistance, castShadowMaxDistance, screenCoverage, geometryActive,
lightActive, shadowCandidate, admittedLane, renderResolution, rejectionReason`.

The crucial addition is `rejectionReason`. Logging only active lights cannot tell
whether a distant lamp failed streaming, prefab distance, screen coverage,
occlusion/antiportal, admission, or shadow residency.

# 2. Weather, wind, fog, clouds and lightning

## Surviving system

The local schemas expose:

- weather clips with duration, wind maximum and fog enable;
- wind speed, wind noise delta, texture speed and channel;
- full distance/height fog and a next-generation top/bottom density model;
- sun/moon light, ambient/GI/probe/reflection, exposure and sun-shadow controls;
- cloud plane/ring layers, albedo/normal maps, speeds and time-of-day steps;
- lightning wetness threshold, calm/period timing, flash duration/intensity,
  light/sky HDR and thunder/strike audio.

TFoWC2's developer explained that its moon shadows use an unused
`lightningflashgenerator.lib` illuminate-world behavior. The unfinished source
resolves to the moon, so the effect is effectively never-ending lightning. A
proper storm restoration should therefore be a finite lightning module, not a
blind reuse of the moon-shadow preset.

## Recommended split

- `Weather Core`: timing, wetness, clouds, fog and wind.
- `Finite Lightning`: short flashes and thunder with explicit on/off state.
- `Moon Lighting`: optional, independent, and never allowed to own the storm
  flash indefinitely.

# 3. Cloth physics FPS fix

`CivilianOptimizationSettings` is live. Ghidra maps both
`ClothSizeLimitOnScreen` and `ClothMaxModulo` to `FUN_7ffc3a71ff70`. Embedded
registration documentation describes multi-frame civilian AI update slicing:
near agents update every frame, then every other frame, then every fourth frame.
The same object separately slices controller, navigation raycast, avoidance,
panic propagation, threat proximity, state, animation and fake crowd work.

This yields two distinct test targets:

- **Low-cadence cloth**: `ClothMaxModulo` may skip simulation or presentation
  frames. Reducing the modulo can improve smoothness at a CPU cost.
- **FPS-dependent speed**: TheWorse's author reported that above 30 FPS Aiden's
  coat and trees could move too quickly. That implies a timestep/scaling error,
  not merely skipped frames.

Do not ship a modulo tweak as an FPS fix. First capture simulation update count,
delta time and a repeatable coat/tree displacement at 30/60/120/240 FPS. If
speed scales with FPS, patch or normalize the consumer's delta. If speed is
constant but motion is stepped, tune the modulo/cadence.

# 4. Floating trash, leaves and wind

The debris manager is one of the clearest low-risk targets. Its live registration
includes total spawned entities, in-game spawned entities, normal/heavy-load
fluid-simulation ceilings, spawn/unspawn radii, spawn ratio, and start/stop
simulation radii. `PhysRigidPositionalWindParams` exposes linear/angular
acceleration, lift, gravity reduction and speed conversion. Grass wind factor
and motion frequency are also live renderer settings.

This supports an optional `Living Debris` mod with quality tiers. It should not
start by raising every number. Record total debris, simulated debris, despawns,
physics time and frame time; then change one dimension at a time:

1. simulation radius;
2. total/spawn budget;
3. wind force/lift;
4. heavy-load fallback.

# 5. Ultrawide UI

The runtime registers `Force16_9`, `ForceWidescreen`, `WidescreenFOV`, output
scaling and safe-area border values in `FUN_7ffc39e18bd0`. The installed 85%
ultrawide mod also edits 13 FEU packages. Its own description records why:
ActionScript anchor layout is retargeted from a 1280-wide stage to 1720; the
ctOS wanted element ignores its placement matrix; per-element scaling is needed
because the container transform runs after anchor layout and cancels it.

Conclusion: an engine patch can fix projection, FOV, output scaling and common
safe area. It cannot automatically repair every authored ActionScript anchor.
The practical solution is hybrid:

- runtime aspect/safe-area correction;
- FEU fixes for screens that ignore or override the matrix;
- a screenshot-driven audit at 16:9, 21:9 and 32:9.

# 6. Corona, glare and headlight interaction with rain/fog

The two supplied images show different parts of the optical stack.

- The bright sun image likely combines sun sprite intensity, environment
  `fSunLensFlareVisibility`, `sun_lensflare_d.xbt`, auto exposure, bloom center
  boost/threshold, chromatic aberration and bloom artifact intensity.
- The rainy police image combines physical/deferred light, wet reflection,
  rain/fog illumination, light beam volume and possibly a screen-facing flare.

`Lighteffectprefab` is unusually promising. It supports flare slices, camera
offset/scale, fade distance, beam volume, beam shadow projection, ray-march
occlusion, rain size/intensity multipliers, `bRainLightEnabled`, `bRainFogCone`,
quadratic rain attenuation, rain falloff/intensity, spotlight fog intensity and
beam highlight separation.

The field names are data-model-confirmed; Ghidra did not create executable xrefs
for `RainLightEnabled` or `RainFogCone`, so the actual retail consumer and vehicle
binding remain unresolved. The safe prototype is player-car-only and should add
no new shadow caster:

1. identify Aiden's headlight dynamic-light prefab;
2. find or attach its co-located light-effect component;
3. enable rain/fog beam interaction only while headlights and precipitation are on;
4. add a restrained camera-facing flare with occlusion and angle fade;
5. profile particle-light count and overdraw before enabling traffic vehicles.

# 7. Water and reflections

WD1's runtime actively registers `WaterNextGenQuality` and
`DisableScreenSpaceReflection`. The current graphic-tweaks config enables SSR on
high/ultra, uses 1024x1024 reflection textures, water refraction and raised
next-generation water quality. The data model further exposes IOR, specular,
reflection intensity, transparency depth, wave/storm intensity, foam and debris.

This is a good quality project, but compatibility matters: the user's active
graphics and TFoWC2 mods already replace overlapping render/environment files.
Prototype each change as a mergeable library/config delta, never a wholesale
archive overwrite.

# 8. Crowds, traffic and emergent reactions

Traffic density selectors, vehicle/civilian/activity resource and variety
budgets, spawn radii, preloading and budget scaling survive. City-life settings
control slicing, awake-list refresh and close/far/vigilante update counts.
Civilian settings separately slice controller, path/raycast, avoidance, panic,
threat, state and animation work.

The traffic-reaction graph also contains gunshot and collision threats, panic,
ducking, fleeing, driving away, gathering, helping, rescue, barks and dead-body
responses. Ubisoft's GDC description confirms a shared systemic character
architecture, but neither source proves that reveal-specific routines or content
are still present.

Therefore we can credibly improve density, wake-up rate, reaction range and
update cadence. We cannot yet promise “restore E3 AI.” The first probe must count
active/fake crowd objects and per-subsystem update slices before any density raise,
otherwise higher counts may only create more low-frequency/fake agents.

# 9. Destruction

`RoadDestructionHole` appears six times in the runtime and has executable xrefs.
Its schema includes hole and warning archetypes, steam/explosion particles and
sounds, push forces, camera shake, and delayed roadworks/reset aftermath. This is
a real surviving destruction system.

It is not evidence that general E3 micro-destruction assets survive. First trace
the known road-hack event from registration to spawn and inventory all referenced
archetypes. Only after that should we search for unused variants or broaden the
event to other surfaces.

# 10. E3 restoration assessment

## High-confidence restoration candidates

- vehicle-light interaction with rain/fog and restrained beam volume;
- stronger but controlled wind/debris/leaf activity;
- optical sun glare and camera artifacts;
- reflection quality/range and wet-scene response;
- more dynamic light shadows, using the validated shadow-engine work;
- selected LOD and distant-light improvements.

## Possible, but requires careful runtime work

- cloth timestep/cadence correction;
- denser traffic/crowds with higher awake/update budgets;
- richer reactions by tuning existing threat and gather/help systems;
- finite storm lightning while preserving optional moon lighting;
- existing road-destruction event expansion.

## Not currently defensible promises

- the exact E3 crowd AI;
- all reveal-era micro-destruction;
- a complete hidden E3 renderer preset;
- one switch that removes every kind of pop-in.

TheWorse is useful historical evidence, but its documented 1.0 changes were
primarily rain speed/intensity, camera DOF, storm bloom, color grading and small
fixes. Earlier notes also mention exposure, fog, lighting, LOD and a test
helicopter spotlight volume. Treat it as a retail-preset precedent, not proof
that every E3 subsystem can be restored by copying its values.

# Probe architecture and build order

Do not create one hot-path logger. The shadow project already demonstrated that
synchronous broad logging can produce an 85 MB file and collapse performance.

Use a read-only `EngineSurveyProbe` with disabled-by-default modules, a ring
buffer, deduplication and a low-frequency flush thread:

1. `LightLOD` - one selected light plus reason-coded state transitions.
2. `Environment` - active weather clip, precipitation, wetness, fog, wind,
   cloud and lightning state sampled at 2 Hz.
3. `ClothTiming` - update count/delta only for Aiden and one nearby civilian.
4. `Debris` - aggregate counts and physics load at 2 Hz.
5. `CrowdBudget` - aggregate real/fake/awake counts and sliced updates.
6. `Display` - resolution, aspect, FOV, safe area and active FEU name on changes.

Every module needs an explicit start/stop and one-shot dump in NexusTools. The
first build should contain `LightLOD` and `Environment` only; cloth/debris/crowd
hooks should be added after their consumer functions are mapped.

## Recommended project order

1. **Distant light/LOD reason probe** - directly answers the lamp pop-in report
   and reuses known shadow/light IDs.
2. **Player-headlight rain/fog optical prototype** - visually valuable and can
   avoid increasing shadow demand.
3. **Cloth timing probe and FPS normalization** - potentially universal PC fix.
4. **Debris/wind quality tiers** - well-exposed data path, easy A/B testing.
5. **Finite storm lightning** - separate it from moon-shadow behavior.
6. **Ultrawide runtime + FEU audit** - consolidate the existing partial fix.
7. **Water/reflection merge** - after resolving active-mod ownership conflicts.
8. **Crowd/AI and destruction research** - highest CPU/content risk and easiest
   areas to overclaim as an E3 restoration.

# Packaging boundaries

Keep future releases separable:

- Disrupt Engine Patch (validated shared capacities and safe engine fixes);
- Dynamic Vehicle Shadows (depends on the engine patch);
- Environment Optics (rain/fog light interaction, glare, corona);
- Weather and Wind (finite lightning, debris tiers, environment tuning);
- Cloth Timing Fix;
- Ultrawide UI Runtime + FEU Pack;
- optional E3-inspired preset that depends on the individual systems.

# Evidence locations

- Runtime string/xref exports:
  `Research/SystemSurvey/WD1_Subsystem_StringXrefs.txt`,
  `WD1_Subsystem_StringXrefs_2.txt`, and
  `WD1_Subsystem_StringXrefs_3.txt`.
- WD1 schemas:
  `Tools/Applications/GibbedDisruptTools/Gibbed Tools/projects/Watch Dogs/binary objects/classes/`.
- Active graphics-tweaks render config:
  `Research/SystemSurvey/ExtractedMods/graphics_tweaks_focus/engine/settings/defaultrenderconfig.xml`.
- TFoWC2 focused environment assets:
  `Research/SystemSurvey/ExtractedMods/tfowc2_focus/`.
- Ultrawide FEUs:
  `Research/SystemSurvey/ExtractedMods/wd_uw_85/`.
- WD2 comparison config:
  `Research/WD2/Watch_Dogs2_Shadow_Analysis/patch2/engine/settings/defaultrenderconfig.xml`.

Core evidence SHA-256 values:

- `WD1_Subsystem_StringXrefs.txt`:
  `0AF942C57DC1C821C2140CB89457DE7122085C34C64BCE57ADE6D0F0DA165C33`
- `WD1_Subsystem_StringXrefs_2.txt`:
  `3A8FDC2A1FBDE1D9F3EFBA91E8564A921D5F18F3B90A4F3DA66631F97C6B02F8`
- `WD1_Subsystem_StringXrefs_3.txt`:
  `722E48B7BFC67DDC78D95406403038A97DF8A37C35F38C30A6C4C4EF697A7F39`
- active graphics-tweaks render config:
  `CDD7B2DADCFBB2EBCE1B616BF8D03DD090BA24CA3C194A1E4A82EA1423EA3C03`
- WD2 comparison render config:
  `0E6D38EC9ED8C8D5A1F59B3F96ACB72CBD84AEEAEC7B0CC974AD41AE1F94D522`

# External references

- NVIDIA Watch Dogs graphics/setting analysis:
  https://www.nvidia.com/en-us/geforce/news/watch-dogs-graphics-performance-and-tweaking-guide/
- Ubisoft GDC character architecture description:
  https://www.gdcvault.com/play/1022074/In-Your-Hands-The-Character
- TheWorse 1.0 archived release/changelog:
  https://www.moddb.com/games/watch-dogs/downloads/theworse-mod-1-0
- TheWorse author's historical blog:
  https://theworsemod.blogspot.com/
- E3 2012 gameplay reference:
  https://www.youtube.com/watch?v=xU7WGAJPRRw
