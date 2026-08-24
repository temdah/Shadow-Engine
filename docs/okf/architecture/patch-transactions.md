---
type: Architecture Reference
title: Patch Transaction Model
description: Immutable preflight plans, journaled executable writes, and bounded rollback guarantees.
tags: [shadow-engine, patching, transaction, rollback]
status: draft
generated:
  by: codex/gpt-5
  at: 2026-08-24T00:00:00+02:00
sources:
  - id: transaction
    resource: ../../../src/modules/15_patch_transaction.inc
    title: Patch transaction implementation
  - id: orchestration
    resource: ../../../src/modules/70_bootstrap_orchestration.inc
    title: Bootstrap orchestration
---

# Prepare, then commit

Runtime preflight resolves one complete profile, recovers helpers, and validates
all mutation sites without changing engine memory. Orchestration commits only a
prepared immutable plan.

# Rollback

Executable writes are journaled before mutation. A failed phase restores bytes
in reverse order, clears any published trampoline pointers, and releases
transaction-owned allocations.[^transaction]

# Phase boundary

Early construction and deferred downstream installation are separate
transactions because the game creates required engine objects between them.
Each transaction is atomic within its phase. The model does not claim that
engine-owned objects created between phases can be rolled back.

[^transaction]: Patch transaction implementation
