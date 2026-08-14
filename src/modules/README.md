# Unity-module architecture

`src/shadow_engine_patch.c` is the only translation unit passed to TinyCC. It
includes these modules in dependency order so the project still produces one
compact `ShadowEnginePatch.asi` without changing the tested ABI, static linkage,
initialization order or hook trampoline behavior.

The module order is a contract:

1. `00_shared_config_state.inc` owns constants, signatures, types, bounded state
   and forward declarations.
2. `10_runtime_primitives.inc` owns logging, memory checks, atomic helpers and
   generic detour installation.
3. `20_manager_owner_profile.inc` owns the manager profile and owner-vector
   pre-reservation.
4. `30_renderer_queue_diagnostics.inc` owns whole-record face admission, queue
   validation, residency sampling and F8/F9 queue capture.
5. `40_render_record_repair.inc` owns render-record lineage and black-world
   repair/sidecar lifetime.
6. `50_resource_lifecycle_trace.inc` owns resource wrapper/producer tracing and
   null acquire/release compaction.
7. `60_engine_expansion.inc` owns maps, passes, queue layout, routing relays and
   scheduler writes.
8. `70_bootstrap_orchestration.inc` owns signature preflight, phased startup and
   exported entry points.

Later modules may call earlier helpers and the forward-declared hook entry
points in module 00. Earlier modules must not depend on implementations in later
modules except through those declarations. Runtime-policy changes must stay in
their owning module instead of being added to the bootstrap orchestrator.

This is intentionally a unity build rather than eight separately linked object
files. The v0.8.9 code relies on shared static state and a tested TinyCC binary
layout. Separate translation units can be considered later, after narrow module
interfaces replace the shared-state surface and an in-game A/B validates it.
