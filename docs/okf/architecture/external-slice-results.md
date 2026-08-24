---
type: Engine Lifecycle Contract
title: External Slice Results
description: Why indices above the native inline result array require generation-safe external storage, consumption, and release.
tags: [shadow-engine, sliceexecute, results, lifecycle, release]
status: stable
generated:
  by: codex/gpt-5
  at: 2026-08-24T10:30:00+02:00
sources:
  - id: result-module
    resource: ../../../src/modules/40_external_slice_results.inc
    title: External slice-result implementation
  - id: constants
    resource: ../../../src/modules/00_shared_config_state.inc
    title: Result-store sites and lifecycle state
  - id: current-status
    resource: ../current-status.md
    title: Accepted external-result behavior
  - id: historical-findings
    resource: https://github.com/temdah/Shadow-Engine/blob/2082a3bec1bfc4213ae4572b0e14b4f20c17906b/docs/okf/findings-and-evidence.md
    title: Historical public indexed-overwrite evidence
---

# Native overwrite boundary

`SliceExecute` writes results into an inline array beginning at render-record
offset `+0x70`. The native array has 17 entries, indices 0-16. Treating indices
17 and above as more inline entries overwrites independent render-record fields
at `+0xF8`, `+0x100`, `+0x108`, `+0x110`, and `+0x118`. Historical runtime work
linked this corruption to black-world/flicker failures.[^historical-findings]

Native indices remain untouched. The two `SliceExecute` store sites redirect
only indices `17..M-1` into patch-owned storage.[^constants]

# Generation-safe ownership

External results belong to a render-record lineage identified by record
pointer, constructor generation, and builder call. A store is accepted only
while the consumer is open and its lineage matches. Accepted indices set a
written mask and store the returned pointer atomically. Stale lineage, late
writes, duplicates, slot exhaustion, and failed releases are counted
separately.[^result-module]

The normal consumer runs after the unchanged stock post-call consumer, submits
all distinct non-null external results through the native resource wrapper,
clears the external slots, and closes the cycle. Render-record release performs
the same consumption as a fallback when results remain pending. The next
constructor or builder generation resets the cycle only after active writers
are accounted for.

# Interpretation of counters

Store failures are fail-closed rejected attempts, not automatically fatal.
`staleLineage` means work completed for an older record generation;
`lateWrites` means a writer arrived after consumption began. They become a
correctness problem if accepted results are missed, releases fail, pending
state survives reset, duplicates are consumed, or visual/runtime behavior
regresses. A small rejected stale/late count with balanced accepted consumption
and releases is evidence of asynchronous completion outside the usable window,
not evidence that accepted resources leaked.

The external written mask is 32-bit. Therefore external result count must not
exceed 32 without replacing the representation and auditing every atomic
reader/writer.

[^result-module]: External slice-result implementation
[^constants]: Result-store sites and lifecycle state
[^current-status]: Accepted external-result behavior
[^historical-findings]: Historical public indexed-overwrite evidence
