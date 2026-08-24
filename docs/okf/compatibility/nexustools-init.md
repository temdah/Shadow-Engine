---
type: Loader Compatibility Contract
title: NexusTools Early Initialization
description: Loader callback contract required to patch Shadow Engine before dependent initialization.
tags: [shadow-engine, nexustools, loader, bootstrap]
status: draft
generated:
  by: codex/gpt-5
  at: 2026-08-24T00:00:00+02:00
sources:
  - id: entry-module
    resource: ../../../src/modules/80_runtime_entry.inc
    title: Runtime entry and exported callbacks
---

# Contract

Shadow Engine exports the NexusTools early callbacks implemented by the runtime
entry module. The early callback must run before dependent engine construction;
post-engine fallback is diagnostic, not equivalent activation.[^entry-module]

# Failure behavior

Missing or late activation must not be reported as an engine crash without
separate evidence. The patch records its lifecycle markers and performs no
partial profile installation when preflight is incomplete.

[^entry-module]: Runtime entry and exported callbacks
