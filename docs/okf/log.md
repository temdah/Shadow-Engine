# Update Log

## 2026-08-13 - Stage B split-package build

- Split the former combined mod into an external engine patch and a pure
  NexusTools vehicle-headlight data package.
- Built `ShadowEnginePatch v0.2.0-stage-b` from the validated v0.1.1 early
  bootstrap. It waits for Troplo's RunGame interception while Nexus remains not
  ready, then installs only the two constructor-observation detours.
- Resource hook calls the original constructor first, snapshots all 16 native
  handles at manager `+0x410`, counts non-zero/distinct handles, and performs no
  creation. Pass hook calls the original registration routine and records native
  Shadow/ShadowAlpha activity, including last-native keys `0x200C/0x210C`; it
  injects nothing.
- Signature guards retain the established 15-byte WD1 prologues at RVAs
  `0x308DB0` and `0x2BFF60`. A late-ready state aborts instead of modifying the
  engine after initialization.
- Updated the NexusTools loader patch script to accept a validated ASI target
  filename while preserving its known-source SHA guard.
- Built `Vehicle Headlight Dynamic Shadows v1.0.0` from the archived, tested
  v0.4.0 headlight database archive. Removed all engine ASI, Lua developer menu,
  traffic controls, ownership probes and vehicle-selection logic.
- New delivery policy recorded: all artifacts go only to `Installables`; Codex
  does not install either package.
- Final engine ZIP SHA-256:
  `F76F899FE0675BD9063B7442B947CE3018BF438CFF501CC60B6FBAE08FFEF8DE`.
- Superseded vehicle ZIP SHA-256 with rejected nested runtime layout:
  `8386A03B44EE703B5658D0FDF1E417FDC4F689388712BF4EC878EEF70F8370D3`.
- Corrected NexusTools import ZIP SHA-256:
  `23A5340518B9ABC14700F386417D3E0C0AE844F658053C1235DF389037CA1E53`.
- NexusTools' own `NexusToolsModManager_Temp` folder confirmed its import rule:
  `modconfig.json`, DAT and FAT must be at extracted archive root. Installed
  runtime placement under `data_win64/mods/<friendlyId>` is performed by the
  manager; an import ZIP must not pre-wrap those files in `mods/<friendlyId>`.
- Correction after comparing directly against the known-working v1.2.16 ZIP and
  reading the private NexusTools `cout.ModManager.log`: NexusTools does
  accept and expect the `mods/<friendlyId>/` distribution hierarchy. The actual
  v1.0.0 rejection was manifest schema: non-empty `incompatibleMods` string IDs
  triggered `type_error.304 cannot use at() with string`. The UI collapsed this
  parse error into its generic missing-modconfig dialog.
- Built v1.0.1 with the working hierarchy, normalized `/` entry names, version
  bump, and empty dependency/conflict arrays. SHA-256:
  `6C87B42BA9D94F6D2C049580384F0DDF7C4985BC06907A2454C26B237751BE14`.

## 2026-08-13 - Stage B passed in game

- Game launched and ran for about one minute, vehicle headlight shadows worked,
  and the user quit normally.
- `STAGE_B_COMPLETE` confirms both early constructor detours executed. The
  resource hook observed 16/16 non-zero, 16/16 distinct native handles. Pass
  telemetry observed 17 Shadow and 17 ShadowAlpha registrations including the
  native final keys.
- No resource/pass expansion, scheduling change or limiter was active.
- The reappearance of coarse and disappearing world shadows is an explicit
  regression relative to v1.2.16, caused by enabling headlights against native
  Stage B capacity. It does not invalidate the timing/constructor proof.
- Preserved evidence SHA-256:
  `E4DB2B0D4EB2B85E378CA1EA71AAB6F29577E5AD4ACFEA71945F61FB454D235B`.
- Next isolated experiment: Stage C creates ShadowMap16 inside the resource
  constructor and registers its Shadow/ShadowAlpha pair inside native pass
  construction, while keeping the renderer/scheduler from requesting it. This
  tests safe lifetime and teardown before any capacity becomes operational.

## 2026-08-13 - Stage C v0.3.0 built

- Branched directly from validated Stage B and added only one early resource plus
  one paired pass set.
- `ShadowMap16` is constructed with the previously mapped native builder from
  inside the now-proven resource-constructor window. Its handle remains in patch
  storage; native manager layout remains untouched.
- `Shadow17`/`ShadowAlpha17` are registered through the original registration
  routine while the native registry is still mutable.
- No queue, layout, pass-index, map-handle addressing, scheduler, profile,
  renderer-demand, vehicle classification or filtering changes are included.
- Compiled x64 with warnings-as-errors. PE validation passed; compiler-only PDB,
  LIB and EXP outputs are absent from the package.
- ZIP SHA-256:
  `16CCA4032894D0D1A2453727950953D4F2BE53ACDA8DC8FEA996B5E09E384276`.
- ASI SHA-256:
  `6ECD2AB2DF57EA4FA7528FB74BDB3F1E3DA777BC23574BB5386AD24C2054D022`.
- Test for five minutes with headlight enabler v1.0.1 active, then quit normally.
  Stability across loading, gameplay and shutdown is the target; shadow visuals
  are expected to remain Stage-B-like until downstream capacity is reconnected.

## 2026-08-13 - Stage C v0.3.0 rejected

- Game did not launch into a playable state.
- Hooks installed successfully, but the log ended immediately after
  `STAGE_C_RESOURCE_CREATE_BEGIN`; no resource return or pass registration was
  reached. This isolates the failure to `create_shadow_map_id(16)`.
- The helper was invoked after the original resource constructor returned. The
  experiment proves that this return boundary is already outside the native
  resource builder's valid mutation lifetime, even though it is still early in
  overall RunGame initialization.
- Native constructor decompilation confirms map construction occurs in a fixed
  16-iteration internal loop with two adjacent fixed arrays. Extending the loop
  bound directly would corrupt/overlap manager layout and must not be attempted.
- Evidence SHA-256:
  `37E99CDED4FC55ACA365EF6A6C099B53F997C3AB16AF9118A54B7D5055307396`.
- Safe next route: recover an in-loop interception point and construct/capture
  map 16 before the native registry closes, redirecting all extra output away
  from the fixed manager arrays. Stage B remains the rollback build.

## 2026-08-13 - Stage C in-loop v0.3.1 built

- Disassembled the native constructor loop and confirmed EDI index, R12 handle
  pointer, R13 companion pointer, the two native stores, and loop bound.
- Added a signature-guarded relay at `0x309050`. On iteration 16 it redirects
  R12/R13 to external patch storage; iterations 0-15 remain unchanged. The
  engine's own loop now builds ShadowMap16 before its native cleanup.
- Static review caught unsafe copied relative calls and replaced them with
  absolute calls. Modeled relay disassembly verifies the conditional target and
  both explicit call targets.
- ZIP contains only the two requested runtime files; `backup.zip` owns rollback.
- ZIP SHA-256:
  `210B8E0D61F9ADE613F2E0E15BE41D375D3DA7E628F02CD560F39DA960D33290`.
- ASI SHA-256:
  `6A972A6FCB789981CDA4F9C40BF6F4E99E2DBCF643712339B82867730E5DAABC`.

## 2026-08-13 - Stage C in-loop v0.3.1 passed

- Game loaded, ran for about one minute and exited normally.
- Extra native-loop handle `4264827B` was non-zero and distinct from all native
  handles. Paired pass-17 registration returned, and `STAGE_C_COMPLETE` was
  recorded with zero renderer/scheduler demand.
- No recent crash dump was found for the run.
- This validates the in-loop plus external-output architecture and rejects only
  the earlier post-constructor replay timing—not extra resources in principle.
- Preserved runtime log SHA-256:
  `212C1F1BB627E16C8C1971B96514C8A9323E0B9A25CB3145BEB69F4EC5C8C8BD`.
- Next build should create all eight dormant resources 16-23 and paired passes
  17-24 using the same method. Do not reconnect downstream consumers in that
  build; isolate multi-resource construction and lifetime first.

## 2026-08-13 - Dormant maps 16-23 v0.4.0 built

- Generalized the proven external redirect to eight handles and eight aligned
  companion records. Loop bound is 24; external array index is EDI-16.
- Modeled relay disassembly verifies native indices jump directly to the common
  store, while extra indices compute `handleBase + index*4` and
  `companionBase + index*16`. Relative calls remain absolute in the relay.
- Registers paired passes 17-24 without making any resource consumable.
- No queue, handle lookup, pass-index, scheduler, B4/A8, renderer demand or
  vehicle policy changes are present.
- ZIP SHA-256:
  `E27363FD67259B2F3B047B24960C7B5282F66852B27E3815CE56067F39266F6E`.
- ASI SHA-256:
  `4D386328499B7F514961717AF2554A65F4B75F72E043D1E73231E2918507377C`.
- Projected baseline timeline after this test passes: one native-demand routing
  build, then one v1.2.16 scheduler/queue integration build. Endurance validation
  is still required before the result is called stable.
- Static validation confirmed the engine ZIP has only the patched loader, ASI,
  preserved original loader, restore script and README; compiler PDB/import LIB
  artifacts were removed before final hashing. The vehicle DAT is byte-identical
  to its tested source baseline.

## 2026-08-13 - v0.1.1 Stage A timing probe passed

- User confirmed successful game launch with the minimally patched NexusTools
  loader and early bootstrap.
- Preserved the 1,333-byte appended log under
  `Research/Logs/v0.1.1_early_bootstrap_timing`, SHA-256
  `37E2B26DCC54939B46F7343671D3BEFF06949ABE23E3069CF25CD2EEEEA27EDF`.
- Second process recorded RunGame interception, OriginalRunGame publication,
  Nexus readiness and the final `noEngineMutation=1` marker.
- Measured about 3541.32 ms between OriginalRunGame publication and Nexus ready.
- Concluded Disrupt is already mapped but real engine startup is held by Troplo,
  providing the required deterministic Stage B constructor-hook window.
- Next build is constructor-reachability only; it must not create resources or
  alter B4/A8, scheduling, candidates or vehicle policy.

## 2026-08-13 - v0.1.1 loader compatibility correction and backup

- Rejected v0.1.0's newly compiled dinput8 proxy before user installation and
  removed it from Installables.
