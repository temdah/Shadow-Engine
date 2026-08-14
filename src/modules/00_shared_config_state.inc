/* Module 00: platform includes, engine constants, signatures, shared types,
 * bounded state, and forward declarations. No hook is installed here. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define PATCH_VERSION "0.9.0-stage-p-modular-unity-refactor"
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
#define RENDER_QUEUE_PROCESS_RVA 0x00306E60ULL
#define FACE_COST_READER_RVA     0x002C0490ULL
#define RESOURCE_SUBMIT_RVA      0x00427710ULL
#define RESOURCE_WRAPPER_RVA     0x00427860ULL
#define FRAME_BUILDER_RVA        0x003DC780ULL
#define RENDER_RECORD_ACQUIRE_RVA 0x003E0F80ULL
#define RENDER_RECORD_RELEASE_RVA 0x003C1350ULL
#define RENDER_RECORD_CONSTRUCTOR_RVA 0x003E4420ULL
#define SHARED_TEXTURE_LOOKUP_RVA 0x00430A80ULL
#define FIELD_100_LOOKUP_RETURN_RVA 0x003DDD9CULL
#define FIELD_F8_LOOKUP_RETURN_RVA  0x003DDFC0ULL
#define SOURCE_CONFIG_LOOKUP_RVA 0x0030ACC9ULL
#define FACE_OBJECT_BUILD_RVA    0x0030CB99ULL
#define RENDER_QUEUE_COUNT_OFFSET 0x39798U
#define RENDER_QUEUE_TAIL_OFFSET  0x397A0U
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
#define RESOURCE_TRACE_INITIAL_EVENTS 64U
#define RESOURCE_TRACE_PERIOD       64U
#define RESOURCE_PRODUCER_SLOTS    256U
#define LIFECYCLE_RECORD_SLOTS      2048U
#define LIFECYCLE_THREAD_SLOTS       32U
#define LIFECYCLE_TRACE_INITIAL     128U
#define LIFECYCLE_TRACE_PERIOD      128U
#define LIFECYCLE_LOOKUP_LOG_INITIAL 32U
#define LIFECYCLE_LOOKUP_LOG_PERIOD 8192U

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
typedef void (__fastcall *ResourceSubmitFn)(void *,unsigned char,void **,
    uint32_t,void **,uint32_t,uint64_t,uint32_t);
typedef int64_t (__fastcall *ResourceWrapperFn)(unsigned char,void **,
    uint32_t,void **,uint32_t,unsigned char *,uint64_t,uint32_t);
typedef uint32_t (__fastcall *FaceCostReaderFn)(void *);
typedef void (__fastcall *FrameBuilderFn)(void *,void *,void *,uint64_t,
    uint64_t,uint64_t,uint64_t);
typedef void (__fastcall *RenderRecordAcquireFn)(void *,void *,void *);
typedef void (__fastcall *RenderRecordReleaseFn)(void *,void *,unsigned char);
typedef void *(__fastcall *RenderRecordConstructorFn)(void *);
typedef void *(__fastcall *SharedTextureLookupFn)(void *,uint32_t,void *);
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

typedef struct ResourceNullSnapshot {
    volatile LONG serial;
    LONG submit_call;
    LONG renderer_context;
    LONG renderer_call;
    uint32_t entries;
    uint32_t faces;
    uint32_t highest_map;
    void *manager;
    void *queue;
    void **primary;
    void **secondary;
    uint32_t primary_count;
    uint32_t primary_valid;
    uint32_t secondary_count;
    uint32_t secondary_valid;
    uint64_t primary_null_mask;
    uint64_t secondary_null_mask;
    uint64_t argument7;
    uint32_t mode;
    uint32_t active;
    LONG producer_found;
    LONG producer_call;
    void *producer_caller;
    uint64_t producer_caller_rva;
    uint32_t producer_primary_valid;
    uint32_t producer_secondary_valid;
    uint64_t producer_primary_null_mask;
    uint64_t producer_secondary_null_mask;
} ResourceNullSnapshot;

typedef struct ResourceProducerSnapshot {
    volatile LONG serial;
    void *manager;
    void *caller;
    uint64_t caller_rva;
    uint32_t mode;
    uint32_t active;
    uint32_t primary_count;
    uint32_t primary_valid;
    uint32_t secondary_count;
    uint32_t secondary_valid;
    uint64_t primary_null_mask;
    uint64_t secondary_null_mask;
} ResourceProducerSnapshot;

typedef struct LifecycleRecordSnapshot {
    void *volatile record;
    volatile LONG generation;
    volatile LONG constructor_call;
    volatile LONG builder_call;
    volatile LONG builder_thread;
    volatile LONG builder_enter_flag;
    volatile LONG builder_exit_flag;
    void *volatile builder_enter_f8;
    void *volatile builder_enter_100;
    void *volatile builder_exit_f8;
    void *volatile builder_exit_100;
    volatile LONG lookup_f8_call;
    volatile LONG lookup_100_call;
    volatile LONG lookup_f8_thread;
    volatile LONG lookup_100_thread;
    void *volatile lookup_f8_result;
    void *volatile lookup_100_result;
    volatile LONG renderer_call;
    volatile LONG renderer_thread;
    volatile LONG renderer_flag;
    void *volatile renderer_f8;
    void *volatile renderer_100;
    volatile LONG release_call;
    volatile LONG release_thread;
    volatile LONG release_flag;
    void *volatile release_f8;
    void *volatile release_100;
    volatile LONG sidecar_generation;
    volatile LONG sidecar_count;
    void *volatile sidecar_resource_0;
    void *volatile sidecar_resource_1;
    volatile LONG sidecar_acquired;
    volatile LONG sidecar_acquire_mode;
    volatile LONG sidecar_release_mode;
} LifecycleRecordSnapshot;

typedef struct LifecycleThreadContext {
    volatile LONG thread_id;
    volatile LONG active;
    void *volatile record;
    volatile LONG builder_call;
} LifecycleThreadContext;

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
static const unsigned char g_resource_submit_prologue[15] = {
    0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,0x24,0x10,
    0x48,0x89,0x74,0x24,0x18
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
static const unsigned char g_render_record_acquire_prologue[15] = {
    0x48,0x89,0x5C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,
    0x48,0x89,0x7C,0x24,0x20
};
static const unsigned char g_render_record_release_prologue[20] = {
    0x48,0x89,0x5C,0x24,0x18,0x56,0x57,0x41,0x54,0x48,
    0x83,0xEC,0x60,0x44,0x8B,0x89,0x90,0x13,0x00,0x00
};
static const unsigned char g_render_record_constructor_prologue[14] = {
    0x45,0x33,0xC0,0x48,0x8D,0x41,0x70,0x41,0x8D,0x50,0x02,0x4C,0x89,0x01
};
static const unsigned char g_shared_texture_lookup_prologue[15] = {
    0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,0x24,0x18,
    0x48,0x89,0x74,0x24,0x20
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

static HMODULE g_self;
static LONG g_initialized;
static BootstrapSnapshot g_snapshot;
static unsigned char *g_disrupt_base;
static ResourceConstructorFn g_original_resource_constructor;
static RegisterPassFn g_original_register_pass;
static ShadowManagerFn g_original_manager;
static ProfileCallbackFn g_profile_callback;
static OwnerVectorReserveFn g_owner_vector_reserve;
static RendererQueueFn g_original_renderer_queue;
static ResourceSubmitFn g_original_resource_submit;
static ResourceWrapperFn g_original_resource_wrapper;
static FaceCostReaderFn g_original_face_cost_reader;
static FrameBuilderFn g_original_frame_builder;
static RenderRecordAcquireFn g_original_render_record_acquire;
static RenderRecordReleaseFn g_original_render_record_release;
static RenderRecordConstructorFn g_original_render_record_constructor;
static SharedTextureLookupFn g_original_shared_texture_lookup;
static GetAsyncKeyStateFn g_get_async_key_state;
static volatile LONG g_profile_state;
static volatile LONG g_manager_calls;
static volatile LONG g_downstream_state;
static volatile LONG g_live_a8_ready_logged;
static volatile LONG g_renderer_hook_state;
static volatile LONG g_renderer_calls;
static volatile LONG g_extra_face_use_seen;
static volatile LONG g_null_face_cost_skips;
static volatile LONG g_null_submit_drops;
static volatile LONG g_null_acquire_drops;
static volatile LONG g_null_release_drops;
static volatile LONG g_owner_reserve_state;
static volatile LONG g_owner_reserve_fail_closed;
static volatile LONG g_owner_base_changed_during_manager;
static volatile LONG g_owner_capacity_before;
static volatile LONG g_owner_capacity_after;
static volatile LONG g_owner_reserve_failure_logged;
static volatile LONG g_face_budget_clamps;
static volatile LONG g_face_budget_dropped_entries;
static volatile LONG g_face_budget_peak_requested;
static volatile LONG g_tail_invalid_before_renderer;
static volatile LONG g_tail_invalid_after_renderer;
static volatile LONG g_pass_table_state;
static volatile LONG g_pass_table_stored_max_key;
static volatile LONG g_pass_table_slots;
static volatile LONG g_pass_table_added_nonzero;
static volatile LONG g_pass_table_added_distinct;
static void *g_pass_table_base;
static volatile LONG g_resource_submit_calls;
static volatile LONG g_resource_wrapper_calls;
static volatile LONG g_null_resource_events;
static volatile LONG g_null_events_with_renderer_context;
static volatile LONG g_null_events_with_extra_maps;
static volatile LONG g_first_null_extra_logged;
static volatile LONG g_active_renderer_context;
static volatile LONG g_active_renderer_call;
static volatile LONG g_active_renderer_entries;
static volatile LONG g_active_renderer_faces;
static volatile LONG g_active_renderer_highest_map;
static void *g_active_renderer_queue;
static ResourceNullSnapshot g_last_null_acquire;
static ResourceNullSnapshot g_last_null_release;
static ResourceProducerSnapshot g_resource_producers[RESOURCE_PRODUCER_SLOTS];
static LifecycleRecordSnapshot g_lifecycle_records[LIFECYCLE_RECORD_SLOTS];
static LifecycleThreadContext g_lifecycle_threads[LIFECYCLE_THREAD_SLOTS];
static volatile LONG g_lifecycle_constructor_calls;
static volatile LONG g_lifecycle_builder_calls;
static volatile LONG g_lifecycle_lookup_calls;
static volatile LONG g_lifecycle_lookup_f8_calls;
static volatile LONG g_lifecycle_lookup_100_calls;
static volatile LONG g_lifecycle_renderer_calls;
static volatile LONG g_lifecycle_release_calls;
static volatile LONG g_lifecycle_builder_null_events;
static volatile LONG g_lifecycle_renderer_null_events;
static volatile LONG g_lifecycle_release_null_events;
static volatile LONG g_lifecycle_record_slot_misses;
static volatile LONG g_lifecycle_thread_slot_misses;
static volatile LONG g_pre_acquire_repair_events;
static volatile LONG g_pre_acquire_repair_f8;
static volatile LONG g_pre_acquire_repair_100;
static volatile LONG g_pre_acquire_repair_rejected;
static volatile LONG g_pre_acquire_repair_stale;
static volatile LONG g_pre_acquire_repair_unverified;
static volatile LONG g_pre_acquire_repair_postwrite_failure;
static volatile LONG g_pre_acquire_nonnull_f8;
static volatile LONG g_pre_acquire_nonnull_100;
static volatile LONG g_sidecar_acquire_events;
static volatile LONG g_sidecar_acquire_resources;
static volatile LONG g_sidecar_release_events;
static volatile LONG g_sidecar_release_resources;
static volatile LONG g_sidecar_context_failures;
static volatile LONG g_sidecar_symmetry_failures;
static volatile LONG g_sidecar_reset_while_acquired;
static void *volatile g_last_lifecycle_null_record;
static volatile LONG g_lifecycle_hooks_state;
static volatile LONG g_f8_was_down;
static volatile LONG g_f9_was_down;
static volatile LONG g_session_log_state;
static DWORD g_last_residency_probe_tick;
static DWORD g_last_residency_log_tick;
static volatile LONG g_hooks_installed;
static volatile LONG g_resource_hits;
static volatile LONG g_register_hits;
static volatile LONG g_shadow_hits;
static volatile LONG g_shadow_alpha_hits;
static volatile LONG g_last_native_shadow_seen;
static volatile LONG g_last_native_alpha_seen;
static volatile LONG g_native_nonzero_handles;
static volatile LONG g_native_distinct_handles;
static volatile LONG g_range_resource_state;
static volatile LONG g_range_pass_state;
static volatile LONG g_range_nonzero_handles;
static volatile LONG g_range_distinct_handles;
static volatile LONG g_routing_state;
static volatile LONG g_observed_scheduler_b4;
static volatile LONG g_observed_cache_a8;
static void *g_last_resource_manager;
static uint32_t g_native_handles[ORIGINAL_LOCAL_MAPS];
static uint32_t g_extra_handles[EXTRA_LOCAL_MAPS];
static uint32_t g_shadow_map_ids[TOTAL_LOCAL_MAPS];
static unsigned char g_extra_companions[EXTRA_LOCAL_MAPS][16]
    __attribute__((aligned(16)));
static uint64_t g_extra_shadow_names[EXTRA_LOCAL_MAPS];
static uint64_t g_extra_alpha_names[EXTRA_LOCAL_MAPS];

static void __fastcall hooked_shadow_manager(void *manager,void *candidates,
    uint32_t candidate_count,void *bindings,void *context);
static void __fastcall hooked_renderer_queue(void *manager,void *queue,
    uint64_t argument3,void *argument4,void *argument5,uint64_t argument6,
    uint64_t argument7,uint64_t argument8,uint64_t argument9);
static void __fastcall hooked_resource_submit(void *manager,unsigned char active,
    void **primary,uint32_t primary_count,void **secondary,
    uint32_t secondary_count,uint64_t argument7,uint32_t mode);
static int64_t __fastcall hooked_resource_wrapper(unsigned char active,
    void **primary,uint32_t primary_count,void **secondary,
    uint32_t secondary_count,unsigned char *context,uint64_t argument7,
    uint32_t mode);
static uint32_t __fastcall hooked_face_cost_reader(void *queue);
static void __fastcall hooked_frame_builder(void *,void *,void *,uint64_t,
    uint64_t,uint64_t,uint64_t);
static void __fastcall hooked_render_record_acquire(void *,void *,void *);
static void __fastcall hooked_render_record_release(void *,void *,unsigned char);
static void *__fastcall hooked_render_record_constructor(void *);
static void *__fastcall hooked_shared_texture_lookup(void *,uint32_t,void *);
static void log_manual_lifecycle_snapshot(const char *state,LONG serial);
static int readable_memory(const void *address,size_t size);
