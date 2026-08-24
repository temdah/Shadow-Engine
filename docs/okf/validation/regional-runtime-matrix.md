---
type: Validation Plan
title: Regional Runtime Matrix
description: One-candidate in-game validation across every supported runtime profile.
tags: [shadow-engine, validation, runtime, compatibility]
status: draft
generated:
  by: codex/gpt-5
  at: 2026-08-24T00:00:00+02:00
sources:
  - id: profiles
    resource: ../../../src/modules/05_runtime_profiles.inc
    title: Supported runtime profiles
---

# Required profiles

Run the exact same candidate on Global/04DF, Shev/A4EE, VMPless, Complete
Edition, and Asia/Miru.

# Required evidence

Each run must select its expected profile, pass complete preflight, reach patch
completion, retain valid routing and integrity counters, and show no user-
reported crash or visual regression.

Hashes identify the candidate and runtime DLL used for each run. Preserve the
complete produced log before changing either. Runtime automation may prepare
files and collect evidence, but the user controls the launcher, game process,
and visual judgment.

# Acceptance

The matrix passes only when all five profiles pass one frozen candidate. A
failure on one profile blocks the candidate without invalidating previously
accepted release behavior.
