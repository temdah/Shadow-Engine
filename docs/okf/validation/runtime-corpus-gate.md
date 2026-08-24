---
type: Validation Reference
title: Regional Runtime Corpus Gate
description: Offline verification of capacity-sensitive patch sites across all supported executable layouts.
tags: [shadow-engine, validation, regional, capacity, automation]
status: stable
generated:
  by: codex/gpt-5
  at: 2026-08-24T10:27:50+02:00
sources:
  - id: validator
    resource: ../../../tests/validate_runtime_corpus.py
    title: Runtime corpus validator
  - id: profiles
    resource: ../../../src/modules/05_runtime_profiles.inc
    title: Runtime profiles and address resolution
---

# Purpose

Capacity changes must be checked against the executable layouts they will
mutate, not only against C source. The corpus validator verifies all five exact
PE identities and independently resolves the capacity-sensitive RVAs for each
profile.[^validator]

# Coverage

For Global/04DF, VMPless, Complete Edition, and Asia/Miru, the gate performs 62
byte-exact checks per profile: 15 inline signatures, 46 relocation sites, and
the map-construction loop. It verifies the old immediate at every relocation,
derives the new value from the owning `0x24C0`, `0x24C4`, or `0x24C8` region,
and simulates every write in a disposable memory copy.

The packed Shev/A4EE file is currently identity-checked and coupled to a frozen
successful runtime attestation containing profile selection, cluster deltas,
complete preflight, 46 tail relocations, and patch completion. Capture one
mapped A4EE image to replace that attestation with the same 62 byte-exact
checks used by the other profiles.

# Boundary

This gate catches wrong RVAs, wrong old bytes, incorrect regional mapping, and
incorrect capacity-layout formulas such as the rejected v1.2.2 uniform-stride
change. It does not execute the game, prove that a detour runs, or judge visual
output. Manual validation remains proportionate to the changed compatibility
surface.

[^validator]: Runtime corpus validator
