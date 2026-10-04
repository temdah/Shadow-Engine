/* Internal bounded observations. One worker owns capture lifetime and output;
 * manager/renderer use a nonwaiting lock and never retain native pointers.
 * 50 fixed slots per stream, at most 10 observations/second, no idle scans. */
#include <math.h>
#include <stdlib.h>
#include "../internal/overhead_probe.h"
#define WORKLOAD_SECONDS 5U
#define WORKLOAD_SAMPLES (WORKLOAD_SECONDS*10U)
typedef struct WorkloadOwner {
    uint64_t owner,handle;
    float distance_squared;
    DWORD age_ms,published;
    uint32_t lights,kept;
} WorkloadOwner;
typedef struct WorkloadManagerSample {
    uint32_t valid,candidates,owners,matched,unknown,suppressed,kept,enabled,fail_open,degraded;
    DWORD elapsed_ms;
    uint32_t player_anchor_valid;
    int64_t prepare_ticks,observer_ticks;
    int64_t stages[4];
    uint32_t validation_queries,validation_hits,links_changed;
    int64_t native_ticks,restore_ticks;
    uint32_t timing_complete;
    WorkloadOwner owner[VEHICLE_SELECTION_MAX_VEHICLES];
} WorkloadManagerSample;
typedef struct WorkloadQueueSample {
    uint32_t valid,entries,faces,requested_entries,requested_faces;
    uint32_t headlights,type1,other,unknown,ambiguous,unreadable,owners;
    int64_t observer_ticks;
    int64_t native_ticks;
    uint32_t timing_complete,handle_count,missing_handles;
    uint64_t owner_tokens[TOTAL_LOCAL_MAPS],handles[TOTAL_LOCAL_MAPS];
} WorkloadQueueSample;
typedef struct ShadowWorkloadState {
    volatile LONG active,lock,skipped,inflight;
    unsigned serial,phase; /* worker: 0 idle, 1 capture, 2 draining */
    unsigned notice; /* worker: 1 lead-in, 2 sampling, 3 draining, 4 saved, 5 start/save failed */
    LONG policy,world,changes;
    int64_t start,end,frequency,manager_before,observer_before;
    uint32_t manager_slot,drain_warning;
    WorkloadPresent present;
    WorkloadManagerSample managers[WORKLOAD_SAMPLES];
    WorkloadQueueSample queues[WORKLOAD_SAMPLES];
    VehicleCandidateSample scratch;
    VehicleSelectionSnapshot snapshot;
} ShadowWorkloadState;

/* Exclusive patch-owned slot, never an engine pointer. Reserve under the
 * capture lock; release after native work. Drain forbids slot reuse meanwhile. */
typedef struct WorkloadTimingTicket {
    ShadowWorkloadState *state;
    uint32_t slot,queue;
    int64_t started;
} WorkloadTimingTicket;

static WorkloadTimingTicket workload_reserve_manager(int active)
{
    WorkloadTimingTicket ticket={0};
    if(active) {
        ticket.state=g_shadow_engine.workload;
        ticket.slot=ticket.state->manager_slot;
        InterlockedIncrement(&ticket.state->inflight);
    }
    return ticket;
}

static void workload_timing_start(WorkloadTimingTicket *ticket)
{
    if(ticket->state) ticket->started=workload_qpc();
}

static void workload_timing_stop(WorkloadTimingTicket *ticket,int restore)
{
    int64_t elapsed;
    if(!ticket->state) return;
    elapsed=workload_qpc()-ticket->started;
    if(ticket->queue) ticket->state->queues[ticket->slot].native_ticks=elapsed;
    else if(restore) ticket->state->managers[ticket->slot].restore_ticks=elapsed;
    else ticket->state->managers[ticket->slot].native_ticks=elapsed;
}

static void workload_timing_release(WorkloadTimingTicket *ticket)
{
    ShadowWorkloadState *w=ticket->state;
    if(!w) return;
    if(ticket->queue) w->queues[ticket->slot].timing_complete=1;
    else w->managers[ticket->slot].timing_complete=1;
    ticket->state=NULL;
    InterlockedDecrement(&w->inflight);
}

