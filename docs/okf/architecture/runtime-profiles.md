---
type: Compatibility Contract
title: Runtime Profile Architecture
description: Immutable regional executable layouts selected before common patch policy is applied.
tags: [shadow-engine, runtime-profile, regional, compatibility]
status: draft
generated:
  by: codex/gpt-5
  at: 2026-08-24T00:00:00+02:00
sources:
  - id: profiles
    resource: ../../../src/modules/05_runtime_profiles.inc
    title: Runtime profile table
  - id: preflight
    resource: ../../../src/modules/65_runtime_preflight.inc
    title: Runtime preflight
---

# Model

The five supported executable layouts are immutable `RuntimeProfile` data.
Each profile describes its PE identity, clustered or explicit RVA strategy,
helper addresses, validation signatures, and detour prologue.[^profiles]

# Invariant

Core modules do not branch on profile number or store label. Capacity,
residency, admission, result routing, and cleanup are shared policy. A new build
is supported by adding a complete, validated profile rather than forking the
engine implementation.

# Failure behavior

Profile selection must be unique and coherent. Every required mutation site is
validated during read-only preflight. Unknown, ambiguous, or incomplete layouts
may produce diagnostics but receive no hooks or writes.

[^profiles]: Runtime profile table
