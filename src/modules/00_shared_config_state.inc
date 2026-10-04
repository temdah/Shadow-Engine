/* Module 00: platform includes, engine constants, signatures, shared types,
 * bounded state, and forward declarations. No hook is installed here. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../internal/saved_settings.h"
#include "../internal/driver_identity_trace.h"

#define PATCH_VERSION "2.0.95"
#ifndef SHADOW_ENGINE_INTERNAL_DIAGNOSTICS
#define SHADOW_ENGINE_INTERNAL_DIAGNOSTICS 1
#endif
#if SHADOW_ENGINE_INTERNAL_DIAGNOSTICS != 0 && SHADOW_ENGINE_INTERNAL_DIAGNOSTICS != 1
#error SHADOW_ENGINE_INTERNAL_DIAGNOSTICS must be 0 or 1
#endif
#ifndef SHADOW_ENGINE_OVERHEAD_MEASUREMENT
#define SHADOW_ENGINE_OVERHEAD_MEASUREMENT SHADOW_ENGINE_INTERNAL_DIAGNOSTICS
#endif
#include "../internal/overhead_probe.h"
#ifndef SHADOW_ENGINE_DETAIL_WINDOWS
#define SHADOW_ENGINE_DETAIL_WINDOWS SHADOW_ENGINE_INTERNAL_DIAGNOSTICS
#endif
#include "../internal/detail_window.h"
#if SHADOW_ENGINE_INTERNAL_DIAGNOSTICS
#include "../internal/periodic_summary.h"
#endif
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
#define SHADOW_FACE_SCHEDULER_RVA 0x002D6070ULL
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
#define FRAME_GRAPH_FINALIZER_RVA 0x003E0E70ULL
#define FRAME_GRAPH_PRE_FINALIZER_RVA 0x003E19E3ULL
#define RENDER_RECORD_RELEASE_RVA 0x003C1350ULL
#define RENDER_RECORD_CONSTRUCTOR_RVA 0x003E4420ULL
#define SHARED_TEXTURE_LOOKUP_RVA 0x00430A80ULL
#define FIELD_100_LOOKUP_RETURN_RVA 0x003DDD9CULL
#define FIELD_F8_LOOKUP_RETURN_RVA  0x003DDFC0ULL
#define FIELD_110_LOOKUP_RETURN_RVA 0x003DE059ULL
#define SOURCE_CONFIG_LOOKUP_RVA 0x0030ACC9ULL
#define FACE_OBJECT_BUILD_RVA    0x0030CB99ULL
#define RENDER_QUEUE_COUNT_OFFSET 0x47448U
#define RENDER_QUEUE_TAIL_OFFSET  0x47450U
#define RENDER_QUEUE_RESOURCE_CONTEXT_OFFSET 0x47458U
#define RENDER_QUEUE_RESULT_OFFSET 0x47460U
#define RENDER_QUEUE_ENTRY_BYTES  0x24C0U
#define RENDER_QUEUE_FIRST_MAP_OFFSET 0x47350U
#define RENDER_QUEUE_SECOND_MAP_OFFSET 0x473CCU
#define RENDER_QUEUE_FIRST_MAP_STRIDE 0x24C4U
#define RENDER_QUEUE_LAYOUT_STRIDE 0x24C8U
#define RENDER_QUEUE_ALLOCATION_BYTES (RENDER_QUEUE_TAIL_OFFSET+0x20U)
#define ORIGINAL_LOCAL_MAPS 16U
#define EXTRA_LOCAL_MAPS 14U
#define TOTAL_LOCAL_MAPS 30U
#define PHYSICAL_QUEUE_ENTRIES (TOTAL_LOCAL_MAPS+1U)
#define QUEUE_ARRAY_CLEAR_BYTES (4U+TOTAL_LOCAL_MAPS*4U)
/* v2.0.15 tests intermediate admission at fixed 30-map capacity.
 * Constructor and both normal bounds share B4=21; mechanisms stay unchanged. */
#define TARGET_DYNAMIC_B4 21U
#define TARGET_CACHE_A8 4U
#define OWNER_VECTOR_RESERVE_CAPACITY 8U
#define SHADOW_PASS_CATEGORY      0x0CU
#define REQUIRED_PASS_MAX_KEY     0x3D0CU
#define REQUIRED_PASS_TABLE_SLOTS 62U
#define FIRST_EXTRA_PASS_SLOT     34U
#define EXTRA_PASS_SLOT_COUNT     28U
#define LIFECYCLE_RECORD_SLOTS      2048U
#define EXTERNAL_SLICE_SUMMARY_PERIOD_MS 30000U
#define NATIVE_SLICE_RESULT_COUNT 17U
#define EXTRA_SLICE_RESULT_COUNT (TOTAL_LOCAL_MAPS-NATIVE_SLICE_RESULT_COUNT)
#define VEHICLE_CAPTURE_SAMPLES 4U
#define VEHICLE_CAPTURE_INTERVAL_MS 500U
#define VEHICLE_CAPTURE_TIMEOUT_MS 2500U
#define VEHICLE_CAPTURE_MAX_CANDIDATES 256U
#define VEHICLE_OWNER_MAX_RECORDS 256U
#define VEHICLE_OWNER_RECORDS_PER_WINDOW (VEHICLE_OWNER_MAX_RECORDS/VEHICLE_CAPTURE_SAMPLES)
#define VEHICLE_OWNER_WINDOW_MS 100U
#define VEHICLE_OWNER_PROBES_PER_WINDOW 4096U
#define VEHICLE_OWNER_RETRY_MS 16U
#define VEHICLE_IDENTITY_MAX_RECORDS 256U
#define VEHICLE_SELECTION_EPOCH_BANKS 4U
#define VEHICLE_SELECTION_SLOTS_PER_EPOCH 512U
#define VEHICLE_SELECTION_SNAPSHOT_RECORDS \
    (VEHICLE_SELECTION_EPOCH_BANKS*VEHICLE_SELECTION_SLOTS_PER_EPOCH)
#define VEHICLE_SELECTION_MAX_VEHICLES 128U
#define VEHICLE_SELECTION_STABLE_VEHICLES 4U
#define VEHICLE_SELECTION_MAX_STABLE_VEHICLES 10U
#define VEHICLE_SELECTION_MAX_RESIDENTS 12U
#define VEHICLE_SELECTION_OVERRIDE_VEHICLES 2U
#define VEHICLE_SELECTION_KEEP_VEHICLES \
    (VEHICLE_SELECTION_STABLE_VEHICLES+VEHICLE_SELECTION_OVERRIDE_VEHICLES)
