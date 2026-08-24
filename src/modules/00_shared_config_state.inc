/* Module 00: platform includes, engine constants, signatures, shared types,
 * bounded state, and forward declarations. No hook is installed here. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define PATCH_VERSION "1.2.0"
#define DOWNSTREAM_STABILIZE_MS 1000U
#define POLL_INTERVAL_MS 10U
#define POLL_TIMEOUT_MS 60000U
#define RESOURCE_CONSTRUCTOR_RVA 0x00308DB0ULL
#define REGISTER_PASS_RVA        0x002BFF60ULL
#define MAP_LOOP_STORE_RVA       0x00309050ULL
#define MAP_LOOP_BOUND_RVA       0x003090B9ULL
#define QUEUE_COUNT_INIT_RVA     0x002E2236ULL
#define QUEUE_CLEAR_A_SIZE_RVA   0x00306ECDULL
#define QUEUE_CLEAR_B_SIZE_RVA   0x00306EEFULL
#define PASS_INDEX_LOOKUP_RVA    0x003073E2ULL
#define PASS_INDEX_STORE_RVA     0x00308441ULL
#define HANDLE_LOAD_EDX_RVA      0x00307168ULL
#define HANDLE_LOAD_ECX_RVA      0x003071ABULL
#define HANDLE_ADDRESS_RDX_RVA   0x00308389ULL
#define SCHEDULER_INIT_RVA       0x002E8E89ULL
#define SCHEDULER_FLOOR_RVA      0x002E9C21ULL
#define SCHEDULER_UPPER_RVA      0x002E9C3BULL
#define SHADOW_MANAGER_RVA       0x002E9B60ULL
#define PROFILE_CALLBACK_RVA     0x00308A90ULL
#define OWNER_VECTOR_RESERVE_RVA 0x00319160ULL
#define OWNER_VECTOR_RESERVE_CALL_RVA 0x00308B53ULL
#define RENDER_QUEUE_PROCESS_RVA 0x00306E60ULL
#define FACE_COST_READER_RVA     0x002C0490ULL
#define RESOURCE_SUBMIT_RVA      0x00427710ULL
#define RESOURCE_WRAPPER_RVA     0x00427860ULL
#define FRAME_BUILDER_RVA        0x003DC780ULL
#define FRAME_BUILDER_END_RVA    0x003DF833ULL
#define SLICE_RESULT_NONNULL_RVA  0x002CC300ULL
#define SLICE_RESULT_NULL_RVA     0x002CC31CULL
#define SLICE_RESULT_EPILOGUE_RVA 0x002CC333ULL
#define POST_CALL_TASK_CONSTRUCTOR_RVA 0x003E69D0ULL
#define POST_CALL_TASK_EXECUTE_RVA 0x003E4DF0ULL
#define POST_CALL_OBJECT_RENDERERS_RVA 0x003CA980ULL
#define EXECUTE_FRAME_GRAPH_DISPATCH_RVA 0x003BA460ULL
#define RENDER_RECORD_ACQUIRE_RVA 0x003E0F80ULL
#define RENDER_RECORD_RELEASE_RVA 0x003C1350ULL
#define RENDER_RECORD_CONSTRUCTOR_RVA 0x003E4420ULL
#define SHARED_TEXTURE_LOOKUP_RVA 0x00430A80ULL
#define FIELD_100_LOOKUP_RETURN_RVA 0x003DDD9CULL
#define FIELD_F8_LOOKUP_RETURN_RVA  0x003DDFC0ULL
#define FIELD_110_LOOKUP_RETURN_RVA 0x003DE059ULL
#define SOURCE_CONFIG_LOOKUP_RVA 0x0030ACC9ULL
#define FACE_OBJECT_BUILD_RVA    0x0030CB99ULL
#define RENDER_QUEUE_COUNT_OFFSET 0x39798U
#define RENDER_QUEUE_TAIL_OFFSET  0x397A0U
#define RENDER_QUEUE_RESULT_OFFSET 0x397B0U
#define RENDER_QUEUE_ENTRY_BYTES  0x24C0U
#define ORIGINAL_LOCAL_MAPS 16U
#define EXTRA_LOCAL_MAPS 8U
#define TOTAL_LOCAL_MAPS 24U
#define TARGET_DYNAMIC_B4 16U
#define TARGET_CACHE_A8 4U
#define OWNER_VECTOR_RESERVE_CAPACITY 8U
#define SHADOW_PASS_CATEGORY      0x0CU
#define REQUIRED_PASS_MAX_KEY     0x310CU
#define REQUIRED_PASS_TABLE_SLOTS 50U
#define FIRST_EXTRA_PASS_SLOT     34U
#define EXTRA_PASS_SLOT_COUNT     16U
#define LIFECYCLE_RECORD_SLOTS      2048U
#define EXTERNAL_SLICE_SUMMARY_PERIOD_MS 30000U
#define NATIVE_SLICE_RESULT_COUNT 17U
#define EXTRA_SLICE_RESULT_COUNT (TOTAL_LOCAL_MAPS-NATIVE_SLICE_RESULT_COUNT)

#define STRING_ASSIGN_RVA         0x000D60B0ULL
#define STRING_VALUE_RVA          0x000CCEB0ULL

typedef void (__fastcall *ResourceConstructorFn)(void *manager);
typedef void (__fastcall *RegisterPassFn)(void *registry, uint32_t key,
    uint64_t name_value, const char *category, uint32_t mode, uint32_t enabled);
typedef uint64_t (__fastcall *StringValueFn)(void *reference);
typedef void (__fastcall *StringAssignFn)(uint64_t *destination,const char *value);
typedef void (__fastcall *ShadowManagerFn)(void *,void *,uint32_t,void *,void *);
typedef void (__fastcall *ProfileCallbackFn)(void *);
typedef void (__fastcall *OwnerVectorReserveFn)(void *,uint64_t);
typedef void (__fastcall *RendererQueueFn)(void *,void *,uint64_t,void *,void *,
    uint64_t,uint64_t,uint64_t,uint64_t);
typedef int64_t (__fastcall *ResourceWrapperFn)(unsigned char,void **,
    uint32_t,void **,uint32_t,unsigned char *,uint64_t,uint32_t);
typedef uint32_t (__fastcall *FaceCostReaderFn)(void *);
typedef void (__fastcall *FrameBuilderFn)(void *,void *,void *,uint64_t,
    uint64_t,uint64_t,uint64_t);
typedef void (__fastcall *PostCallObjectRenderersFn)(void *,uint64_t,
    unsigned char,int,unsigned char,uint64_t,void *,uint64_t,uint64_t,uint64_t);
typedef void (__fastcall *RenderRecordReleaseFn)(void *,void *,unsigned char);
typedef void *(__fastcall *RenderRecordConstructorFn)(void *);
typedef SHORT (WINAPI *GetAsyncKeyStateFn)(int);

typedef struct TailPatch {
    uint64_t instruction_rva;
    uint32_t old_value;
    uint32_t new_value;
} TailPatch;

typedef struct BootstrapSnapshot {
    LARGE_INTEGER frequency;
    LARGE_INTEGER initialize_time;
    LARGE_INTEGER troplo_return_time;
    DWORD initialize_thread;
    HMODULE proxy;
    HMODULE disrupt_before;
    HMODULE troplo;
    DWORD troplo_error;
    wchar_t bin_directory[MAX_PATH];
} BootstrapSnapshot;

typedef struct LifecycleRecordSnapshot {
    void *volatile record;
    volatile LONG generation;
    volatile LONG constructor_call;
    volatile LONG builder_call;
    volatile LONG extra_slice_result_generation;
    volatile LONG extra_slice_result_builder_call;
    volatile LONG extra_slice_result_written_mask;
    volatile LONG extra_slice_result_consumer_state;
    volatile LONG extra_slice_result_active_writers;
    void *volatile extra_slice_results[EXTRA_SLICE_RESULT_COUNT];
} LifecycleRecordSnapshot;

static const unsigned char g_resource_prologue[15] = {
    0x48,0x8B,0xC4,0x55,0x53,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57
};
static const unsigned char g_register_pass_prologue[15] = {
    0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,0x24,0x10,
    0x48,0x89,0x74,0x24,0x18
};
static const unsigned char g_manager_prologue[14] = {
    0x48,0x85,0xD2,0x0F,0x84,0xE6,0x03,0x00,0x00,
    0x48,0x8B,0xC4,0x53,0x57
};
static const unsigned char g_renderer_queue_prologue[14] = {
    0x4C,0x89,0x4C,0x24,0x20,0x48,0x89,0x54,0x24,0x10,0x55,0x53,0x56,0x57
};
static const unsigned char g_resource_wrapper_prologue[16] = {
    0x88,0x4C,0x24,0x08,0x41,0x54,0x41,0x55,0x41,0x56,
    0x41,0x57,0x48,0x83,0xEC,0x48
};
static const unsigned char g_face_cost_reader_prologue[14] = {
    0x48,0x89,0x5C,0x24,0x08,0x45,0x33,0xD2,
    0x45,0x33,0xDB,0x48,0x8B,0xD9
};
static const unsigned char g_frame_builder_prologue[14] = {
    0x48,0x8B,0xC4,0x4C,0x89,0x48,0x20,0x48,0x89,0x50,0x10,0x55,0x53,0x56
};
static const unsigned char g_frame_builder_asia_prologue[15] = {
    0x48,0x8B,0xC4,0x4C,0x89,0x48,0x20,0x4C,0x89,0x40,0x18,
    0x48,0x89,0x50,0x10
};
static const unsigned char g_post_call_object_renderers_prologue[15] = {
    0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,0x24,0x10,
    0x48,0x89,0x74,0x24,0x18
};
static const unsigned char g_render_record_release_prologue[20] = {
    0x48,0x89,0x5C,0x24,0x18,0x56,0x57,0x41,0x54,0x48,
    0x83,0xEC,0x60,0x44,0x8B,0x89,0x90,0x13,0x00,0x00
};
static const unsigned char g_render_record_constructor_prologue[14] = {
    0x45,0x33,0xC0,0x48,0x8D,0x41,0x70,0x41,0x8D,0x50,0x02,0x4C,0x89,0x01
};
/* The queue owns one entry per local map plus the preserved long-range pass.
 * These are the complete downstream tail-offset relocations recovered and
 * exercised by the old v1.2.16 line: 16/17 becomes 24/25. */
