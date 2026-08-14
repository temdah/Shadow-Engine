---
okf_version: "0.1"
title: Watch Dogs Dynamic Vehicle Shadows Knowledge Bundle
version: "1.0.0"
---

# Watch Dogs Dynamic Vehicle Shadows

Canonical technical OKF for the Watch Dogs 1 Disrupt-engine shadow project.
Start with the architecture and findings documents. Referenced raw runtime
artifacts are private provenance and are governed by the
[evidence policy](../EVIDENCE_POLICY.md).

All version labels, artifact names and hashes below identify private experiments
or preserved evidence. No compiled mod build or installable release is published
in this repository.

Latest tested engine build is v0.8.8, rejected as a complete fix. The user observed
rapid black/normal flicker. All 288 null repairs succeeded with zero rejection,
post-write failure, release null or downstream null, proving the repair reached
the full acquire/release path. However, five logged `+0xF8` values and six
`+0x100` values held the same rogue non-null pointer and were not repaired.
Private evidence ID:
`v0.8.8_black_world_flicker_nonnull_overwrite_pid33332/RESULT.md`.

Latest built and tested repair is v0.8.9:
`Installables/ShadowEnginePatch_NonNullSidecarRepair_v0.8.9.zip`. It repairs any
same-cycle mismatch, preserves distinct displaced non-null pointers through a
bounded native acquire/release sidecar, and rolls back on a failed guard. It
changes no capacity, scheduler or candidate policy. ASI SHA-256 is
`D622440495530E905C96B0B8D5CC6C0D9A4E7EDE3F0979BF75341F8A260415C0`; ZIP
SHA-256 is `7C9DADB0A72BB4CAE61A5A7BDE933D60843FBD6D02698B321D0F9D9530A2AD02`.

PID 45180 showed major black-world improvement, with one possible unconfirmed
instant flicker, but a major FPS regression. All 5,785 sidecar acquires had
matching successful releases and no repair failed. The 6.5 MB log emitted
14,084 repair/release lines and the sidecar issued 11,570 native wrapper calls,
so their individual FPS costs are not yet isolated. Private evidence ID:
`v0.8.9_major_improvement_fps_regression_pid45180/RESULT.md`.

Latest built artifact is the untested v0.9.0 modular refactor:
`Installables/ShadowEnginePatch_ModularRefactor_v0.9.0.zip`. It changes source
layout and version labeling only. A pre-label modular build was byte-identical
to tested v0.8.9; final binary differences are confined to two same-length
version labels and the PE checksum. The known v0.8.9 FPS regression remains.
ZIP SHA-256 is `87FA1D54DCC27947B9ACEFECBB980A52A48818E098A9779C21665F5E1C1A60D3`;
ASI SHA-256 is `23334CBE5B0D69619F23CFDFE610489EA553C94EB52D788BFDA777911E8F7BE2`.

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
