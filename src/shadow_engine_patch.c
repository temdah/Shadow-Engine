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
#if SHADOW_ENGINE_OVERHEAD_MEASUREMENT
#include "modules/12_overhead_measurement.inc"
#endif
#if SHADOW_ENGINE_INTERNAL_DIAGNOSTICS
#include "modules/14_periodic_summary.inc"
#endif
#include "modules/15_patch_transaction.inc"
#include "modules/16_policy_controls.inc"
#include "modules/17_saved_settings.inc"
#if SHADOW_ENGINE_INTERNAL_DIAGNOSTICS
#include "modules/17_intersection_diagnostics.inc"
#endif
#include "modules/18_vehicle_owner_lifetime.inc"
#include "modules/18_shadow_resolution_diagnostics.inc"
#include "modules/18_vehicle_driver_identity.inc"
#include "modules/18_vehicle_light_diagnostics.inc"
#include "modules/19_world_light_quality.inc"
#if SHADOW_ENGINE_INTERNAL_DIAGNOSTICS
#include "modules/19_external_completion_diagnostics.inc"
#include "modules/19_workload_present.inc"
#include "modules/19_workload_capture.inc"
#include "modules/19_population_capture.inc"
#endif
#include "modules/20_manager_owner_profile.inc"
#include "modules/30_renderer_queue_diagnostics.inc"
#include "modules/40_external_slice_results.inc"
#include "modules/60_engine_expansion.inc"
#include "modules/65_runtime_preflight.inc"
#if SHADOW_ENGINE_INTERNAL_DIAGNOSTICS
#include "modules/66_lua_frame_compatibility.inc"
#endif
#include "modules/70_bootstrap_orchestration.inc"
#if SHADOW_ENGINE_INTERNAL_DIAGNOSTICS
#include "modules/75_internal_tools.inc"
#include "modules/76_support_report.inc"
#endif
#include "modules/80_runtime_entry.inc"
