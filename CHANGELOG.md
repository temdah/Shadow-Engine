# Changelog

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
