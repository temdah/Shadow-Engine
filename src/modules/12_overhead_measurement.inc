/* H1: bounded wall-interval sampling of patch work, independent of observer
 * arming. One worker requests/closes/reports. Callback admission never waits.
 * Published storage lasts for the process; a closed window is not reusable
 * until every admitted ticket has released its writer reference. */
#include "../internal/detail_window.h"
#define OVERHEAD_BUCKET_MS 10U
#define OVERHEAD_SAMPLE_SLOTS 500U
#define OVERHEAD_MAX_WINDOW_MS (OVERHEAD_BUCKET_MS*OVERHEAD_SAMPLE_SLOTS)
#define OVERHEAD_CALIBRATION_BATCHES 16U
#define OVERHEAD_CALIBRATION_CALLS 128U

typedef struct OverheadSample {
    volatile LONG state; /* 0 empty, 1 exclusively reserved, 2 published */
    DWORD thread;
    unsigned valid,segments,ended_after_window;
    int64_t ticks;
} OverheadSample;

typedef struct OverheadStream {
    volatile LONG calls,collisions;
    OverheadSample samples[OVERHEAD_SAMPLE_SLOTS];
} OverheadStream;

typedef struct OverheadState {
    volatile LONG active,inflight; /* NEVER reset inflight or reuse active IDs. */
    LONG next_token;
    unsigned serial,reported;
    DWORD requested_at,lead_ms,window_ms;
    int64_t frequency;
    DetailWindowStatus detail_begin,detail_end;
    volatile LONG qpc_calls,timer_failures;
    unsigned calibration_valid;
    double timer_min,timer_mean,timer_max;
    double idle_min,idle_mean,idle_max;
    double busy_min,busy_mean,busy_max;
    OverheadStream streams[OH_CATEGORY_COUNT];
} OverheadState;

static const char *const overhead_names[OH_CATEGORY_COUNT]={
    "vehicleBatchPatch","vehicleUpdatePatch","vehicleTransformPatch",
    "vehiclePublish","vehicleRetirePatch","managerPatch","managerNative",
    "managerAdmission","limiterPrepare","limiterRestore","rendererPatch",
    "rendererNative","queueClamp","queueTail","vehicleQuality","worldQuality",
    "producerOpen","completionBegin","completionSeal","completionSchedule",
    "schedulerNative","completionRecord","completionMarkers","externalStore",
    "externalConsume","externalReset","resourceSubmitNative","lifecyclePatch",
    "queueCapacity","faceCostReader","captureObserver","logWrite","logFlush",
    "driverIdentity","driverPublication"
};

static OverheadState *overhead_state(void)
{
    return (OverheadState *)InterlockedCompareExchangePointer(
        (void *volatile *)&g_shadow_engine.overhead,NULL,NULL);
}

static int overhead_clock(OverheadState *s,int64_t *value)
{
    LARGE_INTEGER now;
    atomic_add_long(&s->qpc_calls,1);
    if(!QueryPerformanceCounter(&now)) {
        atomic_add_long(&s->timer_failures,1);
        return 0;
    }
    *value=now.QuadPart;
    return 1;
}

static OverheadTicket overhead_enter_state(OverheadState *s,OverheadCategory category)
{
    OverheadTicket ticket={0};
    OverheadStream *stream;
    OverheadSample *sample;
    LONG token;
    DWORD elapsed;
    unsigned slot;
    if(!s || (unsigned)category>=OH_CATEGORY_COUNT) return ticket;
    token=InterlockedCompareExchange(&s->active,0,0);
    if(token<=0) return ticket;
    atomic_add_long(&s->inflight,1);
    /* A worker may close and reuse buffers before a delayed entrant increments
     * inflight. The unique token recheck rejects that entrant before data access.
     * Inflight itself is process-lifetime and is never included in a reset. */
    if(InterlockedCompareExchange(&s->active,0,0)!=token) goto leave;
    elapsed=GetTickCount()-s->requested_at;
    if(elapsed<s->lead_ms || elapsed-s->lead_ms>=s->window_ms) goto leave;
    slot=(unsigned)((elapsed-s->lead_ms)/OVERHEAD_BUCKET_MS);
    if(slot>=OVERHEAD_SAMPLE_SLOTS) goto leave;
    stream=&s->streams[category];
    atomic_add_long(&stream->calls,1);
    sample=&stream->samples[slot];
    if(InterlockedCompareExchange(&sample->state,1,0)!=0) {
        atomic_add_long(&stream->collisions,1);
        goto leave;
    }
    ticket.state=s; ticket.category=(unsigned)category; ticket.slot=slot;
    sample->thread=GetCurrentThreadId();
    if(overhead_clock(s,&ticket.started)) { ticket.running=1; ticket.segments=1; }
    else ticket.invalid=1;
    return ticket;
leave:
    atomic_add_long(&s->inflight,-1);
    return ticket;
}