static ShadowWorkloadState *workload_state(void)
{
    return (ShadowWorkloadState *)InterlockedCompareExchangePointer(
        (void *volatile *)&g_shadow_engine.workload,NULL,NULL);
}

static ShadowWorkloadState *workload_enter(uint32_t *slot,int64_t *now)
{
    ShadowWorkloadState *w=workload_state();
    if(!w || !InterlockedCompareExchange(&w->active,0,0)) return NULL;
    *now=workload_qpc();
    if(*now<w->start || *now>=w->end) return NULL;
    if(InterlockedCompareExchange(&w->lock,1,0)) {
        InterlockedIncrement(&w->skipped); return NULL;
    }
    if(!w->active) { InterlockedExchange(&w->lock,0); return NULL; }
    *slot=(uint32_t)((*now-w->start)*10/w->frequency);
    if(*slot>=WORKLOAD_SAMPLES) { InterlockedExchange(&w->lock,0); return NULL; }
    if(w->policy!=InterlockedCompareExchange(&g_shadow_engine.policy_control.disabled,0,0) ||
       w->world!=InterlockedCompareExchange(&g_shadow_engine.policy_control.world_configuration,0,0))
        w->changes=1;
    return w;
}

static int workload_manager_begin(void *candidates,uint32_t count,LONG call)
{
    uint32_t slot,i;
    int64_t now;
    unsigned char *node=(unsigned char *)candidates;
    ShadowWorkloadState *w=workload_enter(&slot,&now);
    if(!w) return 0;
    if(w->managers[slot].valid || !count || count>VEHICLE_CAPTURE_MAX_CANDIDATES)
        goto skip;
    w->observer_before=now;
    memset(&w->scratch,0,sizeof(w->scratch));
    w->scratch.tick=GetTickCount(); w->scratch.manager_call=call;
    w->scratch.renderer_epoch=InterlockedCompareExchange(&g_shadow_engine.renderer.renderer_calls,0,0);
    w->scratch.reported_candidates=count;
    snapshot_vehicle_selection(&w->snapshot,w->scratch.renderer_epoch);
    if(!w->snapshot.lock_acquired) goto skip;
    for(i=0;i<count && node;++i) {
        if(!copy_vehicle_candidate_fields(&w->scratch.records[i],node,0)) goto skip;
        node=(unsigned char *)w->scratch.records[i].next;
    }
    if(i!=count || node) goto skip;
    w->scratch.copied_candidates=count; w->scratch.chain_ended=1;
    classify_vehicle_selection_sample(&w->scratch,&w->snapshot);
    w->manager_slot=slot;
    w->manager_before=workload_qpc();
    return 1; /* Lock released immediately after preparation, before native work. */
skip:
    InterlockedIncrement(&w->skipped);
    InterlockedExchange(&w->lock,0);
    return 0;
}

static void workload_manager_end(int ticket,const VehicleLimiterChain *chain)
{
    ShadowWorkloadState *w;
    WorkloadManagerSample *s;
    int64_t end;
    uint32_t i,j;
    if(!ticket) return;
    end=workload_qpc(); w=workload_state();
    s=&w->managers[w->manager_slot];
    s->prepare_ticks=end-w->manager_before;
    memcpy(s->stages,chain->prepare_stage_ticks,sizeof(s->stages));
    s->validation_queries=chain->validation_queries;
    s->validation_hits=chain->validation_hits; s->links_changed=chain->links_changed;
    s->elapsed_ms=(DWORD)((w->manager_before-w->start)*1000/w->frequency);
    s->candidates=w->scratch.copied_candidates; s->suppressed=chain->suppressed;
    s->player_anchor_valid=w->scratch.player_anchor_valid;
    s->kept=chain->count; s->enabled=chain->enabled;
    s->fail_open=chain->fail_open; s->degraded=chain->degraded;
    for(i=0;i<w->scratch.copied_candidates;++i) {
        VehicleCandidateRecord *r=&w->scratch.records[i];
        WorkloadOwner *o;
        if(r->renderer_type!=3) continue;
        if(!r->selection_vehicle || !r->selection_renderer_recent ||
           !r->selection_group_complete) { ++s->unknown; continue; }
        ++s->matched;
        for(j=0;j<s->owners;++j) if(s->owner[j].owner==r->selection_vehicle) break;
        if(j==s->owners) {
            if(j>=VEHICLE_SELECTION_MAX_VEHICLES) { ++s->unknown; continue; }
            ++s->owners; o=&s->owner[j];
            o->owner=r->selection_vehicle; o->handle=r->descriptor_handle;
            o->distance_squared=r->selection_distance_squared;
            o->age_ms=r->selection_age_ms; o->published=w->scratch.tick-r->selection_age_ms;
        }
        o=&s->owner[j]; ++o->lights;
        if(!chain->enabled || chain->fail_open || !chain->evaluated ||
           i>=chain->sample.copied_candidates ||
           !chain->sample.records[i].selection_group_complete ||
           !chain->sample.records[i].selection_renderer_recent ||
           chain->sample.records[i].selection_would_keep) ++o->kept;
    }
    s->observer_ticks=(w->manager_before-w->observer_before)+(workload_qpc()-end);
    s->valid=1;
    InterlockedExchange(&w->lock,0);
}

