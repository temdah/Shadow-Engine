---
type: Engineering Playbook
title: Shadow Engine Continuation
description: Required sequence from the behavior-neutral refactor to measured shadow-capacity feature work.
tags: [shadow-engine, continuation, testing, capacity]
status: draft
generated:
  by: codex/gpt-5
  at: 2026-08-24T10:30:00+02:00
sources:
  - id: refactor-notes
    resource: ../../../REFACTOR_NOTES.md
    title: Refactor notes
---

# Runtime-equivalence gate

Completed for commit `ac74c2a`: one frozen ASI passed all
[offline gates](../validation/offline-gates.md) and the complete
[regional matrix](../validation/regional-runtime-matrix.md). Preserve this
commit as the feature baseline until the next candidate passes proportionate
offline and runtime validation.

# Feature investigation

Complete the [knowledge preflight](knowledge-preflight.md) before adding a probe
or deriving another capacity target.

Instrument the path affected by emergency-light and dense-traffic shadow
pressure: the 24-face physical budget, admission, owner reservation, live
resources, queue occupancy, residency, result routing, and release lifetime.

Change one bounded, proven bottleneck at a time. Keep regional differences in
`RuntimeProfile`; do not add profile-specific engine policy. Repeat offline
checks and test the final feature candidate on all supported profiles.

For the first pressure capture, press F8 while the loss is visible and F9 after
recovery. Compare manager candidates/admissions/cache ownership with queue
entries/faces. Do not treat near-full occupancy as proof of a renderer clamp.
