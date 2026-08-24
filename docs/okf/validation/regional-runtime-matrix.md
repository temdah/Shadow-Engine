---
type: Validation Plan
title: Regional Runtime Matrix
description: Completed baseline matrix and risk-based manual gates for later candidates.
tags: [shadow-engine, validation, runtime, compatibility]
status: stable
generated:
  by: codex/gpt-5
  at: 2026-08-24T08:03:00+02:00
sources:
  - id: profiles
    resource: ../../../src/modules/05_runtime_profiles.inc
    title: Supported runtime profiles
---

# Required profiles

Run the exact same candidate on Global/04DF, Shev/A4EE, VMPless, Complete
Edition, and Asia/Miru.

# Required evidence

Each run must select its expected profile, pass complete preflight, reach patch
completion, retain valid routing and integrity counters, and show no user-
reported crash or visual regression.

Hashes identify the candidate and runtime DLL used for each run. Preserve the
complete produced log before changing either. Runtime automation may prepare
files and collect evidence, but the user controls the launcher, game process,
and visual judgment.

# Risk-based gates after the accepted matrix

The completed five-profile v1.2.1 matrix is the regional runtime baseline.
Later candidates use the smallest manual gate justified by their changed
surface, after all relevant offline gates pass:

* Documentation, packaging, or diagnostics with no runtime-policy effect:
  offline checks only, unless the diagnostic itself needs runtime evidence.
* Shared capacity or scheduling policy with profiles, signatures, resolver,
  preflight, hook set, and transaction topology unchanged: pass the regional
  runtime corpus gate, then run one representative Global/04DF high-load visual
  and stability test.
* A change to an explicit regional mapping or profile-local address: test every
  affected profile after the corpus gate.
* A change to profile selection, resolver logic, signatures, preflight,
  bootstrap, detour topology, ABI, or mutation transaction: run the complete
  five-profile matrix on one frozen candidate.

Any failure expands the gate to the affected profiles and blocks the candidate.
Static checks never replace the representative in-game visual test for an
engine-policy change.

# Result

The unchanged `ac74c2a` candidate passed Global/04DF, Shev/A4EE, VMPless,
Complete Edition, and Asia/Miru on 2026-08-24. No profile crashed or showed a
baseline shadow regression. Global included approximately 20 minutes of stable
gameplay.

Extreme traffic on VMPless and Complete Edition exposed temporary world-shadow
loss near the 24-face physical budget. This is retained as the next capacity
investigation rather than a regional compatibility failure.

# v1.2.3 calibration result

The frozen v1.2.3 ASI passed a fresh manual matrix on Global/04DF, Shev/A4EE,
VMPless, Complete Edition, and Asia/Miru. All five exact executable layouts
matched the offline corpus prediction: expected profile selection, complete
preflight, operational patch activation, stable gameplay, and no baseline
regional shadow regression.

Complete Edition activated its manager state after the one-time completion
observer timed out. Later owner-reserve, `B4=21`, `A8=4`, extra-capacity,
renderer, and lifecycle markers proved successful activation. This identifies
a false negative in the legacy log classifier, not a regional failure.

The matrix calibrates the risk tiers above: future shared capacity/scheduling
changes with an unchanged compatibility surface may use the five-profile
offline corpus plus one representative high-load runtime test. A profile,
resolver, mapping, signature, preflight, bootstrap, hook, ABI, or transaction
change still expands manual testing to affected profiles or all five.

Under extreme Complete Edition load, rapid dynamic spotlight/moon-shadow
flicker remained at 26 manager admissions while only 26 of 30 physical faces
were occupied. This is a shared manager admission/residency feature limit, not
a regional compatibility failure.
