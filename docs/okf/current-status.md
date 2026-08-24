---
type: Project Status
title: Shadow Engine Current Status
description: Accepted release behavior, refactor validation state, and next feature gate.
tags: [shadow-engine, status, runtime, refactor]
status: stable
generated:
  by: codex/gpt-5
  at: 2026-08-24T09:16:36+02:00
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
budget, but none of the five validated logs recorded a renderer face-budget
clamp. Earlier manager admission (`B4=16`) or cached-owner retention (`A8=4`) is
therefore the stronger current hypothesis.

The diagnostic capture confirmed simultaneous saturation: visible loss had 147
candidates, 17 dynamic-or-special admissions, four cached bindings, and 21/24
faces; recovery had 71 candidates, the same 17 dynamic-or-special admissions,
zero cached bindings, and 20/24 faces. No renderer clamp or lifecycle failure
occurred.

v1.2.2 attempted the resulting 30-map expansion but is rejected. Its first
runtime test loaded with every shadow missing and produced zero native or
external `SliceExecute` results. The queue itself contained valid records and
face pointers. Review against the historical OKF layout contract found that
the two mapping arrays had been advanced uniformly by `0x24C8` per map instead
of their distinct `0x24C0` and `0x24C4` strides.

v1.2.3 corrects those mapping offsets while retaining the intended capacity. It expands to 30
physical maps and 31 queue entries, registers maps 16-29 through passes 17-30,
routes external results 17-29, and sets `B4=21` for approximately 20 ordinary
dynamic positions while retaining `A8=4`. The v1.2.1 release remains the
accepted baseline until v1.2.3 passes runtime testing.

[^refactor-notes]: Behavior-neutral refactor notes