static const TailPatch g_tail_patches[] = {
    { 0x2C049EULL, 0x27158, 0x39798 },
    { 0x2C04E2ULL, 0x27158, 0x39798 },
    { 0x2CBF18ULL, 0x27168, 0x397A8 },
    { 0x2CC150ULL, 0x27168, 0x397A8 },
    { 0x2CC1B9ULL, 0x27168, 0x397A8 },
    { 0x2CC1C4ULL, 0x27168, 0x397A8 },
    { 0x2CC1D6ULL, 0x27168, 0x397A8 },
    { 0x2CC1E8ULL, 0x27168, 0x397A8 },
    { 0x2CC1FAULL, 0x27168, 0x397A8 },
    { 0x2CC27FULL, 0x27168, 0x397A8 },
    { 0x2CC291ULL, 0x27168, 0x397A8 },
    { 0x2CC2A3ULL, 0x27168, 0x397A8 },
    { 0x2CC2C3ULL, 0x27168, 0x397A8 },
    { 0x2CC300ULL, 0x27170, 0x397B0 },
    { 0x2CC31CULL, 0x27170, 0x397B0 },
    { 0x2D608DULL, 0x27170, 0x397B0 },
    { 0x2D60B2ULL, 0x27168, 0x397A8 },
    { 0x2D61D1ULL, 0x27168, 0x397A8 },
    { 0x2D6269ULL, 0x27158, 0x39798 },
    { 0x2E225CULL, 0x27158, 0x39798 },
    { 0x2E2262ULL, 0x27160, 0x397A0 },
    { 0x2E2269ULL, 0x27168, 0x397A8 },
    { 0x2E2270ULL, 0x27170, 0x397B0 },
    { 0x2E2277ULL, 0x2715C, 0x3979C },
    { 0x304F7FULL, 0x27168, 0x397A8 },
    { 0x304FC3ULL, 0x27168, 0x397A8 },
    { 0x305018ULL, 0x27168, 0x397A8 },
    { 0x3050C8ULL, 0x2715C, 0x3979C },
    { 0x306EAEULL, 0x27168, 0x397A8 },
    { 0x306EB9ULL, 0x270D0, 0x396D0 },
    { 0x306EF3ULL, 0x27114, 0x39734 },
    { 0x306F64ULL, 0x27158, 0x39798 },
    { 0x307285ULL, 0x270D0, 0x396D0 },
    { 0x30728CULL, 0x27114, 0x39734 },
    { 0x308A01ULL, 0x27158, 0x39798 },
    { 0x30A5F8ULL, 0x27158, 0x39798 },
    { 0x30ABC0ULL, 0x27158, 0x39798 },
    { 0x30D1BDULL, 0x27160, 0x397A0 },
    { 0x30D222ULL, 0x27158, 0x39798 },
    { 0x3C6D1DULL, 0x27160, 0x397A0 },
    { 0x3CA3E2ULL, 0x27180, 0x397C0 },
    { 0x3CA441ULL, 0x2715C, 0x3979C },
    { 0x3CA76FULL, 0x27160, 0x397A0 },
    { 0x3CA77AULL, 0x27160, 0x397A0 },
    { 0x3E072FULL, 0x27160, 0x397A0 },
    { 0x3E07E7ULL, 0x27160, 0x397A0 }
};


