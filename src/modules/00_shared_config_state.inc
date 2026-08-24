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
#define RESOURCE_TRACE_INITIAL_EVENTS 64U
#define RESOURCE_TRACE_PERIOD       64U
#define RESOURCE_PRODUCER_SLOTS    256U
#define LIFECYCLE_RECORD_SLOTS      2048U
#define LIFECYCLE_THREAD_SLOTS       32U
#define POST_CALL_TASK_SLOTS       4096U
#define LIFECYCLE_TRACE_INITIAL     128U
#define LIFECYCLE_TRACE_PERIOD      128U
#define LIFECYCLE_LOOKUP_LOG_INITIAL 32U
#define LIFECYCLE_LOOKUP_LOG_PERIOD 8192U
#define SUCCESS_TRACE_INITIAL 16U
#define SUCCESS_TRACE_PERIOD 1024U
#define BOUNDARY_TRACE_INITIAL 32U
#define BOUNDARY_TRACE_PERIOD 1024U
#define PROVENANCE_SUMMARY_PERIOD_MS 30000U
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
typedef void (__fastcall *ResourceSubmitFn)(void *,unsigned char,void **,
    uint32_t,void **,uint32_t,uint64_t,uint32_t);
typedef int64_t (__fastcall *ResourceWrapperFn)(unsigned char,void **,
    uint32_t,void **,uint32_t,unsigned char *,uint64_t,uint32_t);
typedef uint32_t (__fastcall *FaceCostReaderFn)(void *);
typedef void (__fastcall *FrameBuilderFn)(void *,void *,void *,uint64_t,
    uint64_t,uint64_t,uint64_t);
typedef void *(__fastcall *PostCallTaskConstructorFn)(uint64_t,const char *,
    uint64_t,void *,void *,uint64_t,unsigned char,uint32_t,unsigned char,
    uint64_t,void *,uint64_t,uint64_t,uint64_t);
typedef void (__fastcall *PostCallTaskExecuteFn)(void *);
typedef void (__fastcall *PostCallObjectRenderersFn)(void *,uint64_t,
    unsigned char,int,unsigned char,uint64_t,void *,uint64_t,uint64_t,uint64_t);
typedef void (__fastcall *ExecuteFrameGraphDispatchFn)(void *,uint64_t,void *);
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
    volatile LONG builder_exit_captured;
    volatile LONG builder_exit_flag;
    void *volatile builder_enter_f8;
    void *volatile builder_enter_100;
    void *volatile builder_enter_110;
    void *volatile builder_exit_f8;
    void *volatile builder_exit_100;
    void *volatile builder_exit_108;
    void *volatile builder_exit_110;
    void *volatile builder_exit_118;
    volatile LONG lookup_f8_call;
    volatile LONG lookup_100_call;
    volatile LONG lookup_110_call;
    volatile LONG lookup_f8_thread;
    volatile LONG lookup_100_thread;
    volatile LONG lookup_110_thread;
    void *volatile lookup_f8_result;
    void *volatile lookup_100_result;
    void *volatile lookup_110_result;
    volatile LONG checkpoint_110_valid;
    volatile LONG checkpoint_110_call;
    void *volatile checkpoint_110_f8;
    void *volatile checkpoint_110_100;
    void *volatile checkpoint_110_108;
    void *volatile checkpoint_110_110;
    void *volatile checkpoint_110_118;
    volatile LONG tail_checkpoint_call;
    volatile LONG tail_checkpoint_caller_rva;
    volatile LONG tail_divergence_reported;
    void *volatile tail_checkpoint_f8;
    volatile LONG postcall_call;
    void *volatile postcall_enter_f8;
    void *volatile postcall_exit_f8;
    volatile LONG framegraph_dispatch_call;
    void *volatile framegraph_dispatch_enter_f8;
    void *volatile framegraph_dispatch_exit_f8;
    volatile LONG renderer_call;
    volatile LONG renderer_thread;
    volatile LONG renderer_flag;
    void *volatile renderer_f8;
    void *volatile renderer_100;
    void *volatile renderer_110;
    volatile LONG release_call;
    volatile LONG release_thread;
    volatile LONG release_flag;
    void *volatile release_f8;
    void *volatile release_100;
    void *volatile release_110;
    volatile LONG sidecar_generation;
    volatile LONG sidecar_count;
    void *volatile sidecar_resource_0;
    void *volatile sidecar_resource_1;
    volatile LONG sidecar_acquired;
    volatile LONG sidecar_acquire_mode;
    volatile LONG sidecar_release_mode;
    volatile LONG queued_postcall_tasks;
    volatile LONG last_queued_task_serial;
    volatile LONG extra_slice_result_generation;
    volatile LONG extra_slice_result_builder_call;
    volatile LONG extra_slice_result_written_mask;
    volatile LONG extra_slice_result_consumer_state;
    volatile LONG extra_slice_result_active_writers;
    void *volatile extra_slice_results[EXTRA_SLICE_RESULT_COUNT];
} LifecycleRecordSnapshot;

