# Shadow Engine v1.2.0 behavior-neutral refactor

This workspace starts from the exact tested v1.2.0 release. Engine capacity,
residency, scheduling, routing, light admission, and diagnostic policy remain
unchanged until the refactor equivalence gates pass.

Completed structural slice:

- Replaced five profile-selection branches with one immutable `RuntimeProfile`
  table and a small selected-profile state object.
- Encapsulated clustered versus explicit RVA resolution and the per-profile
  frame-builder prologue.
- Removed profile-number checks from engine expansion and bootstrap.
- Renamed module 40 around its active responsibility and removed the compiled,
  uninstalled historical repair, sidecar, task-provenance and frame-graph code.
- Removed the compiled but uninstalled resource lifecycle trace module.
- Reduced `LifecycleRecordSnapshot` to the generation and external-result state
  used by the v1.2.0 hooks.
- Split early bootstrap into an explicit read-only patch-plan preparation phase
  and a mutation-only commit phase; deferred hooks require the same plan.
- Added a baseline comparison test plus the private 88-check Asia capture gate.
- Added separate early and deferred patch transactions. Every executable write
  is journaled before mutation; failures restore bytes in reverse order, clear
  published trampoline pointers and release transaction allocations.
- Added a standalone native fault-injection harness for rollback and commit.
- Consolidated mutable runtime storage into one `ShadowEngineContext` with
  bootstrap, hook-binding, manager/renderer, resource/pass and external-result
  ownership boundaries.
- Replaced all 98 transitional global-name aliases with 457 explicit subsystem
  field accesses. The ASI is byte-identical before and after that rewrite.
- Split the former 1,239-line bootstrap module into 678 lines of runtime
  preflight, 278 lines of patch orchestration and 287 lines of runtime entry /
  completion monitoring. Patch orchestration now owns ordering, not profile
  recovery or lifecycle reporting implementations.

Deliberately unchanged:

- Version label `1.2.0`.
- B4=16, A8=4, owner reserve 8, 24 maps and 25 queue entries.
- All active signatures, 46 tail relocations and 77 Asia mappings.
- External results for indices 17-23, their consumer and release fallback.
- Full native light passthrough and unknown-build fail-closed behavior.

Remaining architectural debt:

- Early construction and deferred downstream installation remain separate
  lifecycle phases; each phase is now internally transactional. Engine objects
  created between phases are deliberately not treated as reversible code
  mutations.
- The root context remains one process-lifetime static object because plain-C
  detour callbacks need stable storage without object construction or teardown.
- The unity build remains required until a runtime-tested interface boundary is
  available.
