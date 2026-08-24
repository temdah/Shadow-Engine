---
type: Engine Lifecycle Contract
title: Shadow Resource and Pass Lifecycle
description: Safe construction, storage, registration, finalization, and routing rules for expanded maps and render passes.
tags: [shadow-engine, resources, passes, constructor, lifecycle]
status: stable
generated:
  by: codex/gpt-5
  at: 2026-08-24T10:30:00+02:00
sources:
  - id: expansion
    resource: ../../../src/modules/60_engine_expansion.inc
    title: Engine expansion implementation
  - id: bootstrap
    resource: ../../../src/modules/70_bootstrap_orchestration.inc
    title: Early and deferred orchestration
  - id: current-status
    resource: ../current-status.md
    title: Accepted resource and pass behavior
  - id: historical-bootstrap
    resource: https://github.com/temdah/Shadow-Engine/blob/2082a3bec1bfc4213ae4572b0e14b4f20c17906b/docs/EARLY_BOOTSTRAP_ARCHITECTURE.md
    title: Historical public early-bootstrap investigation
  - id: historical-regressions
    resource: https://github.com/temdah/Shadow-Engine/blob/2082a3bec1bfc4213ae4572b0e14b4f20c17906b/docs/okf/experiments-and-regressions.md
    title: Historical public regression ledger
---

# Construction window

Extra shadow maps must be created by extending the native resource-constructor
loop while its engine-owned dependencies are valid. Native indices 0-15 keep
their manager storage. Indices 16 and later are redirected to patch-owned
handle and aligned 16-byte companion arrays because continuing through the
native manager tail would overlap unrelated fields.[^expansion]

Late calls to the resource builder are unsafe. Historical staged tests crashed
on the first late `ShadowMap16` construction even when invoked from a renderer
thread.[^historical-bootstrap] The accepted implementation installs the constructor hooks during the
early NexusTools callback and performs downstream routing only after the early
resource and pass invariants are proven.[^current-status][^bootstrap]

# Pass registration and finalization

Pass registration is also an object factory: it constructs a pass and inserts
it into the renderer's two-level table. Added passes must be registered inside
the native registration phase, immediately after native pass 16 and before
table finalization. Registering after finalization can return without making
the objects visible to the renderer.[^expansion][^current-status]

For each added local map `m`, register pass `p = m + 1`:

* `Shadow` key = `(p << 9) | 0x0C`;
* `ShadowAlpha` key = `(p << 9) | 0x10C`.

Before registration, validate the physical table, stored maximum key, every
assigned name, and the entire required range. After registration, require every
added slot to be non-null and mutually distinct. Failure blocks downstream
installation; the patch must not partially expose new demand.

# Activation proof

Allocation is not proof of capacity. A candidate must separately prove:

1. all external handles are non-zero and distinct from native and one another;
2. all paired pass slots are non-zero and distinct;
3. downstream routing was installed only after those checks;
4. added map, face, pass, and result indices are actually consumed;
5. teardown/release counters remain coherent.

[^expansion]: Engine expansion implementation
[^bootstrap]: Early and deferred orchestration
[^current-status]: Accepted resource and pass behavior
[^historical-bootstrap]: Historical public early-bootstrap investigation
[^historical-regressions]: Historical public regression ledger