- Built v0.1.1 from the exact NexusTools dinput8 baseline, replacing only the
  fixed-size UTF-16 helper target at file offset `0x5D28`.
- Byte audit found 27 changed character bytes and no size/export/import changes.
- ShadowEarlyBootstrap synchronously loads the untouched Troplo helper during
  attach, preserving the original NexusTools chain.
- Delivered v0.1.1 ZIP SHA-256
  `CAB163FBF37562D24C5EAF0619488B47DC9D71F402B229DF5DCBE1488E8FB8F5`.
- Created requested `Installables/backup.zip` directly from the current game
  dinput8/helper/IPE files with a manifest; SHA-256
  `350354EA6B458E78624F7DEA5375F5E6DBCB21A4E158E3CE33766DD59FE955D3`.

## 2026-08-13 - external timing bootstrap v0.1.0 delivered

- Built a new x64 dinput8 forwarding proxy with the original six export names
  and ordinals plus `ShadowEarlyBootstrap.asi`.
- Bootstrap loads the untouched Troplo helper and observes Disrupt mapping,
  Troplo RunGame interception, OriginalRunGame population and Nexus readiness.
- Performs zero Disrupt mutations and creates no resources.
- Local smoke test loaded the proxy, resolved all exports and successfully
  forwarded `DllCanUnloadNow` to the Windows system dinput8.
- Added the preserved NexusTools dinput8 baseline and a root-level restore script.
- Delivered only to
  `Installables/ShadowEngineEarlyBootstrap_TimingProbe_v0.1.0.zip`, SHA-256
  `63A081840CBDA329B4395EDEFB0A11E711754F603D313F1023AA2F1CE891A9D5`.
- No game directory or Custom mods file was written.
- New standing delivery rule: all future user artifacts go to
  the private development workspace's `Installables` directory; the tester
  performs installation.

## 2026-08-13 - public bootstrap packaging decision

- Selected a game-root-shaped drag-and-drop ZIP for eventual public release.
- The archive will contain `bin/...` paths and be extracted into the Watch Dogs
  directory containing `bin` and `data_win64`.
- The distributable dinput8 proxy will be our own system-DirectInput forwarder
  and Troplo chain loader; do not redistribute a modified Troplo binary.
- Development probes still require hash validation, backup and restore because
  extraction alone cannot safely preserve an overwritten loader.
- Documented the proposed archive tree and loader-conflict notice in
  `docs/EARLY_BOOTSTRAP_ARCHITECTURE.md`.

## 2026-08-13 - deterministic pre-RunGame bootstrap boundary recovered

- Reconstructed the installed dinput8/Troplo loader chain from preserved
  binaries without modifying the game.
- Confirmed the dinput8 proxy hard-codes `TroploAsiInjectionHelper.asi` rather
  than scanning arbitrary root ASIs.
- Confirmed Troplo exports and uses GetProcAddress, RunGame, RunGameEx and entry
  wrappers plus an initialization thread.
- Selected a reversible chain-loader design that interposes Troplo's wrappers
  after Disrupt maps but before its real RunGame.
- Documented timing-only, constructor-reachability, early-resource and
  operational-24-map proof stages in
  `docs/EARLY_BOOTSTRAP_ARCHITECTURE.md`.
- No game files were changed and no external loader was installed.

## 2026-08-13 - v1.2.26 rejected; early bootstrap required

- User reported a loading-screen crash with v1.2.26.
- Preserved the 1,140-byte log with SHA-256
  `95CA59598B679100C98CC51FB88A09A16F7FC327AF293127ACD37ADE71FFE70E`.
- Confirmed pass range 17-24 completed before failure.
- Isolated the fault boundary to the first late `create_shadow_map_id(16)` call:
  the BEGIN marker is final and no returned handle marker exists.
- Rejected all further late resource-creation timing variants.
- Audited the installed bootstrap: `dinput8.dll` hard-codes only
  `TroploAsiInjectionHelper.asi`; it is not the normal arbitrary-ASI scan path.
- Promoted deterministic early chain-loading or a Troplo-provided pre-RunGame
  callback as the required architecture.

## 2026-08-13 - v1.2.26 staged ShadowMap16-23 resource probe

- Branched directly from unfiltered v1.2.16 and removed the dormant vehicle
  pairing/filter helper plus headlight-toggle native exports.
- Removed all Aiden/nearest-car limiter integration from the runtime Lua path;
  the WD2-style limiter is formally deferred unless shared capacity fails.
- Kept B4=8, A8=8, pass 16 reservation, passes 17-24, queue/layout work and the
  complete native candidate chain unchanged.
- Replaced v1.2.13's all-at-once live mutation with one extra handle construction
  every 120 renderer calls beginning at call 240.
- Added pre/post markers, zero-handle and duplicate-handle checks, and a final
  eight-handle readiness marker. Extended demand remains disabled.
- Compiled with warnings-as-errors, removed PDB/LIB artifacts, validated ZIP
  paths, and copied only the ZIP to the game's Custom mods folder.
- ZIP SHA-256 `10EB64CED2E50DD1D8E829CCB0DB2E7398956A5E95366F3B918B4F486F8D4BF5`;
  ASI SHA-256 `08BBBEF9EB8D95AE46449C3BAF795FE1B6947CACE693755A4EDCCAFC09D51CA5`.

## 2026-08-13 - hard-confirmation audit and 16-dynamic target

- Audited the v1.2.16 source and preserved runtime logs against every public
  engine-patch claim.
- Corrected the central overclaim: v1.2.16 patches a 24-entry address/layout
  envelope but copies only native ShadowMap0-15 handles and deliberately creates
  zero extra resources.
- Confirmed live registration of every Shadow/ShadowAlpha pass 17-24, but found
  no saved frame using an extended map/face index; maximum observed was 11 queue
  entries and 14 faces.
- Replaced the B4=12 proposal with a gated B4=17 target for sixteen ordinary
  dynamic positions. Safe ShadowMap16-23 creation must be solved first.
- Kept A8=8 as the first-test cache ceiling because increasing it adds coarse
  retained owners and refresh latency rather than high-resolution capacity.
- Added `engine-patch-validation-audit.md` as the canonical claim/evidence matrix.

## 2026-08-13 - v1.2.25 result and shared-capacity pivot

- Preserved the v1.2.25 runtime log under
  `Research/Logs/v1.2.25_temporal_classifier_test/` with SHA-256
  `85E348EE1E4691F19C8453146B7D65D508CCB1FB0B30A1291FA94F0BF83BCC93`.
- Proved the limiter was active rather than a no-op: up to 30 records were
  suppressed, with 64 sampled manager intervals containing suppression.
- Rejected temporal co-motion as a production vehicle classifier after it
  inferred as many as 24 nodes and produced one- and three-member nodes.
- Promoted shared spotlight capacity/residency as the primary engineering path.
- Defined a bounded next control from unfiltered v1.2.16: B4 8-to-12 only, A8=8,
  24 maps, 25 queue entries, passes 17-24 and no candidate filtering.
- Saved the broad non-shadow research as
  `docs/AFTER_SHADOWS_ENGINE_ROADMAP.md` and marked it deferred.

## 2026-08-13 - broad Disrupt engine system survey

- Added `engine-system-survey.md` with an evidence/confidence model and ranked
  roadmap covering LOD/distant lighting, weather, cloth, debris/wind,
  ultrawide, optical effects, water/reflections, crowds/AI, destruction, and
  E3-inspired restoration.
- Ran a third targeted Ghidra string/xref pass and preserved it as
  `Research/SystemSurvey/WD1_Subsystem_StringXrefs_3.txt`.
- Runtime-confirmed per-light turnoff/shadow distance, spotlight coverage,
  quadratic cutoff, water SSR/quality, LOD dithering, grass wind/motion, and
  road-destruction registrations.
- Confirmed `ExtendDrawDistance` is absent from WD1 but present in the WD2 render
  config; recorded a multi-gate LOD/light-culling model instead of a one-flag fix.
- Data-model-confirmed rain/fog headlight effects, beam volume, flare elements,
  wind/debris budgets, cloth cadence, environment/lighting/fog/cloud controls,
  traffic/crowd budgets/reactions, and road-destruction content.
- Separated cloth update cadence from FPS-dependent physics speed; neither is
  labeled fixed until multi-FPS timing telemetry identifies the consumer.
- Recorded the first probe architecture: disabled-by-default modules, reason
  codes, ring buffer, deduplication, and low-frequency flushing.

## 2026-08-12 - v1.2.25 Aiden-centered temporal nodes

- Branched directly from validated v1.2.16.
- Added motion-history classification instead of guessed component handles.
- Added three-metre co-motion clustering and four Aiden-relative resident slots
  with hysteresis.
- Added Aiden's current vehicle as a separate priority bypass outside the four
  traffic slots.
- Preserved all unknown/static type-3 records as fail-open world lights.
- Recorded TFoWC2 developer confirmation that each vehicle contributes two
  independently shadow-casting lights and that a limiter controls maps/draws.
- Recorded the `lightningflashgenerator.lib` never-ending-lightning explanation
  for the future moon/storm-lighting project.
- Compiled with warnings-as-errors and packaged a clean NexusTools archive.
- ZIP SHA-256 `F2922F4770633DE9026104A1E81125CE791B44F5861260D5841C28BDEEDAC6C5`;
  ASI SHA-256 `551FBA1901D1B15972AC0378BBAE8490F1E420D8C01D200CD0A3D3F3EF407E83`.

## 2026-08-12 - corrected regression boundary after v1.2.24

- User reported no vehicle shadows with the byte-identical v1.2.18 control.
- Retired v1.2.18 as a validated working visual baseline.
- Established v1.2.16/v1.2.17 as the last confirmed shadow-present builds.
- Verified that the native engine patch source is identical across
  v1.2.16-v1.2.18.
- Isolated the meaningful boundary: v1.2.17 classified zero vehicles and was
  effectively fail-open; v1.2.18 introduced the `430C/4343` handle classifier
  and became the first build to actively remove type-3 records.
- Demoted the 8-dynamic versus 4-dynamic/4-cached observation from established
  cause to an unresolved runtime-state difference.

## 2026-08-12 - v1.2.24 exact binary control

- Exact source comparison did not explain loss relative to v1.2.18.
- Log comparison exposed `localCapacity=8/cacheCount=0` in v1.2.18 versus
  `localCapacity=4/cacheCount=4` in v1.2.23.
