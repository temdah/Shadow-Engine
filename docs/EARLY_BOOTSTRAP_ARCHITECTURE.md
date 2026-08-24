# Early Shadow Bootstrap Architecture

Status: historical design and binary-analysis result, 2026-08-13. The accepted
v1.2.0 runtime uses the later validated bootstrap path; preserve this document
as provenance rather than current implementation instructions.

## v0.1.1 timing result

The minimally patched NexusTools loader launched the game successfully and the
second recorded process reached the complete marker with zero engine mutation.

Observed sequence:

```text
bootstrap entry / Disrupt already mapped
Troplo helper loaded successfully
RunGame interception active                         2167.538 ms
OriginalRunGame pointer published                   2167.958 ms
NexusTools ready                                    5709.278 ms
timing probe complete
```

The interval between `OriginalRunGame` publication and Nexus readiness was
approximately **3541.32 ms**. Troplo's wrapper waits for Nexus readiness before
calling the real RunGame. This is a deterministic pre-RunGame hook-installation
window: Disrupt code is mapped and addressable, while its native engine startup
has not yet been released.

This does not yet prove the resource constructor hook fires. Stage B must install
logging-only constructor detours during this interval and retain native
16-resource behavior.

Preserved evidence:
`Research/Logs/v0.1.1_early_bootstrap_timing/ShadowEarlyBootstrap.log`, SHA-256
`37E2B26DCC54939B46F7343671D3BEFF06949ABE23E3069CF25CD2EEEEA27EDF`.

## Why this path is required

v1.2.26 proved that the first late call which attempts to create `ShadowMap16`
crashes before returning. The added render-pass records can be registered late,
but the backing shadow-map resources must be created during Disrupt's native
renderer/resource initialization. NexusTools loads the current shadow ASI after
that lifecycle window.

## Reconstructed loader chain

The installed chain is:

`Watch_Dogs.exe -> bin/dinput8.dll -> TroploAsiInjectionHelper.asi -> Disrupt RunGame -> NexusTools`

Preserved baselines live in `Research/BootstrapBaselines`.

- `dinput8.dll` contains a hard-coded UTF-16
  `TroploAsiInjectionHelper.asi` target. It is not a proven generic ASI scanner.
- `TroploAsiInjectionHelper.asi` exports and uses:
  `HookedGetProcAddress`, `HookedRunGame`, `HookedRunGameEx`,
  `HookedEntryPoint`, and `InitializationThread`.
- Its `HookedGetProcAddress` recognizes the decorated and undecorated
  `RunGame`/`RunGameEx` names and substitutes Troplo's wrapper.
- Troplo's wrapper initializes `TroploNexusTools.ipe`, waits for NexusTools to
  become ready, then calls the real Disrupt `RunGame`.

This means a deterministic pre-`RunGame` boundary already exists. It is a much
better target than a late NexusTools ASI or a loader-notification callback.

## Preferred implementation

Use a reversible chain bootstrap in two stages:

1. Patch a copy of the current `dinput8.dll` so its hard-coded helper filename
   points to a small `ShadowEarlyBootstrap.asi` chain DLL. Do not replace the
   dinput proxy implementation or its DirectInput exports.
2. `ShadowEarlyBootstrap.asi` loads the untouched
   `TroploAsiInjectionHelper.asi` and interposes Troplo's exported
   `HookedRunGame` and `HookedRunGameEx` wrappers.
3. At the first wrapper entry, while Disrupt is mapped but before the real
   `RunGame` executes, install the shadow resource-constructor and render-pass
   constructor hooks.
4. Call Troplo's original wrapper. NexusTools then starts normally, but the
   shadow hooks are already armed when Disrupt constructs the renderer.

This preserves the current launcher/NexusTools integration and avoids executing
engine functions from `DllMain` or from `LdrRegisterDllNotification`, both of
which run under loader-lock restrictions.

## Staged validation

Do not begin by creating resources.

### Stage A: timing-only chain probe

- Load the untouched Troplo helper.
- Interpose its RunGame wrappers.
- Log module bases, thread IDs, and the entry/exit order.
- Make no Disrupt code or data changes.
- Pass control through unchanged.

Acceptance: game and NexusTools load normally; the log proves the bootstrap
wrapper runs before the real RunGame.

**Result: passed in v0.1.1.** The log contains two appended process sessions.
The first ended before readiness; the second recorded interception,
OriginalRunGame publication, Nexus readiness and the final success marker.

### Stage B: constructor-hook reachability

- Install only the resource-constructor and pass-registration detours.
- Log that both hooks execute during native initialization.
- Preserve the native 16-resource behavior; create no extra resources.

Acceptance: constructor hooks fire exactly once before the first shadow-manager
or renderer call and gameplay remains stable.

### Stage C: early extra resources without extra demand

- During the native constructor window, create `ShadowMap16..23` and register
  pass pairs 17..24.
- Keep B4=8 and A8=8 so the scheduler cannot request the extra maps yet.
- Log 24 non-zero, distinct handles and verify normal gameplay.

### Stage D: operational 24-map proof

- Set B4=17, keep A8=8, retain pass 16 for LongRangeShadow.
- Retain the already mapped 24-entry handle/layout and 25-entry queue repairs.
- Log the first actual uses of map/face indices 16..23, queue entries 17..24,
  and pass pairs 17..24.
- Guard any predicted or observed demand beyond those physical envelopes.

Only Stage D can support the claim that sixteen ordinary high-resolution
dynamic positions are operational.

## Safety and rollback

- Never overwrite the user's root loader automatically.
- Ship the preserved original hashes, a backup, an install script which refuses
  unknown hashes, and a one-step restore script.
- The first test package must be timing-only.
- Keep the rejected nearest-vehicle/Aiden limiter out of this branch.
- Keep `TroploAsiInjectionHelper.asi` and `TroploNexusTools.ipe` untouched.
- Do not call engine resource builders from `DllMain`, a loader-notification
  callback, or a late renderer callback.

## User-facing package layout

The final release can still use a simple drag-and-drop archive. Its root mirrors
the Watch Dogs installation directory:

```text
WatchDogs_ShadowEnginePatch.zip
|-- bin
|   |-- dinput8.dll
|   |-- ShadowEarlyBootstrap.asi
|   `-- ShadowEnginePatch
|       |-- ShadowEnginePatch.asi
|       `-- version.json
|-- README.txt
`-- Restore_NexusTools_Loader.cmd
```

The user opens the Watch Dogs directory (the folder containing `bin` and
`data_win64`) and extracts the archive there. The archive must not create an
extra outer project folder.

For public distribution, `dinput8.dll` should be our own small DirectInput proxy
which forwards the original DirectInput exports to the Windows system DLL,
loads `ShadowEarlyBootstrap.asi`, and then preserves the Troplo/NexusTools
chain. Do not redistribute a modified third-party Troplo binary.

During development, prefer an installer/restore pair even if the final package
is drag-and-drop. The installer can hash and back up the existing loader before
replacement; plain ZIP extraction cannot make a reliable backup of the file it
overwrites. Once the proxy is proven compatible with the supported NexusTools
version, the public instructions can be reduced to extract, confirm overwrite,
and launch.

The archive must include a visible compatibility notice: it owns
`bin/dinput8.dll`, so another mod which also replaces that proxy must be chained
explicitly rather than installed over it.

## Alternative

The cleanest long-term integration would be an official pre-RunGame callback in
Troplo's helper. If its developer adds one, use that instead of interposing the
exported wrappers. The chain bootstrap remains a viable independent research
path and can prove the required lifecycle before proposing that integration.
