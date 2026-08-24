---
type: Project Status
title: Shadow Engine Current Status
description: Accepted release behavior, refactor validation state, and next feature gate.
tags: [shadow-engine, status, runtime, refactor]
status: draft
generated:
  by: codex/gpt-5
  at: 2026-08-24T00:00:00+02:00
sources:
  - id: patch-source
    resource: ../../src/shadow_engine_patch.c
    title: Shadow Engine patch source
  - id: refactor-notes
    resource: ../../REFACTOR_NOTES.md
    title: Behavior-neutral refactor notes
---

# Accepted behavior

Shadow Engine v1.2.0 is the accepted runtime baseline. One policy supports
Global/04DF, Shev/A4EE, VMPless, Complete Edition, and Asia/Miru while unknown
or ambiguous builds fail closed.

The patch preserves full native light passthrough, expands the established
shadow-manager path, and routes generation-safe external `SliceExecute` results
for indices 17-23.

# Refactor gate

The current branch reorganizes runtime profiles, subsystem state, bootstrap
responsibilities, and patch rollback without changing capacity, residency,
scheduling, routing, or light admission.[^refactor-notes]

Offline equivalence and fault-injection checks are necessary but not sufficient.
One unchanged candidate must pass the complete five-profile runtime matrix
before the refactor becomes a feature-development baseline.

# Next investigation

After that gate, measure emergency-light shadow pressure. Do not raise a value
until its allocation, writers, readers, indexing, and lifetime are proven.

[^refactor-notes]: Behavior-neutral refactor notes
