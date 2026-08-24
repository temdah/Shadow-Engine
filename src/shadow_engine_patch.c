/*
 * Shadow Engine Patch unity build.
 *
 * Modules are ordered by dependency and compile into one ASI. Keeping a
 * single translation unit preserves the tested TinyCC ABI, static linkage,
 * initialization order, hook trampolines, and byte-level behavior.
 */

#include "modules/00_shared_config_state.inc"
#include "modules/05_runtime_profiles.inc"
#include "modules/10_runtime_primitives.inc"
#include "modules/15_patch_transaction.inc"
#include "modules/20_manager_owner_profile.inc"
#include "modules/30_renderer_queue_diagnostics.inc"
#include "modules/40_external_slice_results.inc"
#include "modules/60_engine_expansion.inc"
#include "modules/70_bootstrap_orchestration.inc"