#define VEHICLE_TRANSITION_HOLD_MS 1000U
#define VEHICLE_RESIDENCY_DISTANCE_NUMERATOR 9U
#define VEHICLE_RESIDENCY_DISTANCE_DENOMINATOR 10U
#define VEHICLE_SHADOW_MAX_DIMENSION 2048U
#define VEHICLE_SPATIAL_DESCRIPTOR_HANDLE_OFFSET 0x50U
#define VEHICLE_OWNER_COMPONENT_BYTES 0x120U
#define VEHICLE_OWNER_SETTINGS_OFFSET 0x08U
#define VEHICLE_OWNER_LIGHT_OFFSET 0x10U
#define VEHICLE_OWNER_BONE_OFFSET 0x11CU
#define VEHICLE_OWNER_LIGHT_BYTES 0x3AU
#define VEHICLE_OWNER_SPATIAL_HANDLE_OFFSET 0x20U
#define VEHICLE_OWNER_DESCRIPTOR_HANDLE_OFFSET 0x28U
#define VEHICLE_OWNER_ENABLED_OFFSET 0x38U
#define VEHICLE_OWNER_SECONDARY_OFFSET 0x39U
/* Queue fields consumed by the native renderer, not new capacity values. */
#define VEHICLE_QUEUE_CANDIDATE_OFFSET 0x10U
#define VEHICLE_QUEUE_FACE_COUNT_OFFSET 0x20A0U
#define VEHICLE_QUEUE_WIDTH_OFFSET 0x20A4U
#define VEHICLE_QUEUE_HEIGHT_OFFSET 0x20A8U
#define VEHICLE_QUEUE_RESOURCE_OFFSET 0x20B0U
#define VEHICLE_RESOURCE_WIDTH_OFFSET 0x40U
#define VEHICLE_RESOURCE_HEIGHT_OFFSET 0x44U
#define SHADOW_CANDIDATE_BACKING_OFFSET 0x58U
#define SHADOW_BACKING_RESOURCE_OFFSET 0x40U
#define COMPLETION_DIAGNOSTIC_SLOTS 256U
#define COMPLETION_DIAGNOSTIC_HISTORY 8U
#define COMPLETION_LATENCY_BUCKETS 8U

#if TOTAL_LOCAL_MAPS != ORIGINAL_LOCAL_MAPS+EXTRA_LOCAL_MAPS
#error local map totals are inconsistent
#endif
#if PHYSICAL_QUEUE_ENTRIES != TOTAL_LOCAL_MAPS+1U
#error queue entry count must preserve the long-range entry
#endif
#if QUEUE_ARRAY_CLEAR_BYTES != 4U+TOTAL_LOCAL_MAPS*4U
#error queue clear size is inconsistent with the local-map count
#endif
#if RENDER_QUEUE_FIRST_MAP_STRIDE != RENDER_QUEUE_ENTRY_BYTES+4U || \
    RENDER_QUEUE_LAYOUT_STRIDE != RENDER_QUEUE_ENTRY_BYTES+8U
#error queue mapping strides are inconsistent with the record size
#endif
#if RENDER_QUEUE_FIRST_MAP_OFFSET != 0x270D0U+EXTRA_LOCAL_MAPS*RENDER_QUEUE_ENTRY_BYTES
#error first queue mapping offset is inconsistent with the record array
#endif
#if RENDER_QUEUE_SECOND_MAP_OFFSET != 0x27114U+EXTRA_LOCAL_MAPS*RENDER_QUEUE_FIRST_MAP_STRIDE
#error second queue mapping offset is inconsistent with the first mapping array
#endif
#if RENDER_QUEUE_COUNT_OFFSET != 0x27158U+EXTRA_LOCAL_MAPS*RENDER_QUEUE_LAYOUT_STRIDE
#error queue count offset is inconsistent with the relocated layout
#endif
#if RENDER_QUEUE_TAIL_OFFSET != RENDER_QUEUE_COUNT_OFFSET+8U
#error queue tail offset is inconsistent with the relocated layout
#endif
#if RENDER_QUEUE_RESOURCE_CONTEXT_OFFSET != RENDER_QUEUE_TAIL_OFFSET+8U
#error queue resource-context offset is inconsistent with the relocated layout
#endif
#if RENDER_QUEUE_RESULT_OFFSET != RENDER_QUEUE_RESOURCE_CONTEXT_OFFSET+8U
#error queue result offset is inconsistent with the resource context
#endif
#if RENDER_QUEUE_RESULT_OFFSET != RENDER_QUEUE_COUNT_OFFSET+0x18U
#error queue result offset is inconsistent with the relocated layout
#endif
#if RENDER_QUEUE_ALLOCATION_BYTES != RENDER_QUEUE_COUNT_OFFSET+0x28U
#error queue allocation size is inconsistent with the relocated layout
#endif
#if EXTRA_PASS_SLOT_COUNT != EXTRA_LOCAL_MAPS*2U
#error each external map requires Shadow and ShadowAlpha pass slots
#endif
#if REQUIRED_PASS_TABLE_SLOTS != FIRST_EXTRA_PASS_SLOT+EXTRA_PASS_SLOT_COUNT
#error required pass-table slots are inconsistent with the external pass range
#endif
#if REQUIRED_PASS_MAX_KEY != ((TOTAL_LOCAL_MAPS<<9U)|0x10CU)
#error required pass key is inconsistent with the final ShadowAlpha pass
#endif
#if REQUIRED_PASS_TABLE_SLOTS > 64U || REQUIRED_PASS_MAX_KEY > 0x3F00U
#error capacity target exceeds the proven native pass registry
#endif
#if EXTRA_SLICE_RESULT_COUNT > 32U
#error external result count exceeds the 32-bit lifecycle written mask
#endif
#if (COMPLETION_DIAGNOSTIC_SLOTS&(COMPLETION_DIAGNOSTIC_SLOTS-1U)) != 0U || \
    (COMPLETION_DIAGNOSTIC_HISTORY&(COMPLETION_DIAGNOSTIC_HISTORY-1U)) != 0U
#error completion diagnostic table and history sizes must be powers of two
#endif
#if (VEHICLE_SELECTION_EPOCH_BANKS&(VEHICLE_SELECTION_EPOCH_BANKS-1U)) != 0U || \
    (VEHICLE_SELECTION_SLOTS_PER_EPOCH&(VEHICLE_SELECTION_SLOTS_PER_EPOCH-1U)) != 0U
#error vehicle selection epoch banks and hash slots must be powers of two
#endif
#if VEHICLE_RESIDENCY_DISTANCE_NUMERATOR >= VEHICLE_RESIDENCY_DISTANCE_DENOMINATOR
#error vehicle residency replacement must require a strict distance advantage
#endif
#if VEHICLE_SELECTION_STABLE_VEHICLES != 4U || \
    VEHICLE_SELECTION_OVERRIDE_VEHICLES != 2U || \
    VEHICLE_SELECTION_KEEP_VEHICLES != 6U
#error vehicle hybrid policy must retain four stable plus two override owners
#endif
#if VEHICLE_SELECTION_MAX_STABLE_VEHICLES+VEHICLE_SELECTION_OVERRIDE_VEHICLES != VEHICLE_SELECTION_MAX_RESIDENTS
#error patch-owned resident storage must fit the slider maximum plus overrides
#endif
#if TOTAL_LOCAL_MAPS > 255U || TARGET_DYNAMIC_B4 > 255U || QUEUE_ARRAY_CLEAR_BYTES > 255U
#error patched immediate no longer fits its encoded byte field
#endif
#if TARGET_CACHE_A8 > OWNER_VECTOR_RESERVE_CAPACITY
#error logical cached-owner ceiling exceeds the pre-reserved owner vector
#endif

