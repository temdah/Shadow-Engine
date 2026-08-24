---
type: Engineering Convention
title: Shadow Engine Code Quality Rules
description: Mandatory ownership, coupling, mutation-safety, and validation rules for Shadow Engine changes.
tags: [shadow-engine, code-quality, architecture, solid]
status: draft
generated:
  by: codex/gpt-5
  at: 2026-08-24T00:00:00+02:00
sources:
  - id: agent-rules
    resource: ../../../AGENTS.md
    title: Shadow Engine contributor rules
  - id: module-map
    resource: ../../../src/modules/README.md
    title: Unity-module architecture
---

# Ownership and cohesion

Each module has one primary responsibility and owns the mutable state for that
responsibility. Add new state to the owning `ShadowEngineContext` subsystem;
do not add free-floating mutable globals, compatibility aliases, or a second
owner.[^module-map]

Regional executable differences belong only in immutable `RuntimeProfile`
data. Core capacity, admission, routing, residency, and release policy must not
branch on profile number or storefront.

# Dependency and orchestration

Respect the documented unity-module order. Earlier modules do not reach later
implementations except through declared hook interfaces. Bootstrap coordinates
preflight and commit; it does not absorb profile recovery, subsystem logic,
diagnostics, or resource policy.

# Mutation and lifetime

Resolve and validate the complete patch plan before writing. Reversible writes
must use the transaction journal. Allocation, publication, consumption,
release, rollback, and fallback ownership must be explicit. Unknown or
ambiguous builds stay read-only and fail closed.

# Feature changes

Change one proven hypothesis at a time. A value is not a capacity merely
because increasing it changes output: map its allocation, writers, readers,
indexing, and lifetime first. Keep inactive probes and historical repair code
out of the production unity build.

# Completion gate

A change is incomplete until it is warning-clean, passes relevant offline and
fault-injection tests, reproduces byte-for-byte when required, updates affected
OKF concepts, and receives proportionate runtime validation. Final engine
policy candidates require the full supported-profile matrix.

[^module-map]: Unity-module architecture
