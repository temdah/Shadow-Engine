# Contributing

The patch is still experimental and currently has no contribution license.
Discussion and evidence review are welcome, but do not submit code until a
repository license and contributor policy are selected.

When contributions open, changes will be expected to:

- separate confirmed facts from inference;
- preserve full native light passthrough unless a structurally proven owner
  policy is being tested;
- change one runtime variable at a time;
- compile warning-free with the verified TinyCC command;
- document retained, changed, removed and regressed behavior;
- avoid shipping Ubisoft binaries, dumps, decompiled source or third-party
  binaries without redistribution permission.

Do not describe renderer `SpotLight3` records as vehicle ownership. Vehicle
headlights and world spotlights converge before the currently patched manager
stage.