- Packaged the untouched v1.2.18 ASI byte-for-byte as v1.2.24 to isolate native
  rebuild effects from external runtime/config/load-order effects.
- Archive validated and copied to `.claude\Custom mods`; not manually installed.

## 2026-08-12 - second endgame formalized

- Added shared spotlight-pipeline expansion as a first-class alternative to the
  Aiden-centered four-vehicle limiter.
- Documented why the existing 24-map/25-queue/pass extension is necessary but not
  sufficient: admission, residency, redraw cadence, eviction, face indexing, and
  downstream consumers remain linked limits.
- Added a v1.2.23 decision gate to choose based on fail-open visual behavior and
  raw nearest-type3 telemetry rather than further classifier guesses.

## 2026-08-12 - v1.2.23 fail-open classifier probe

- v1.2.22 retained four sources, proving direct selection worked, but their
  75-230 m distances proved the classifier excluded nearby car lights.
- Disabled candidate filtering and added rate-limited nearest raw type-3 handle
  telemetry.
- v1.2.23 compiled cleanly after removing an obsolete pair-filter variable.
- Clean v1.2.23 archive validated and copied to `.claude\Custom mods`; no manual
  installation was performed.

## 2026-08-12 - v1.2.22 combined-source test

- v1.2.21 confirmed that spatial/direction pairing also produces zero pairs.
- Stopped treating classified records as individual left/right lamps.
- v1.2.22 ranks the nearest four classified vehicle shadow sources directly
  against Aiden, without changing casting range or engine behavior.
- Native module compiled cleanly with warnings as errors.
- Clean v1.2.22 archive validated and copied to `.claude\Custom mods`; no manual
  installation was performed.

## 2026-08-12 - v1.2.21 spatial pairing diagnostic

- v1.2.20 log proved the failure occurred in pair construction, not native
  admission: up to 12 vehicle records, zero pairs, zero kept headlights.
- Replaced consecutive-ID pairing with nearest world-space/direction-matched lamp
  pairing as an interim diagnostic.
- Clarified that the three-metre lamp association threshold is unrelated to
  casting range; native light and shadow projection ranges remain untouched.
- v1.2.21 compiled cleanly with warnings as errors.
- Clean v1.2.21 NexusTools ZIP validated and copied to `.claude\Custom mods`;
  no manual installation was performed.

## 2026-08-12 - Aiden-centered residency design and handoff

- Adopted the user's Aiden-as-anchor proposal as the recommended architecture.
- Defined four persistent vehicle entity slots with distance hysteresis, minimum
  residency time, and farthest-member replacement.
- Separated stable vehicle selection from transient entity-to-two-headlights
  candidate mapping.
- Created root `handoff.md` with current builds, hashes, validated engine work,
  regressions, implementation plan, constraints, build steps, and test procedure.

## 2026-08-12 - v1.2.20 position correction

- v1.2.19 test produced no visible nearby vehicle shadows.
- Telemetry conclusively showed that pair ranking used normalized light direction,
  not world position.
- Cross-checked the preserved renderer decompilation and corrected the holder
  offsets from `+0x14/+0x18/+0x1C` to `+0x08/+0x0C/+0x10`.
- Strict four-car admission and all validated engine work remain unchanged.
- v1.2.20 compiled cleanly with warnings as errors.
- Clean NexusTools ZIP validated and copied to `.claude\Custom mods`; no manual
  installation was performed.

## 2026-08-12 - v1.2.19 prepared

- Preserved and hashed the v1.2.18 runtime log.
- Identified and removed its classified-but-unpaired headlight bypass.
- Raised policy to four complete cars/eight lamps.
- Added rate-limited distance-selection telemetry.
- Left engine capacity, pass registration, queue layout, and scheduler constants
  identical to the validated v1.2.16 foundation.
- Native ASI compiled with `-Wall -Wextra -Werror`.
- Clean NexusTools archive validated with the required root `modconfig.json`, the
  ASI, Lua entrypoint, README, and patch report; linker/debug files were excluded.
- ZIP copied to the game `.claude\Custom mods` handoff folder. It was not
  installed manually.

## 2026-08-12

- **v1.2.12 result**: Crashed at `+0x408A4F`; the one-shot marker captured key
  `0x220C`, pass 17, at manager call 6,745. Normal low-volume logging was retained.
- **Major correction**: Injection count was zero. NexusTools loaded the ASI after
  the native pass constructor had completed, so v1.2.11/v1.2.12 never intercepted
  pass 16. Their visual improvement cannot be credited to that inactive hook.
- **Next architecture target**: populate or rebuild the already-live renderer
  lookup table, including creation of valid pass objects; alternatively evaluate
  a genuinely earlier bootstrap mechanism. Do not raise map capacity yet.
- **Evidence**: `VehicleShadowLimiter_v1.2.12_pass17_null.log`, SHA-256
  `3A991E1B0A5A1B7A3755D170548F053ED3E403C29761B00ED9902DFBE52074A4`.
- **v1.2.11 result**: Best functional baseline. No coarse shadows and no shadow
  disappearance were observed for about ten minutes. It then crashed at the same
  `Disrupt_b64.dll + 0x408A4F` fault after roughly 71,306 manager calls.
- **Interpretation**: Pre-finalization pass registration fixed the visual path and
  the immediate pass-17 failure, but a rarer missing/out-of-range shadow key may
  remain. Exact key is not available from the low-overhead v1.2.11 log.
- **Evidence**: `VehicleShadowLimiter_v1.2.11_best_quality_crash.log`, SHA-256
  `340EB8BA7665270BDB3F4266FF78F5ED47624E1A32D938D8AE3EB50A82E7818E`.
- **Build**: v1.2.12 preserves v1.2.11 engine behavior and adds a one-shot probe
  restricted to null Shadow/ShadowAlpha keys for pass >=17. Ordinary nulls never
  open the log; matching output is atomically limited to one line.
- **Artifact**: v1.2.12 ZIP
  `F0453EEC1A1F55475F1DBAF2DAB7E3C412A672D5A461830DF8E3562FB3C92F6E`;
  ASI `1C996D5963C6BFC713B562984E4659598048EF96F74F4A939FD8A51650DB2527`.
  Nine-entry NexusTools archive validated with no PDB/LIB.
- **v1.2.10 result**: Reproduced `Disrupt_b64.dll + 0x408A4F`. Loading slowed and
  observed FPS fell from 200+ to about 30 because the hot lookup probe synchronously
  wrote 85,547,562 bytes. v1.2.10 is retired and must not be reused.
- **Root cause isolated**: A unique null key `0x220C` came from shadow lookup call
  RVA `0x307405`; reset call RVA `0x307410` consumed it and faulted. The key is
  `(17 << 9) | 0x0C`, proving the first added pass is absent at runtime.
- **Implementation fault**: Extra passes were registered after the native frame
  constructor returned, but its renderer tables had already been finalized. The
  replacement must inject passes 17-24 after native pass 16 and before finalization.
- **Preserved evidence**: Full 85.5 MB log compressed to
  `VehicleShadowLimiter_v1.2.10_null_lookup_full_log.zip`, SHA-256
  `C25AD9DF6DB0347F227929900A7EA83CA80E7FB298A6735B3D1DC743590E6ED8`;
  exact caller context in `WD1_v1.2.10_ShadowCaller_307405.txt`.
- **Build**: v1.2.11 removes both lookup probes and replaces post-constructor pass
  registration with injection immediately after native pass 16, before native
  lookup-table finalization. All v1.2.9 quality settings and physical patches are
  retained unchanged.
- **Artifact**: v1.2.11 ZIP
  `4C841F2F69AA7ECEFFB768DC5BE5C98D771ADBE48402030144001213DB95DB2A`;
  ASI `866994D73A4DAD600002F7F6A6E1345DF69C60BA31F48578A920B57BA8924705`.
  NexusTools layout validated with nine entries and no PDB/LIB.
- **Correction**: `FUN_7ffc39ff5130/5150` are two-level renderer table lookups,
  not allocators. They return `table[key & 0xff][key >> 8]`; the recurring
  `0x408A4F` path therefore follows a null table entry. Pool exhaustion and an
  extra-pass mismatch remain hypotheses, not findings.
- **Diagnostic build**: v1.2.10 retains v1.2.9's B4=8/A8=8 and complete physical
  expansion, adding only null-return probes at RVAs `0x405130` and `0x405150`.
  Null results log key, decoded bucket/index, exact caller, manager-call count,
  and thread ID, then follow the original crash path unchanged.
- **Artifact**: v1.2.10 ZIP
  `BB1069EB7F5E84B960D8D47221E0329577E0664E34B3C5A9BEAB1AD7D8B12D87`;
  ASI `967DA8F048EC98E5983FBCDB7B48E3282188B2BEC1279273B8F9247B627E8888`.
  NexusTools root layout validated, nine archive entries, and PDB/LIB excluded.
- **Evidence**: preserved `WD1_RenderResourceLookup_5130.txt` with the exact
  lookup bytes and disassembly.
- **Regression**: v1.2.9 crashed after roughly one minute despite stable shadow
  quality and no observed coarse fallback.
- **Crash boundary**: v1.2.9 repeatedly admitted 17 total records (15 ordinary plus
  two special) before recurring fault RVA `0x408A4F`. Stable v1.2.8 peaked at 13.
- **Superseded interpretation**: The recurring fault resets a renderer object
  returned immediately beforehand by `FUN_7ffc39ff5130/5150` without a null check.
  These were initially mistaken for allocators; the correction above records that
  they are renderer table lookups.
- **Evidence**: Preserved `VehicleShadowLimiter_v1.2.9_B4_8_crash.log`, SHA-256
  `521B4AB92543EBB6B290D497DB85D716119514D2996998B8A10F4E040AF25154`.
- **Build**: Compiled v1.2.9 from stable v1.2.8, changing only normal B4
  initialization/floor from 4 to native ceiling 8. A8 remains native 8.
- **Rationale**: Alley trace contained four type-3/headlight IDs and three world
  type-1 IDs competing for three dynamic positions; seven is the smallest targeted
  dynamic lane that can hold that observed set.
