---
type: Engineering Playbook
title: Knowledge Preflight
description: Mandatory lookup-first workflow before reverse engineering, changing engine policy, or deriving a capacity.
tags: [shadow-engine, okf, search, preflight, workflow]
status: stable
generated:
  by: codex/gpt-5
  at: 2026-08-24T10:30:00+02:00
sources:
  - id: okf-rules
    resource: okf-maintenance.md
    title: Shadow Engine OKF maintenance
  - id: code-rules
    resource: code-quality.md
    title: Shadow Engine code quality rules
---

# Required lookup

Before reverse engineering or changing capacity, admission, routing, resources,
passes, results, or lifecycle behavior:

1. Read `docs/okf/index.md`, `current-status.md`, and the relevant architecture,
   validation, and compatibility concepts.
2. Search the active public OKF, the private workspace OKF when available, source,
   and archived OKF for the affected symbol, value, RVA, failure marker, and
   neighboring subsystem. Archive content is evidence, not current authority.
3. Check each important claim against its linked source or current code.
4. Record a short knowledge preflight in the work notes: search terms, concepts
   read, invariants found, rejected assumptions, and unresolved questions.
5. If durable evidence exists only in an archive, log, handoff, or code comment,
   promote it into one focused active concept before or with implementation.

# Capacity checklist

A proposed capacity is incomplete until the preflight maps:

* allocation and construction timing;
* every writer and reader;
* index and offset formulas for each variable-length region;
* admission and scheduling policy;
* pass/resource lookup and finalization;
* result storage, consumption, release, and reset;
* rollback/fail-closed behavior;
* independent offline validation and runtime evidence.

The validator must derive expectations independently. A table cannot validate
itself by reusing the same candidate constants or generalized formula that
created it.

# Finish condition

Update active concepts, indexes, and the bundle log in the same work unit. Do
not leave the only explanation in chat, a handoff, an append-only status page,
or source comments. Run OKF validation for every affected bundle.
