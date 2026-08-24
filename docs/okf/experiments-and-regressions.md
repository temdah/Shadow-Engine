---
type: Decision Record
title: Experiments and Regressions
description: Historical build outcomes that must not be repeated without new evidence.
tags: [regressions, decisions, testing]
timestamp: 2026-08-12T01:20:00+02:00
---

# v1.2.25 - temporal Aiden-centered four-node tracker (awaiting test)

> Historical experiment ledger. Version labels in this file belong to the
> pre-release research series and are not newer than public Shadow Engine
> v1.2.0. See `index.md` for current authority.

- Branches directly from v1.2.16; no v1.2.18+ classifier logic retained.
- Motion history distinguishes movable type-3 sources from static type-3 world
  lights without component-handle guesses.
- Co-moving lights within three metres form one vehicle-like node.
- Keeps four nearest traffic nodes with hysteresis, plus Aiden's current vehicle
  as a priority bypass.
- Unknown records fail open.
- Known risk: co-motion clustering is not yet a proven vehicle entity owner key.
- ZIP SHA-256:
  `F2922F4770633DE9026104A1E81125CE791B44F5861260D5841C28BDEEDAC6C5`.
- ASI SHA-256:
  `551FBA1901D1B15972AC0378BBAE8490F1E420D8C01D200CD0A3D3F3EF407E83`.

# v1.2.24 - exact v1.2.18 ASI binary control (rejected)

- Native ASI is byte-for-byte v1.2.18; no recompilation.
- Only mod version/description and Lua identification text changed.
- Result: no vehicle shadows, matching later failing builds.
- Conclusion: v1.2.18 was incorrectly treated as a working visual baseline.
  The last confirmed shadow-present builds are v1.2.16 and v1.2.17.
- The v1.2.18 handle classifier is the first active filtering boundary because
  v1.2.17 matched zero vehicle candidates and consequently passed all records.
- ZIP SHA-256:
  `723D011EA49DDD44F367844EBD519F63B5C51D4EE01B7E797F88689B79D457AC`.
- ASI SHA-256:
  `BE2AE52D9F536CDD78FA9DEBC2B7E793EAFC46CCDA73100AB1ABD2B4F3E7E522`.

# v1.2.23 - fail-open nearest-type3 classifier probe

- No candidate filtering, pairing, reordering, or truncation.
- Logs the closest eight raw type-3 records with ID, handle, position, distance.
- Purpose: restore observation and identify nearby car handles without guessing.
- Compiled and packaged successfully; game validation pending.
- ZIP SHA-256:
  `CFCC19CBCDA9AC3ECFDD2B2337D4723DE5E4FB915AE635BD53C6EADAFD71E1D3`.
- ASI SHA-256:
  `AE25E1AADA7FA9F75973003D46530A7A28D40A77A9666DEE5FEED1A52527F391`.

# v1.2.22 - nearest four vehicle sources (rejected)

- Selects the four closest positively classified vehicle sources directly.
- Uses renderer-confirmed world coordinates and Aiden's position.
- Does not alter illumination distance, shadow projection length, capacity,
  scheduling, or the validated pass repair.
- Result: rejected; selection math worked on the wrong classified light subset.
- ZIP SHA-256:
  `8ACDA6BD4674BF93E30DE414FEF6F23CC3F21FFC766B3C3EF6067341F02ACB6A`.
- ASI SHA-256:
  `40AE5506B6F2847BD93EA169A20469958AFF945103A0378FB90A4D617C360565`.

# v1.2.21 - spatial-pair nearest four (rejected)

- Interim pairing: nearest unpaired vehicle lamp within three metres whose
  spotlight direction dot product is at least 0.90.
- Ranking: world-position pair midpoint against Aiden; no casting-distance cap.
- Diagnostic: each `PAIR_SELECTION` entry includes lamp separation as `sep=`.
- Final target remains stable vehicle entity IDs mapped directly to lamp IDs.
- Result: rejected; it again formed zero pairs and removed all vehicle sources.
- ZIP SHA-256:
  `2E6AF504763D8BD70F7F7F8BACB0DD9DADFCA423B0E442F54FCE33AC49FD2EC4`.
- ASI SHA-256:
  `E3F2B04BEF0A8E251F5AA876C2317C3BF0E079318E05BC1440BEED8435480F8F`.

# v1.2.20 - corrected world-position nearest four (rejected)

- Change from v1.2.19: candidate position moves from holder `+0x14..+0x1C` to
  renderer-confirmed `+0x08..+0x10`.
- Everything else, including strict four-pair/eight-lamp admission and the
  validated v1.2.16 engine/pass foundation, remains unchanged.
- Required evidence: selected coordinates must resemble player/world coordinates;
  chosen distances must vary plausibly as the player walks among cars.
- Result: rejected. Vehicle classification worked, but no candidate pair passed
  the inherited pairing rule, so every vehicle shadow was filtered out.
- ZIP SHA-256:
  `C83E309A81422FCD35FC6A8EFD4B3E9A42956683966EEBEC513AF63BA166D005`.
- ASI SHA-256:
  `01F03F79BC85879DD67A59A4196C6443D9E791AF635982F4A435940E38B9ECC9`.

# v1.2.19 - strict nearest four (rejected)