- **Artifact**: v1.2.9 ZIP `4557AE92...226627A`; ASI
  `05346529...300F99`. NexusTools layout validated, PDB/LIB excluded, and no manual
  installation performed.
- **Test result**: v1.2.8 remained stable through 44,294 manager calls with 186
  candidates, 69 raw type 3, 13 admitted records, and native cache count 8.
- **Visual result**: One alley car retained acceptable shadows; two cars caused a
  hard fence/world-shadow transition to coarse quality, while the pole shadow no
  longer disappeared.
- **Trace finding**: Only three ordinary queue positions were dynamic at 4096x4096.
  Seven cached owners rotated through one 512x512 queue slot, producing 5,234 adds
  and 5,222 drops. Renderer demand stayed at 9/24 faces with no overflow.
- **Evidence**: Preserved `VehicleShadowLimiter_v1.2.8_nativeA8_alley_trace.log`.
- **Build**: Compiled and packaged v1.2.8 Native-A8 Retention A/B. Full candidate
  chain, native B4, and physical 24-map/25-queue layout retained; only A8 expansion
  is removed.
- **Artifact**: v1.2.8 ZIP `2747324D...A02F95A`; ASI
  `8999C8B7...C73B081`. NexusTools layout validated, development files excluded,
  and no manual installation performed.
- **Regression retest**: v1.2.2 crashed with traffic enabled. Windows Event 1000
  reports `Disrupt_b64.dll + 0x408A4F`, exactly matching v1.2.4.
- **Validated interpretation**: v1.2.2's forced B4=21 removed visible coarse
  fallback by expanding the fully dynamic lane, but recreated unsafe retained-light
  pressure despite the same type-3 cap used by stable v1.2.7.
- **Refined hypothesis**: failure correlates with total retained dynamic/cache
  light-owner pressure, not one renderer type or cache allocation alone.
- **Architecture finding**: Decompilation of `FUN_7ffc39ed9b60` proves that
  ordinary light admission is approximately `(B4 - 1) + A8`: B4 controls the
  dynamic lane and A8 controls the later cached lane.
- **Quality explanation**: Native B4=4 leaves about three ordinary lights fully
  dynamic, explaining coarse fallback in v1.2.7 despite 24 physical maps.
- **Next isolation**: Proposed full candidate chain plus native A8=8, while keeping
  the 24-map/queue layout and native B4, to test whether expanded cached-owner
  retention causes the stale light-handle crash.
- **Evidence**: Exported `WD1_ShadowAdmissionAndCacheReconciliation.txt`.
- **Test result**: v1.2.7 survived an extended 35,647-manager-call run without a
  crash. Raw candidates reached 163, admitted reached 13, cache count reached 9,
  and selected type 3 remained capped at four.
- **Validated hypothesis**: Restoring the type-3 admission safety valve removes
  the reproduced crash trigger from v1.2.4-v1.2.6.
- **Validated regression**: v1.2.7 still produces coarse fallback in some areas
  and makes legitimate pole shadows disappear. Class-wide type-3 filtering remains
  rejected as a final design.
- **Evidence**: Preserved supplied log as
  `VehicleShadowLimiter_v1.2.7_stable_safety_valve.log`, SHA-256
  `9C339841E3ADF580CD184EACEEC69286F63D75ECEFCA8A8EC955676E46829417`.
- **Build**: Compiled and packaged v1.2.7 Type-3 Safety-Valve A/B. It changes only
  candidate partitioning relative to v1.2.6; native scheduler and gated diagnostics
  are retained.
- **Expected regression**: v1.2.7 deliberately drops non-selected type-3 pole/world
  lights, reproducing v1.2.2 visual disappearance for crash isolation only.
- **Artifact**: ZIP `3095F110...4A378F`; ASI `87100D9F...3CDF50`. NexusTools archive
  structure validated; no PDB or LIB included; no manual installation performed.
- **Verification**: Captured runtime-image bytes match the rebuilt DLL around
  v1.2.5 RVA `0x25DBFA`; it is genuinely mid-instruction and likely reflects
  corrupted control flow rather than a rebuild offset error.
- **Call-chain finding**: v1.2.6 failed in general per-light scene processing after
  an unchecked registry lookup using the light object's `+0x50` handle. Exported
  its upstream callers to `WD1_v1.2.6_FaultCallerChain.txt`.
- **Correction**: Disassembly of the manager `+98/+A0/+A8` allocator shows a
  dynamically grown `0x700`-stride vector. `cacheCount=16` is not proof of a
  fixed 16-entry backing allocation.
- **Crash evidence**: Recorded Windows fault RVAs for v1.2.4 (`0x408A4F`),
  v1.2.5 (`0x25DBFA`), and v1.2.6 (`0x1EFDAB`).
- **Finding**: v1.2.6 dereferenced an invalid light/descriptor lookup result at
  `[RAX+8]`; v1.2.4 failed through invalid `RCX`; v1.2.5 may show corrupted
  control flow and requires a live-byte alignment check.
- **Finding**: v1.2.2's incorrect type-3 filter also acted as a safety valve.
  Removing it is the principal shared behavioral change in v1.2.4-v1.2.6.
- **Regression**: v1.2.6 crashed with tracing inactive and is rejected.
- **Superseded hypothesis**: The earlier fixed-16 backing-cache interpretation
  was weakened by allocator disassembly and must not guide a blind capacity patch.
- **Create**: Established the OKF v0.1 continuation bundle.
- **Status**: Recorded v1.2.6 as the current untested gated-tracing control.
- **Finding**: Documented that renderer type 3 is shared by vehicle and world spotlights.
- **Correction**: Recorded native B4 4-8 values as scheduler controls, not capacities.
- **Reference**: Added the known WD2 successor-manager architecture and backport plan.
- **Crash result**: v1.2.12 reproduced `Disrupt_b64.dll + 0x408A4F` and captured
  missing key `0x220C` / pass 17 at manager call 6,745. Preserved log SHA-256
  `3A991E1B0A5A1B7A3755D170548F053ED3E403C29761B00ED9902DFBE52074A4`.
- **Correction**: v1.2.11/v1.2.12 constructor hooks had zero executions because
  NexusTools loads the ASI from Lua after engine construction. Their visual gain
  was real, but cannot be attributed to pass injection.
- **Reverse engineering**: `FUN_7ffc39eaff60` allocates and constructs a
  `0x1B0`-byte live pass object, then inserts it through `FUN_7ffc39ff5110` into
  `registry+8`. This supports direct late registration without aliasing objects.
- **Build**: Compiled v1.2.13 Live Capacity Repair. The first renderer-queue call
  initializes ShadowMap0-23 handles, registers and verifies both pass families
  for indices 17-24, then calls the original renderer.
- **Artifact**: v1.2.13 ZIP `C0E51325...F61495A`; ASI
  `DAA41637...25237DA`. NexusTools layout verified; no manual install performed.
- **Regression**: v1.2.13 crashed twice before loading at `MSVCR100.dll +0x53C73`.
  No initialization result was logged, so the build is rejected.
- **Evidence**: Preserved `VehicleShadowLimiter_v1.2.13_startup_crash.log`, SHA-256
  `2B0CE6DC3D964D97A0697B2642216ACE53A19685B066E486DAA7D245B64A28BF`.
- **Build**: v1.2.14 removes all live resource/pass mutations and records the
  live manager/registry/table, native handles, and pass 16/17 lookup state once.
- **Artifact**: v1.2.14 ZIP `386B6C62...B73870E5`; ASI
  `AB57594C...F51AC120`. NexusTools package only; no manual installation.
- **Test result**: v1.2.14 loaded and remained stable through 6,309 manager calls.
  Native pass 16 exists; both pass-17 entries are null. Preserved log SHA-256
  `7E253A7EAACE8FEA142D71E336B9BC7AAB0F1F303A5205009886BAD3C9695A6E`.
- **Build**: v1.2.15 isolates pass registration: add only `0x230C` and `0x220C`
  with before/after markers; create no ShadowMap resources.
- **Artifact**: v1.2.15 ZIP `83743717...C5F823B`; ASI
  `543F05A5...15FB31EE`. NexusTools-only handoff.
- **Test result**: v1.2.15 registered pass 17 and alpha successfully, then crashed
  at manager call 11,551 on missing `0x240C` / pass 18 with the same `+0x408A4F`.
  Preserved log SHA-256 `55F1A56C...BED0F63`.
- **Conclusion**: late pass registration is safe; v1.2.13's immediate startup
  regression originated in its late resource-creation half.
- **Build**: v1.2.16 registers/verifies passes 17-24 only and creates no resources.
- **Artifact**: v1.2.16 ZIP `EEB146B6...D061406`; ASI
  `805C8C58...CC7CBFB`. NexusTools-only handoff.
- **Endurance result**: v1.2.16 exceeded 71,900 manager calls with every added
  pass valid, no missing-key marker, and no corresponding Windows crash event.
  Preserved log SHA-256 `7CC45FC077B3ED779D181C057E79857BBA7B3B9829FDDBB773C6C3322409FD46`.
- **Status**: v1.2.16 promoted to beta candidate. The sequential missing-pass
  `+0x408A4F` crash is considered fixed pending longer community testing.
- **Visual comparison**: v1.2.11 and v1.2.16 both reproduce a tree/world-spotlight
  shadow becoming coarse and low-cadence when a nearby car casts a headlight
  shadow; native game does not. This predates the v1.2.16 pass repair.
- **Remaining bottleneck**: logs saturate at eight dynamic and eight cached
  records. Leading hypothesis is a headlight displacing a world spotlight into
  the engine's coarse/slower cached-refresh lane. Next work must trace that exact
  owner transition while preserving v1.2.16's pass registration unchanged.
- **Trace confirmation**: Preserved the focused v1.2.16 tree-residency log as
  `Research/RuntimeCaptures/VehicleShadowLimiter_v1.2.16_tree_residency_trace.log`,
  SHA-256 `053C753E2CE0C12C99E9240DE37B42CC7D15E236D2D85D13CBD3164122976E5`.
- **Finding**: v1.2.16 has no active vehicle limiter. It passed 26 distinct traced
  vehicle light IDs and retained as many as 14 vehicle IDs in one snapshot.
- **Finding**: Seven vehicle headlights occupied the 4096 ordinary dynamic lane
  while world spotlights `0x1D25` and `0x1D1B` rotated through queue slot 8 at
  512x512. The trace captured vehicles directly replacing both world owners.
