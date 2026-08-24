---
type: Architecture Reference
title: Shadow Engine Runtime Architecture
description: Current module cohesion, dependency direction, and process-lifetime state ownership.
tags: [shadow-engine, architecture, cohesion, ownership]
status: draft
generated:
  by: codex/gpt-5
  at: 2026-08-24T00:00:00+02:00
sources:
  - id: module-map
    resource: ../../../src/modules/README.md
    title: Unity-module architecture
  - id: root-context
    resource: ../../../src/modules/00_shared_config_state.inc
    title: Shared configuration and root context
---

# Translation unit

`src/shadow_engine_patch.c` is the only translation unit passed to TinyCC. The
ordered `.inc` modules retain the tested ABI, static linkage, initialization
order, and hook trampoline behavior while making ownership explicit.[^module-map]

# Dependency direction

The module order is a contract: later modules may use earlier helpers; earlier
modules may reach later hook implementations only through forward declarations
in module 00. Engine policy belongs in its owning module rather than bootstrap.

# State ownership

One process-lifetime `ShadowEngineContext` holds cohesive bootstrap,
hook-binding, manager/renderer, resource/pass, and external-result state. Plain-C
detour callbacks access these regions explicitly. The root remains static to
avoid construction and teardown hazards inside injected code.

# Deliberate constraint

The unity build remains until a runtime-tested interface boundary justifies
separate translation units. It is an explicit deployment constraint, not a
reason to blur subsystem responsibilities.

[^module-map]: Unity-module architecture
