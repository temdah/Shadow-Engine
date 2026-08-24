---
okf_version: "0.2"
---

# Shadow Engine knowledge bundle

Public technical knowledge for Shadow Engine. The bundle separates current
architecture and validation contracts from historical experiments.

# Start here

* [Current status](current-status.md) - Release baseline, refactor state, and next gate.
* [Runtime architecture](architecture/runtime-architecture.md) - Module boundaries and subsystem ownership.
* [Runtime profiles](architecture/runtime-profiles.md) - Regional compatibility without profile-specific policy.
* [Patch transactions](architecture/patch-transactions.md) - Preflight, commit, and rollback guarantees.

# Validation and operations

* [Offline gates](validation/offline-gates.md) - Mechanical checks before runtime testing.
* [Regional runtime matrix](validation/regional-runtime-matrix.md) - Five-profile in-game validation contract.
* [Continuation](operations/continuation.md) - Ordered work before capacity feature changes.
* [Evidence policy](operations/evidence-policy.md) - Public evidence and claim boundaries.
* [Code quality](operations/code-quality.md) - Mandatory architecture and implementation rules.
* [OKF maintenance](operations/okf-maintenance.md) - Mandatory public-bundle authoring rules.

# Compatibility

* [NexusTools early initialization](compatibility/nexustools-init.md) - Required loader callback contract.
* [Watch Dogs 2 reference](compatibility/wd2-reference.md) - Architectural evidence that must not be treated as portable offsets.

# History

* [Bundle update log](log.md) - Public knowledge changes, newest first.