- **Conclusion**: The remaining tree resolution and update-rate failures are the
  same dynamic-to-cache eviction. The next build must select vehicle owners by
  player distance and remove only excess vehicle shadow candidates, never all
  renderer type-3 records and never the visible headlight illumination.
- **Build**: Compiled v1.2.17 Nearest-Two. It preserves the complete v1.2.16 pass
  and layout repair and adds only confirmed-vehicle candidate selection.
- **Selection design**: NexusTools supplies Aiden's position at 10 Hz; the ASI
  pairs consecutive-ID vehicle lights within four metres, ranks pair midpoints,
  retains the nearest two pairs, and applies a five-metre hysteresis advantage.
- **Safety behavior**: All non-vehicle and unknown records retain native order;
  unpaired vehicle records also pass through; missing position data disables
  filtering for that call. Only excess confirmed complete pairs are removed.
- **Artifact**: v1.2.17 ZIP `83FD5101...3F5FE6`; ASI
  `2EA7A699...329124`. NexusTools structure validated; no manual installation.
- **Safety correction**: Incomplete/unpaired positively classified vehicle lights
  now pass through; only confirmed complete excess pairs are removed. Player XYZ
  publication is synchronized so the manager cannot observe a partially updated tuple.
- **v1.2.17 test result**: Limiter did not engage. `playerPos=1` proved the position
  bridge worked, but every manager sample reported `vehicleCandidates=0`,
  `vehiclePairs=0`, `keptHeadlights=0`, and `filtered=candidates`.
- **Evidence**: Preserved v1.2.17 trace as
  `Research/RuntimeCaptures/VehicleShadowLimiter_v1.2.17_failed_classifier_trace.log`,
  SHA-256 `A20AFE0BA5384910D35134CEDE84413C738FBBC37499DC249E6E4193DEC030DF`.
- **Classifier correction**: 791 traced vehicle render records used handle
  `430C0000`, 152 used `43430000`, and captured world spotlights used `43820000`.
  v1.2.18 replaces only the failed registry lookup with the two traced handles.
- **Artifact**: v1.2.18 ZIP `CB878577...7B0337`; ASI
  `BE2AE52D...E7E522`. In-game result pending; no manual installation.
- **Runtime proof**: v0.4.0 ran for approximately four to five minutes and looked
  good. All external maps 16-23 had unique non-zero handles, all paired passes
  17-24 returned, and `STAGE_C_RANGE_COMPLETE` was reached without a crash log.
- **Evidence**: Preserved the v0.4.0 log under
  `Research/Logs/v0.4.0_stage_c_range_pass`, SHA-256
  `4C825226AC1492D1028C0519A9E39955582B6A18DC5672F0B3B0BD0AB8745F44`.
- **Build**: Created v0.5.0 native-demand routing. It reconnects the 24-handle
  table, maps 16-23 to passes 17-24, expands the physical queue to 25 entries,
  and applies all 46 recovered tail relocations. It deliberately leaves
  scheduler/demand and all vehicle policy native.
- **Artifact**: v0.5.0 ZIP `69673428...72114`; ASI
  `C85BA6EF...1C13068`. Archive contains exactly the two approved root files.
- **NexusTools integration request**: Prepared a developer-facing request for a
  supported pre-RunGame bootstrap extension point. Documented that the reference
  loader differs from the original NexusTools dinput8 by 26 bytes at embedded
  UTF-16 helper-name offset `0x5D28`, then chains the untouched Troplo helper.
  The request explicitly does not ask NexusTools to bundle the experimental
  shadow patch.
- **v0.5.0 result**: `STAGE_D_COMPLETE` with no crash, but severe coarse,
  low-cadence and missing-shadow artifacts remained. Structural routing passed;
  visuals were rejected compared with old v1.2.16.
- **Evidence**: Preserved v0.5.0 log SHA-256
  `F36D80EA629C792E910FC312094F75D61083B87F955FC7DC165F07DDAF48882C`.
- **Build**: v0.6.0 restores only the old v1.2.16 B4 initialization/floor from
  4 to 8 on top of the real early-created 24-map foundation. A8 remains 8;
  manager/profile/filter/limiter changes remain absent.
- **Artifact**: v0.6.0 ZIP `F08A0BC2...2015A7`; ASI
  `5DCD0FAB...2336B2`.
- **Correction**: v0.6.0 did not match old v1.2.16. Missing shadows improved,
  but coarse fallback remained materially worse. Do not call it equivalent.
- **Root difference**: old v1.2.16 invoked the native profile callback once on
  the first manager call; v0.6.0 only patched B4 and sampled A8 too early.
- **Build**: v0.6.1 restores the pass-through manager/profile initialization
  while excluding obsolete late initialization and every filtering policy.
- **Artifact**: v0.6.1 ZIP `4C1B3482...B265FF`; ASI
  `31D100B4...B4CBF`.
- **v0.6.1 result**: rejected; missing/reappearing and coarse shadows persisted.
  Callback left A8 at 4. Evidence `11DDECEB...4B50`.
- **Archive audit**: Exact old v1.2.16 ZIP contains one ASI, ruling out a hidden
  companion engine component.
- **Build**: v0.6.2 preserves early resources/passes but defers all downstream
  routing/B4/manager changes until constructors finish plus 1000 ms.
- **Artifact**: v0.6.2 ZIP `6F46A534...FD55C`; ASI
  `C41A8BFB...AB921`.
- **v0.6.2 result**: visually rejected. The deferred downstream patch did run
  after both constructors plus 1000 ms; all extra maps/passes were valid, but
  runtime settled at `B4=8 A8=4` and coarse/missing/reappearing shadows remained
  when cars entered the scene. Timing was not the missing v1.2.16 variable.
- **Evidence**: preserved v0.6.2 log SHA-256
  `D7F8B96DF0EC20FFA3B3E3413ED3B6FC653B84A98D9C3CD7D381DAE2B454740D`.
- **Build**: v0.7.0 active-capacity proof retains the proven 24-map/25-queue
  foundation and native candidate passthrough, raises only B4 to 17, holds A8
  at 8, and adds a 4 Hz read-only renderer face-count probe. No vehicle limiter.
- **Proof criterion**: `STAGE_F_EXTRA_CAPACITY_FIRST_USE` with faces 17-24 and
  `overflow=0`; any crash or overflow identifies a remaining linked limit.
- **Artifact**: v0.7.0 ZIP `44E2E2C4...B3B6E`; ASI
  `F7B38C13...CA0FC`. Exactly the two approved `bin/` runtime files.
- **v0.7.0 result**: visual success but stability failure. No coarse fallback
  was observed and retention improved. The renderer reached 16 faces, then the
  first 17-face frame used extra map 16 with `overflow=0` and immediately
  crashed at new fault `Disrupt +0x426C2A` (`inc [RDX+0x94]`).
- **Evidence**: v0.7.0 log SHA-256 `FBC55211...7B1F9`.
- **Build**: v0.7.1 preserves v0.7.0 and adds a one-shot, continue-search
  exception observer at `+0x426C2A` to capture registers, bad RDX state and the
  caller return RVA. It does not catch or repair the exception.
- **Artifact**: v0.7.1 ZIP `B776BF6E...09FB7`; ASI
  `8EBDACA6...9590C`.
- **Logging requirement**: All builds after v0.7.1 must reset the canonical
  `ShadowEnginePatch.log` once per confirmed real-game session, then emit a
  version/PID/timestamp/configuration header. Never concatenate previous runs.
  Auxiliary bootstrap processes may not truncate the log.
- **v0.7.1 captured result**: 21 faces were active with no numeric overflow, but
  the world briefly blacked out and the process faulted at
  `Disrupt_b64 +0x426C2A`. Captured `RDX=0` and caller return `+0x42774E`
  prove the admitted extra face lacked a valid backing render/resource object.
  The mixed legacy log is retained only under
  `Research/Logs/v0.7.1_null_backing_object_capture`.
- **Build**: v0.7.2 implements a single clean current-session log. Helper
  processes remain silent; only the confirmed real game process truncates and
  creates exactly `bin/ShadowEnginePatch.log`, whose header contains schema,
  version, PID, role and timestamps. No secondary game-folder logs are created.
  The broad v0.7.1 exception observer is removed.
- **Artifact**: v0.7.2 ZIP `30480C32...D5B3`; ASI
  `7185DFEF...1742`. Exactly the two approved `bin/` runtime files. The active
  capacity behavior remains unsafe until backing-object initialization is
  repaired.
- **Handoff guard**: v0.7.2 was moved out of `Installables` into
  `Builds/Archive/InternalDiagnostics/..._INTERNAL_UNSAFE.zip`. The next user
  test ZIP must include both clean-session logging and the actual null backing
  object crash repair.
- **Crash-path correlation**: queue construction at `+0x30CB80` allocates a
  `0x230`-byte object per face through `+0x0FB170` and publishes it at face
  offset `+0x7D8`. Null there explains both `+0x2C04CD` (`[object+0x20]`) and
  `+0x426C2A` (`[object+0x94]`). Investigate and repair the producer/lifecycle;
  do not mistake a consumer null-skip for a completed capacity fix.
- **Build**: v0.7.3 combines clean one-session logging with guards for both
  proven null per-face consumers. The face-cost reader skips invalid objects;
  the active resource dispatcher compacts null primary/secondary entries before
  calling the engine and skips an empty submission. It logs skip/drop counters.
- **Artifact**: v0.7.3 ZIP `10E5CED2...C8FB`; ASI
  `6AA5A4BA...5A76`. Exactly the two approved `bin/` files; not manually
  installed. Stable gameplay would prove the crash guard, while zero counters
  are still required before calling backing capacity fully repaired.
- **v0.7.3 result**: rejected. Clean-session logging passed and extra capacity
  reached 21 faces, but two global world-lighting collapses preceded a new crash at
  `Disrupt +0x407806`. Dump registers prove `RBX=0` in resource release. Old
  `+0x2C04CD`/`+0x426C2A` did not recur.
- **Root mistake**: v0.7.3 compacted null resource arrays only for acquire and
  passed release arrays through. The new fault is the lifecycle-symmetric
  release-side equivalent.
- **Build**: v0.7.4 compacts both acquire and release submissions, retains the
  face-cost guard and clean-session log, and separates acquire/release counters.
