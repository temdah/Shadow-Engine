---
type: Validation Reference
title: Known Failure Boundaries
description: Rejected assumptions and runtime signatures that constrain future Shadow Engine changes.
tags: [shadow-engine, failures, regressions, diagnostics, safety]
status: stable
generated:
  by: codex/gpt-5
  at: 2026-08-24T10:30:00+02:00
sources:
  - id: current-status
    resource: ../current-status.md
    title: Current project status
  - id: expansion
    resource: ../../../src/modules/60_engine_expansion.inc
    title: Current expansion safety checks
  - id: historical-regressions
    resource: https://github.com/temdah/Shadow-Engine/blob/2082a3bec1bfc4213ae4572b0e14b4f20c17906b/docs/okf/experiments-and-regressions.md
    title: Historical public regression ledger
  - id: historical-crashes
    resource: https://github.com/temdah/Shadow-Engine/blob/2082a3bec1bfc4213ae4572b0e14b4f20c17906b/docs/okf/crash-investigation.md
    title: Historical public crash investigation
  - id: pipeline
    resource: ../architecture/shadow-pipeline.md
    title: Shadow Engine pipeline
---

# Hard boundaries

* Renderer type 3 is shared by vehicle and world spotlights; class-wide filtering
  removes valid world shadows.
* `B4`, `A8`, candidate count, queue records, and cumulative faces are different
  quantities. None is a synonym for physical map capacity.
* Raising logical owner limits without pre-reserving the contiguous owner vector
  can move `0x700`-byte records after raw pointers were published.
* Pass 16 is reserved for `LongRangeShadow`; extra local maps skip it.
* Added pass objects must exist before table finalization. Missing key `0x220C`
  identified absent pass 17; later demand can similarly expose the next absent key.
* Late shadow-map construction is outside the proven safe lifecycle.
* Native inline slice results stop at index 16. Indices 17+ require external
  result storage and balanced consumption/release.
* All 46 queue relocations do not share one stride. The uniform-stride v1.2.2
  candidate populated queue data but dispatched zero shadow jobs.
* Unknown or ambiguous executable builds remain read-only and fail closed.

# Disproven shortcuts

Do not repeat these conclusions without new ownership evidence:

* type 3 equals vehicle headlight;
* a direction-matched lamp pair proves a common vehicle owner;
* more cached owners produce more high-resolution shadows;
* near-full face occupancy proves the face guard clamped work;
* a successful allocation or registration proves downstream consumption;
* absence of a shutdown marker proves a crash;
* a uniform delta across one old/new layout pair proves every embedded region.

# Diagnostic signatures

* All shadows absent, queue populated, zero native/external result stores:
  inspect map/pass lookup and queue mapping before changing capacity again.
* Coarse or low-cadence world shadows with cached owners saturated and no face
  clamp: inspect manager admission/cache competition.
* Missing pass key followed by reset fault: inspect construction timing, full
  pass range, and table finalization.
* Rising store failures composed only of stale/late rejects with balanced
  accepted consumption and release: asynchronous window pressure, nonfatal by
  itself.
* Owner base movement during admission: stop; published pointers may be stale.

Use the [pipeline](../architecture/shadow-pipeline.md) to find the first broken
stage and compare it with a frozen known-good log.[^pipeline]

[^current-status]: Current project status
[^expansion]: Current expansion safety checks
[^historical-regressions]: Historical public regression ledger
[^historical-crashes]: Historical public crash investigation
[^pipeline]: Shadow Engine pipeline