typedef struct LifecycleThreadContext {
    volatile LONG thread_id;
    volatile LONG active;
    void *volatile record;
    volatile LONG builder_call;
    volatile LONG task_execute_active;
    void *volatile task;
    void *volatile task_record;
    volatile LONG task_creation_serial;
    volatile LONG task_creation_generation;
    volatile LONG task_creation_builder_call;
    void *volatile task_expected_f8;
    void *volatile task_creation_f8;
    volatile LONG task_execute_generation;
    volatile LONG task_execute_builder_call;
    void *volatile task_execute_f8;
} LifecycleThreadContext;

typedef struct PostCallTaskProvenance {
    void *volatile task;
    volatile LONG published;
    volatile LONG creation_serial;
    void *volatile record;
    volatile LONG creation_generation;
    volatile LONG creation_builder_call;
    volatile LONG creation_builder_thread;
    volatile LONG creation_thread;
    void *volatile expected_f8;
    void *volatile creation_f8;
} PostCallTaskProvenance;

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
static const unsigned char g_frame_builder_asia_prologue[15] = {
    0x48,0x8B,0xC4,0x4C,0x89,0x48,0x20,0x4C,0x89,0x40,0x18,
    0x48,0x89,0x50,0x10
};
static const unsigned char g_post_call_task_constructor_prologue[15] = {
    0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74,0x24,0x10,
    0x57,0x48,0x83,0xEC,0x20
};
static const unsigned char g_post_call_task_execute_prologue[17] = {
    0x48,0x83,0xEC,0x58,0x48,0x8B,0x41,0x60,
    0x44,0x8B,0x49,0x34,0x44,0x0F,0xB6,0x41,0x30
};
static const unsigned char g_post_call_object_renderers_prologue[15] = {
    0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,0x24,0x10,
    0x48,0x89,0x74,0x24,0x18
};
static const unsigned char g_execute_frame_graph_dispatch_prologue[16] = {
    0x48,0x89,0x6C,0x24,0x18,0x48,0x89,0x7C,0x24,0x20,
    0x41,0x54,0x48,0x83,0xEC,0x20
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

typedef struct RuntimeRvaMap {
    uint64_t supported_rva;
    uint64_t runtime_rva;
} RuntimeRvaMap;

/* Asia/Miru does not preserve the three large relocation clusters used by
 * the other protected layouts.  Every active address is therefore mapped
 * independently from the captured unpacked image and the complete table is
 * validated before the first write.  Keep this array sorted by supported RVA
 * so the resolver can use a bounded binary search. */
static const RuntimeRvaMap g_asia_rva_map[] = {
    { 0x002BFF60ULL, 0x008D9410ULL },
    { 0x002C0490ULL, 0x008D9940ULL },
    { 0x002C049EULL, 0x008D994EULL },
    { 0x002C04E2ULL, 0x008D9992ULL },
    { 0x002CBF18ULL, 0x008E2CB8ULL },
    { 0x002CC150ULL, 0x008E2EF0ULL },
    { 0x002CC1B9ULL, 0x008E2F59ULL },
    { 0x002CC1C4ULL, 0x008E2F64ULL },
    { 0x002CC1D6ULL, 0x008E2F76ULL },
    { 0x002CC1E8ULL, 0x008E2F88ULL },
    { 0x002CC1FAULL, 0x008E2F9AULL },
    { 0x002CC27FULL, 0x008E301FULL },
    { 0x002CC291ULL, 0x008E3031ULL },
    { 0x002CC2A3ULL, 0x008E3043ULL },
    { 0x002CC2C3ULL, 0x008E3063ULL },
    { 0x002CC300ULL, 0x008E30A0ULL },
    { 0x002CC31CULL, 0x008E30BCULL },
    { 0x002CC333ULL, 0x008E30D3ULL },
    { 0x002D608DULL, 0x008F0EADULL },
    { 0x002D60B2ULL, 0x008F0ED2ULL },
    { 0x002D61D1ULL, 0x008F0FF1ULL },
    { 0x002D6269ULL, 0x008F1089ULL },
    { 0x002E2236ULL, 0x008FD8C6ULL },
    { 0x002E225CULL, 0x008FD8ECULL },
    { 0x002E2262ULL, 0x008FD8F2ULL },
    { 0x002E2269ULL, 0x008FD8F9ULL },
    { 0x002E2270ULL, 0x008FD900ULL },
    { 0x002E2277ULL, 0x008FD907ULL },
    { 0x002E8E89ULL, 0x00907D59ULL },
    { 0x002E9B60ULL, 0x00908C00ULL },
    { 0x002E9C21ULL, 0x00908CC1ULL },
    { 0x002E9C3BULL, 0x00908CDBULL },
    { 0x00304F7FULL, 0x0094018FULL },
    { 0x00304FC3ULL, 0x009401D3ULL },
    { 0x00305018ULL, 0x00940228ULL },
    { 0x003050C8ULL, 0x009402D8ULL },
    { 0x00306E60ULL, 0x00940510ULL },
    { 0x00306EAEULL, 0x00940564ULL },
    { 0x00306EB9ULL, 0x0094056FULL },
    { 0x00306ECDULL, 0x00940583ULL },
    { 0x00306EEFULL, 0x009405A5ULL },
    { 0x00306EF3ULL, 0x009405A9ULL },
    { 0x00306F64ULL, 0x0094061AULL },
    { 0x00307168ULL, 0x00940818ULL },
    { 0x003071ABULL, 0x0094085BULL },
    { 0x00307285ULL, 0x00940935ULL },
    { 0x0030728CULL, 0x0094093CULL },
    { 0x003073E2ULL, 0x00940A92ULL },
    { 0x00308389ULL, 0x00941B00ULL },
    { 0x00308441ULL, 0x00941BB8ULL },
    { 0x00308A01ULL, 0x00942178ULL },
    { 0x00308A90ULL, 0x0092F4B0ULL },
    { 0x00308B53ULL, 0x0092F573ULL },
    { 0x00308DB0ULL, 0x00933780ULL },
    { 0x00309050ULL, 0x00933A20ULL },
    { 0x003090B9ULL, 0x00933A89ULL },
    { 0x0030A5F8ULL, 0x00934F98ULL },
    { 0x0030ABC0ULL, 0x00935560ULL },
    { 0x0030ACC9ULL, 0x00935669ULL },
    { 0x0030D1BDULL, 0x00937B56ULL },
    { 0x0030D222ULL, 0x00937BBBULL },
    { 0x003C1350ULL, 0x00A1D5B0ULL },
    { 0x003C6D1DULL, 0x00A2346DULL },
    { 0x003CA3E2ULL, 0x00A29E62ULL },
    { 0x003CA441ULL, 0x00A29EC1ULL },
    { 0x003CA76FULL, 0x00A2A1EFULL },
    { 0x003CA77AULL, 0x00A2A1FAULL },
    { 0x003CA980ULL, 0x00A2A400ULL },
    { 0x003DC780ULL, 0x00A53740ULL },
    { 0x003DDD9CULL, 0x00A54EF2ULL },
    { 0x003DDFC0ULL, 0x00A55138ULL },
    { 0x003DE059ULL, 0x00A551DAULL },
    { 0x003DF833ULL, 0x00A56A1BULL },
    { 0x003E072FULL, 0x00A571EFULL },
    { 0x003E07E7ULL, 0x00A572A7ULL },
    { 0x003E4420ULL, 0x00A5B330ULL },
    { 0x00427860ULL, 0x009947B0ULL }
};

static HMODULE g_self;
static LONG g_initialized;
static volatile LONG g_nexus_init_calls;
static volatile LONG g_nexus_entry_kind;
static volatile LONG g_dllmain_fallback_attempts;
static void *volatile g_nexus_interface;
static BootstrapSnapshot g_snapshot;
static unsigned char *g_disrupt_base;
static uint64_t g_runtime_image_size;
static uint64_t g_cluster_a_delta;
static uint64_t g_cluster_b_delta;
static uint64_t g_cluster_c_delta;
static uint64_t g_string_assign_rva=STRING_ASSIGN_RVA;
static uint64_t g_native_random_rva=0x000B9780ULL;
static uint64_t g_owner_vector_reserve_rva=OWNER_VECTOR_RESERVE_RVA;
static volatile LONG g_runtime_profile;

/* Known protected builds either preserve three coherent code regions or use
 * the independently mapped Asia layout.  All engine access goes through this
 * resolver; no hook or write may use a raw supported-build RVA after profile
 * selection. */
static uint64_t resolve_engine_rva(uint64_t rva)
{
    if(g_runtime_profile==5) {
        size_t low=0;
        size_t high=sizeof(g_asia_rva_map)/sizeof(g_asia_rva_map[0]);
        while(low<high) {
            size_t middle=low+(high-low)/2;
            uint64_t supported=g_asia_rva_map[middle].supported_rva;
            if(rva<supported) high=middle;
            else if(rva>supported) low=middle+1;
            else return g_asia_rva_map[middle].runtime_rva;
        }
        return 0;
    }
    return rva+
        (rva>=0x00420000ULL ? g_cluster_c_delta :
        (rva>=0x003C0000ULL ? g_cluster_b_delta :
        (rva>=0x002B0000ULL ? g_cluster_a_delta : 0ULL)));
}
#define RESOLVED_ENGINE_RVA(rva) resolve_engine_rva((uint64_t)(rva))
#define ENGINE_ADDRESS(rva) (g_disrupt_base+RESOLVED_ENGINE_RVA(rva))
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
static PostCallTaskConstructorFn g_original_post_call_task_constructor;
static PostCallTaskExecuteFn g_original_post_call_task_execute;
static PostCallObjectRenderersFn g_original_post_call_object_renderers;
static ExecuteFrameGraphDispatchFn g_original_execute_frame_graph_dispatch;
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
static PostCallTaskProvenance g_postcall_tasks[POST_CALL_TASK_SLOTS];
static volatile LONG g_lifecycle_constructor_calls;
static volatile LONG g_lifecycle_builder_calls;
static volatile LONG g_lifecycle_lookup_calls;
static volatile LONG g_lifecycle_lookup_f8_calls;
static volatile LONG g_lifecycle_lookup_100_calls;
static volatile LONG g_lifecycle_lookup_110_calls;
static volatile LONG g_lifecycle_renderer_calls;
static volatile LONG g_lifecycle_release_calls;
static volatile LONG g_lifecycle_builder_null_events;
static volatile LONG g_lifecycle_renderer_null_events;
static volatile LONG g_lifecycle_release_null_events;
static volatile LONG g_lifecycle_record_slot_misses;
static volatile LONG g_lifecycle_thread_slot_misses;
static volatile LONG g_lookup_110_null_events;
static volatile LONG g_f8_before_110_divergence_events;
static volatile LONG g_f8_after_110_divergence_events;
static volatile LONG g_builder_tail_lookup_checkpoints;
static volatile LONG g_builder_tail_lookup_divergence_events;
static volatile LONG g_postcall_calls;
static volatile LONG g_postcall_entry_divergence_events;
static volatile LONG g_postcall_exit_divergence_events;
static volatile LONG g_postcall_entry_builder_overlap;
static volatile LONG g_postcall_entry_after_correct_exit;
static volatile LONG g_postcall_entry_after_wrong_exit;
static volatile LONG g_postcall_entry_same_thread;
static volatile LONG g_postcall_entry_cross_thread;
static volatile LONG g_framegraph_dispatch_calls;
static volatile LONG g_framegraph_dispatch_entry_divergence_events;
static volatile LONG g_framegraph_dispatch_exit_divergence_events;
static volatile LONG g_renderer_target_observations;
static volatile LONG g_renderer_target_changes;
static void *volatile g_renderer_vtable;
static void *volatile g_renderer_slot_a0;
static void *volatile g_renderer_slot_08;
static volatile LONG g_postcall_task_constructor_calls;
static volatile LONG g_postcall_task_execute_calls;
static volatile LONG g_postcall_task_registry_misses;
static volatile LONG g_postcall_task_record_mismatches;
static volatile LONG g_postcall_task_creation_wrong;
static volatile LONG g_postcall_task_execute_wrong;
static volatile LONG g_postcall_task_execute_correct;
static volatile LONG g_postcall_task_reused_before_execute;
static volatile LONG g_postcall_task_same_generation_mutation;
static volatile LONG g_postcall_task_callback_linked;
static volatile LONG g_postcall_task_wrong_at_execute;
static volatile LONG g_postcall_task_changed_execute_to_callback;
static volatile LONG g_postcall_task_corrected_execute_to_callback;
static volatile LONG g_postcall_task_queue_underflow;
static volatile LONG g_slice_result_guard_state;
static volatile LONG g_slice_result_native_nonnull;
static volatile LONG g_slice_result_native_null;
static volatile LONG g_slice_result_external_nonnull;
static volatile LONG g_slice_result_external_null;
static volatile LONG g_slice_result_external_store_failures;
static volatile LONG g_slice_result_external_duplicate_indices;
static volatile LONG g_slice_result_external_duplicate_resources;
static volatile LONG g_slice_result_external_stale_lineage;
static volatile LONG g_slice_result_external_late_writes;
static volatile LONG g_slice_result_external_consumed_results;
static volatile LONG g_slice_result_external_release_events;
static volatile LONG g_slice_result_external_release_resources;
static volatile LONG g_slice_result_external_release_failures;
static volatile LONG g_slice_result_external_release_fallbacks;
static volatile LONG g_slice_result_external_reset_pending;
static volatile LONG g_builder_110_divergence_events;
static volatile LONG g_renderer_110_divergence_events;
static volatile LONG g_release_110_divergence_events;
static volatile LONG g_last_provenance_summary_tick;
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
static void *__fastcall hooked_post_call_task_constructor(uint64_t,const char *,
    uint64_t,void *,void *,uint64_t,unsigned char,uint32_t,unsigned char,
    uint64_t,void *,uint64_t,uint64_t,uint64_t);
static void __fastcall hooked_post_call_task_execute(void *);
static void __fastcall hooked_post_call_object_renderers(void *,uint64_t,
    unsigned char,int,unsigned char,uint64_t,void *,uint64_t,uint64_t,uint64_t);
static void __fastcall hooked_execute_frame_graph_dispatch(void *,uint64_t,
    void *);
static void __fastcall hooked_render_record_acquire(void *,void *,void *);
static void __fastcall hooked_render_record_release(void *,void *,unsigned char);
static void *__fastcall hooked_render_record_constructor(void *);
static void *__fastcall hooked_shared_texture_lookup(void *,uint32_t,void *);
static void log_manual_lifecycle_snapshot(const char *state,LONG serial);
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
