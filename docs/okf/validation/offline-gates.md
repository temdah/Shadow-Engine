---
type: Validation Reference
title: Offline Refactor Gates
description: Source-equivalence, mapping, transaction, and reproducibility checks before runtime testing.
tags: [shadow-engine, validation, reproducibility, rollback]
status: draft
generated:
  by: codex/gpt-5
  at: 2026-08-24T00:00:00+02:00
sources:
  - id: equivalence-test
    resource: ../../../tests/validate_refactor.py
    title: Refactor equivalence validator
  - id: transaction-test
    resource: ../../../tests/test_patch_transaction.ps1
    title: Transaction fault-injection test
---

# Gate set

* Preserve the release policy constants and all active signatures.
* Preserve 46 tail relocations and the complete Asia explicit map.
* Preserve external results for indices 17-23 and their release fallback.
* Preserve full native light passthrough and unknown-build fail-closed behavior.
* Prove transaction restoration and allocation cleanup under injected faults.
* Produce byte-identical binaries from two clean builds.

These checks detect behavioral drift visible in source and mutation mechanics.
They cannot establish that injected execution or rendered output is equivalent;
that requires the [regional runtime matrix](regional-runtime-matrix.md).
