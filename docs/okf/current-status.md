---
type: Project Status
title: Shadow Engine Current Status
description: Accepted release behavior, refactor validation state, and next feature gate.
tags: [shadow-engine, status, runtime, refactor]
status: stable
generated:
  by: codex/gpt-5
  at: 2026-08-24T18:00:00+02:00
sources:
  - id: patch-source
    resource: ../../src/shadow_engine_patch.c
    title: Shadow Engine patch source
  - id: refactor-notes
    resource: ../../REFACTOR_NOTES.md
    title: Behavior-neutral refactor notes
---

# Accepted behavior

Shadow Engine v2.0.0 is the accepted release baseline. One policy supports
Global/04DF, Shev/A4EE, VMPless, Complete Edition, and Asia/Miru while unknown
or ambiguous builds fail closed.

The patch preserves full native light passthrough, expands the established
shadow-manager path, and routes generation-safe external `SliceExecute` results
for indices 17-29.

# Refactor gate

The current branch reorganizes runtime profiles, subsystem state, bootstrap
responsibilities, and patch rollback without changing capacity, residency,
scheduling, routing, or light admission.[^refactor-notes]

The unchanged candidate at commit `ac74c2a` passed offline equivalence,
fault-injection checks, and the complete five-profile runtime matrix. v1.2.1
promotes that refactor with only its embedded version identity changed; engine
capacity and runtime policy remain identical. It is the validated release
source and feature-development baseline.

# Next investigation

Measure emergency-light and dense-traffic shadow pressure. Stress testing
reproduced temporary world-shadow loss near the current 24-face physical
budget, but none of the five validated logs recorded a renderer face-budget
clamp. Earlier manager admission (`B4=16`) or cached-owner retention (`A8=4`) is
therefore the stronger current hypothesis.

The diagnostic capture confirmed simultaneous saturation: visible loss had 147
candidates, 17 dynamic-or-special admissions, four cached bindings, and 21/24
faces; recovery had 71 candidates, the same 17 dynamic-or-special admissions,
zero cached bindings, and 20/24 faces. No renderer clamp or lifecycle failure
occurred.

v1.2.2 attempted the resulting 30-map expansion but is rejected. Its first
runtime test loaded with every shadow missing and produced zero native or
external `SliceExecute` results. The queue itself contained valid records and
face pointers. Review against historical patch tables and current source found
that the two mapping arrays had been advanced uniformly by `0x24C8` per map instead
of their distinct `0x24C0` and `0x24C4` strides.

The exact formulas and coupled capacity contract are maintained in
[Shadow Capacity Layout](architecture/engine-capacity-layout.md).

v1.2.3 corrects those mapping offsets while retaining the intended capacity. It expands to 30
physical maps and 31 queue entries, registers maps 16-29 through passes 17-30,
routes external results 17-29, and sets `B4=21` for approximately 20 ordinary
dynamic positions while retaining `A8=4`.

Tim's Global/04DF extreme-load run is a strong pass. A scene exceeding the old
failure reproducer showed no shadow-quality reduction or disappearance;
sampled use reached 23 queue entries, 26 faces, 10 extra maps, 180 candidates,
and 26 admissions. One distant flicker remains unclassified and may be ordinary
LOD behavior. The regional runtime corpus gate passes four profiles with 62
byte-exact checks each and couples packed A4EE to its frozen successful runtime
attestation. Under the risk-based policy, this satisfies the shared-capacity
candidate gate because profiles, signatures, resolver, preflight, hook set, and
transaction topology are unchanged.

A subsequent frozen-candidate calibration matrix passed all five supported
profiles and matched the offline compatibility prediction. This empirically
supports the reduced future retest tier for shared policy changes. v1.2.3 was
published to `main` at validated release commit `33490f1`.

v1.2.3 greatly reduces the original prolonged extreme-load shadow loss but does
not eliminate all pressure artifacts. Complete Edition reproduced rapid
spotlight/moon-shadow eviction and reacquisition at 26 manager admissions while
only 26/30 faces were occupied. The next investigation therefore owns manager
admission/residency stability rather than another blind physical-map increase.

The v1.2.4 manager-residency candidate changed coherent `B4` from 21 to 25
while retaining `A8=4`, all 30 maps, every layout relocation, profile, hook,
queue guard, and result-lifecycle path. Tim's Global/04DF extreme-load run
reported substantially improved stability and no observed flicker, crash, or
visual regression. Runtime pressure reached 154 candidates, 30 admissions and
30/30 faces. The priority guard safely bounded requests up to 33 faces by
dropping at most three trailing records; no overflow, corruption, owner
movement, late write, duplicate result, or release failure occurred.

v2.0.0 promotes that exact tested engine behavior. Byte comparison against the
frozen v1.2.4 ASI found only the two embedded version strings and PE checksum
changed. Release ASI SHA-256:
`3756387276CD81D185C0702B73D08B4B0CC1E6666A7D92A082ABA3FE648C166B`.
Nexus ZIP SHA-256:
`6EB57F073F81587603A7D884F74DB87078B9B2CDE5A2DB2A80241A06BE336581`.

Corrected candidate source commit: `f31769d`. Reproducible ASI SHA-256:
`5510E9F34F62DD61F6C49E4F946664A9241A743C9E989B98DABF335506ED4CC2`.
Nexus-layout ZIP: `Installables/ShadowEngine-v1.2.3.zip`, SHA-256
`675D166CFA14A04EA73AA3FF54BEEEE91D0E4849637A15A26866377ABE41026B`.

[^refactor-notes]: Behavior-neutral refactor notes