static WorkloadTimingTicket workload_queue(void *queue,uint32_t requested,uint32_t entries,
    uint32_t requested_faces,uint32_t faces)
{
    uint32_t slot,i,j;
    int64_t now;
    WorkloadTimingTicket ticket={0};
    LONG epoch;
    WorkloadQueueSample *s;
    ShadowWorkloadState *w=workload_enter(&slot,&now);
    if(!w) return ticket;
    s=&w->queues[slot];
    if(s->valid) goto leave;
    if(!queue || entries>TOTAL_LOCAL_MAPS || !vehicle_selection_lock()) {
        InterlockedIncrement(&w->skipped); goto leave;
    }
    epoch=InterlockedCompareExchange(&g_shadow_engine.renderer.renderer_calls,0,0);
    s->entries=entries; s->faces=faces;
    s->requested_entries=requested; s->requested_faces=requested_faces;
    for(i=0;i<entries;++i) {
        unsigned char *entry=(unsigned char *)queue+(size_t)i*RENDER_QUEUE_ENTRY_BYTES;
        void *candidate;
        VehicleCandidateRecord r;
        uint64_t owner=0;
        int identity;
        if(!readable_memory(entry+VEHICLE_QUEUE_CANDIDATE_OFFSET,sizeof(void *))) {
            ++s->unreadable; continue;
        }
        memcpy(&candidate,entry+VEHICLE_QUEUE_CANDIDATE_OFFSET,sizeof(candidate));
        memset(&r,0,sizeof(r));
        if(!copy_vehicle_candidate_fields(&r,candidate,0)) { ++s->unreadable; continue; }
        if(r.renderer_type<0) { ++s->missing_handles; ++s->unknown; continue; }
        if(r.renderer_type!=1 && r.renderer_type!=3) { ++s->other; continue; }
        if(!r.descriptor_handle_valid) { ++s->unknown; ++s->missing_handles; continue; }
        for(j=0;j<s->handle_count;++j) if(s->handles[j]==r.descriptor_handle) break;
        if(j==s->handle_count && j<TOTAL_LOCAL_MAPS) s->handles[s->handle_count++]=r.descriptor_handle;
        identity=lookup_vehicle_quality_identity(r.descriptor_handle,epoch,&owner,NULL);
        if(identity<0) { ++s->ambiguous; continue; }
        if(!identity) { ++s->unknown; continue; }
        if(r.renderer_type==3) ++s->headlights; else ++s->type1;
        for(j=0;j<s->owners;++j) if(s->owner_tokens[j]==owner) break;
        if(j==s->owners && j<TOTAL_LOCAL_MAPS) s->owner_tokens[s->owners++]=owner;
    }
    vehicle_selection_unlock();
    s->observer_ticks=workload_qpc()-now; s->valid=1;
    ticket.state=w; ticket.slot=slot; ticket.queue=1;
    InterlockedIncrement(&w->inflight);
leave:
    InterlockedExchange(&w->lock,0);
    return ticket;
}

static uint32_t workload_set_added(const uint64_t *current,uint32_t count,
    const uint64_t *previous,uint32_t previous_count)
{
    uint32_t i,j,added=0;
    for(i=0;i<count;++i) {
        for(j=0;j<previous_count;++j) if(current[i]==previous[j]) break;
        if(j==previous_count) ++added;
    }
    return added;
}