- **Artifact**: v0.7.4 ZIP `9D81D77D...B195`; ASI
  `61A17339...296B`. v0.7.3 was removed from Installables and archived rejected.
- **v0.7.4 result**: rejected. The clean current-session log reached 22 faces,
  six extra maps in use, zero numeric overflow and zero face/resource guard
  counters, but world-lighting collapse recurred and the game crashed.
- **Visual terminology correction**: these were not black frames. Geometry and
  the scene remained visible while world illumination turned black globally,
  then recovered. Treat this as invalid lighting/shadow render state rather
  than a dropped presentation frame.
- **Fresh dump correction**: v0.7.4 faulted at `Disrupt +0x25DBFA` with
  `RCX=0`, reading address `0x1A0`. Captured runtime bytes decode the RVA as the
  valid first dereference in `+0x25DBF0`; the older mid-instruction conclusion
  is superseded for this runtime.
- **Caller/root state**: `+0x30ACE5` checks an outer configuration wrapper,
  loads `wrapper[0]`, and calls the query without testing that inner pointer.
  The dump loop index was 17, an admitted position above the native range.
- **Build**: v0.7.5 inserts the missing inner-null test and routes both null
  states to the engine's existing `+0x30ACFC` default-resolution fallback.
  It does not fabricate objects or filter lights. `nullSourceFallbacks` is
  sampled in residency logs.
- **Retained**: 24 early maps, passes 17-24, 25-entry queue, 46 tail
  relocations, B4=17/A8=8, full candidate passthrough, clean-session logging,
  face-cost guard, and symmetric resource acquire/release compaction.
- **Artifact**: `Installables/ShadowEnginePatch_NullSourceFallback_v0.7.5.zip`,
  ZIP SHA `705BFBD3...2823A1`, ASI SHA `DDDDA100...C45C6B`; exactly the two
  approved `bin/` runtime files. v0.7.4 archived rejected.
- **v0.7.5 result**: rejected. It bypassed the old CPU null fault, but the world
  illumination collapsed to black and the game crashed simultaneously in
  `d3d11.dll +0x15D700`. The last sample was the first 22-face/19-entry burst.
- **Dump evidence**: D3D11 received invalid tagged graphics state including
  `0xFFFFFC00000000D1`. Evidence log SHA `27896F1E...E04D818`; dump SHA
  `13B1794F...7DE157`.
- **Allocation audit**: queue allocation `+0x3CA3E2` uses native `0x27180` and
  is one of the relocated values, already expanded to `0x397C0`; parent buffer
  under-allocation is ruled out.
- **Diagnostic correction**: v0.7.5 `nullSourceFallbacks` mixed normal outer-null
  fallback calls with abnormal inner-null calls and cannot quantify corruption.
- **Build**: v0.7.6 preserves native outer-null fallback but rejects only
  non-null-wrapper/null-inner records through existing next-candidate path
  `+0x30D249`, before queue count commit. Counter is now
  `innerNullCandidateRejects`.
- **Retained**: 24 real maps, passes 17-24, 25 queue entries, enlarged queue
  allocation, B4=17/A8=8, candidate passthrough and all prior guards. No vehicle
  limiter and no capacity reduction.
- **Artifact**: v0.7.6 ZIP `C2952DFD...20BBBB`; ASI
  `CCABCC97...F2587F4`; exact two-file package.
- **v0.7.6 result**: rejected. `innerNullCandidateRejects` stayed zero. The new
  dump faults at `Disrupt +0x25DC09`, where a non-null queued configuration has
  a null/stale backend at `+0x1A0`. Final residency was 23 faces, seven extra
  maps, and zero numeric overflow.
- **Root cause**: the profile callback reserved four contiguous `0x700` cache-
  owner records, then the hook raised only logical ceiling `A8` to eight. Owner
  five invoked native grow during admission, moving the vector after candidate
  descriptors had captured raw pointers into the original allocation.
- **Build**: v0.7.7 pre-reserves eight owners with native helper `+0x319160`
  before current-frame admission, verifies existing `A0` is preserved and
  physical capacity decoded from packed `A4` reaches eight, and only then sets
  `A8=8`. It fails closed to actual capacity and logs any vector-base movement
  across the manager call.
- **Removed**: v0.7.6's disproven inner-null candidate reject. Full candidate
  passthrough is restored; no vehicle filtering or capacity reduction added.
- **Retained**: 24 real maps, passes 17-24, 25 queue entries, all relocated queue
  consumers, `B4=17`, eight cache owners, prior guards, and one clean session
  log.
- **Evidence**: `Research/Logs/v0.7.6_owner_vector_reallocation_root_cause`.
- **Artifact**: `Installables/ShadowEnginePatch_OwnerVectorPreReserve_v0.7.7.zip`;
  exact two-file root package; ZIP SHA `255A3DFB...44CD4CB`; ASI SHA
  `48997648...757CD88`; in-game validation pending.
- **v0.7.7 result**: rejected, but the owner-vector fix is validated. Native
  reserve expanded packed physical capacity from four to eight before admission,
  the owner base remained stable, and the prior stale-owner/D3D11 faults did not
  recur while six extra maps were active.
- **New root boundary**: dump PID 47828 faults at `Disrupt +0x408A4F` after a
  registry lookup for key `0x320C`, exactly Shadow pass 25. The added real range
  ends at local map 23/pass 24. This is a 25th-face request, not a corrupt key or
  pass-index skip error.
- **Architecture correction**: `B4=17` budgets admitted dynamic-light records;
  renderer pass indices use cumulative per-record face counts. Multi-face lights
  can therefore exceed the 24-map physical pool before the 25-entry queue is full.
- **Build**: v0.7.8 preserves `B4=17`, maps 0-23, passes 17-24, cache A8=8 and
  the pre-reserve fix. Before rendering, it admits the largest whole-record
  priority prefix fitting 24 faces, then restores the manager-built queue count
  after the renderer returns for native cleanup. It logs face-budget clamps and
  never splits a multi-face record.
- **Evidence**: `Research/Logs/v0.7.7_owner_reserve_pass25_overrun`; dump SHA
  `45C9F11F...EE435`.
- **Artifact**: `Installables/ShadowEnginePatch_FaceBudgetCap_v0.7.8.zip`;
  exact two-file root package. ZIP SHA `200C03E0...276D03`; ASI SHA
  `EF0944DA...2B8E2C`; in-game validation pending.
# 2026-08-13 - v0.7.8 night transition and v0.7.9

- Rejected v0.7.8 after a deterministic night-activation crash.
- User still observed coarse world shadows when headlights entered, but did not
  observe the earlier global black-world effect in this run.
- Runtime B4 returned from 17 to 8, the queue remained at eight/nine faces and
  extra-map use stayed zero; the expanded physical pool was not being consumed.
- Decompiled manager `+0x2E9B60`: B4 is adaptive, with normal lower/upper bounds
  4/8 and reduced-mode bounds 1/2. The old patch changed only the normal lower
  bound to 17, leaving the upper clamp at 8 and creating contradictory 17/8
  behavior. A coherent two-bound scheduler repair is the next quality step after
  v0.7.9 lifecycle validation.
- Proved the crash occurred with only nine faces, before the renderer face cap,
  and was unrelated to pass-25 overflow.
- Dump PID 46900 faulted at `Disrupt +0x1300A0` from queue builder `+0x30D1CC`
  while consuming the relocated global queue reference at `+0x397A0`.
- Identified late queue-layout mutation as the root cause: existing queue objects
  had native 17-entry allocation/initialization and no constructed expanded tail.
- Built v0.7.9 with queue allocation/count/clear/tail relocation before native
  queue construction; retained later runtime routing and the 24-face cap.
- Hardened cache-owner reserve invariants and true fail-closed behavior.

# 2026-08-13 - WD2 shadow-budget comparison

- Confirmed WD2's `HeadlightShadowsLevel` contract: 0 off, 1 player car, and
  levels 2-4 for 2-4 cars. This proves upstream vehicle-aware admission, not a
  late generic-spotlight classifier.
- Parsed shipped render profiles: pc_3 uses level 1 and pc_4 uses level 2; lower
  profiles use 0. Levels 3-4 are supported but are not enabled by the extracted
  stock profiles.
- Parsed shadow profiles: dynamic maps scale 8 -> 10 -> 12; static maps remain
  four and scale from 256 to 1024. Coverage thresholds are independently tuned.
- Corrected the earlier WD2 interpretation: the 0-8 manager owns
  `StaticShadowMap` resources. The separate 0-4 function is vegetation/wind
  code, not the headlight selector.
- Confirmed the current WD1 manager hook is too late for reliable vehicle
  identity: headlights and pole/tree world lights are generic type-3 candidates.
  Direct WD1 searches found none of the named WD2 manager/setting strings. This
  does not prove vehicle ownership is absent upstream.
- Decision: retain coherent engine expansion and add protected world-shadow
  residency; separately trace vehicle/light ownership before generic admission.
  Use WD2's shipped 12-dynamic/four-static high profile as the first coherent
  validation point, then validate 16 and 24 as extended profiles.
- Report: `docs/WD2_SHADOW_LIGHTING_BUDGET_REPORT.md`.

# 2026-08-13 - fresh-chat handoff consolidation

- Preserved the previous full handoff byte-for-byte as
  private archive `Archive/AgentState/handoff_full_2026-08-13_before_fresh_chat.md`.
- Replaced root `handoff.md` with a concise authoritative continuation centered
  on the in-progress v0.7.9 test, exact crash/no-crash response, confirmed WD1
  linked limits, WD2 comparison, and two-track next direction.
- Updated stale `AGENTS.md` entry text so a fresh agent is not directed back to
  the superseded v1.2.12/pass-17 state.

# 2026-08-13 - v0.7.9 black-world crash and v0.8.0

- Preserved the clean PID 27652 log, matching dump, original JXR and decoded PNG
  in `Research/Logs/v0.7.9_black_world_crash_pid27652`.
- Rejected v0.7.9 for stability after frequent black-world states and a later
  access violation at `Disrupt +0x1300A0`; pitching the camera upward could
  restore normal lighting.
- Confirmed v0.7.9's early queue construction, extra maps/passes, owner reserve
  and face clamp all operated successfully. Peak admitted load was 20 entries,
  23 faces and seven extra maps with no physical overflow.