static OverheadTicket overhead_begin(OverheadCategory category)
{
    return overhead_enter_state(overhead_state(),category);
}

static void overhead_pause(OverheadTicket *ticket)
{
    OverheadState *s;
    int64_t now;
    if(!ticket || !ticket->state || !ticket->running) return;
    s=(OverheadState *)ticket->state;
    if(!overhead_clock(s,&now)) ticket->invalid=1;
    else if(now<ticket->started) {
        ticket->invalid=1;
        atomic_add_long(&s->timer_failures,1);
    } else ticket->elapsed+=now-ticket->started;
    ticket->running=0;
}

static void overhead_resume(OverheadTicket *ticket)
{
    if(!ticket || !ticket->state || ticket->running) return;
    if(overhead_clock((OverheadState *)ticket->state,&ticket->started)) {
        ticket->running=1; ++ticket->segments;
    } else ticket->invalid=1;
}

static void overhead_end(OverheadTicket *ticket)
{
    OverheadState *s;
    OverheadSample *sample;
    if(!ticket || !ticket->state) return;
    s=(OverheadState *)ticket->state;
    overhead_pause(ticket);
    sample=&s->streams[ticket->category].samples[ticket->slot];
    sample->ticks=ticket->elapsed;
    sample->segments=ticket->segments;
    sample->valid=!ticket->invalid && ticket->segments!=0;
    sample->ended_after_window=(GetTickCount()-s->requested_at-s->lead_ms)>=s->window_ms;
    InterlockedExchange(&sample->state,2);
    ticket->state=NULL;
    atomic_add_long(&s->inflight,-1);
}

static void overhead_calibration_add(double value,unsigned index,
    double *minimum,double *mean,double *maximum)
{
    if(!index || value<*minimum) *minimum=value;
    if(!index || value>*maximum) *maximum=value;
    *mean+=value/OVERHEAD_CALIBRATION_BATCHES;
}

/* Worker-only raw local calibration. Busy gates use separate unpublished
 * storage with occupied buckets, so no engine callback is admitted by this
 * calibration. This measures this machine's uncontended mechanism, not live
 * contention, scheduler preemption or the complete observer's CPU cost. */
static void overhead_calibrate(OverheadState *s)
{
    OverheadState *scratch=(OverheadState *)calloc(1,sizeof(*scratch));
    LARGE_INTEGER before,after;
    unsigned i,j;
    double scale=1000000.0/(double)s->frequency;
    s->calibration_valid=0;
    s->timer_min=s->timer_mean=s->timer_max=0;
    s->idle_min=s->idle_mean=s->idle_max=0;
    s->busy_min=s->busy_mean=s->busy_max=0;
    if(!scratch) return;
    scratch->frequency=s->frequency;
    scratch->requested_at=GetTickCount();
    scratch->window_ms=OVERHEAD_MAX_WINDOW_MS;
    for(i=0;i<OVERHEAD_SAMPLE_SLOTS;++i)
        scratch->streams[OH_QUEUE_CLAMP].samples[i].state=2;
    for(i=0;i<OVERHEAD_CALIBRATION_BATCHES;++i) {
        if(!QueryPerformanceCounter(&before) || !QueryPerformanceCounter(&after) ||
           after.QuadPart<before.QuadPart) goto done;
        overhead_calibration_add((after.QuadPart-before.QuadPart)*scale,i,
            &s->timer_min,&s->timer_mean,&s->timer_max);
        if(!QueryPerformanceCounter(&before)) goto done;
        for(j=0;j<OVERHEAD_CALIBRATION_CALLS;++j) {
            OverheadTicket ticket=overhead_enter_state(scratch,OH_QUEUE_CLAMP);
            overhead_end(&ticket);
        }
        if(!QueryPerformanceCounter(&after) || after.QuadPart<before.QuadPart) goto done;
        overhead_calibration_add((after.QuadPart-before.QuadPart)*scale/OVERHEAD_CALIBRATION_CALLS,i,
            &s->idle_min,&s->idle_mean,&s->idle_max);
        scratch->active=1;
        if(!QueryPerformanceCounter(&before)) goto done;
        for(j=0;j<OVERHEAD_CALIBRATION_CALLS;++j) {
            OverheadTicket ticket=overhead_enter_state(scratch,OH_QUEUE_CLAMP);
            overhead_end(&ticket);
        }
        if(!QueryPerformanceCounter(&after) || after.QuadPart<before.QuadPart) goto done;
        scratch->active=0;
        overhead_calibration_add((after.QuadPart-before.QuadPart)*scale/OVERHEAD_CALIBRATION_CALLS,i,
            &s->busy_min,&s->busy_mean,&s->busy_max);
    }
    /* A long worker suspension can move the coarse clock beyond the temporary
     * window; those iterations measured closed admission, not occupied gates. */
    s->calibration_valid=scratch->streams[OH_QUEUE_CLAMP].calls==
        OVERHEAD_CALIBRATION_BATCHES*OVERHEAD_CALIBRATION_CALLS;
done:
    free(scratch);
}

