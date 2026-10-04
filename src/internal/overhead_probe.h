#ifndef SHADOW_ENGINE_OVERHEAD_PROBE_H
#define SHADOW_ENGINE_OVERHEAD_PROBE_H

/* Standalone source-fragment fixtures are uninstrumented unless explicitly
 * enabled. Module 00 selects the real build profile before this interface. */
#ifndef SHADOW_ENGINE_OVERHEAD_MEASUREMENT
#define SHADOW_ENGINE_OVERHEAD_MEASUREMENT 0
#endif

typedef enum OverheadCategory {
    OH_VEHICLE_BATCH_PATCH, OH_VEHICLE_UPDATE_PATCH, OH_VEHICLE_TRANSFORM_PATCH,
    OH_VEHICLE_PUBLISH, OH_VEHICLE_RETIRE_PATCH, OH_MANAGER_PATCH,
    OH_MANAGER_NATIVE, OH_MANAGER_ADMISSION, OH_LIMITER_PREPARE,
    OH_LIMITER_RESTORE, OH_RENDERER_PATCH, OH_RENDERER_NATIVE, OH_QUEUE_CLAMP,
    OH_QUEUE_TAIL, OH_VEHICLE_QUALITY, OH_WORLD_QUALITY, OH_PRODUCER_OPEN,
    OH_COMPLETION_BEGIN, OH_COMPLETION_SEAL, OH_COMPLETION_SCHEDULE,
    OH_SCHEDULER_NATIVE, OH_COMPLETION_RECORD, OH_COMPLETION_MARKERS,
    OH_EXTERNAL_STORE, OH_EXTERNAL_CONSUME, OH_EXTERNAL_RESET,
    OH_RESOURCE_SUBMIT_NATIVE, OH_LIFECYCLE_PATCH, OH_QUEUE_CAPACITY,
    OH_FACE_COST_READER, OH_CAPTURE_OBSERVER, OH_LOG_WRITE, OH_LOG_FLUSH,
    OH_DRIVER_IDENTITY, OH_DRIVER_PUBLICATION,
    OH_CATEGORY_COUNT
} OverheadCategory;

#if SHADOW_ENGINE_OVERHEAD_MEASUREMENT
typedef struct OverheadTicket {
    void *state; /* Patch-owned process-lifetime storage; never an engine object. */
    unsigned category, slot, running, invalid, segments;
    int64_t started, elapsed;
} OverheadTicket;

static OverheadTicket overhead_begin(OverheadCategory category);
static void overhead_end(OverheadTicket *ticket);
static void overhead_pause(OverheadTicket *ticket);
static void overhead_resume(OverheadTicket *ticket);
static int overhead_request(unsigned workload_serial,DWORD lead_ms,DWORD window_ms);
static void overhead_close(void);
static int overhead_ready(void);
static void overhead_report(void);

#define OVERHEAD_BEGIN(name,category) OverheadTicket name=overhead_begin(category)
#define OVERHEAD_END(name) overhead_end(&(name))
#define OVERHEAD_PAUSE(name) overhead_pause(&(name))
#define OVERHEAD_RESUME(name) overhead_resume(&(name))
#else
#define OVERHEAD_BEGIN(name,category)
#define OVERHEAD_END(name) ((void)0)
#define OVERHEAD_PAUSE(name) ((void)0)
#define OVERHEAD_RESUME(name) ((void)0)
#endif

#endif
