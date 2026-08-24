---
okf_version: "0.1"
title: Watch Dogs Dynamic Vehicle Shadows Knowledge Bundle
version: "1.2.0"
---

# Watch Dogs Dynamic Vehicle Shadows

Canonical technical OKF for the Watch Dogs 1 Disrupt-engine shadow project.
Start with the architecture and findings documents. Referenced raw runtime
artifacts are private provenance and are governed by the
[evidence policy](../EVIDENCE_POLICY.md).

## Current baseline — 2026-08-24

- Accepted public/runtime baseline: Shadow Engine v1.2.0, commit `02762e6`.
- Supported runtime profiles: Global/04DF, Shev/A4EE, VMPless, Complete
  Edition, and Asia/Miru.
- Current source candidate: behavior-neutral v1.2.0 refactor; offline gates
  pass, but the five-profile runtime regression matrix is still required.
- Next feature after that gate: diagnose emergency-light shadow pressure before
  changing any capacity, residency, admission, or scheduling value.
- Older headings containing “current” or “next” describe their dated
  experiment, not present authority. This section and the repository README own
  current public status.

All older version labels, artifact names and hashes below identify private experiments
or preserved evidence. No compiled mod build or installable release is published
in this repository.

Latest built artifact is the untested v0.9.3 indexed-writer narrowing diagnostic:
`Installables/ShadowEnginePatch_IndexedWriterNarrowing_v0.9.3.zip`. Latest
tested artifact is v0.9.2 PID 9552. v0.9.3 ASI SHA-256 is
`02F062C307E36DB95DE8081F62FB2402E75A7179F19D35FFE5F906B2A6E37D2F`;
ZIP SHA-256 is
`2F580AA78CAE486E57C3C436CD43119EF3878746E01AB77C3AA013DEE7519A49`.

PID 10316 loaded `0.9.0-stage-p-modular-unity-refactor`, reached startup
completion and ran for more than ten minutes without crashing according to the
user, materially exceeding the earlier 10–60 second crash window. The preserved
log spans approximately 592.903 seconds, reaches renderer call 81,850 and ends
in ordinary lifecycle tracing without a shutdown marker; the absent marker is
not crash evidence. Seven F8/F9 captures were all 18 entries/21 faces/maps 0–20.
Because the reported flickers were too rapid to classify the keypress frames,
all seven are unclassified snapshots and the F8/F9 labels are not visual proof.

The run recorded 29,933 repair attempts, 20,967 balanced sidecar acquire/release
pairs, 41,934 sidecar wrapper submissions, 13 fail-closed unverified repairs,
and zero observed post-write failure, rollback or sidecar release failure.
Repair plus release lines were 50,900 of 53,064 lines (95.93%) in a 22.9 MB log.
Private evidence ID:
`v0.9.0_modular_flicker_unclassified_captures_pid10316/RESULT.md`.

PID 49316 loaded `0.9.1-stage-q-sparse-success-logging`. Its 252.154-second
log contains 1,089 lines / 377,135 bytes, about 48.7x fewer lines and 60.8x
fewer bytes than v0.9.0. Sparse logging therefore removed the dominant output
volume. With no F8/F9 or shutdown summary, sampled counters bound repairs to
11,264-12,287 and balanced sidecars to 9,216-10,239. Sidecars consequently still
added 18,432-20,478 native wrapper submissions, about 73.1-81.2 per second;
their cost remains active, but the log alone cannot quantify FPS improvement.

The user reported coarse/disappearing shadows in a modded high-traffic scene.
The preserved screenshot proves roughly nine visible cars only; it does not show
the brief shadow failure. Residency reached 21 entries / 24 faces / 8 extra maps
without sampled overflow. Four producer-side null cycles at that full envelope
map exactly to render-record `+0x110`: acquire count six had null index 4 and
release count two had null index 0. The builder sources `+0x110` from lookup key
`0xCCB53E0B`. Private evidence ID:
`v0.9.1_sparse_logging_coarse_high_traffic_pid49316/RESULT.md`.

That diagnostic is now built and tested as v0.9.2:
`Installables/ShadowEnginePatch_ResourceProvenance_v0.9.2.zip`. It retains all
v0.9.1 behavior and observes `+0xF8/+0x100/+0x110` at lookup, builder exit,
renderer entry and release entry. `+0x110` lookup is identified by the statically
proven return RVA `+0x3DE059`. It emits exact 30-second totals and classifies the
first invalid interval without writing `+0x110`. No capacity, residency,
scheduler or filtering behavior changed. ASI SHA-256 is
`ABA642468C1BC2E20FC6804D69EC6859D734174C0A518E441F554925701CDEA0`;
ZIP SHA-256 is
`D3FC4C9B7F66F5C1A9E6A68A80AC2C64ADFB1D357884EFC5A4A77C03378EF806`.

