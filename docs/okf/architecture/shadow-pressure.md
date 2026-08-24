---
type: Architecture Reference
title: Shadow Pressure Boundaries
description: Evidence separating manager admission, cached-owner retention, and renderer face capacity.
tags: [shadow-engine, shadows, admission, cache, capacity]
status: draft
generated:
  by: codex/gpt-5
  at: 2026-08-24T08:44:24+02:00
sources:
  - id: manager-source
    resource: ../../../src/modules/20_manager_owner_profile.inc
    title: Shadow manager and owner-profile module
  - id: renderer-source
    resource: ../../../src/modules/30_renderer_queue_diagnostics.inc
    title: Renderer queue diagnostics module
  - id: matrix
    resource: ../validation/regional-runtime-matrix.md
    title: Regional runtime matrix
---

# Boundaries

Manager admission and cached-owner reconciliation happen before renderer queue
submission. A light can lose a dynamic position or cached representation even
when the later queue remains below 24 physical faces.

The accepted policy uses `B4=16`, approximately 15 ordinary dynamic positions
when caching is enabled, and `A8=4`, four retained cached owners. Cached owners
do not add high-resolution maps; they share the engine's coarse refresh lane.

# Current evidence and next measurement

The validated five-profile logs contain no face-budget clamp. Near-full
occupancy is evidence of pressure, not proof that the renderer guard dropped a
record.[^matrix]

The diagnostic candidate records raw candidates, final manager admissions,
dynamic-or-special records, cached bindings, and live owner records in the
existing residency and F8/F9 output. Capture F8 during visible loss and F9
after recovery. Change `A8`, `B4`, or physical maps only after those paired
measurements identify the saturated boundary.[^manager-source][^renderer-source]

[^matrix]: Regional runtime matrix
[^manager-source]: Shadow manager and owner-profile module
[^renderer-source]: Renderer queue diagnostics module