static int workload_turnover_valid(const WorkloadQueueSample *a,const WorkloadQueueSample *b)
{
    return a->valid && b->valid && !a->unreadable && !b->unreadable &&
        !a->missing_handles && !b->missing_handles && !a->ambiguous && !b->ambiguous;
}

static void workload_report(ShadowWorkloadState *w)
{
    uint32_t i,j,nm=0,nq=0,np=0,best=0;
    double prepare=0,observer=0,candidates=0,suppressed=0,owner_count=0;
    double entries=0,faces=0,headlights=0,type1=0,other=0,unknown=0,ambiguous=0;
    double queue_owners=0,requested_faces=0;
    double stages[4]={0},queries=0,hits=0,links=0;
    double native_manager=0,restore=0,native_queue=0,manager_peak=0,queue_peak=0;
    uint32_t timed_managers=0,timed_queues=0,pairs=0,handles_in=0,handles_out=0;
    uint32_t owners_in=0,owners_out=0;
    uint32_t unreadable=0,failopen=0,degraded=0;
    double scale=1000000.0/(double)w->frequency;
    WorkloadPresent *p=&w->present;
    append_log("STAGE_WORKLOAD_BEGIN serial=%u version=%s section=4 build=P-A3 workRegister=P1 purpose=wholePassPopulationRecording retained=H3,H4 observationMode=windowed baseline=0 windowMs=5000 sampleHz=10 mainLimit=%u limiterEnabled=%u policy=0x%08lX world=0x%08lX settingsChangeObserved=%ld skipped=%ld unit=managerOrQueueCallNotFrame",
        w->serial,PATCH_VERSION,(w->policy&SHADOW_POLICY_DISABLE_LIMITER)?0U:
        (((w->policy&SHADOW_POLICY_LIMIT_MASK)>>SHADOW_POLICY_LIMIT_SHIFT)?
        (unsigned)((w->policy&SHADOW_POLICY_LIMIT_MASK)>>SHADOW_POLICY_LIMIT_SHIFT):VEHICLE_SELECTION_STABLE_VEHICLES),
        (w->policy&SHADOW_POLICY_DISABLE_LIMITER)?0U:1U,(unsigned long)w->policy,
        (unsigned long)w->world,(long)w->changes,(long)w->skipped);
    for(i=0;i<WORKLOAD_SAMPLES;++i) {
        WorkloadManagerSample *m=&w->managers[i];
        WorkloadQueueSample *q=&w->queues[i];
        if(m->valid) {
            if(m->timing_complete) {
                ++timed_managers; native_manager+=m->native_ticks; restore+=m->restore_ticks;
                if(m->native_ticks>manager_peak) manager_peak=(double)m->native_ticks;
            }
            for(j=0;j<4;++j) stages[j]+=m->stages[j];
            queries+=m->validation_queries; hits+=m->validation_hits; links+=m->links_changed;
            ++nm; prepare+=m->prepare_ticks; observer+=m->observer_ticks;
            candidates+=m->candidates; suppressed+=m->suppressed; owner_count+=m->owners;
            failopen+=m->fail_open; degraded+=m->degraded;
            /* At most 80 copied movement rows, flushed only after the window. */
            for(j=0;i%10U==0U && j<m->owners && j<8U;++j) {
                WorkloadOwner *o=&m->owner[j];
                uint32_t k;
                int trend=0;
                double rate=0;
                if(i && w->managers[i-1U].valid) {
                    WorkloadManagerSample *previous=&w->managers[i-1U];
                    for(k=0;k<previous->owners;++k) {
                        WorkloadOwner *old=&previous->owner[k];
                        DWORD dt=o->published-old->published;
                        if(o->owner==old->owner && o->handle==old->handle &&
                           dt>=50U && dt<=500U && o->age_ms<=200U && old->age_ms<=200U) {
                            rate=(sqrt((double)o->distance_squared)-sqrt((double)old->distance_squared))*1000.0/dt;
                            trend=1; break;
                        }
                    }
                }
                append_log("STAGE_WORKLOAD_ROPE serial=%u sample=%u ms=%lu owner=0x%llX handle=0x%llX selectionDistance=%.3f ageMs=%lu headlights=%u policyKept=%u trendValid=%d nativeUnitsPerSecond=%.3f playerAnchor=%u",
                    w->serial,i,(unsigned long)m->elapsed_ms,(unsigned long long)o->owner,
                    (unsigned long long)o->handle,sqrt((double)o->distance_squared),
                    (unsigned long)o->age_ms,o->lights,o->kept,trend,rate,m->player_anchor_valid);
            }
        }
        if(q->valid) {
            if(q->timing_complete) {
                ++timed_queues; native_queue+=q->native_ticks;
                if(q->native_ticks>queue_peak) queue_peak=(double)q->native_ticks;
            }
            if(i && workload_turnover_valid(q,&w->queues[i-1U])) {
                const WorkloadQueueSample *previous=&w->queues[i-1U];
                ++pairs;
                handles_in+=workload_set_added(q->handles,q->handle_count,previous->handles,previous->handle_count);
                handles_out+=workload_set_added(previous->handles,previous->handle_count,q->handles,q->handle_count);
                owners_in+=workload_set_added(q->owner_tokens,q->owners,previous->owner_tokens,previous->owners);
                owners_out+=workload_set_added(previous->owner_tokens,previous->owners,q->owner_tokens,q->owners);
            }
            ++nq; entries+=q->entries; faces+=q->faces; headlights+=q->headlights;
            queue_owners+=q->owners; requested_faces+=q->requested_faces;
            type1+=q->type1; other+=q->other; unknown+=q->unknown; ambiguous+=q->ambiguous;
            unreadable+=q->unreadable; observer+=q->observer_ticks;
        }
    }
    append_log("STAGE_WORKLOAD_SUMMARY serial=%u managerSamples=%u queueSamples=%u meanCandidates=%.2f meanCandidateVehicleOwners=%.2f meanSuppressedLights=%.2f meanPrepareUs=%.3f failOpenSamples=%u degradedSamples=%u meanQueueEntries=%.2f meanFaces=%.2f meanVehicleHeadlights=%.2f meanVehicleType1=%.2f meanOtherTypes=%.2f meanUnclassifiedType1Or3=%.2f meanAmbiguous=%.2f unreadable=%u observerTotalUs=%.3f meanQueueVehicleOwners=%.2f meanRequestedFaces=%.2f worldCount=notSeparatelyProven",
        w->serial,nm,nq,nm?candidates/nm:-1.0,nm?owner_count/nm:-1.0,
        nm?suppressed/nm:-1.0,nm?prepare*scale/nm:-1.0,failopen,degraded,
        nq?entries/nq:-1.0,nq?faces/nq:-1.0,nq?headlights/nq:-1.0,nq?type1/nq:-1.0,
        nq?other/nq:-1.0,nq?unknown/nq:-1.0,nq?ambiguous/nq:-1.0,unreadable,observer*scale,
        nq?queue_owners/nq:-1.0,nq?requested_faces/nq:-1.0);
    for(i=0;i<WORKLOAD_PRESENT_CHAINS;++i) if(p->chains[i].presents) {
        ++np; if(p->chains[i].presents>p->chains[best].presents) best=i;
    }
    {
        WorkloadPresentChain *c=&p->chains[best];
        uint32_t cumulative=0,p95=0,p95_open=0,pending=0;
        int valid=workload_present_valid(p);
        for(i=0;i<WORKLOAD_PRESENT_THREADS;++i) if(p->pending[i].thread) ++pending;
        for(i=0;i<102U;++i) { cumulative+=c->histogram[i]; if(cumulative*100U>=c->intervals*95U) { p95=i+1U; break; } }
        if(p95==102U) { p95=0; p95_open=1; } /* Last bucket has no finite upper bound. */
        append_log("STAGE_WORKLOAD_PRESENT serial=%u source=DXGI_PrivateInProcess_SuccessfulPresentCalls displayedFPS=notMeasured error=%lu stopError=%lu consumeError=%lu eventsLost=%lu buffersLost=%lu propertyErrors=%lu failedOrOccluded=%lu unpaired=%lu overflow=%lu swapchains=%u intervals=%u valid=%d pendingStarts=%u presentationFPS=%.3f meanIntervalMs=%.3f p95BucketUpperMs=%u p95OpenEnded=%u maxIntervalMs=%.3f callbackTotalUs=%.3f providerFileCost=notMeasured",
            w->serial,(unsigned long)p->error,(unsigned long)p->stop_error,(unsigned long)p->consume_error,
            (unsigned long)p->properties.p.events_lost,(unsigned long)p->properties.p.log_lost,
            (unsigned long)p->property_errors,(unsigned long)p->failed,(unsigned long)p->unpaired,
            (unsigned long)p->overflow,np,c->intervals,valid,pending,
            valid && c->sum?c->intervals*(double)w->frequency/(double)c->sum:-1.0,
            valid?c->sum*1000.0/w->frequency/c->intervals:-1.0,
            valid?p95:0U,valid?p95_open:0U,c->max*1000.0/w->frequency,p->callback_ticks*scale);
        append_log("STAGE_WORKLOAD_PRESENT_STORAGE serial=%u mode=privateInProcessSequential fileLimitMiB=%u traceBytes=%llu startError=%lu enableError=%lu openError=%lu fileError=%lu closeError=%lu consumerEventsLost=%lu headerBuffersLost=%lu consumed=%u retainedDirectory=%u elevationRequested=0 presentHook=0",
            w->serial,WORKLOAD_PRESENT_FILE_MB,(unsigned long long)p->file_bytes,
            (unsigned long)p->start_error,(unsigned long)p->enable_error,
            (unsigned long)p->open_error,(unsigned long)p->file_error,(unsigned long)p->close_error,
            (unsigned long)p->logfile.events_lost,(unsigned long)p->logfile.header.lost,
            p->consumed,p->directory_owned);
    }
    append_log("STAGE_WORKLOAD_PREPARATION serial=%u samples=%u snapshotUs=%.3f copyUs=%.3f classifyUs=%.3f selectUs=%.3f meanCopyQueries=%.3f meanCopyQueryHits=%.3f meanChangedLinks=%.3f timesPerSampledManager=1 queryCountsExcludePlayerReads=1",
        w->serial,nm,nm?stages[0]*scale/nm:0.0,nm?stages[1]*scale/nm:0.0,
        nm?stages[2]*scale/nm:0.0,nm?stages[3]*scale/nm:0.0,
        nm?queries/nm:0.0,nm?hits/nm:0.0,nm?links/nm:0.0);
    append_log("STAGE_WORKLOAD_DOWNSTREAM serial=%u timedManagers=%u timedQueues=%u meanNativeManagerUs=%.3f peakNativeManagerUs=%.3f meanRestoreUs=%.3f meanNativeQueueUs=%.3f peakNativeQueueUs=%.3f cpuWallOnly=1 gpuTime=0 nestedIntervalsNotAdditive=1",
        w->serial,timed_managers,timed_queues,timed_managers?native_manager*scale/timed_managers:0.0,
        manager_peak*scale,timed_managers?restore*scale/timed_managers:0.0,
        timed_queues?native_queue*scale/timed_queues:0.0,queue_peak*scale);
    append_log("STAGE_WORKLOAD_TURNOVER serial=%u adjacentValidPairs=%u handlesAdded=%u handlesRemoved=%u knownVehicleOwnersAdded=%u knownVehicleOwnersRemoved=%u sampledNetChangeOnly=1 nativeCacheMissesNotMeasured=1 handleScope=type1And3 knownOwnerCoveragePartial=1",
        w->serial,pairs,handles_in,handles_out,owners_in,owners_out);
    append_log("STAGE_WORKLOAD_END serial=%u compareOnlyMatchingSettingsAndSimilarWorkload=1",w->serial);
}

