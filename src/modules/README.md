# Unity-module architecture

`src/shadow_engine_patch.c` is the only translation unit passed to TinyCC. It
includes these modules in dependency order so the project still produces one
compact `ShadowEnginePatch.asi` without changing the tested ABI, static linkage,
initialization order or hook trampoline behavior.

The module order is a contract:

1. `00_shared_config_state.inc` owns shared engine constants, hook signatures,
   active bounded state and forward declarations.
2. `05_runtime_profiles.inc` owns the five immutable PE identities, clustered
   or explicit RVA strategy, helper RVAs and per-profile detour prologue.
3. `10_runtime_primitives.inc` owns logging, memory checks, atomic helpers and
   generic detour installation.
4. `15_patch_transaction.inc` owns journaled executable writes, relay/trampoline
   allocations, reverse-order rollback and transaction commit.
5. `20_manager_owner_profile.inc` owns the manager profile and owner-vector
   pre-reservation.
6. `30_renderer_queue_diagnostics.inc` owns whole-record face admission, queue
   validation, residency sampling and F8/F9 queue capture.
7. `40_external_slice_results.inc` owns generation-safe external SliceExecute
   result storage, consumption, release and bounded F8/F9 diagnostics.
8. `60_engine_expansion.inc` owns maps, passes, queue layout, routing relays and
   scheduler writes.
9. `70_bootstrap_orchestration.inc` owns signature preflight, phased startup and
   exported entry points.

Later modules may call earlier helpers and the forward-declared hook entry
points in module 00. Earlier modules must not depend on implementations in later
modules except through those declarations. Runtime-policy changes must stay in
their owning module instead of being added to the bootstrap orchestrator.

This remains a unity build until the refactored interfaces pass the full runtime
matrix. Regional differences must be added only through `RuntimeProfile`; core
modules must not branch on profile numbers or storefront labels.