#define STRING_ASSIGN_RVA         0x000D60B0ULL
#define STRING_VALUE_RVA          0x000CCEB0ULL

typedef void (__fastcall *ResourceConstructorFn)(void *manager);
typedef void (__fastcall *RegisterPassFn)(void *registry, uint32_t key,
    uint64_t name_value, const char *category, uint32_t mode, uint32_t enabled);
typedef uint64_t (__fastcall *StringValueFn)(void *reference);
typedef void (__fastcall *StringAssignFn)(uint64_t *destination,const char *value);
typedef void (__fastcall *ShadowManagerFn)(void *,void *,uint32_t,void *,void *,void *,void *);
typedef void (__fastcall *ProfileCallbackFn)(void *);
typedef void (__fastcall *OwnerVectorReserveFn)(void *,uint64_t);
typedef void (__fastcall *RendererQueueFn)(void *,void *,uint64_t,void *,void *,
    uint64_t,uint64_t,uint64_t,uint64_t);
typedef void (__fastcall *ShadowFaceSchedulerFn)(void *,void *,uint32_t,
    uint32_t,void *);
typedef int64_t (__fastcall *ResourceWrapperFn)(unsigned char,void **,
    uint32_t,void **,uint32_t,unsigned char *,uint64_t,uint32_t);
typedef uint32_t (__fastcall *FaceCostReaderFn)(void *);
typedef void (__fastcall *FrameBuilderFn)(void *,void *,void *,uint64_t,
    uint64_t,uint64_t,uint64_t);
typedef void (__fastcall *RenderRecordReleaseFn)(void *,void *,unsigned char);
typedef void *(__fastcall *RenderRecordConstructorFn)(void *);
typedef SHORT (WINAPI *GetAsyncKeyStateFn)(int);
typedef void *(__fastcall *IntersectionLookupFn)(void *,uint32_t,void *);
typedef BOOL (WINAPI *IntersectionQueueWorkFn)(LPTHREAD_START_ROUTINE,PVOID,ULONG);
typedef void (__fastcall *VehicleLightUpdateFn)(void *,void *,void *,float,unsigned char);
typedef void (__fastcall *VehicleLightTransformFn)(void *,void *,void *);
typedef void (__fastcall *VehicleLightBatchFn)(void *,float,void *,void *,float);
typedef void (__fastcall *VehicleRetireFn)(void *);

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
    volatile LONG producer_cycle;
    volatile LONG producer_cycle_ready;
    volatile LONG extra_slice_result_generation;
    volatile LONG extra_slice_result_cycle;
    volatile LONG extra_slice_result_written_mask;
    volatile LONG extra_slice_result_consumer_state;
    volatile LONG extra_slice_result_active_writers;
    void *volatile extra_slice_results[EXTRA_SLICE_RESULT_COUNT];
} LifecycleRecordSnapshot;

typedef struct CompletionGenerationRecord {
    volatile LONG serial;
    volatile LONG lifecycle_generation;
    volatile LONG builder_call;
    volatile LONG producer_cycle;
    volatile LONG scheduled_calls;
    volatile LONG scheduled_external_mask;
    volatile LONG completed_external_mask;
    volatile LONG pre_finalizer_mask;
    volatile LONG reset_mask;
    volatile LONG pre_finalizer_seen;
    volatile LONG reset_seen;
    volatile LONG maximum_generation_lag;
    volatile LONG schedule_thread;
    volatile LONG completion_thread;
    volatile LONG completion_thread_switches;
    volatile LONG scheduled_native_mask;
    volatile LONG scheduled_after_pre_finalizer_mask;
    volatile LONG scheduled_after_reset_mask;
    volatile LONG producer_sealed;
    volatile LONG completed_at_producer_seal_mask;
    volatile LONG producer_thread;
    volatile LONG producer_begin_consumer_state;
    volatile LONG producer_end_consumer_state;
    volatile LONG producer_end_generation;
    volatile LONG producer_end_builder_call;
    volatile LONG producer_end_cycle;
    volatile LONGLONG schedule_qpc[EXTRA_SLICE_RESULT_COUNT];
} CompletionGenerationRecord;