typedef struct BootstrapState {
    HMODULE self;
    LONG initialized;
    volatile LONG nexus_init_calls;
    volatile LONG nexus_entry_kind;
    volatile LONG dllmain_fallback_attempts;
    void *volatile nexus_interface;
    BootstrapSnapshot snapshot;
    unsigned char *disrupt_base;
    volatile LONG downstream_state;
    volatile LONG session_log_state;
    volatile LONG hooks_installed;
} BootstrapState;

typedef struct HookBindings {
    ResourceConstructorFn original_resource_constructor;
    RegisterPassFn original_register_pass;
    ShadowManagerFn original_manager;
    ProfileCallbackFn profile_callback;
    OwnerVectorReserveFn owner_vector_reserve;
    RendererQueueFn original_renderer_queue;
    ResourceWrapperFn original_resource_wrapper;
    FaceCostReaderFn original_face_cost_reader;
    FrameBuilderFn original_frame_builder;
    PostCallObjectRenderersFn original_post_call_object_renderers;
    RenderRecordReleaseFn original_render_record_release;
    RenderRecordConstructorFn original_render_record_constructor;
    GetAsyncKeyStateFn get_async_key_state;
} HookBindings;

typedef struct ManagerRendererState {
    volatile LONG profile_state;
    volatile LONG manager_calls;
    volatile LONG live_a8_ready_logged;
    volatile LONG renderer_hook_state;
    volatile LONG renderer_calls;
    volatile LONG extra_face_use_seen;
    volatile LONG null_face_cost_skips;
    volatile LONG owner_reserve_state;
    volatile LONG owner_reserve_fail_closed;
    volatile LONG owner_base_changed_during_manager;
    volatile LONG owner_capacity_before;
    volatile LONG owner_capacity_after;
    volatile LONG owner_reserve_failure_logged;
    volatile LONG face_budget_clamps;
    volatile LONG face_budget_dropped_entries;
    volatile LONG face_budget_peak_requested;
    volatile LONG tail_invalid_before_renderer;
    volatile LONG tail_invalid_after_renderer;
    volatile LONG observed_scheduler_b4;
    volatile LONG observed_cache_a8;
    volatile LONG f8_was_down;
    volatile LONG f9_was_down;
    DWORD last_residency_probe_tick;
    DWORD last_residency_log_tick;
} ManagerRendererState;

