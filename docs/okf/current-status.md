---
type: Project Status
title: Shadow Engine Current Status
description: Accepted release behavior, refactor validation state, and next feature gate.
tags: [shadow-engine, status, runtime, refactor]
status: stable
generated:
  by: codex/gpt-5
  at: 2026-08-24T08:03:00+02:00
sources:
  - id: patch-source
    resource: ../../src/shadow_engine_patch.c
    title: Shadow Engine patch source
  - id: refactor-notes
    resource: ../../REFACTOR_NOTES.md
    title: Behavior-neutral refactor notes
---

# Accepted behavior

Shadow Engine v1.2.1 is the accepted release baseline. One policy supports
Global/04DF, Shev/A4EE, VMPless, Complete Edition, and Asia/Miru while unknown
or ambiguous builds fail closed.

The patch preserves full native light passthrough, expands the established
shadow-manager path, and routes generation-safe external `SliceExecute` results
for indices 17-23.

# Refactor gate

The current branch reorganizes runtime profiles, subsystem state, bootstrap
responsibilities, and patch rollback without changing capacity, residency,
scheduling, routing, or light admission.[^refactor-notes]

The unchanged candidate at commit `ac74c2a` passed offline equivalence,
fault-injection checks, and the complete five-profile runtime matrix. v1.2.1
promotes that refactor with only its embedded version identity changed; engine
capacity and runtime policy remain identical. It is the validated release
source and feature-development baseline.

# Next investigation

Measure emergency-light and dense-traffic shadow pressure. Stress testing
reproduced temporary world-shadow loss near the current 24-face physical
budget, making that budget the leading bottleneck. Do not raise it in isolation:
prove every coupled allocation, writer, reader, index, and lifetime first.

[^refactor-notes]: Behavior-neutral refactor notes
