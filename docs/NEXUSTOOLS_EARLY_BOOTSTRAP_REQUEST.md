# NexusTools early-bootstrap request

Hi! I have been developing an engine-level shadow-capacity patch for Watch Dogs
1. During testing, I found that ordinary NexusTools ASI/Lua startup occurs after
Disrupt has already constructed and finalized its shadow-map resources and render
passes. At that point it is too late to safely extend those native structures.

We now have repeatable runtime proof that the patch works when its constructor
hooks are installed in the interval after `Disrupt_b64.dll` is mapped but before
the real `RunGame` call is released. Using that window, the game successfully
constructs eight additional shadow maps (16-23) and registers their paired render
passes (17-24) inside the engine's original construction lifecycle. A test build
ran normally for several minutes and exited without a patch crash.

At present I reach that window by using a minimally modified copy of NexusTools'
`dinput8.dll`. It is still your exact 46,592-byte proxy implementation; the only
change is its embedded UTF-16 helper filename at file offset `0x5D28`:

```text
TroploAsiInjectionHelper.asi -> ShadowEnginePatch.asi
```

Only 26 bytes differ. `ShadowEnginePatch.asi` then explicitly loads the untouched
`TroploAsiInjectionHelper.asi`, so the normal NexusTools chain continues. The
reference DLL is retained only in private research provenance and is not
published in this repository. I am not proposing that this game-specific
filename be adopted upstream.

Would you consider adding a supported generic early-bootstrap extension point to
the NexusTools loader? A minimal version could be:

1. Before loading `TroploAsiInjectionHelper.asi`, check for an optional,
   conventionally named bootstrap such as `NexusToolsEarlyBootstrap.asi` (or a
   small configured list/directory).
2. Load that bootstrap first.
3. Always continue by loading `TroploAsiInjectionHelper.asi` normally, so an
   extension does not need to replace or manually chain the helper itself.
4. Keep the existing Troplo exports/interception state available, especially
   `OriginalRunGame`, `g_RunGameIntercepted`, and `g_NexusReady`, or provide an
   equivalent documented pre-RunGame callback.
5. Make load order deterministic and fail open: if no bootstrap exists or it
   fails to load, NexusTools should continue through its current path.

The preferable long-term interface would be a documented callback invoked after
Disrupt is mapped and the real `RunGame` pointer is known, but before NexusTools
releases that call. This avoids doing engine work directly under `DllMain` and
lets extensions install hooks on their own worker thread during the valid native
construction window.

Why this is needed: late creation of the same shadow resources consistently
crashes because the native builder/registry lifetime has ended. Early in-loop
construction succeeds. A supported hook would let engine extensions coexist
with NexusTools without every mod shipping a modified `dinput8.dll`, eliminating
loader conflicts and making root installation much cleaner.

Reference binary hashes:

- Original NexusTools `dinput8.dll`:
  `D827076530406E467C593D23A6B84AC22883B853A579EA816BC10FCE1CDAA4EC`
- Modified reference `dinput8.dll`:
  `10110E4BD7A960C66B6CF55664BF9919126B096154FDCFC0D9322BEA565ED178`

The engine patch is still experimental, so I am not asking for it to be bundled
with NexusTools. I am only asking whether the generic early-load facility can be
added or discussed while development continues.