- Confirmed the final fault is the same relocated-tail add-ref family as v0.7.8.
  Static reconstruction verifies the expanded allocation and direct writer, but
  does not yet identify the late overwriter.
- Fully mapped the normal adaptive scheduler: constructor B4=4, lower floor 4
  and upper cap 8. v0.7.9 patched only constructor/lower to 17, explaining the
  observed 17/8 oscillation.
- Built v0.8.0 with coherent 12/12 dynamic bounds, logical static-cache target
  four, physical owner reserve eight, and read-only pre/post-render queue-tail
  telemetry. All validated 24-map layout and safety patches remain.
- Packaged exact root-layout ZIP
  `Installables/ShadowEnginePatch_Coherent12x4_v0.8.0.zip`; ZIP SHA
  `C40EAB3...50A`, ASI SHA `9642900B...4EB`. In-game test pending.

# 2026-08-13 - v0.8.0 controlled-session result

- Preserved PID 13908's clean log in
  `Research/Logs/v0.8.0_five_minute_stable_tree_coarse_pid13908`.
- Logged 258.495 seconds through renderer call 41,901 with no matching dump or
  Event 1000. The user reported no black world and no crash.
- Confirmed coherent B4=12/A8=4, physical owner reserve eight, zero owner-base
  movement/failure, zero tail-invalid markers and zero null-resource guards.
- Runtime reached 15 entries, 18 faces and two extra maps without a face clamp
  or overflow; added capacity was used but the 24-face pool was not exhausted.
- User observed one brief tree-test shadow loss and repeatable coarse demotion
  with two nearby headlight cars.
- Diagnosis: remaining quality failure is upstream dynamic/cache residency, not
  missing physical maps. Do not raise A8 as a sharpness fix.
- Next isolated hypothesis: coherent B4=17 (16 ordinary dynamic positions),
  A8=4, all v0.8.0 safeguards retained. Not built or validated.
- Added hard-evidence report
  `docs/SHADOW_ENGINE_PATCH_PROVEN_CAPABILITIES_2026-08-13.md`.

# 2026-08-13 - v0.8.1 coherent 16-dynamic build

- Copied the validated v0.8.0 baseline and changed only the coherent normal B4
  profile from 12 to 17 at constructor, adaptive lower and adaptive upper sites.
- Retained A8=4, physical owner reserve eight, 24 maps, passes 17-24, 25 queue
  entries, whole-record 24-face clamp, tail probes and full candidate pass-through.
- Built twice with TinyCC; outputs are byte-identical with ASI SHA
  `58FA5E41...D929`.
- Packaged exact two-file root ZIP
  `Installables/ShadowEnginePatch_Coherent16x4_v0.8.1.zip`, SHA
  `95849518...AFE6`.
- In-game validation pending. Primary test is the identical two-car tree scene,
  followed by dense-traffic endurance and performance observation.

# 2026-08-13 - v0.8.1 black world and v0.8.2

- Preserved PID 32008's clean log in
  `Research/Logs/v0.8.1_black_world_face_clamp_pid32008`.
- User reported improved tree-shadow quality, black-world recurrence with
  camera-up recovery, and no crash.
- Logged eleven identical 22-entry/25-face requests clamped to 21 entries/24
  faces. No physical overflow reached rendering; tail, owner and null-resource
  diagnostics remained clean.
- Diagnosis: strongest current cause is split admission/render state after the
  renderer-only prefix clamp, not unchecked memory overwrite. Exact lighting
  consumer remains unproven.
- Built v0.8.2 with only coherent B4 reduced 17 -> 16, targeting 15 dynamic
  positions while retaining A8=4 and all validated v0.8.1 repairs.
- Reproducible ASI SHA `74858CA6...B5812`; exact two-file ZIP
  `Installables/ShadowEnginePatch_FaceEnvelope15x4_v0.8.2.zip`, SHA
  `59D01016...5095D`. In-game validation pending.

# 2026-08-13 - v0.8.2 falsification and v0.8.3 manual capture

- Preserved PID 33544's clean log in
  `Research/Logs/v0.8.2_black_world_no_clamp_pid33544` with SHA
  `6480C817...5905`.
- User reported black-world recurrence, no crash, and apparent improvement of the
  two-car coarse tree shadow that could not be confirmed fully through the bug.
- Session reached 18 entries, 21 faces and five added maps through map 20 with
  zero clamps, overflow, tail, owner or null-resource errors.
- Falsified the v0.8.1 face-clamp/split-state theory. Deeper added-map activation
  is now the strongest boundary correlation, but is not yet a proven cause.
- Built v0.8.3 with no behavior change from v0.8.2. F8 records the visibly black
  queue state and F9 records the camera-up recovered state.
- Two warning-free TinyCC outputs are byte-identical; ASI SHA
  `06E69CEF...372BC`.
- Packaged exact two-file root ZIP
  `Installables/ShadowEnginePatch_BlackWorldCapture_v0.8.3.zip`, SHA
  `6EE66730...C408`. In-game validation pending.

# 2026-08-13 - v0.8.3 matched black/recovered capture

- Preserved PID 28360's clean log in
  `Research/Logs/v0.8.3_black_recovered_manual_capture_pid28360`, SHA
  `6B997B75...ADBBF`.
- Two F8 black captures were both 18 entries / 21 faces through map 20. The F9
  recovered capture was five entries / eight native faces.
- All 24 handles and manager/queue/tail pointers remained identical; face objects
  were readable/non-null. Clamp, overflow, tail, owner and resource errors stayed
  zero; no matching dump exists.
- Duplicate `serial=1` labels are an instrumentation-counter defect; unique
  renderer calls preserve event order.
- Leading hypothesis moves to downstream render/pass/target state activated by
  deeper added maps. A specific defective map is not yet proven.

# 2026-08-13 - v0.8.4 physical pass-table expansion

- Recovered the direct pass store `table[category].slots[key >> 8]` and its
  allocator formula `(maximumKey >> 8) + 1`; neither path bounds-checks a store.
- Determined added Shadow/ShadowAlpha passes 17-24 require pointer slots 34-49
  and maximum key `0x310C`. Earlier builds expanded registration but not this
  physical table.
- Built v0.8.4 to expand only category `0x0C` to 50 slots at the native allocator
  boundary, validate capacity before extra registration, and verify all 16 added
  pointers directly afterward.
- Retained v0.8.3 B4=16/A8=4, maps, queue, owners, guards, full passthrough and
  F8/F9 captures. Removed nothing.
- Two TinyCC outputs are byte-identical; ASI SHA `6213CD83...A2E4A`.
- Packaged exact two-file ZIP
  `Installables/ShadowEnginePatch_PassTableExpansion_v0.8.4.zip`, SHA
  `2B750D4B...F0D9`. In-game validation pending.

# 2026-08-13 - v0.8.4 black world; pass-table theory rejected

- Preserved PID 39504 in
  `Research/Logs/v0.8.4_black_world_native64_pass_table_pid39504`, log SHA
  `E710F2C3...49FB`.
- Live registry inspection proved 64 pass slots / maximum `0x3F00`; added slots
  34-49 were all non-null/distinct. Allocator hook had zero hits because the
  table already existed. The suspected 34-slot overflow is falsified.
- F8 black: renderer 21,665, 18 entries / 21 faces through map 20. F9 recovered:
  renderer 23,103, 10 entries / 13 native faces.
- Zero face clamps, overflow, invalid tails, owner failures/movement or null face
  objects. No crash/dump/event.
- Timeout before F8 exposed 190 null acquire plus 190 symmetric release array
  entries. Exact positions/context are unlogged, so causal status is open.
- Next diagnostic: remove allocator experiment; trace bounded resource-array
  null positions and map/pass context while retaining B4=16/A8=4 and F8/F9.

# 2026-08-13 - v0.8.5 resource-lifecycle trace

- Removed only v0.8.4's inert allocator detour; retained read-only 64-slot pass
  proof and every B4=16/A8=4 map/queue/owner/guard behavior.
- Added bounded `+0x427710` null-event records with acquire/release direction,
  exact primary/secondary null-index masks and active renderer/map context.
- F8/F9 now snapshot cumulative resource totals and the latest acquire/release
  events. Renderer-call numbers replace the defective repeated capture serial.
- Two TinyCC builds are byte-identical; ASI SHA `517E6D17...0559B`.
- Packaged exact two-file ZIP
  `Installables/ShadowEnginePatch_ResourceLifecycleTrace_v0.8.5.zip`, SHA
  `C0570D21...9895`. In-game validation pending.

# 2026-08-13 - v0.8.5 proves map-20 resource boundary

- Preserved PID 12272 under
  `Research/Logs/v0.8.5_black_world_resource_boundary_pid12272`, log SHA
  `3974BCD6...D6A`.
- F8 black: 18 entries / 21 faces through map 20. F9 recovered: 16 entries /
  19 faces through map 18; added maps 16-18 remained healthy.
- All 1,295 null-resource records occurred only at 21/22 faces, none at 20 or
  fewer. At 21 faces one acquire/release entry was absent; at 22, two were absent.
- Acquire masks progressed `0x4 -> 0xC`; release was symmetric. This strongly
  indicates another physical backing pool stops after positions 0-19.
- Pass table, queue, tail, owners and face objects remained valid. No clamp,
  overflow, crash, dump or event.
- Next: statically recover this 20-position pool before a coherent 20 -> 24
  expansion. Fix local returned IDs with `atomic_add_long(...,1)+1`.

# 2026-08-14 - v0.8.6 resource-producer trace

- Recovered `Disrupt+0x427710` as a generic acquire/release array iterator and
  `+0x427860` as its outer synchronous/async wrapper.
- Exported all 22 direct wrapper callers and the likely dynamic-vector acquire /
  release pair at `+0x431B40/+0x431C40`, plus their owners/callers.
- Determined the logged `mode` value is the render-context sequence counter at
  `+0xBC8`, not a shadow-map index.
- Built v0.8.6 with a diagnostic-only wrapper detour. A 256-record bounded ring
  carries direct producer caller RVAs forward to later null dispatches.
- Fixed TinyCC local submit/event IDs with `atomic_add_long(...,1)+1`; global
  counter semantics are unchanged.