typedef struct ResourcePassState {
    volatile LONG pass_table_state;
    volatile LONG pass_table_stored_max_key;
    volatile LONG pass_table_slots;
    volatile LONG pass_table_added_nonzero;
    volatile LONG pass_table_added_distinct;
    void *pass_table_base;
    volatile LONG resource_hits;
    volatile LONG register_hits;
    volatile LONG shadow_hits;
    volatile LONG shadow_alpha_hits;
    volatile LONG last_native_shadow_seen;
    volatile LONG last_native_alpha_seen;
    volatile LONG native_nonzero_handles;
    volatile LONG native_distinct_handles;
    volatile LONG range_resource_state;
    volatile LONG range_pass_state;
    volatile LONG range_nonzero_handles;
    volatile LONG range_distinct_handles;
    volatile LONG routing_state;
    void *last_resource_manager;
    uint32_t native_handles[ORIGINAL_LOCAL_MAPS];
    uint32_t extra_handles[EXTRA_LOCAL_MAPS];
    uint32_t shadow_map_ids[TOTAL_LOCAL_MAPS];
    unsigned char extra_companions[EXTRA_LOCAL_MAPS][16]
        __attribute__((aligned(16)));
    uint64_t extra_shadow_names[EXTRA_LOCAL_MAPS];
    uint64_t extra_alpha_names[EXTRA_LOCAL_MAPS];
} ResourcePassState;

