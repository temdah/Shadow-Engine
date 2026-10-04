# Shadow Engine validation

Run from the repository root with TinyCC 0.9.27 and the canonical builder.
All native harnesses use owned fixtures; none installs the mod or runs the game.

```powershell
python -B -m unittest discover -s tests -p 'test_*.py'
./tests/test_patch_transaction.ps1 -CompilerPath <tcc.exe>
./tests/test_external_cycle.ps1 -CompilerPath <tcc.exe>
./tests/test_intersection.ps1 -CompilerPath <tcc.exe>
./tests/test_vehicle_capture.ps1 -CompilerPath <tcc.exe>
./tests/test_driver_policy.ps1 -CompilerPath <tcc.exe>
./tests/test_saved_settings.ps1 -CompilerPath <tcc.exe>
./tests/test_quality_processing.ps1 -CompilerPath <tcc.exe>
./tests/test_world_quality.ps1 -CompilerPath <tcc.exe>
```

- Transaction/lifecycle fixtures check write faults, reverse rollback, ownership,
  rejected completions and diagnostic-independent behavior.
- Vehicle/driver fixtures check full identities, grouping, expiry, conflicts,
  finite and bypass settings, driver exclusion from counts and uniform quality.
- Settings fixtures check exact formats, worker/file failures, terminal receipts,
  revision lifetime and persistence. Experimental SESAVE3 loads discard only
  valid retired background bits, preserve ordinary choices and do not rewrite
  at startup. The next edit writes SESAVE2; retired requests are rejected.
- Quality fixtures compare complete queues against frozen references, including
  lower native requests, unknown/world identities and guarded memory. Host CPU
  microbenchmarks are not in-game FPS measurements.

The Lua `internal_tools_menu.lua` fixture requires a desktop Lua interpreter,
run from the repository root. `lua_menu_runner.c` is a small adapter for an
available Lua library. It checks fake host callbacks, file transport, SE10 status,
settings, capture controls and lifetime. It does not certify the game's VM.

`test_nexus_compatibility.ps1` also requires `-KnownHost` and `-UpdatedHost`:
private, hash-checked 1.1.12 and 1.1.13 images. It reads bytes without loading
those hosts. `test_population_capture.ps1` needs the preserved Global image;
`-ArtifactDirectory` exports synthetic binary fixtures for the decoder's
`test_population_capture_decoder.py --fixtures <directory>` integration test.
Without that argument the integration case reports a deliberate skip.

Run targeted bookkeeping, capture, startup, driver-proof and regional validation
scripts when their owners change. See [BUILDING.md](../BUILDING.md) for the current
source baseline, reproducible profiles and compiled PE checks. Runtime-corpus
inputs are private: four mapped profiles have byte-exact checks; A4EE remains
manual-required. Never convert absent runtime evidence into a pass.

Historical results, engine roles and failed experiments belong in the separate
[Watch Dogs OKF](https://github.com/temdah/Watch_Dogs_OKF). Offline checks authorize
manual testing; only the user can classify visual behavior and accept a build.
