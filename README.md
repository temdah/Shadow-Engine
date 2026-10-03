[Watch Dogs OKF — engine research, experiments and modding knowledge](https://github.com/temdah/Watch_Dogs_OKF)

# Shadow Engine

An engine-level shadow mod for the original Windows PC release of **Watch Dogs**.
It expands the native local-shadow pipeline and manages shadow resources so
compatible lighting mods can make use of more dynamic shadows.

Shadow Engine patches the engine; lighting mods supply the lights and assets.
Shadow Engine Addons and The Fall of Windy City II are separate projects.

## Install

1. Install a complete [NexusTools](https://www.nexusmods.com/watchdogs/mods/491) setup.
2. Close the game and extract a [Shadow Engine release](https://github.com/temdah/Shadow-Engine/releases)
   into the game folder, merging its `bin` directory.
3. Confirm `bin/ShadowEnginePatch.asi` exists, then launch normally.

Install any accompanying Shadow Engine tools ZIP separately through NexusTools.
To uninstall, close the game and remove `bin/ShadowEnginePatch.asi`.

## Compatibility and support

Executable checks determine whether the patch can activate. A loaded ASI does
not by itself confirm activation; consult `bin/ShadowEnginePatch.log` when
reporting a problem. Include the Shadow Engine and NexusTools versions, the log,
and a short description of what happened. Experimental builds need in-game testing.

## Development

See [BUILDING.md](BUILDING.md) for the native build. Technical findings and failed
experiments are maintained in the separate [Watch Dogs OKF](https://github.com/temdah/Watch_Dogs_OKF).

Created by Tim with AI-assisted development. NexusTools is by Troplo and is not
bundled. This project is unaffiliated with Ubisoft. No open-source license has
been selected; normal copyright applies.
