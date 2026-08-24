---
type: Validation Reference
title: Offline Refactor Gates
description: Source-equivalence, mapping, transaction, and reproducibility checks before runtime testing.
tags: [shadow-engine, validation, reproducibility, rollback]
status: draft
generated:
  by: codex/gpt-5
  at: 2026-08-24T09:16:36+02:00
sources:
  - id: equivalence-test
    resource: ../../../tests/validate_refactor.py
    title: Refactor equivalence validator
  - id: transaction-test
    resource: ../../../tests/test_patch_transaction.ps1
    title: Transaction fault-injection test
  - id: main-artifact-workflow
    resource: ../../../.github/workflows/build-mod.yml
    title: Main-branch installable artifact workflow
---

# Gate set

* In equivalence mode, preserve the release policy constants.
* In `--capacity-target-v122` mode, require exactly 30 maps, 31 queue entries,
  `B4=21`, `A8=4`, 28 external pass slots, and 13 external results.
* Preserve all active signatures and the complete Asia explicit map.
* Preserve all 46 tail source references; capacity mode requires each target to
  advance by six complete `0x24C8` layout strides.
* Preserve external-result release fallback and enforce the 32-bit written-mask
  bound.
* Preserve full native light passthrough and unknown-build fail-closed behavior.
* Prove transaction restoration and allocation cleanup under injected faults.
* Produce byte-identical binaries from two clean builds.
* On every push to `main`, build and upload the actual installable mod ZIP and
  its checksum without creating a tag or GitHub Release.

These checks detect behavioral drift visible in source and mutation mechanics.
They cannot establish that injected execution or rendered output is equivalent;
that requires the [regional runtime matrix](regional-runtime-matrix.md).