- Base: v1.2.18 selector on the validated v1.2.16 engine/pass repair.
- Change: select four complete vehicle pairs and reject all unmatched classified
  vehicle shadow candidates.
- Diagnostic: `PAIR_SELECTION` logs chosen IDs, positions, and distances.
- Invariant: `keptHeadlights <= 8`.
- Explicit risk: eight headlights may revive coarse world shadows or reduce their
  update cadence. That would be a four-car budget regression, not a regression of
  the underlying 24-map/pass repair.
- Result: rejected; strict filtering worked, but direction-vector ranking selected
  arbitrary vehicle pairs and nearby cars cast no visible shadows.
- NexusTools ZIP SHA-256:
  `37EB37ED6B3B4D151BC5B14560025EA4086312363DC1DDC29003401E2402BDDC`.
- ASI SHA-256:
  `5372231F3FE4A5EF9E61D141520126C95111A46787B813E30B46DB1BC749CDEA`.

# Important build history

| Build | Result |
|---|---|
| v0.9.0 | Modular-equivalence test passed startup and ran for more than ten minutes without a crash. Seven rapid-flicker F8/F9 captures were all 18 entries/21 faces/maps 0–20 and remain unclassified. Heavy logging and sidecar work remain; 13 repairs failed closed as unverified. |
| v1.1.6 | Coarse cached-world scheduling experiment rejected after severe fidelity regression. |
| v1.1.7 | Restored stable high-resolution behavior, but dynamic world-shadow disappearance remained. |
| v1.1.8 | Preserved full native world chain, but disappearance still reproduced. |
| v1.2.2 | Retest with traffic enabled crashed at RVA `0x408A4F`. Same type-3 valve as stable v1.2.7, but forced B4=21; coarse fallback was not observed before crash. |
| v1.2.3 | Vehicle component lookup and lazy handle initialization; crashed before or shortly after gameplay. |
| v1.2.4 | Removed filtration as an A/B control; still crashed as scene candidates rose. |
| v1.2.5 | Restored native B4 4-8 scheduler and kept no filtration; crashed. Diagnostic traversal ran even when disabled. |
| v1.2.6 | Rejected: still crashed with diagnostic traversal gated off. Cache count reached 16, but allocator disassembly disproved treating that alone as a fixed-16 backing limit. |
| v1.2.7 | Stable extended A/B run: 35,647 manager calls without a crash. Exact v1.2.2 type-3 valve capped selected type 3 at four, but pole shadows disappeared and coarse fallback remained in some areas. Diagnostic only. |
| v1.2.8 | Stable through 44,294 manager calls with full chain and native A8=8. Pole disappearance reduced, but two alley cars demoted the fence/world shadow to the 512 cached lane. No physical face overflow. |
| v1.2.9 | Quality stable with no coarse fallback, but crashed after about one minute at recurring RVA `0x408A4F`. Repeatedly reached 17 total admitted records: seven dynamic + eight cached ordinary + two special. |
| v1.2.10 | Rejected diagnostic. Reproduced `+0x408A4F`, caused loading delays and ~200+ to ~30 FPS collapse, and wrote 85.5 MB. It identified unique missing key `0x220C` from shadow caller RVA `0x307405`: added pass 17 was absent from the finalized renderer table. |
| v1.2.11 | Best functional baseline: no coarse or disappearing shadows for ~10 minutes, then recurring `+0x408A4F` after ~71,306 manager calls. Pre-finalization registration is validated but another rarer missing pass/face key remains possible. |
| v1.2.12 | Rejected diagnostic after success. Crashed at `+0x408A4F`; one-shot key was again `0x220C` at manager call 6,745. Zero injection confirmations prove the ASI loaded after the constructor, so the v1.2.11 registration hook never executed. |
| v1.2.16 | Beta baseline. Passes 17-24 are live and stable; alley disappearance is fixed. Focused tracing proves there is no active vehicle cap: excess headlights fill the 4096 dynamic lane and demote world spotlights into a single rotating 512 cached refresh position, causing the remaining coarse/low-cadence tree artifact. |
| v1.2.17 | Last confirmed shadow-present build. Its attempted classifier matched zero vehicles, so the candidate chain remained complete; nearest-two enforcement was not actually active. |
| v1.2.18 | First active-filter regression boundary. It replaced the zero-match classifier with `430C0000`/`43430000`; byte-identical v1.2.24 retest showed no vehicle shadows. |

# Rejected assumptions

- `renderer type 3 == vehicle headlight` is false.
- `B4 4/8 == maximum shadow-map count` is false.
- `24 physical maps == 24 safely admitted dynamic records` is unproven.
- `candidate count == allocated map count` is false.
- `no visible traffic == no vehicle/type-3 pressure` is unsafe to assume.
- `the current build limits the nearest 2-4 cars` is false; v1.2.16 is full-chain pass-through.
- `two direction-matched headlights == proven common vehicle owner` is unproven.
- `cacheCount stopped at 16 == fixed 16-entry cache` is unproven and contradicted
  by the dynamic-vector allocator at manager `+98/+A0/+A8`.

# Build reporting rule

Every future build must list:

- validated patches retained;
- patches changed or reverted;
- experimental logic added;
- known behavioral regressions;
- exact comparison baseline;
- ZIP and ASI SHA-256 hashes.
