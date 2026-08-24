# Shadow Engine

Shadow Engine is an engine-level shadow-capacity and stability patch for the
original 2014 Windows release of *Watch Dogs*.

It does not add vehicle shadows by itself. It expands and repairs the native
shadow pipeline so compatible lighting mods can request more dynamic shadows
without displacing visible world shadows as aggressively.

## What it changes

- Constructs 30 physical local shadow maps instead of the native 16.
- Registers fourteen additional paired shadow and alpha render-pass groups.
- Expands the physical render queue from 17 to 31 entries.
- Pre-reserves storage for eight native shadow-owner records.
- Routes added SliceExecute results through external storage with balanced
  native release handling.
- Preserves the complete native light candidate chain without broad spotlight
  or vehicle filtering.
- Targets 20 ordinary high-quality dynamic positions with a 30-face physical
  boundary and whole-record overflow protection.
- Fails closed on unknown or partially matching game builds and produces a
  diagnostic log instead of installing unverified hooks.

Shadow Engine increases capacity and stability. It does not increase shadow
distance, add headlights, replace weather, or guarantee unlimited shadows.

Version 2.0.0 retains the five Global, Shev, VMPless, Asia/Miru and Complete
Edition runtime layouts in one ASI. Its corrected 30-map capacity policy passed
a frozen-candidate runtime matrix on all five layouts. v1.2.2 is rejected
because two internal mapping arrays were relocated with the wrong per-map
stride, preventing shadow jobs from dispatching.

## Requirements

- The original 2014 Windows PC release of *Watch Dogs*.
- A complete installation of
  [NexusTools 1.1.12 or newer](https://www.nexusmods.com/watchdogs/mods/491).
- The Fall of Windy City II and Windy City Addons for the showcased
  vehicle-headlight configuration.

NexusTools belongs to Troplo and is not redistributed by Shadow Engine.
Replacing only `dinput8.dll` is not a complete NexusTools installation.

## Installation

1. Close *Watch Dogs*.
2. Install NexusTools completely.
3. Install The Fall of Windy City II and Windy City Addons according to their
   instructions.
4. Extract the Shadow Engine archive into the folder containing
   `Watch_Dogs.exe` and allow its `bin` directory to merge.
5. Confirm this file exists:

   `Watch_Dogs\bin\ShadowEnginePatch.asi`

6. Launch the game normally.

On a successful start, Shadow Engine creates
`Watch_Dogs\bin\ShadowEnginePatch.log`. For v2.0.0, the log should identify
`version=2.0.0` and contain `STAGE_M_COMPLETE`.

In WATCH_DOGS Mod Manager, place Windy City Addons below The Fall of Windy
City II in priority so the Addons combined DynamicLightPrefab database wins the
conflict. Shadow Engine is an ASI engine patch and is not ordered in that list.

## Updating and uninstalling

To update, close the game and replace `bin\ShadowEnginePatch.asi` with the new
release file. To uninstall, close the game and delete that ASI. The generated
log may also be deleted.

## Reporting an issue

Preserve the complete, unedited `bin\ShadowEnginePatch.log` before launching
the game again. Include your game/store version, installed lighting, traffic
and graphics mods, reproduction steps, visible symptoms, approximate FPS and
whether the game actually crashed. A missing shutdown marker alone does not
prove a crash.

If practical, press **F8** while a visual failure is present and **F9** after it
recovers. Extremely fast flicker may be impossible to capture; describe what
you saw instead.

Reports can be sent through Nexus Mods or the
[Watch Dogs Modding Discord](https://discord.gg/AQnVwkZ7k).

[Video showcase](https://www.youtube.com/watch?v=4fGO-WEQN-Q)

## Source and evidence

The source is published for technical review and community research. This
repository does not redistribute Ubisoft binaries, NexusTools, game dumps or
decompilation exports. Claims are classified according to the
[evidence policy](docs/EVIDENCE_POLICY.md), with detailed research retained in
the [OKF knowledge index](docs/okf/index.md).

No open-source license has been selected. Normal copyright therefore applies.
This project is unaffiliated with Ubisoft. *Watch Dogs* and Disrupt are Ubisoft
properties.
