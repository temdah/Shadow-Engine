# After Vehicle Shadows: Disrupt Engine Roadmap

Status: **future lanes after the accepted v1.2.0 baseline and its refactor
regression gate**. Emergency-light shadow pressure is the first active engine
investigation; the remaining lanes stay deferred.

The detailed evidence report is maintained in
[`okf/engine-system-survey.md`](okf/engine-system-survey.md). This
document keeps the future work visible without mixing it into the active shadow
engineering effort.

## Planned research lanes

1. Distant object, geometry, light-contribution and shadow LOD.
2. Weather, fog, clouds, finite storm lightning and optional moon lighting.
3. Cloth timing at 30/60/120/240 FPS, separating skipped-frame cadence from an
   FPS-dependent physics timestep.
4. Wind-driven trash, leaves and debris budgets/simulation radii.
5. Ultrawide runtime aspect/safe-area correction plus remaining FEU layouts.
6. Sun/camera optical effects and vehicle-headlight interaction with rain/fog.
7. Water, wet-road and screen-space/paraboloid reflection improvements.
8. Traffic/crowd budgets, awake/update cadence and existing reaction systems.
9. Existing road-destruction events and an evidence-based E3-inspired preset.

## Most promising first projects after shadows

- A reason-coded distant-light/LOD probe.
- Aiden-only headlight rain/fog beam and restrained corona prototype.
- Cloth timestep/cadence probe and FPS fix.
- Scalable wind/debris quality tiers.
- Finite lightning separated from TFoWC2's moon-shadow implementation.

## Boundary

There is no evidence for one complete hidden E3 switch. Retail WD1 still has
many relevant subsystems, but restoration must be proven and packaged one system
at a time. No work in this roadmap should alter or delay the active shared
spotlight-shadow residency investigation.