static int overhead_request(unsigned workload_serial,DWORD lead_ms,DWORD window_ms)
{
    OverheadState *s=overhead_state();
    LARGE_INTEGER frequency;
    if(!window_ms || window_ms>OVERHEAD_MAX_WINDOW_MS || lead_ms>60000U ||
       !QueryPerformanceFrequency(&frequency) || frequency.QuadPart<=0) return 0;
    if(!s) {
        s=(OverheadState *)calloc(1,sizeof(*s));
        if(!s) return 0;
        InterlockedExchangePointer((void *volatile *)&g_shadow_engine.overhead,s);
    }
    if(InterlockedCompareExchange(&s->active,0,0) ||
       InterlockedCompareExchange(&s->inflight,0,0) ||
       s->next_token==0x7FFFFFFFL) return 0;
    memset(s->streams,0,sizeof(s->streams));
    s->qpc_calls=0; s->timer_failures=0;
    s->frequency=frequency.QuadPart;
    s->serial=workload_serial; s->reported=0;
    s->lead_ms=lead_ms; s->window_ms=window_ms;
    overhead_calibrate(s);
    s->detail_begin=detail_window_status();
    s->detail_end=s->detail_begin;
    s->requested_at=GetTickCount();
    ++s->next_token;
    InterlockedExchange(&s->active,s->next_token);
    return 1;
}

static void overhead_close(void)
{
    OverheadState *s=overhead_state();
    if(s) {
        InterlockedExchange(&s->active,0);
        s->detail_end=detail_window_status();
    }
}

static int overhead_ready(void)
{
    OverheadState *s=overhead_state();
    return !s || (!InterlockedCompareExchange(&s->active,0,0) &&
        !InterlockedCompareExchange(&s->inflight,0,0));
}

static int overhead_compare_ticks(const void *left,const void *right)
{
    int64_t a=*(const int64_t *)left,b=*(const int64_t *)right;
    return a<b?-1:(a>b?1:0);
}