static int workload_request(void)
{
    ShadowWorkloadState *w=workload_state();
    LARGE_INTEGER frequency;
    if(!w) {
        w=(ShadowWorkloadState *)calloc(1,sizeof(*w));
        if(!w) return 0;
        InterlockedExchangePointer((void *volatile *)&g_shadow_engine.workload,w);
    }
    if(w->phase || !QueryPerformanceFrequency(&frequency) || frequency.QuadPart<=0) return 0;
    ++w->serial;
    memset(w->managers,0,sizeof(w->managers)); memset(w->queues,0,sizeof(w->queues));
    w->frequency=frequency.QuadPart;
    w->policy=InterlockedCompareExchange(&g_shadow_engine.policy_control.disabled,0,0);
    w->world=InterlockedCompareExchange(&g_shadow_engine.policy_control.world_configuration,0,0);
    w->changes=0; w->skipped=0; w->drain_warning=0;
#if SHADOW_ENGINE_OVERHEAD_MEASUREMENT
    /* Worker calibration finishes before either capture's lead-in begins.
     * Failure publishes this request's serial without arming or replaying old data. */
    if(!overhead_request(w->serial,3000U,WORKLOAD_SECONDS*1000U)) {
        w->notice=5;
        append_log("STAGE_WORKLOAD_REQUEST serial=%u section=4 build=P-A3 workRegister=P1 purpose=wholePassPopulationRecording retained=H3,H4 observationMode=windowed baseline=0 failed=overheadUnavailable captureArmed=0",
            w->serial);
        return 0;
    }
#endif
    w->start=workload_qpc()+3*w->frequency; w->end=w->start+WORKLOAD_SECONDS*w->frequency;
    workload_present_start(&w->present,w->start,w->end,w->frequency,w->serial,
        g_shadow_engine.bootstrap.self);
    w->phase=1;
    w->notice=1;
    InterlockedExchange(&w->active,1);
    append_log("STAGE_WORKLOAD_REQUEST serial=%u version=%s section=4 build=P-A3 workRegister=P1 purpose=wholePassPopulationRecording retained=H3,H4 observationMode=windowed baseline=0 leadMs=3000 windowMs=5000 captureArmed=1",
        w->serial,PATCH_VERSION);
    return 1;
}

