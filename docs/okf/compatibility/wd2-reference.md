---
type: Reverse Engineering Reference
title: Watch Dogs 2 Shadow Architecture Reference
description: Successor-engine responsibilities that inform WD1 hypotheses without supplying portable offsets or layouts.
tags: [shadow-engine, wd2, reverse-engineering, backport]
status: draft
generated:
  by: codex/gpt-5
  at: 2026-08-24T00:00:00+02:00
sources:
  - id: wd1-source
    resource: ../../../src/modules/60_engine_expansion.inc
    title: WD1 engine expansion implementation
---

# Use

Watch Dogs 2 demonstrates that the evolved Disrupt shadow system has explicit
vehicle-aware admission and broader manager responsibilities. Those concepts
can guide what to measure in Watch Dogs 1.

# Boundary

Addresses, object layouts, constants, and call contracts are not portable
between the games. A WD2 observation is architectural evidence only until a
matching WD1 allocation, writer, reader, index, and lifetime path is recovered
independently.
