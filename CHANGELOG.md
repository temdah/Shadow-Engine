# Changelog

## 1.2.4

- Raises the coherent dynamic manager target from `B4=21` to `B4=25` to test
  four additional high-quality residency positions under extreme traffic and
  emergency-light pressure.
- Retains the validated v1.2.3 30-map layout, `A8=4` coarse-owner policy,
  regional profiles, hooks, queue guard, and external-result lifecycle.
- Adds an explicit compile-time and unit-test invariant that the logical cache
  ceiling cannot exceed the pre-reserved owner-vector capacity.

## 1.2.3

### Fixed

- Corrected the 30-map queue layout so the record region, first mapping array,
  and post-mapping fields grow by their actual `0x24C0`, `0x24C4`, and
  `0x24C8` per-map strides.
- Fixed v1.2.2 relocating the two renderer mapping arrays 48 and 24 bytes too
  far, which left queue records populated but prevented all shadow jobs from
  dispatching.

### Validation

- Added compile-time mapping-offset invariants and an exact three-stride check
  across all 46 relocated references.
- Added a five-profile offline runtime-corpus gate with 62 byte-exact
  capacity-site checks on four layouts and a frozen successful A4EE runtime
  attestation.
- Produced byte-identical warning-clean TinyCC builds and passed transaction
  fault injection.
- The exact frozen ASI passed Global/04DF, Shev/A4EE, VMPless, Complete Edition,
  and Asia/Miru. All five manual outcomes matched the offline compatibility
  prediction without a crash or baseline regional regression.

### Known limit

- Extreme pressure can still cause rapid dynamic spotlight/moon-shadow
  eviction and reacquisition when manager admission saturates. v1.2.3 greatly
  shortens the previous prolonged loss but does not claim unlimited shadows.

## 1.2.2 (rejected)

- Attempted the 30-map/31-entry and `B4=21` capacity expansion.
- Rejected after the first runtime test loaded with every shadow missing and
  recorded zero native or external `SliceExecute` results.

## 1.2.1

### Changed

- Refactored the runtime patch into cohesive profile, bootstrap, subsystem,
  lifecycle, diagnostics and transaction modules without changing the
  established 24-map/24-face shadow policy.
- Consolidated mutable runtime state under explicit subsystem ownership and
  reduced bootstrap to orchestration.
- Added transactional rollback for reversible patch writes so a failed commit
  does not leave a partially modified engine.

### Validation

- The v1.2.1 release ASI differs from the five-profile-tested refactor binary
  only at the two embedded version characters and the resulting PE checksum.
- Retested the unchanged refactor build on all five supported executable
  variants. All five launched, activated the patch and remained stable;
  Global/04DF included a 20-minute gameplay run.
- Extreme traffic testing identified the existing 24-face physical budget as
  the next fidelity bottleneck; v1.2.1 intentionally keeps capacity policy
  unchanged.

## 1.2.0

### Added

- Support for VMPless 1.06, Asia/Miru 1.06.329 and Complete Edition 1.06.329.
- One ASI now recognizes all five verified regional and store runtime layouts.
- Asia uses a complete independent 77-site RVA map because its protected
  layout does not preserve the relocation-cluster model used by other builds.

### Fixed

- Fixed Shadow Engine failing closed on VMPless, Asia/Miru and Complete
  Edition installations.
- Fixed the Asia frame-builder hook by selecting the active structurally
  verified function and its exact 15-byte entry prologue.
- Runtime owner-target bounds now use the selected profile's actual PE image
  size.

### Validation

- Global/04DF, Shev/A4EE, VMPless, Asia/Miru and Complete Edition all launched
  and activated the complete patch without a reported visual regression.
- The corrected Asia profile completed F9 recovery with balanced builder,
  consumer and release activity and zero integrity failures.

## 1.1.0

### Added

- Support for a second verified signed Watch Dogs runtime layout.
- Runtime profile selection followed by complete hook, helper, inline-site,
  call-target and relocation validation.
- Automatic recovery of relocated native helpers through validated callsites.
- A read-only full-site diagnostic mapper for unknown game builds.

### Fixed

- Fixed Shadow Engine failing to activate on some legitimate game
  installations whose engine layout differs from the original supported build.
- Fixed owner-storage reservation on the second supported runtime layout.
- Verified construction and use of the additional shadow maps and passes on
  both supported layouts.

### Changed

- Reduced the successful `STAGE_F_RESIDENCY` log heartbeat from once per second
  to once every 30 seconds. This changes logging only, not residency management
  or rendering frequency.
- Immediate activation, first-capacity-use, failure, timeout, integrity and
  F8/F9 diagnostics remain enabled.
- Unknown or partially matching builds now fail closed after producing a
  detailed compatibility report.

## 1.0.1

- Added compatibility with the shipped NexusTools early callback and a guarded
  deferred fallback for installations where the callback is not dispatched.
- Retained the verified 24-map expansion, render-pass registration, queue
  expansion, native light passthrough and external result-routing design.
