---
type: Engineering Playbook
title: Shadow Engine Continuation
description: Required sequence from the behavior-neutral refactor to measured shadow-capacity feature work.
tags: [shadow-engine, continuation, testing, capacity]
status: draft
generated:
  by: codex/gpt-5
  at: 2026-08-24T00:00:00+02:00
sources:
  - id: refactor-notes
    resource: ../../../REFACTOR_NOTES.md
    title: Refactor notes
---

# Runtime-equivalence gate

1. Freeze one candidate commit and ASI hash.
2. Pass all [offline gates](../validation/offline-gates.md).
3. Run that candidate through the complete [regional matrix](../validation/regional-runtime-matrix.md).
4. Preserve and review every log before declaring equivalence.

# Feature investigation

Only after equivalence, instrument the path affected by emergency-light shadow
pressure: admission, owner reservation, live resources, queue occupancy,
residency, result routing, and release lifetime.

Change one bounded, proven bottleneck at a time. Keep regional differences in
`RuntimeProfile`; do not add profile-specific engine policy. Repeat offline
checks and test the final feature candidate on all supported profiles.
