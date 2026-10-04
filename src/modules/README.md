# Working with unity modules

Compile only `src/shadow_engine_patch.c` through the project build entry point;
the `.inc` files are not independent translation units. Preserve include order,
static linkage, initialization order and explicit interface ownership.

Change the owning module and its declared interface. Keep composition in the
aggregator and use the project build/validation instructions for the affected
configuration. Do not copy module bodies between experimental worktrees.

The maintained [architecture](../../../OKF/projects/shadow-engine/architecture/runtime-architecture.md) and
[function map](../../../OKF/projects/shadow-engine/architecture/function-map.md) explain responsibilities,
dependencies and evidence. Record new findings there, not as a version diary here.