PID 9552 proved the relevant lookups succeed before corruption. Through F9,
all 31,961 `+0x110` lookups were non-null. Eight correct builder-exit values
changed before renderer entry: three to null and five to the same displaced
pointer. At the bridge-adjacent F8 failure, `+0xF8` lookup was valid but the
builder exited with null, causing repair to fail closed because its saved value
was already corrupt. This localizes F8 to lookup-to-builder-exit and `+0x110` to
builder-exit-to-renderer. It is indexed overwrite evidence, not missing lookup
capacity. Private evidence ID:
`v0.9.2_bridge_black_flicker_pid9552/RESULT.md`.

The coarse/disappearing-shadow residency failure was not reproduced in this
run. Do not treat it as disproven. Next correct or further narrow the indexed
writer before changing residency policy, removing repair/sidecars or increasing
capacity.

v0.9.3 is the next narrowing build. It reuses the existing F110 lookup detour as
a mid-builder checkpoint: native F8 storage has completed, while native F110
storage has not. It fingerprints F8/100/108/110/118 there and at the existing
builder/renderer boundaries. This splits F8 corruption into
`f8_store_to_110_lookup_return` versus `110_lookup_return_to_builder_exit` and
adds adjacent-field evidence for post-builder F110 changes. It installs no new
engine hook, mutates none of these observed fields, and leaves repair, sidecars,
capacity, residency and full native passthrough unchanged.

PID 50976 tested v0.9.3. All 51 F8 divergences occurred after the F110 lookup
checkpoint and before builder exit: five became null and 46 became pointer
`0x454E04E0`. Zero occurred before the checkpoint. All 19 F110 divergences
remained post-builder: 13 null and six the same `0x454E04E0`. The common wrong
pointer strongly implicates a shared indexed clear/copy source. BLACK_F8 at the
bridge was only 4 entries / 7 faces and did not coincide closely with a traced
overwrite, so it supports streaming depletion rather than capacity pressure but
does not yet identify the black-world writer. Private evidence ID:
`v0.9.3_bridge_black_flicker_pid50976/RESULT.md`.

The untested v0.9.4 follow-up is
`Installables/ShadowEnginePatch_BuilderTailBracketing_v0.9.4.zip`. It brackets
every existing shared-texture lookup return from `+0x3DE059` through builder end
`+0x3DF833`, identifying the first lookup-to-lookup F8 change or proving the
change follows the final lookup. It adds no detour or mutation. ASI SHA-256 is
`180E01D8EAC0155C38BA664D9736EE6582D6DF2FD1FCE9B8DE32900ADF29ADB2`;
ZIP SHA-256 is
`CC180A79F2E90CCEDDAEA3A7E5DC1CBDD7C10D13EBA58BB67792B13BBDDEC519`.

PID 28488 tested v0.9.4 without manual snapshots. All 115 F8 failures were
correct at the final tail lookup return `+0x3DE0FE` and corrupt only by builder
exit; zero lookup-to-lookup divergence occurred. Four became null and 111 the
same displaced pointer `0x45861D10`. Texture lookup code is therefore cleared;
the writer is in the post-F118-return builder tail. Private evidence ID:
`v0.9.4_builder_tail_pid28488/RESULT.md`.

- [Architecture](architecture.md) - reconstructed WD1 pipeline and current patch layout.
- [Findings and evidence](findings-and-evidence.md) - conclusions supported by tests or disassembly.
- [Engine patch validation audit](engine-patch-validation-audit.md) - exact boundary
  between patched 24-entry layout, proven live resources, and the required
  B4=17/16-dynamic proof build.
- [Early bootstrap architecture](../EARLY_BOOTSTRAP_ARCHITECTURE.md) -
  reconstructed dinput8/Troplo/RunGame chain and the staged pre-RunGame resource
  initialization design required after the v1.2.26 late-construction crash.
- [NexusTools early-bootstrap request](../NEXUSTOOLS_EARLY_BOOTSTRAP_REQUEST.md) -
  proposed generic pre-RunGame extension point based on the validated loader
  ordering constraint; no reference DLL is distributed.
- [Crash investigation](crash-investigation.md) - v1.2.3-v1.2.6 crash families, fault RVAs, and current hypothesis.
- [Experiments and regressions](experiments-and-regressions.md) - rejected builds and why they failed.
- [WD2 backport reference](wd2-backport.md) - successor-engine implementation discovered so far.
- [WD2 shadow/lighting budget report](../WD2_SHADOW_LIGHTING_BUDGET_REPORT.md) -
  confirmed vehicle-level headlight policy, 8/10/12 dynamic budgets, separate
  four-map static cache, WD1 comparison, and revised two-track architecture.
- [Engine system survey](engine-system-survey.md) - evidence map and probe roadmap for LOD,
  weather, cloth, debris/wind, ultrawide, optics, reflections, crowds,
  destruction, and E3-inspired restoration; implementation is deferred until
  vehicle shadows are stable. See also the human-facing
  [after-shadows roadmap](../AFTER_SHADOWS_ENGINE_ROADMAP.md).
- [Public build instructions](../../BUILDING.md) - toolchain and reproducibility workflow.
- [Update log](log.md) - knowledge-bundle history.
- [Proven capability report](../SHADOW_ENGINE_PATCH_PROVEN_CAPABILITIES_2026-08-13.md) -
  hard evidence boundary through the v0.8.0 test.
