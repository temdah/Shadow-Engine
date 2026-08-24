---
type: Architecture Reference
title: Shadow Engine Pipeline
description: End-to-end ownership map from light candidates through manager residency, renderer dispatch, result consumption, and release.
tags: [shadow-engine, pipeline, admission, rendering, lifecycle]
status: stable
generated:
  by: codex/gpt-5
  at: 2026-08-24T10:30:00+02:00
sources:
  - id: manager-module
    resource: ../../../src/modules/20_manager_owner_profile.inc
    title: Manager admission and owner-vector module
  - id: renderer-module
    resource: ../../../src/modules/30_renderer_queue_diagnostics.inc
    title: Renderer queue and face-admission module
  - id: expansion-module
    resource: ../../../src/modules/60_engine_expansion.inc
    title: Map, pass, queue, and scheduler expansion module
  - id: result-module
    resource: ../../../src/modules/40_external_slice_results.inc
    title: External slice-result lifecycle module
---

# Processing stages

```text
world and vehicle lights
  -> candidate construction and classification
  -> manager admission and priority
  -> fully dynamic or cached-owner residency
  -> render-queue records and cumulative face allocation
  -> local-map handle and Shadow/ShadowAlpha pass lookup
  -> SliceExecute dispatch and result store
  -> stock post-call consumer
  -> external-result consumer and native-wrapper release
```

Each arrow crosses a distinct capacity or lifetime boundary. Increasing one
number cannot prove that later allocations, writers, readers, indexing, and
release paths can consume the added work.[^manager-module][^expansion-module]

# Policy boundaries

`B4` controls the manager's fully dynamic lane; with caching enabled, ordinary
dynamic positions are approximately `B4 - 1`. `A8` bounds cached owners. Those
owners rotate through a shared low-resolution refresh lane and are not extra
high-resolution maps. The renderer then counts cumulative faces, so one queue
record may consume more than one local map.[^manager-module][^renderer-module]

Vehicle headlights and world spotlights converge before the generic renderer
path. Renderer type 3 is therefore not a safe vehicle identity. Full native
candidate passthrough is the accepted policy until a stable earlier ownership
signal is recovered.

# Failure localization

When rendering regresses, compare the first stage that differs from a known
good run:

1. profile selection and complete preflight;
2. native and external resource handles;
3. pass-table population;
4. manager candidates, admissions, `B4`, `A8`, and cached bindings;
5. queue records and cumulative faces;
6. native and external `SliceExecute` stores;
7. external consumption, release, stale-lineage, and late-write counters.

A populated queue with zero result stores localizes failure before or at job
dispatch. Balanced accepted stores and releases with visual loss points back
toward admission/residency or sampling rather than result lifetime.[^result-module]

[^manager-module]: Manager admission and owner-vector module
[^renderer-module]: Renderer queue and face-admission module
[^expansion-module]: Map, pass, queue, and scheduler expansion module
[^result-module]: External slice-result lifecycle module