typedef struct CompletionDiagnosticSlot {
    void *volatile record;
    volatile LONG current_serial;
    volatile LONG active_producer_serial;
    CompletionGenerationRecord history[COMPLETION_DIAGNOSTIC_HISTORY];
} CompletionDiagnosticSlot;

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
static const unsigned char g_shadow_face_scheduler_prologue[14] = {
    0x48,0x89,0x6C,0x24,0x18,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57
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
static const unsigned char g_render_record_release_prologue[20] = {
    0x48,0x89,0x5C,0x24,0x18,0x56,0x57,0x41,0x54,0x48,
    0x83,0xEC,0x60,0x44,0x8B,0x89,0x90,0x13,0x00,0x00
};
static const unsigned char g_render_record_constructor_prologue[14] = {
    0x45,0x33,0xC0,0x48,0x8D,0x41,0x70,0x41,0x8D,0x50,0x02,0x4C,0x89,0x01
};
/* The queue owns one entry per local map plus the preserved long-range pass.
 * These are the complete downstream tail-offset relocations recovered for the
 * native 16/17 layout. Each map adds one 0x24C0-byte record and one eight-byte
 * auxiliary mapping arrays grow at separate boundaries: record-only fields use
 * 0x24C0 per map, the first mapping array adds four bytes per map, and fields
 * after the second mapping array add eight bytes per map. v1.2.3 preserves
 * those three distinct layout strides; this comparison restores the
 * 30/31 layout without changing admission or the relocation mechanism. */
static const TailPatch g_tail_patches[] = {
    { 0x2C049EULL, 0x27158, 0x47448 },
    { 0x2C04E2ULL, 0x27158, 0x47448 },
    { 0x2CBF18ULL, 0x27168, 0x47458 },
    { 0x2CC150ULL, 0x27168, 0x47458 },
    { 0x2CC1B9ULL, 0x27168, 0x47458 },
    { 0x2CC1C4ULL, 0x27168, 0x47458 },
    { 0x2CC1D6ULL, 0x27168, 0x47458 },
    { 0x2CC1E8ULL, 0x27168, 0x47458 },
    { 0x2CC1FAULL, 0x27168, 0x47458 },
    { 0x2CC27FULL, 0x27168, 0x47458 },
    { 0x2CC291ULL, 0x27168, 0x47458 },
    { 0x2CC2A3ULL, 0x27168, 0x47458 },
    { 0x2CC2C3ULL, 0x27168, 0x47458 },
    { 0x2CC300ULL, 0x27170, 0x47460 },
    { 0x2CC31CULL, 0x27170, 0x47460 },
    { 0x2D608DULL, 0x27170, 0x47460 },
    { 0x2D60B2ULL, 0x27168, 0x47458 },
    { 0x2D61D1ULL, 0x27168, 0x47458 },
    { 0x2D6269ULL, 0x27158, 0x47448 },
    { 0x2E225CULL, 0x27158, 0x47448 },
    { 0x2E2262ULL, 0x27160, 0x47450 },
    { 0x2E2269ULL, 0x27168, 0x47458 },
    { 0x2E2270ULL, 0x27170, 0x47460 },
    { 0x2E2277ULL, 0x2715C, 0x4744C },
    { 0x304F7FULL, 0x27168, 0x47458 },
    { 0x304FC3ULL, 0x27168, 0x47458 },
    { 0x305018ULL, 0x27168, 0x47458 },
    { 0x3050C8ULL, 0x2715C, 0x4744C },
    { 0x306EAEULL, 0x27168, 0x47458 },
    { 0x306EB9ULL, 0x270D0, 0x47350 },
    { 0x306EF3ULL, 0x27114, 0x473CC },
    { 0x306F64ULL, 0x27158, 0x47448 },
    { 0x307285ULL, 0x270D0, 0x47350 },
    { 0x30728CULL, 0x27114, 0x473CC },
    { 0x308A01ULL, 0x27158, 0x47448 },
    { 0x30A5F8ULL, 0x27158, 0x47448 },
    { 0x30ABC0ULL, 0x27158, 0x47448 },
    { 0x30D1BDULL, 0x27160, 0x47450 },
    { 0x30D222ULL, 0x27158, 0x47448 },
    { 0x3C6D1DULL, 0x27160, 0x47450 },
    { 0x3CA3E2ULL, 0x27180, 0x47470 },
    { 0x3CA441ULL, 0x2715C, 0x4744C },
    { 0x3CA76FULL, 0x27160, 0x47450 },
    { 0x3CA77AULL, 0x27160, 0x47450 },
    { 0x3E072FULL, 0x27160, 0x47450 },
    { 0x3E07E7ULL, 0x27160, 0x47450 }
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
    /* Module80's process-lifetime terminal reason; zero is pending/success.
     * Diagnostic only: never changes or retries the transactional state. */
    volatile LONG startup_failure;
    /* Internal SE10 compatibility notice: 0 pending, 1 mapped supported host,
     * 2 reserved, 3 unknown host, 4 startup/layout unavailable,
     * 5 optional game Lua sites unavailable. Independent of shadow readiness. */
    volatile LONG host_compatibility;
    volatile LONG session_log_state;
    volatile LONG log_write_failures;
    volatile LONG hooks_installed;
} BootstrapState;

typedef struct HookBindings {
    IntersectionLookupFn original_intersection_lookup;
    ResourceConstructorFn original_resource_constructor;
    RegisterPassFn original_register_pass;
    ShadowManagerFn original_manager;
    ProfileCallbackFn profile_callback;
    OwnerVectorReserveFn owner_vector_reserve;
    RendererQueueFn original_renderer_queue;
    ShadowFaceSchedulerFn original_shadow_face_scheduler;
    ResourceWrapperFn original_resource_wrapper;
    FaceCostReaderFn original_face_cost_reader;
    FrameBuilderFn original_frame_builder;
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
    volatile LONG latest_candidate_count;
    volatile LONG peak_candidate_count;
    volatile LONG latest_admitted_count;
    volatile LONG peak_admitted_count;
    volatile LONG latest_dynamic_or_special_count;
    volatile LONG latest_cached_binding_count;
    volatile LONG peak_cached_binding_count;
    volatile LONG latest_owner_record_count;
    volatile LONG peak_owner_record_count;
    volatile LONG admission_scan_state;
    volatile LONG f8_was_down;
    volatile LONG f9_was_down;
    DWORD last_residency_probe_tick;
    DWORD last_residency_log_tick;
} ManagerRendererState;

typedef struct WorldLightQualityState {
    volatile LONG mutated_calls,mutated_entries;
    volatile LONG skipped_vehicle,skipped_ambiguous,skipped_unreadable;
    /* Process totals of actual inspections and certified Q1 bypasses. */
    volatile LONG under_cap_entries,inspected_entries,fast_check_unavailable_calls;
} WorldLightQualityState;

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

typedef enum ExternalRejectReason {
    EXTERNAL_REJECT_INVALID_INPUT,
    EXTERNAL_REJECT_MISSING_RECORD,
    EXTERNAL_REJECT_CLOSED_CONSUMER,
    EXTERNAL_REJECT_ZERO_GENERATION,
    EXTERNAL_REJECT_UNOPENED_CYCLE,
    EXTERNAL_REJECT_GENERATION_MISMATCH,
    EXTERNAL_REJECT_CYCLE_MISMATCH,
    EXTERNAL_REJECT_DUPLICATE_INDEX,
    EXTERNAL_REJECT_QUARANTINED_CYCLE,
    EXTERNAL_REJECT_REASON_COUNT
} ExternalRejectReason;

typedef struct ExternalResultState {
    LifecycleRecordSnapshot lifecycle_records[LIFECYCLE_RECORD_SLOTS];
    volatile LONG lifecycle_constructor_calls;
    volatile LONG lifecycle_builder_calls;
    volatile LONG lifecycle_producer_calls;
    volatile LONG producer_cycle_open_failures;
    volatile LONG rejection_reasons[EXTERNAL_REJECT_REASON_COUNT];
    volatile LONG lifecycle_release_calls;
    volatile LONG lifecycle_record_slot_misses;
    volatile LONG frame_graph_tail_drains;
    volatile LONG frame_graph_tail_empty;
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
    volatile LONG slice_result_external_rejected_nonnull;
    volatile LONG slice_result_external_rejected_null;
    volatile LONG slice_result_external_orphan_release_resources;
    volatile LONG slice_result_external_orphan_release_failures;
    volatile LONG slice_result_external_consumed_results;
    volatile LONG slice_result_external_release_events;
    volatile LONG slice_result_external_release_resources;
    volatile LONG slice_result_external_release_failures;
    volatile LONG slice_result_external_reset_pending;
    volatile LONG last_external_slice_summary_tick;
    volatile LONG lifecycle_hooks_state;
} ExternalResultState;

typedef struct CompletionDiagnosticState {
    CompletionDiagnosticSlot slots[COMPLETION_DIAGNOSTIC_SLOTS];
    LARGE_INTEGER qpc_frequency;
    volatile LONG serial;
    volatile LONG hook_state;
    volatile LONG slot_misses;
    volatile LONG scheduled_native;
    volatile LONG scheduled_external;
    volatile LONG schedule_overflow;
    volatile LONG completions_matched;
    volatile LONG completions_unmatched;
    volatile LONG completions_ambiguous;
    volatile LONG completions_current_generation;
    volatile LONG completions_one_generation_late;
    volatile LONG completions_multiple_generations_late;
    volatile LONG pre_finalizer_outstanding_generations;
    volatile LONG reset_outstanding_generations;
    volatile LONG first_seen_before_pre_finalizer;
    volatile LONG first_seen_after_pre_finalizer;
    volatile LONG first_seen_before_reset;
    volatile LONG first_seen_after_reset;
    volatile LONG producer_batches_started;
    volatile LONG producer_batches_sealed;
    volatile LONG producer_batches_without_lifecycle;
    volatile LONG producer_batch_overlaps;
    volatile LONG producer_active_clear_failures;
    volatile LONG producer_batches_without_external_work;
    volatile LONG producer_batches_outstanding_at_seal;
    volatile LONG producer_lifecycle_changed_before_seal;
    volatile LONG scheduler_calls_outside_producer;
    volatile LONG completions_before_producer_seal;
    volatile LONG completions_after_producer_seal;
    volatile LONG completions_same_lifecycle;
    volatile LONG completions_changed_lifecycle;
    volatile LONG completions_consumer_open;
    volatile LONG completions_consumer_closed;
    volatile LONG latency_buckets[COMPLETION_LATENCY_BUCKETS];
    volatile LONG last_summary_tick;
} CompletionDiagnosticState;

typedef struct VehicleCandidateRecord {
    void *candidate;
    void *next;
    void *descriptor;
    void *spatial;
    void *cache_owner;
    int32_t renderer_type;
    uint32_t descriptor_flags;
    uint32_t binding_flags;
    uint32_t admitted;
    uint32_t readable;
    uint64_t descriptor_handle;
    uint64_t selection_vehicle;
    uint64_t selection_entity_id;
    LONG selection_publish_manager_call;
    LONG selection_manager_delta;
    LONG selection_publish_renderer_epoch;
    LONG selection_renderer_delta;
    DWORD selection_age_ms;
    uint32_t descriptor_handle_valid;
    uint32_t selection_candidate_instances;
    uint32_t selection_matches;
    uint32_t selection_current;
    uint32_t selection_renderer_recent;
    uint32_t selection_rank;
    uint32_t selection_role; /* 0 none,1 main,2 near,3 outgoing,4 forecast,5 driver. */
    uint32_t selection_driver;
    uint32_t selection_group_lights;
    uint32_t selection_group_complete;
    uint32_t selection_would_keep;
    uint32_t selection_vehicle_type1;
    uint32_t policy_protected;
    float selection_distance_squared;
    float observed_x;
    float observed_y;
    float observed_z;
} VehicleCandidateRecord;

/* Immutable proof data; offsets are relative to the already-resolved renderer.
 * Spans exclude unrelated regional stack offsets and existing patch sites. */
typedef struct VehicleDiagnosticProof {
    uint32_t offset,length;
    unsigned char bytes[24];
} VehicleDiagnosticProof;
static const VehicleDiagnosticProof g_vehicle_queue_proofs[] = {
    {0x219U,12U,{0x48,0x69,0xC0,0xC0,0x24,0,0,0x48,0x8D,0x74,0x18,0x10}},
    {0x225U,5U,{0x48,0x8B,0x5C,0x18,0x10}},
    {0x231U,11U,{0x48,0x8B,0x43,0x10,0x4C,0x8B,0x7B,0x08,0x48,0x8B,0x08}},
    {0x2D2U,21U,{0x48,0x8B,0x43,0x58,0x8B,0x8E,0x94,0x20,0,0,0x44,0x8B,0x96,0x98,0x20,0,0,0x48,0x8B,0x40,0x40}},
    {0x36FU,7U,{0x48,0x89,0x86,0xA0,0x20,0,0}}
};
typedef struct VehicleResourceProof {
    uint32_t load_offset,store_offset;
    unsigned char loads[16],stores[8];
    uint32_t load_length;
} VehicleResourceProof;
static const VehicleResourceProof g_vehicle_resource_proofs[] = {
    {0xBCU,0xE0U,{0x44,0x8B,0x55,0x04,0x44,0x8B,0x45,0x00},
        {0x44,0x89,0x46,0x40,0x44,0x89,0x56,0x44},8U},
    {0xA6U,0xCCU,{0x45,0x8B,0x50,0x04,0x8B,0x4E,0x10,0x45,0x8B,0x00},
        {0x44,0x89,0x43,0x40,0x44,0x89,0x53,0x44},10U}
};

/* Numeric snapshots owned by the manual capture; pointers are opaque after
 * the synchronous read. Valid bits distinguish unreadable from known null. */
typedef struct ShadowResourceSnapshot {
    void *backing,*resource;
    uint32_t valid,width,height;
} ShadowResourceSnapshot;


typedef struct VehicleQueueRecord {
    VehicleCandidateRecord light;
    void *spatial_root; /* Native indexed indirection, not a vehicle owner. */
    void *resource;
    uint32_t valid,face_count,width,height,resource_width,resource_height;
    uint32_t native_width,native_height,policy_sampled;
    ShadowResourceSnapshot resource_before;
    uint32_t entry_index,map_start,submitted;
    uint64_t descriptor_handle; /* Raw native identity, not a vehicle owner. */
    uint32_t descriptor_handle_valid;
    /* Copied from the actual quality decision, not a later owner lookup. */
    uint64_t quality_owner;
    uint32_t quality_decision;
} VehicleQueueRecord;
typedef struct VehicleQueueSample {
    DWORD tick,thread;
    LONGLONG native_qpc[2];
    uint32_t world_enabled,world_cap;
    LONG vehicle_lock_skips_before,world_lock_skips_before;
    LONG vehicle_lock_skips_after,world_lock_skips_after;
    LONG renderer_call;
    uint32_t original_entries,admitted_entries,copied_entries,complete;
    LONG requested_policy; /* One copied settings snapshot; not GPU completion. */
    uint32_t quality_evaluated,quality_lock_skipped;
    LONG quality_policy; /* The actual quality call's already-loaded setting. */
    VehicleQueueRecord records[PHYSICAL_QUEUE_ENTRIES];
} VehicleQueueSample;

typedef struct VehicleCandidateSample {
    DWORD tick;
    uint32_t player_anchor_valid;
    /* One fresh scalar observation for this synchronous classifier call. */
    uint64_t driver_owner,driver_entity_id,driver_player_id;
    uint32_t driver_valid,driver_headlight_lights;
    DriverIdentityTrace driver_trace;
    LONGLONG phase_qpc[4];
    DWORD freeze_ms,freeze_peak_ms;
    uint32_t freeze_calls,freeze_peak_calls;
    LONG manager_call;
    LONG renderer_epoch;
    uint32_t reported_candidates;
    uint32_t copied_candidates;
    uint32_t admitted_candidates;
    uint32_t admission_observed;
    uint32_t cached_candidates;
    uint32_t chain_ended;
    uint32_t selection_snapshot_records;
    uint32_t selection_snapshot_vehicles;
    uint32_t selection_snapshot_age_le_16;
    uint32_t selection_snapshot_age_17_50;
    uint32_t selection_snapshot_age_51_250;
    uint32_t selection_snapshot_age_over_250;
    LONG selection_snapshot_gaps;
    LONG selection_snapshot_overflow;
    LONG selection_snapshot_rotations;
    uint32_t selection_snapshot_same_epoch;
    uint32_t selection_snapshot_previous_epoch;
    uint32_t selection_snapshot_older_epoch;
    uint32_t selection_spotlight3;
    uint32_t selection_exact_matches;
    uint32_t selection_exact_vehicles;
    uint32_t selection_exact_complete_vehicles;
    uint32_t selection_current_matches;
    uint32_t selection_older_matches;
    uint32_t selection_delta_zero;
    uint32_t selection_delta_one;
    uint32_t selection_delta_two_to_four;
    uint32_t selection_delta_five_plus;
    uint32_t selection_delta_future;
    uint32_t selection_renderer_same;
    uint32_t selection_renderer_previous;
    uint32_t selection_renderer_two_to_three;
    uint32_t selection_renderer_older;
    uint32_t selection_renderer_future;
    uint32_t selection_renderer_recent_matches;
    uint32_t selection_renderer_nonrecent_matches;
    uint32_t selection_ambiguous;
    uint32_t selection_unknown;
    uint32_t selection_vehicles;
    uint32_t selection_partial_vehicles;
    uint32_t selection_would_keep_lights;
    uint32_t selection_main_limit;
    uint32_t selection_extra_slots;
    uint32_t limiter_enabled;
    uint32_t limiter_evaluated;
    uint32_t limiter_active;
    uint32_t limiter_fail_open;
    uint32_t limiter_suppressed;
    uint32_t residency_applied;
    uint32_t residency_degraded;
    uint32_t residency_initialized;
    uint32_t residency_replaced;
    uint32_t residency_pending;
    uint32_t residency_residents;
    uint32_t protected_headlight_lights;
    uint32_t protected_headlight_admitted_lights;
    uint32_t protected_headlight_vehicles;
    uint32_t protected_headlight_fully_admitted_vehicles;
    uint32_t protected_headlight_partially_admitted_vehicles;
    uint32_t protected_headlight_not_admitted_vehicles;
    uint32_t vehicle_type1_candidates;
    uint32_t vehicle_type1_admitted_candidates;
    uint32_t vehicle_type1_vehicles;
    uint32_t vehicle_type1_admitted_vehicles;
    uint32_t type1_candidates;
    uint32_t type1_ambiguous;
    uint32_t type1_unknown;
    uint32_t protected_loss_with_vehicle_type1_admitted;
    uint32_t queue_protected_handle_matches;
    uint32_t queue_protected_faces;
    uint32_t queue_vehicle_type1_handle_matches;
    uint32_t queue_vehicle_type1_faces;
    uint32_t queue_handle_ambiguous;
    DWORD queue_correlation_tick_delta_ms;
    VehicleCandidateRecord records[VEHICLE_CAPTURE_MAX_CANDIDATES];
} VehicleCandidateSample;

/* Renderer-epoch-bounded copied provenance only. Engine pointers are converted
 * to numeric tokens during a synchronous, proven vehicle batch scope and are
 * never dereferenced from this registry. Exact full-handle reuse by a second
 * vehicle remains visible as ambiguity. Renderer epochs are diagnostic queue
 * identities, not native allocation generations. */
typedef struct VehicleSelectionRecord {
    uint64_t descriptor_handle,vehicle;
    uint64_t vehicle_entity_id; /* Copied under native batch lifetime; UINT64_MAX unknown. */
    float distance_squared;
    float position[3];
    uint32_t position_valid;
    LONG publish_manager_call;
    LONG publish_renderer_epoch;
    DWORD tick;
    uint32_t observations;
} VehicleSelectionRecord;

/* Handle-keyed renderer policy identity. The writer publishes epoch_tag last;
 * readers accept only a stable matching tag. Numeric vehicle values are never
 * dereferenced. A handle observed on different owners is permanently ambiguous
 * for that renderer epoch and therefore cannot authorize quality mutation. */
typedef struct VehicleQualityRecord {
    volatile LONG epoch_tag;
    uint32_t ambiguous;
    uint64_t descriptor_handle,vehicle;
    float distance_squared;
} VehicleQualityRecord;

typedef struct VehicleSelectionSnapshot {
    uint32_t count;
    LONG gaps,overflow,rotations;
    uint32_t lock_acquired,publisher_overlap,same_epoch,previous_epoch,older_epoch;
    uint32_t gap_epochs,overflow_epochs;
    VehicleSelectionRecord records[VEHICLE_SELECTION_SNAPSHOT_RECORDS];
} VehicleSelectionSnapshot;

/* Stack-owned matches for current candidate indices. The accompanying snapshot
 * header describes full registry population; its records are not materialized. */
typedef struct VehicleSelectionProjection {
    uint32_t fallback_full_snapshot;
    uint32_t matches[VEHICLE_CAPTURE_MAX_CANDIDATES];
    VehicleSelectionRecord records[VEHICLE_CAPTURE_MAX_CANDIDATES];
} VehicleSelectionProjection;

/* Synchronous manager-call transaction. Native next links are restored
 * immediately after the original manager returns. No engine pointer escapes
 * the hook and incomplete/uncertain captures remain unmodified. */
typedef struct VehicleLimiterChain {
    void *head;
    uint32_t count,captured,suppressed,classified,complete_vehicles;
    uint32_t active,fail_open,evaluated,enabled,degraded;
    uint32_t residency_applied,residency_initialized,residency_replaced;
    uint32_t residency_pending,residency_residents;
    int64_t prepare_stage_ticks[4]; /* Sampled snapshot/copy/classification/selection. */
    uint32_t validation_queries,validation_hits,links_changed;
    unsigned char *nodes[VEHICLE_CAPTURE_MAX_CANDIDATES];
    void *original_next[VEHICLE_CAPTURE_MAX_CANDIDATES];
    VehicleCandidateSample sample;
} VehicleLimiterChain;

typedef struct VehicleResidentSlot {
    uint64_t vehicle;
    uint64_t vehicle_entity_id;
    uint32_t role;
    float distance_squared;
    /* Primary: selected tick. Transition: departure tick, never refreshed.
     * last_seen_tick is observation-only; all times use unsigned tick deltas. */
    DWORD admitted_tick,last_seen_tick;
} VehicleResidentSlot;

typedef struct VehicleApproachRecord {
    uint64_t vehicle,handle;
    DWORD tick;
    float distance,speed;
} VehicleApproachRecord;

/* Capture-scoped observations, never a persistent owner/classification table. */
typedef struct VehicleOwnerRecord {
    void *component,*component_vtable,*settings,*light,*skeleton,*world_transform;
    void *skeleton_header;
    void *spatial,*spatial_root;
    uintptr_t update_caller;
    /* Numeric copies from the enclosing native batch; never dereferenced by workers. */
    void *vehicle,*vehicle_vtable,*vehicle_array;
    uint32_t vehicle_count,vehicle_index,vehicle_flags;
    uint32_t lifetime_slot;
    uint64_t lifetime_sequence;
    uint64_t spatial_handle,descriptor_handle;
    DWORD tick,thread,last_probe_tick;
    uint32_t epoch,valid,transform_seen,transform_mismatch,update_attempts;
    uint32_t skeleton_header_valid;
    int32_t bone_index;
    float delta;
    unsigned char force_update,light_enabled,light_secondary;
} VehicleOwnerRecord;

/* Process-lifetime copied tokens for F10-to-F10 comparison. These values are
 * never dereferenced after their synchronous observation and do not establish
 * a native owner, generation, destruction event or safe runtime classifier. */
typedef struct VehicleIdentityRecord {
    uint64_t component_token,light_token,skeleton_token,descriptor_handle;
    LONG lineage_id,first_serial,last_serial;
    uint32_t captures_seen,total_sample_observations,last_sample_mask;
    uint32_t absent_captures,consecutive_absent;
} VehicleIdentityRecord;

typedef struct VehicleLightDiagnosticState {
    /* Process-lifetime copied storage. state: 0 idle, 1 initializing, 2 taking
     * samples, 3 frozen, 4 worker flushing. writer_active is a nonwaiting token
     * held by one sampler across its synchronous native call, or by commands.
     * No engine thread waits for this token; frozen workers own only copies. */
    volatile LONG state;
    volatile LONG serial;
    volatile LONG sample_count;
    volatile LONG writer_active;
    volatile LONG f10_was_down;
    volatile LONG flush_failures;
    volatile LONG queue_sample_count;
    LONG resolution_enabled;
    LONG handle_link_enabled;
    DWORD started_tick;
    VehicleCandidateSample samples[VEHICLE_CAPTURE_SAMPLES];
    VehicleQueueSample queue_samples[VEHICLE_CAPTURE_SAMPLES];
    /* Module 18 owns observer installation and TLS. TLS borrows a stack scope
     * only across one original update call; worker output never follows it. */
    VehicleLightUpdateFn original_light_update;
    VehicleLightTransformFn original_light_transform;
    VehicleLightBatchFn original_light_batch;
    unsigned char *light_batch_target;
    DWORD batch_tls;
    LONG batch_tls_valid;
    volatile LONG batch_enabled;
    uint32_t batch_tls_failures;
    unsigned char *light_update_target,*light_transform_target;
    DWORD owner_tls;
    LONG owner_tls_valid;
    volatile LONG owner_enabled;
    uint32_t owner_count,owner_overflow,owner_tls_failures;
    LONG owner_focused;
    uint32_t owner_window_records[VEHICLE_CAPTURE_SAMPLES];
    uint32_t owner_window_overflow[VEHICLE_CAPTURE_SAMPLES];
    uint32_t owner_window_probes[VEHICLE_CAPTURE_SAMPLES];
    uint32_t owner_window_unmatched[VEHICLE_CAPTURE_SAMPLES];
    uint32_t owner_window_unreadable[VEHICLE_CAPTURE_SAMPLES];
    DWORD owner_window_tick[VEHICLE_CAPTURE_SAMPLES];
    VehicleOwnerRecord owner_records[VEHICLE_OWNER_MAX_RECORDS];
    /* Module 18's worker is the sole registry writer. F10 remains blocked in
     * state 4 while it updates these copied tokens, so no engine thread waits
     * on or concurrently mutates the registry. */
    uint32_t identity_count,identity_overflow;
    LONG identity_next_lineage;
    VehicleIdentityRecord identity_records[VEHICLE_IDENTITY_MAX_RECORDS];
    /* Module 18 owns nonwaiting copied-provenance hash banks. The manager F10
     * sampler takes one bounded four-epoch snapshot before native selection. */
    volatile LONG selection_enabled,selection_writer_active,selection_publish_inflight;
    volatile LONG selection_gaps,selection_overflow,selection_rotations;
    /* Epoch plus one; zero is the never-invalidated sentinel. */
    volatile LONG selection_gap_epoch_tag[VEHICLE_SELECTION_EPOCH_BANKS];
    volatile LONG selection_overflow_epoch_tag[VEHICLE_SELECTION_EPOCH_BANKS];
    /* 0 means default-on before initialization, 1 enabled, 2 disabled. */
    volatile LONG limiter_calls,limiter_active_calls,limiter_fail_open_calls;
    volatile LONG limiter_bypass_disabled,limiter_fail_unavailable;
    volatile LONG limiter_fail_snapshot_lock,limiter_fail_gap_epoch;
    volatile LONG limiter_fail_publisher_overlap;
    volatile LONG limiter_fail_overflow_epoch,limiter_fail_candidate_chain;
    volatile LONG limiter_suppressed,limiter_peak_suppressed;
    volatile LONG limiter_degraded_calls,limiter_degraded_publisher_overlap;
    volatile LONG limiter_degraded_gap_epoch,limiter_degraded_overflow_epoch;
    volatile LONG residency_lock,residency_reset_requested;
    /* Internal-only temporary test control; zero keeps the normal +2. */
    volatile LONG extra_slots_disabled;
    volatile LONG residency_applied_calls,residency_lock_bypass;
    volatile LONG residency_initializations,residency_replacements;
    volatile LONG residency_pending_holds,residency_dropped;
    volatile LONG quality_overflow,quality_mutated_calls;
    volatile LONG quality_mutated_type1,quality_mutated_type3;
    volatile LONG quality_mutated_entries,quality_unknown_entries;
    volatile LONG quality_ambiguous_entries;
    /* Process totals; uninspected under-cap lights have no identity labels. */
    volatile LONG quality_under_cap_entries,quality_inspected_entries;
    volatile LONG quality_fast_check_unavailable_calls;
    /* Internal failure telemetry only; zero-cost outside failed lock paths. */
    volatile LONG quality_lock_skips,world_quality_lock_skips;
    /* Residency lock owns these Internal observations, not selection policy. */
    DWORD freeze_started,freeze_peak_ms;
    uint32_t freeze_active,freeze_calls,freeze_peak_calls;
    /* Patch-owned storage, not native shadow capacity. Count owned by the
     * residency lock; each pass uses one atomic policy configuration snapshot. */
    uint32_t residency_stable_count;
    uint32_t residency_extra_slots;
    /* Immutable verified local-player root; no retained live entity pointer. */
    unsigned char *player_root_address;
    volatile LONG driver_proof_ready; /* Optional verified read-only identity path. */
    VehicleApproachRecord approach[256]; /* Residency-lock-owned, bounded scalar history. */
    VehicleResidentSlot residency_slots[VEHICLE_SELECTION_MAX_RESIDENTS];
    LONG selection_bank_epoch[VEHICLE_SELECTION_EPOCH_BANKS];
    uint32_t selection_bank_count[VEHICLE_SELECTION_EPOCH_BANKS];
    /* Module18 publication-lock-owned derived index. Links encode slot+1;
     * zero initialization is empty. Reset heads at bank reuse; next is read
     * only for linked active slots. No independent identity/eviction lifetime. */
    uint32_t selection_heads[VEHICLE_SELECTION_EPOCH_BANKS][VEHICLE_SELECTION_SLOTS_PER_EPOCH];
    uint32_t selection_next[VEHICLE_SELECTION_EPOCH_BANKS][VEHICLE_SELECTION_SLOTS_PER_EPOCH];
    VehicleSelectionRecord
        selection_records[VEHICLE_SELECTION_EPOCH_BANKS][VEHICLE_SELECTION_SLOTS_PER_EPOCH];
    VehicleQualityRecord
        quality_records[VEHICLE_SELECTION_EPOCH_BANKS][VEHICLE_SELECTION_SLOTS_PER_EPOCH];
} VehicleLightDiagnosticState;

#define INTERSECTION_EVENTS 8192U
#define INTERSECTION_ANOMALIES 128U
#define INTERSECTION_RECORDS 256U
#define INTERSECTION_TIMEOUT_MS 60000U
#define INTERSECTION_POST_MS 1500U
typedef struct IntersectionEvent {
    DWORD tick,thread;
    uint32_t sequence,kind,valid_mask,changed_mask,mismatch_mask;
    LONG generation,cycle,builder;
    void *record,*queue,*value;
    uintptr_t caller;
    void *fields[5],*expected[3],*faces[6];
    uint32_t data[6];
} IntersectionEvent;
typedef struct IntersectionRecord {
    void *record;
    LONG capture,generation;
    uint32_t expected_mask,last_valid;
    void *expected[3],*last[5];
} IntersectionRecord;
typedef struct IntersectionDiagnosticState {
    /* 0 idle, 2 recording, 3 frozen/retryable, 4 worker owns immutable buffer. */
    volatile LONG state,lock,capture,requests,contention,slot_misses,flush_failures;
    volatile LONG f7_down;
    DWORD started,marked,last_queue_sample,tls_index;
    int tls_valid,marker_seen;
    void *lookup_target;
    IntersectionQueueWorkFn queue_work;
    uintptr_t lookup_returns[3];
    uint32_t sequence,count,anomaly_count,anomaly_overflow;
    IntersectionRecord records[INTERSECTION_RECORDS];
    IntersectionEvent events[INTERSECTION_EVENTS];
    IntersectionEvent anomalies[INTERSECTION_ANOMALIES];
} IntersectionDiagnosticState;

#define VEHICLE_LIFETIME_MAX_RECORDS 128U
#define VEHICLE_BATCH_MATCH_FLAGS 63U
/* Copied observations only. Slots never evict; saturation is an explicit gap.
 * state 0=observed, 1=teardown in progress, 2=teardown returned. */
typedef struct VehicleLifetimeRecord {
    uint64_t owner,vtable,array,skeleton;
    uint64_t first_sequence,last_sample_sequence,enter_sequence,return_sequence;
    uint32_t array_count,state,observations,retire_entries,retire_returns;
    uint32_t reappearances,tuple_changes,samples_during_retire,overlaps;
    LONG first_serial,last_serial;
    DWORD enter_tick,return_tick,enter_thread,return_thread;
} VehicleLifetimeRecord;

typedef struct VehicleLifetimeSnapshot {
    uint64_t sequence;
    uint32_t count;
    LONG gaps,overflow,enabled;
    VehicleLifetimeRecord records[VEHICLE_LIFETIME_MAX_RECORDS];
} VehicleLifetimeSnapshot;

typedef struct VehicleLifetimeDiagnosticState {
    /* Module 18 lifetime observer owns all live records under a nonwaiting
     * token; F10 worker alone owns snapshot after copying under that token. */
    volatile LONG enabled,writer_active,gaps,overflow;
    unsigned char *retire_target;
    VehicleRetireFn original_retire;
    uint64_t sequence;
    uint32_t count;
    VehicleLifetimeRecord records[VEHICLE_LIFETIME_MAX_RECORDS];
    VehicleLifetimeSnapshot snapshot;
} VehicleLifetimeDiagnosticState;

/* Module17 owns publication of process-lifetime requested policy; module16
 * decodes it and routes controls. Hooks retain their synchronous snapshots. */
typedef struct ShadowPolicyControlState {
    /* Atomic configuration: bits0-1 bypass, bits8-11 main count, bits16-18
     * resolution index. Zero fields mean four/2048. Module17 is sole writer. */
    volatile LONG disabled;
    /* Independent fixed cap for non-vehicle local lights. Zero means original
     * native quality; the encoded resolution is valid only when enabled. */
    volatile LONG world_configuration;
    volatile LONG quality_ready;
} ShadowPolicyControlState;

/* Only the diagnostic worker owns paths/session/sequence. Hooks exchange copied
 * command bits; no file I/O or Lua state crosses that boundary. */
typedef struct InternalToolState {
    struct SupportReportState *support_report; /* Module76, mailbox-worker lifetime. */
    volatile LONG started,capture_requests;
    DWORD session;
    unsigned long acknowledged;
    unsigned long settings_processed;
    int settings_result;
    DWORD settings_error;
} InternalToolState;

typedef struct ShadowEngineContext {
#if SHADOW_ENGINE_INTERNAL_DIAGNOSTICS
    /* Internal workload module owns this process-lifetime allocation. */
    struct ShadowWorkloadState *workload;
    /* Module19 owns explicitly armed population history for process lifetime. */
    struct PopulationCaptureState *population;
    /* Module12 owns bounded, process-lifetime overhead capture storage. */
    void *overhead;
    /* Module19 owns explicit detailed-observation windows for process lifetime. */
    void *detail_window;
    /* Module14 owns a bounded copied residency mailbox; worker drains it. */
    PeriodicSummaryState periodic_summary;
#endif
    ShadowPolicyControlState policy_control;
    SavedSettingsState saved_settings;
    InternalToolState internal_tools;
    BootstrapState bootstrap;
    HookBindings hooks;
    ManagerRendererState renderer;
    WorldLightQualityState world_quality;
    ResourcePassState resources;
    ExternalResultState external_results;
    CompletionDiagnosticState completion_diagnostics;
    VehicleLightDiagnosticState vehicle_diagnostics;
    VehicleLifetimeDiagnosticState vehicle_lifetime;
    IntersectionDiagnosticState intersection;
} ShadowEngineContext;

static ShadowEngineContext g_shadow_engine;


static void __fastcall hooked_shadow_manager(void *manager,void *candidates,
    uint32_t candidate_count,void *bindings,void *context,void *view_position,
    void *call_context);
static void __fastcall hooked_renderer_queue(void *manager,void *queue,
    uint64_t argument3,void *argument4,void *argument5,uint64_t argument6,
    uint64_t argument7,uint64_t argument8,uint64_t argument9);
static uint32_t __fastcall hooked_face_cost_reader(void *queue);
static void *__fastcall hooked_external_render_record_constructor(void *);
static void __fastcall hooked_external_frame_builder(void *,void *,void *,
    uint64_t,uint64_t,uint64_t,uint64_t);
static void __fastcall hooked_external_render_record_release(void *,void *,
    unsigned char);
static void maybe_log_external_slice_summary(void);
static void log_manual_external_slice_snapshot(const char *state,LONG serial);
static int begin_external_slice_producer_cycle(void *);
#if SHADOW_ENGINE_INTERNAL_DIAGNOSTICS
static DetailProducerTicket completion_diagnostic_begin_producer_batch(void *);
static void completion_diagnostic_seal_producer_batch(void *,DetailProducerTicket *);
static void completion_diagnostic_record_completion(void *,uint32_t,LONG,
    LONG,LONG);
static void completion_diagnostic_mark_pre_finalizer(void *);
static void completion_diagnostic_mark_reset(void *);
static void completion_diagnostic_log_summary(void);
static void completion_diagnostic_log_manual(const char *,LONG);
#endif
static int readable_memory(const void *address,size_t size);
