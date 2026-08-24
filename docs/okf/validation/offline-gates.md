---
type: Validation Reference
title: Offline Refactor Gates
description: Source-equivalence, regional corpus, transaction, and reproducibility checks before runtime testing.
tags: [shadow-engine, validation, reproducibility, rollback]
status: draft
generated:
  by: codex/gpt-5
  at: 2026-08-24T18:35:00+02:00
sources:
  - id: equivalence-test
    resource: ../../../tests/validate_refactor.py
    title: Refactor equivalence validator
  - id: transaction-test
    resource: ../../../tests/test_patch_transaction.ps1
    title: Transaction fault-injection test
  - id: corpus-test
    resource: ../../../tests/validate_runtime_corpus.py
    title: Regional runtime corpus validator
  - id: main-release-workflow
    resource: ../../../.github/workflows/build-mod.yml
    title: Main-branch installable release workflow
---

# Gate set

* In equivalence mode, preserve the release policy constants.
* In `--policy-target-v200` mode, require exactly 30 maps, 31 queue entries,
  `B4=25`, `A8=4`, 28 external pass slots, and 13 external results.
* Preserve all active signatures and the complete Asia explicit map.
* Preserve all 46 relocation source references; policy-target mode requires each
  target to follow its owning region's `0x24C0`, `0x24C4`, or `0x24C8`
  per-map stride. Never apply one uniform stride across all three regions.
* Preserve external-result release fallback and enforce the 32-bit written-mask
  bound.
* Preserve full native light passthrough and unknown-build fail-closed behavior.
* Prove transaction restoration and allocation cleanup under injected faults.
* Produce byte-identical binaries from two clean builds.
* For engine-capacity or relocation changes, pass the [regional runtime corpus
  gate](runtime-corpus-gate.md) against all five preserved runtime identities.
* On every push to `main`, build the actual Nexus-ready mod ZIP and checksum,
  then create the matching version tag and GitHub Release with both files as
  permanent release assets. Refuse to overwrite an existing version so every
  new iteration requires a version bump.

These checks detect behavioral drift visible in source, executable layout, and
mutation mechanics. They cannot establish that injected execution or rendered
output is equivalent; apply the risk-based manual gate in the
[regional runtime matrix](regional-runtime-matrix.md).