typedef struct ExternalResultState {
    LifecycleRecordSnapshot lifecycle_records[LIFECYCLE_RECORD_SLOTS];
    volatile LONG lifecycle_constructor_calls;
    volatile LONG lifecycle_builder_calls;
    volatile LONG lifecycle_release_calls;
    volatile LONG lifecycle_record_slot_misses;
    volatile LONG postcall_calls;
    volatile LONG slice_result_guard_state;
    volatile LONG slice_result_native_nonnull;
    volatile LONG slice_result_native_null;
    volatile LONG slice_result_external_nonnull;
    volatile LONG slice_result_external_null;
    volatile LONG slice_result_external_store_failures;
    volatile LONG slice_result_external_duplicate_indices;
    volatile LONG slice_result_external_duplicate_resources;
    volatile LONG slice_result_external_stale_lineage;
    volatile LONG slice_result_external_late_writes;
    volatile LONG slice_result_external_consumed_results;
    volatile LONG slice_result_external_release_events;
    volatile LONG slice_result_external_release_resources;
    volatile LONG slice_result_external_release_failures;
    volatile LONG slice_result_external_release_fallbacks;
    volatile LONG slice_result_external_reset_pending;
    volatile LONG last_external_slice_summary_tick;
    volatile LONG lifecycle_hooks_state;
} ExternalResultState;

typedef struct ShadowEngineContext {
    BootstrapState bootstrap;
    HookBindings hooks;
    ManagerRendererState renderer;
    ResourcePassState resources;
    ExternalResultState external_results;
} ShadowEngineContext;

static ShadowEngineContext g_shadow_engine;

/* Transitional aliases preserve the already-validated hook expressions while
 * storage ownership moves into cohesive subsystem contexts. Remove aliases one
 * module at a time only after the regional runtime matrix validates this layout. */