static void overhead_report(void)
{
    OverheadState *s=overhead_state();
    unsigned category;
    double scale;
    if(!s || !s->next_token || s->reported || !overhead_ready()) return;
    s->reported=1;
    scale=1000000.0/(double)s->frequency;
    append_log("STAGE_OVERHEAD_BEGIN serial=%u version=%s section=4 build=P-A3 workRegister=P1 purpose=wholePassPopulationRecording retained=H3,H4 instrumentation=H1,H2 observationMode=windowed baseline=0 windowMs=%lu bucketMs=%u slotsPerCategory=%u stateBytes=%u clock=GetTickCount sampledClock=QPC sampling=firstEligiblePerCategoryBucket nativePointersRetained=0",
        s->serial,PATCH_VERSION,(unsigned long)s->window_ms,OVERHEAD_BUCKET_MS,OVERHEAD_SAMPLE_SLOTS,(unsigned)sizeof(*s));
    append_log("STAGE_OVERHEAD_DETAIL_MODE serial=%u startEpoch=%ld startPhase=%ld endEpoch=%ld endPhase=%ld changedSinceRequest=%u statusAvailable=%u phaseLegend=-1-unknown,0-off,1-open,2-draining,3-frozen changeScope=includesLeadIn measurementArmsDetail=0 admissionAndTailCountersScope=recordedWindowsOnly",
        s->serial,(long)s->detail_begin.epoch,(long)s->detail_begin.phase,
        (long)s->detail_end.epoch,(long)s->detail_end.phase,
        s->detail_begin.epoch!=s->detail_end.epoch ||
        s->detail_begin.phase!=s->detail_end.phase ||
        s->detail_begin.change!=s->detail_end.change,
        s->detail_begin.phase>=0 && s->detail_end.phase>=0);
    for(category=0;category<OH_CATEGORY_COUNT;++category) {
        OverheadStream *stream=&s->streams[category];
        int64_t ordered[OVERHEAD_SAMPLE_SLOTS],sum=0;
        DWORD threads[8]={0};
        unsigned i,j,n=0,invalid=0,after=0,reserved=0,segments=0,thread_count=0,other_thread_samples=0;
        for(i=0;i<OVERHEAD_SAMPLE_SLOTS;++i) {
            OverheadSample *sample=&stream->samples[i];
            if(!sample->state) continue;
            ++reserved;
            if(sample->state!=2 || !sample->valid) { ++invalid; continue; }
            ordered[n++]=sample->ticks; sum+=sample->ticks;
            segments+=sample->segments; after+=sample->ended_after_window;
            for(j=0;j<thread_count;++j) if(threads[j]==sample->thread) break;
            if(j==thread_count) {
                if(thread_count<8U) threads[thread_count++]=sample->thread;
                else ++other_thread_samples;
            }
        }
        qsort(ordered,n,sizeof(ordered[0]),overhead_compare_ticks);
        append_log("STAGE_OVERHEAD_CATEGORY serial=%u category=%s calls=%ld callsPerSecond=%.3f samples=%u occupiedBuckets=%u invalidSamples=%u collisions=%ld sampleHz=%.3f meanUs=%.6f p50Us=%.6f p95Us=%.6f sampleMaxUs=%.6f sampledSumUs=%.6f timedSegments=%u endedAfterWindow=%u sampledThreadsCapped8=%u otherThreadSamples=%u coverage=%s",
            s->serial,overhead_names[category],(long)stream->calls,
            stream->calls*1000.0/s->window_ms,n,reserved,invalid,(long)stream->collisions,
            n*1000.0/s->window_ms,n?sum*scale/n:-1.0,
            n?ordered[(n-1U)/2U]*scale:-1.0,
            n?ordered[(n*95U+99U)/100U-1U]*scale:-1.0,
            n?ordered[n-1U]*scale:-1.0,sum*scale,segments,after,thread_count,other_thread_samples,
            !stream->calls?"notObserved":(n?"sampled":"timingUnavailable"));
    }
    append_log("STAGE_OVERHEAD_OBSERVER serial=%u qpcCalls=%ld timerFailures=%ld calibrationValid=%u timerPairMinUs=%.6f timerPairMeanUs=%.6f timerPairMaxUs=%.6f idleGateMinUs=%.6f idleGateMeanUs=%.6f idleGateMaxUs=%.6f occupiedGateMinUs=%.6f occupiedGateMeanUs=%.6f occupiedGateMaxUs=%.6f calibration=uncontendedLocal gateCalibrationExcludesRootPointerLoad=1 calibrationSubtracted=0 liveContentionCost=notMeasured",
        s->serial,(long)s->qpc_calls,(long)s->timer_failures,s->calibration_valid,
        s->timer_min,s->timer_mean,s->timer_max,s->idle_min,s->idle_mean,s->idle_max,
        s->busy_min,s->busy_mean,s->busy_max);
    append_log("STAGE_OVERHEAD_END serial=%u cpuWallIntervals=1 exactTotalCpu=notMeasured gpuTime=notMeasured nestedIntervalsNotAdditive=1 patchEnvelopesIncludeChildObservers=1 asyncCallbacksMayOverlap=1 firstCallThreadBurstBias=1 sampleMaxNotPopulationPeak=1 coarseCadenceMaySkipBuckets=1 zeroCallsMeansNotObserved=1",
        s->serial);
}