static void workload_poll(void)
{
    ShadowWorkloadState *w=workload_state();
    if(!w || !w->phase) return;
    if(w->phase==1 && workload_qpc()>=w->start) w->notice=2;
    if(w->phase==1 && workload_qpc()>=w->end) {
        InterlockedExchange(&w->active,0);
#if SHADOW_ENGINE_OVERHEAD_MEASUREMENT
        overhead_close();
#endif
        workload_present_stop(&w->present);
        w->phase=2;
        w->notice=3;
    }
    if(w->phase!=2) return;
#if SHADOW_ENGINE_OVERHEAD_MEASUREMENT
    /* Close stops admission only. Keep all tickets/storage until callbacks
     * publish their samples; worker polling must never block engine threads. */
    if(!overhead_ready()) {
        if(!(w->drain_warning&2U) && workload_qpc()>w->end+5*w->frequency) {
            w->drain_warning|=2U;
            append_log("STAGE_WORKLOAD_DRAIN_DELAY serial=%u reason=overheadTickets captureDisabled=1 memoryRetained=1 section=4 build=P-A3 workRegister=P1 purpose=wholePassPopulationRecording",
                w->serial);
        }
        return;
    }
#endif
    if(!workload_present_cleanup(&w->present)) {
        if(!(w->drain_warning&1U) && workload_qpc()>w->end+5*w->frequency) {
            w->drain_warning|=1U;
            append_log("STAGE_WORKLOAD_DRAIN_DELAY serial=%u stopError=%lu captureDisabled=1 memoryRetained=1",
                w->serial,(unsigned long)w->present.stop_error);
        }
        return;
    }
    if(InterlockedCompareExchange(&w->lock,1,0)) return;
    if(InterlockedCompareExchange(&w->inflight,0,0)) {
        InterlockedExchange(&w->lock,0); return;
    }
    {
        LONG before=InterlockedCompareExchange(&g_shadow_engine.bootstrap.log_write_failures,0,0);
        workload_report(w);
#if SHADOW_ENGINE_OVERHEAD_MEASUREMENT
        overhead_report();
#endif
        w->notice=flush_session_log() &&
            before==InterlockedCompareExchange(&g_shadow_engine.bootstrap.log_write_failures,0,0)?4U:5U;
    }
    w->phase=0;
    InterlockedExchange(&w->lock,0);
}