- Retained v0.8.5 B4=16/A8=4, maps, passes, queue, owner reserve, face guard,
  symmetric compaction, full passthrough and F8/F9 capture behavior unchanged.
- Two warning-clean TinyCC builds are byte-identical; ASI SHA
  `2010BEEC...817D`.
- Packaged and extraction-verified exact two-file root ZIP
  `Installables/ShadowEnginePatch_ResourceProducerTrace_v0.8.6.zip`, SHA
  `527D194D...70C9`. In-game validation pending.

# 2026-08-14 - v0.8.6 producer trace locates missing render-record fields

- Preserved clean PID 36520 under
  `Research/Logs/v0.8.6_black_world_producer_located_pid36520`; log SHA
  `46DD9839...CB94`. The game exited normally and produced no matching dump.
- F8 black was 18 entries / 21 faces through map 20. F9 recovered was 15
  entries / 18 faces through map 17, so added maps 16-17 were active after
  recovery.
- All 1,199 dispatcher nulls matched producer-side nulls. The outer wrapper and
  async copy did not introduce them.
- Exact mapping: `record+0xF8` is absent at 21 faces; `record+0xF8/+0x100` are
  absent at 22. Independent acquire/release callsites agree.
- The old `+0x431B40/+0x431C40` vector-pool hypothesis is rejected.
- Both fields normally receive results from shared-texture lookup/create
  `Disrupt+0x430A80` in frame builder `+0x3DC780`. Static code exposes no normal
  null return, so a fixed 20-slot pool overflow is not proven.
- Next diagnostic follows one record pointer from both assignment sites to
  renderer acquire to distinguish skipped/late population from reset/reuse.

# 2026-08-14 - v0.8.7 comprehensive render-record lifecycle trace built

- Built the requested one-run diagnostic around the full failing record
  lifecycle: constructor `+0x3E4420`, builder `+0x3DC780`, shared-texture lookup
  `+0x430A80`, renderer acquire `+0x3E0F80`, and release `+0x3C1350`.
- The lookup hook identifies the exact `+0x100` return at `+0x3DDD9C` and
  `+0xF8` return at `+0x3DDFC0`; builder-thread context ties them to the record.
- A bounded 2,048-record table stores generations and stage values; 32 thread
  slots connect nested lookups. F8/F9 emit the latest null record's lineage.
- Per-builder-cycle fields are cleared at entry, preventing stale lookup,
  renderer or release values from an earlier reuse masquerading as current data.
- All five hooks are signature-validated, fail closed, and required before
  `STAGE_M_COMPLETE` can claim `renderRecordLifecycleTraceActive=1`.
- Retained all v0.8.6 rendering/capacity behavior unchanged. Removed nothing.
  Regression risk is diagnostic hot-path overhead and larger failure logs.
- Two warning-clean TinyCC builds are byte-identical; ASI SHA
  `C0ABDA5E9D5F897530CD61F7BDE7A361A54A7E2EA247EBB0177E1AC4F0E56DEB`.
- Exact two-entry ZIP verified as `bin/dinput8.dll` and
  `bin/ShadowEnginePatch.asi`: `Installables/ShadowEnginePatch_RenderRecordLifecycleTrace_v0.8.7.zip`,
  SHA `9648A29674FB36B28479A877BED1C8F39389D89ADAD652DFC258A7F200DCEA9B`.
- Status: compiled and package-verified; in-game validation pending.

# 2026-08-14 - v0.8.7 proves post-builder render-record overwrite

- Preserved PID 37428 under
  `Research/Logs/v0.8.7_black_world_clear_between_builder_renderer_pid37428`,
  log SHA `4C0DE923...4315B`.
- Every targeted shared-texture lookup returned non-null and no builder exited
  with a missing `+0xF8/+0x100` field.
- The same record/generation then reached renderer acquire with `+0xF8` null at
  21 faces or both `+0xF8/+0x100` null at 22. No constructor or release
  intervened.
- By the last F9 capture there were 783 renderer/release null pairs, zero builder
  nulls and zero tracking misses.
- F8 black: 18 entries/21 faces/maps 0-20. F9 recovered twice: 14 entries/17
  faces/maps 0-16. No matching crash artifact was found.
- Static layout places a 20-qword region at `+0x58..+0xF0`, immediately followed
  by `+0xF8/+0x100`; the threshold matches an indexed spill exactly. The trace
  proves the overwrite transition, not the concrete virtual-method store.
- Next: same-cycle restoration of only the missing fields immediately before
  renderer acquire, with strict lineage checks and explicit repair logs.

# 2026-08-14 - v0.8.8 same-cycle pre-acquire repair built

- Retained the complete v0.8.7 24-map/24-face engine patch, B4=16/A8=4 owner
  profile, full candidate passthrough, compaction and five-stage lifecycle trace.
- Added a renderer-entry repair that restores only an erased `+0xF8/+0x100`
  pointer matching the same cycle's builder-exit and lookup values.
- Repair requires a stable generation, matching builder/renderer call ID, flag
  1 and no prior release. Generation/call/release state is checked again at the
  write boundary, and the final record value must match the saved pointer.
- Every repair attempt emits `STAGE_O_PRE_ACQUIRE_REPAIR`; F8/F9 summaries add
  repair, rejection, stale, unverified and post-write-failure totals.
- Removed nothing and changed no capacity, scheduler or selection policy.
- Two warning-clean TinyCC builds are byte-identical; ASI SHA
  `D094B1D4...2769C`.
- Exact two-entry installable:
  `Installables/ShadowEnginePatch_PreAcquireRecordRepair_v0.8.8.zip`, SHA
  `987DFDA7...E7E78`. Status: built and package-verified; untested in game.

# 2026-08-14 - v0.8.8 test exposes non-null overwrite form

- Preserved PID 33332 under
  `Research/Logs/v0.8.8_black_world_flicker_nonnull_overwrite_pid33332`, log SHA
  `95FFE1F0...3F48B`.
- User reported no crash, but black world remained and rapidly flickered between
  normal and black.
- All 288 null repair attempts succeeded with zero rejection or post-write
  failure. Release and downstream wrapper null counts were zero.
- Five logged `+0xF8` fields and six `+0x100` fields instead held the same rogue
  non-null pointer `0x4728B360`; v0.8.8 intentionally left them untouched.
- F8 black and F9 recovered were both 18 entries/21 faces/maps 0-20, so flicker
  occurred at identical admitted load.
- Conclusion: indexed overflow data alternates between null and non-null. Next
  repair must preserve non-null displaced resources in a sidecar acquire/release
  path while restoring both special lighting fields to verified builder values.

# 2026-08-14 - v0.8.9 non-null sidecar repair built

- Retained the complete v0.8.8 engine expansion, B4=16/A8=4 quality profile,
  owner reserve, 24-face whole-record boundary, full candidate passthrough,
  compaction, lifecycle tracing and manual F8/F9 captures.
- Changed pre-acquire repair to compare both special fields with the verified
  same-cycle builder/lookup values, covering both null and non-null overwrites.
- Distinct displaced non-null values are preserved in a bounded two-resource
  sidecar, acquired through native wrapper `+0x427860`, and released after the
  matching native record release for the same generation.
- Context, lineage, sidecar-state, compare-exchange and final-value checks fail
  closed. A rejected partial restoration is rolled back if no sidecar was
  submitted.
- Removed nothing and changed no capacity, scheduler, map, pass, queue, owner or
  candidate-policy value.
- Two warning-clean TinyCC builds are byte-identical. Source SHA
  `F8794F20...395FA`; ASI SHA `D6224404...415C0`.
- Exact two-entry installable:
  `Installables/ShadowEnginePatch_NonNullSidecarRepair_v0.8.9.zip`, SHA
  `7C9DADB0...AD02`. Status: built and package-verified; untested in game.

# 2026-08-14 - v0.8.9 test greatly improves black world but lowers FPS

- Preserved PID 45180 under
  `Research/Logs/v0.8.9_major_improvement_fps_regression_pid45180`; log SHA
  `60342772...56ED`.
- User reported major black-world improvement, possibly one unconfirmed instant
  flicker, and a major FPS drop. No crash was reported and no manual capture was
  possible for the possible flicker.
- All 8,299 repair events were accepted. The 5,785 non-null sidecar acquisitions
  have exactly 5,785 successful releases, with zero rejection, post-write
  failure, rollback or release failure.
- The log is 6.5 MB / 15,154 lines in about 241.637 seconds. Repair and sidecar
  release lines are 92.94% of output. Sidecars also added 11,570 native wrapper
  calls. Both are credible FPS costs; this run cannot separate them.
- Next controlled build retains repair behavior but sparsifies successful event
  logs. A same-scene FPS A/B against v0.8.9 will isolate diagnostic overhead.

# 2026-08-14 - v0.9.0 modular source refactor built

- Preserved every v0.8.9 runtime behavior and split its 2,903-line C monolith
  into a small unity aggregator plus eight responsibility-based modules.
- Before changing the embedded label, the modular source produced the exact
  tested v0.8.9 ASI SHA `D6224404...415C0`.
- The final 76,288-byte v0.9.0 ASI differs in only 42 bytes: two same-length
  version labels and the resulting PE checksum. No executable-code range changed.
- Added module dependency/ownership documentation, a modularization audit and a
  reproducible TinyCC build script.
- Retained the known v0.8.9 FPS regression intentionally. Removed nothing.
- Two warning-clean builds are byte-identical; ASI SHA `23334CBE...BE2`.
- Exact two-entry installable:
  `Installables/ShadowEnginePatch_ModularRefactor_v0.9.0.zip`, SHA
  `87FA1D54...60D3`. Status: package-verified; untested in game.

# 2026-08-14 - ShadowEngine Git/CLion root and canonical OKF

- Created the active `ShadowEngine` development root and connected it to
  `https://github.com/temdah/Shadow-Engine`.
- Added the CLion indexing project and retained TinyCC `build.ps1` as the
  authoritative release build path.
- Repository-local double builds reproduced the v0.9.0 ASI SHA
  `23334CBE...BE2`.
- Removed personal agent, project-status and continuation files from public Git
  history while retaining them locally through `.gitignore`.
- Established `ShadowEngine/docs/okf` as the sole technical knowledge base.
  Raw logs, dumps, proprietary material and other private evidence remain in
  the workspace and are referenced through stable evidence IDs.