#define g_self g_shadow_engine.bootstrap.self
#define g_initialized g_shadow_engine.bootstrap.initialized
#define g_nexus_init_calls g_shadow_engine.bootstrap.nexus_init_calls
#define g_nexus_entry_kind g_shadow_engine.bootstrap.nexus_entry_kind
#define g_dllmain_fallback_attempts g_shadow_engine.bootstrap.dllmain_fallback_attempts
#define g_nexus_interface g_shadow_engine.bootstrap.nexus_interface
#define g_snapshot g_shadow_engine.bootstrap.snapshot
#define g_disrupt_base g_shadow_engine.bootstrap.disrupt_base
#define g_downstream_state g_shadow_engine.bootstrap.downstream_state
#define g_session_log_state g_shadow_engine.bootstrap.session_log_state
#define g_hooks_installed g_shadow_engine.bootstrap.hooks_installed
#define g_original_resource_constructor g_shadow_engine.hooks.original_resource_constructor
#define g_original_register_pass g_shadow_engine.hooks.original_register_pass
#define g_original_manager g_shadow_engine.hooks.original_manager
#define g_profile_callback g_shadow_engine.hooks.profile_callback
#define g_owner_vector_reserve g_shadow_engine.hooks.owner_vector_reserve
#define g_original_renderer_queue g_shadow_engine.hooks.original_renderer_queue
#define g_original_resource_wrapper g_shadow_engine.hooks.original_resource_wrapper
#define g_original_face_cost_reader g_shadow_engine.hooks.original_face_cost_reader
#define g_original_frame_builder g_shadow_engine.hooks.original_frame_builder
#define g_original_post_call_object_renderers g_shadow_engine.hooks.original_post_call_object_renderers
#define g_original_render_record_release g_shadow_engine.hooks.original_render_record_release
#define g_original_render_record_constructor g_shadow_engine.hooks.original_render_record_constructor
#define g_get_async_key_state g_shadow_engine.hooks.get_async_key_state
#define g_profile_state g_shadow_engine.renderer.profile_state
#define g_manager_calls g_shadow_engine.renderer.manager_calls
#define g_live_a8_ready_logged g_shadow_engine.renderer.live_a8_ready_logged
#define g_renderer_hook_state g_shadow_engine.renderer.renderer_hook_state
#define g_renderer_calls g_shadow_engine.renderer.renderer_calls
#define g_extra_face_use_seen g_shadow_engine.renderer.extra_face_use_seen
#define g_null_face_cost_skips g_shadow_engine.renderer.null_face_cost_skips
#define g_owner_reserve_state g_shadow_engine.renderer.owner_reserve_state
#define g_owner_reserve_fail_closed g_shadow_engine.renderer.owner_reserve_fail_closed
#define g_owner_base_changed_during_manager g_shadow_engine.renderer.owner_base_changed_during_manager
#define g_owner_capacity_before g_shadow_engine.renderer.owner_capacity_before
#define g_owner_capacity_after g_shadow_engine.renderer.owner_capacity_after
#define g_owner_reserve_failure_logged g_shadow_engine.renderer.owner_reserve_failure_logged
#define g_face_budget_clamps g_shadow_engine.renderer.face_budget_clamps
#define g_face_budget_dropped_entries g_shadow_engine.renderer.face_budget_dropped_entries
#define g_face_budget_peak_requested g_shadow_engine.renderer.face_budget_peak_requested
#define g_tail_invalid_before_renderer g_shadow_engine.renderer.tail_invalid_before_renderer
#define g_tail_invalid_after_renderer g_shadow_engine.renderer.tail_invalid_after_renderer
#define g_observed_scheduler_b4 g_shadow_engine.renderer.observed_scheduler_b4
#define g_observed_cache_a8 g_shadow_engine.renderer.observed_cache_a8
#define g_f8_was_down g_shadow_engine.renderer.f8_was_down
#define g_f9_was_down g_shadow_engine.renderer.f9_was_down
#define g_last_residency_probe_tick g_shadow_engine.renderer.last_residency_probe_tick
#define g_last_residency_log_tick g_shadow_engine.renderer.last_residency_log_tick
#define g_pass_table_state g_shadow_engine.resources.pass_table_state
#define g_pass_table_stored_max_key g_shadow_engine.resources.pass_table_stored_max_key
#define g_pass_table_slots g_shadow_engine.resources.pass_table_slots
#define g_pass_table_added_nonzero g_shadow_engine.resources.pass_table_added_nonzero
#define g_pass_table_added_distinct g_shadow_engine.resources.pass_table_added_distinct
#define g_pass_table_base g_shadow_engine.resources.pass_table_base
#define g_resource_hits g_shadow_engine.resources.resource_hits
#define g_register_hits g_shadow_engine.resources.register_hits
#define g_shadow_hits g_shadow_engine.resources.shadow_hits
#define g_shadow_alpha_hits g_shadow_engine.resources.shadow_alpha_hits
#define g_last_native_shadow_seen g_shadow_engine.resources.last_native_shadow_seen
#define g_last_native_alpha_seen g_shadow_engine.resources.last_native_alpha_seen
#define g_native_nonzero_handles g_shadow_engine.resources.native_nonzero_handles
#define g_native_distinct_handles g_shadow_engine.resources.native_distinct_handles
#define g_range_resource_state g_shadow_engine.resources.range_resource_state
#define g_range_pass_state g_shadow_engine.resources.range_pass_state
#define g_range_nonzero_handles g_shadow_engine.resources.range_nonzero_handles
#define g_range_distinct_handles g_shadow_engine.resources.range_distinct_handles
#define g_routing_state g_shadow_engine.resources.routing_state
#define g_last_resource_manager g_shadow_engine.resources.last_resource_manager
#define g_native_handles g_shadow_engine.resources.native_handles
#define g_extra_handles g_shadow_engine.resources.extra_handles
#define g_shadow_map_ids g_shadow_engine.resources.shadow_map_ids
#define g_extra_companions g_shadow_engine.resources.extra_companions
#define g_extra_shadow_names g_shadow_engine.resources.extra_shadow_names
#define g_extra_alpha_names g_shadow_engine.resources.extra_alpha_names
#define g_lifecycle_records g_shadow_engine.external_results.lifecycle_records
#define g_lifecycle_constructor_calls g_shadow_engine.external_results.lifecycle_constructor_calls
#define g_lifecycle_builder_calls g_shadow_engine.external_results.lifecycle_builder_calls
#define g_lifecycle_release_calls g_shadow_engine.external_results.lifecycle_release_calls
#define g_lifecycle_record_slot_misses g_shadow_engine.external_results.lifecycle_record_slot_misses
#define g_postcall_calls g_shadow_engine.external_results.postcall_calls
#define g_slice_result_guard_state g_shadow_engine.external_results.slice_result_guard_state
#define g_slice_result_native_nonnull g_shadow_engine.external_results.slice_result_native_nonnull
#define g_slice_result_native_null g_shadow_engine.external_results.slice_result_native_null
#define g_slice_result_external_nonnull g_shadow_engine.external_results.slice_result_external_nonnull
#define g_slice_result_external_null g_shadow_engine.external_results.slice_result_external_null
#define g_slice_result_external_store_failures g_shadow_engine.external_results.slice_result_external_store_failures
#define g_slice_result_external_duplicate_indices g_shadow_engine.external_results.slice_result_external_duplicate_indices
#define g_slice_result_external_duplicate_resources g_shadow_engine.external_results.slice_result_external_duplicate_resources
#define g_slice_result_external_stale_lineage g_shadow_engine.external_results.slice_result_external_stale_lineage
#define g_slice_result_external_late_writes g_shadow_engine.external_results.slice_result_external_late_writes
#define g_slice_result_external_consumed_results g_shadow_engine.external_results.slice_result_external_consumed_results
#define g_slice_result_external_release_events g_shadow_engine.external_results.slice_result_external_release_events
#define g_slice_result_external_release_resources g_shadow_engine.external_results.slice_result_external_release_resources
#define g_slice_result_external_release_failures g_shadow_engine.external_results.slice_result_external_release_failures
#define g_slice_result_external_release_fallbacks g_shadow_engine.external_results.slice_result_external_release_fallbacks
#define g_slice_result_external_reset_pending g_shadow_engine.external_results.slice_result_external_reset_pending
#define g_last_external_slice_summary_tick g_shadow_engine.external_results.last_external_slice_summary_tick
#define g_lifecycle_hooks_state g_shadow_engine.external_results.lifecycle_hooks_state

static void __fastcall hooked_shadow_manager(void *manager,void *candidates,
    uint32_t candidate_count,void *bindings,void *context);
static void __fastcall hooked_renderer_queue(void *manager,void *queue,
    uint64_t argument3,void *argument4,void *argument5,uint64_t argument6,
    uint64_t argument7,uint64_t argument8,uint64_t argument9);
static uint32_t __fastcall hooked_face_cost_reader(void *queue);
static void *__fastcall hooked_external_render_record_constructor(void *);
static void __fastcall hooked_external_frame_builder(void *,void *,void *,
    uint64_t,uint64_t,uint64_t,uint64_t);
static void __fastcall hooked_external_post_call_object_renderers(void *,
    uint64_t,unsigned char,int,unsigned char,uint64_t,void *,uint64_t,
    uint64_t,uint64_t);
static void __fastcall hooked_external_render_record_release(void *,void *,
    unsigned char);
static void log_manual_external_slice_snapshot(const char *state,LONG serial);
static int readable_memory(const void *address,size_t size);
