/* Explicit P-A observation. Worker owns allocation/output; callbacks borrow
 * native memory only synchronously and publish copied, bounded packets. */
#include "../internal/population_capture_format.inc"
#define POPULATION_PACKETS 1200U
#define POPULATION_SAMPLE_MS 50U
#define POPULATION_CANDIDATES 512U
#define POPULATION_OWNERS 8U
#define POPULATION_WINDOW_MS 60000U
#define POPULATION_REPORT_BATCH_PACKETS 32U
#define POPULATION_REPORT_BUDGET_MS 20U
#define POPULATION_REPORT_BATCH_BYTES (1024U*1024U)
typedef uint64_t PopulationTicket;
typedef struct PopulationOwnerCopy {
    uint32_t valid,identity,initialized;
    int32_t age;
    uint64_t backing;
} PopulationOwnerCopy;
typedef struct PopulationCacheCopy {
    uint32_t valid,count,encoded,capacity,a8,dimension,b0,b4;
    uint64_t base;
    PopulationOwnerCopy owners[POPULATION_OWNERS];
} PopulationCacheCopy;
typedef struct PopulationCandidateCopy {
    uint64_t candidate,descriptor,spatial,handle;
    uint64_t vehicle,entity_id;
    uint32_t identity_valid,identity_matches,handle_instances,identity_age_ms;
    LONG published_manager,published_renderer;
    uint32_t valid,handle_valid,keep_known,kept,admitted,binding_count,update;
    int32_t cache_slot;
    PopulationGateInputs gate;
    uint32_t gate_result;
} PopulationCandidateCopy;
typedef struct PopulationManagerPacket {
    volatile LONG complete;
    PopulationTicket ticket;
    uint32_t sequence,thread,count,copied,chain_complete,prepared,kept_count;
    uint32_t keep_complete,admission_valid,admitted,binding_unknown,duplicate_candidates;
    uint32_t limiter_active,limiter_fail_open,limiter_degraded;
    uint32_t identity_health;
    LONG call,renderer_epoch;
    int64_t begin,end,observer_ticks;
    PopulationCacheCopy before,after;
    PopulationCandidateCopy rows[POPULATION_CANDIDATES];
} PopulationManagerPacket;
typedef struct PopulationQueueCopy {
    uint64_t candidate,descriptor,spatial,handle,backing_before,resource_before,resource_after;
    uint32_t valid,handle_valid,submitted,faces,width,height,resource_width,resource_height;
    uint32_t before_valid,before_width,before_height;
    uint32_t native_width,native_height,policy_sampled;
} PopulationQueueCopy;
typedef struct PopulationQueuePacket {
    volatile LONG complete;
    PopulationTicket ticket;
    uint32_t sequence,thread,original,admitted,requested_faces,admitted_faces,copied;
    LONG call;
    int64_t begin,end,observer_ticks;
    PopulationQueueCopy rows[PHYSICAL_QUEUE_ENTRIES];
} PopulationQueuePacket;
typedef struct PopulationCaptureState {
    volatile LONG active,manager_lock,queue_lock,manager_busy,queue_busy;
    volatile LONG reservation_inflight;
    volatile LONG manager_skipped,queue_skipped;
    volatile LONG manager_attempted,queue_attempted,manager_rate_skipped,queue_rate_skipped;
    uint32_t serial,phase,notice,marked,manager_sequence,queue_sequence;
    uint32_t manager_bucket,queue_bucket,stop_reason,elapsed_ms,expected_bins;
    uint32_t report_partial;
    volatile LONG clock_failures;
    int64_t report_ticks,report_peak_batch_ticks;
    uint32_t report_batches;
    PopulationTicket manager_ticket,queue_ticket;
    DWORD started_tick,end_tick,marked_tick;
    int64_t started,marked_qpc,frequency,stopped;
    uint64_t started_filetime;
    HANDLE artifact;
    wchar_t artifact_name[96],artifact_path[MAX_PATH];
    uint32_t export_started,export_cursor,export_records,export_managers,export_queues;
    uint32_t export_crc,export_error;
    uint64_t export_bytes;
    PopulationCaptureFileHeader export_header;
    const PopulationCaptureProfile *profile;
    VehicleSelectionSnapshot selection_snapshot;
    VehicleSelectionIndex selection_index;
    PopulationManagerPacket managers[POPULATION_PACKETS];
    PopulationQueuePacket queues[POPULATION_PACKETS];
} PopulationCaptureState;
typedef char PopulationManagerPrefix[(offsetof(PopulationManagerPacket,rows)==POPULATION_MANAGER_PREFIX_BYTES)?1:-1];
typedef char PopulationCandidateSize[(sizeof(PopulationCandidateCopy)==POPULATION_CANDIDATE_BYTES)?1:-1];
typedef char PopulationQueuePrefix[(offsetof(PopulationQueuePacket,rows)==POPULATION_QUEUE_PREFIX_BYTES)?1:-1];
typedef char PopulationQueueRowSize[(sizeof(PopulationQueueCopy)==POPULATION_QUEUE_ROW_BYTES)?1:-1];
typedef char PopulationStorageBound[(sizeof(PopulationCaptureState)<128U*1024U*1024U)?1:-1];

static PopulationCaptureState *population_state(void)
{
    return (PopulationCaptureState *)InterlockedCompareExchangePointer(
        (void *volatile *)&g_shadow_engine.population,NULL,NULL);
}
static int64_t population_qpc(void)
{
    LARGE_INTEGER value;
    return QueryPerformanceCounter(&value)?value.QuadPart:0;
}
static int64_t population_observer_elapsed(int64_t begin,int64_t end)
{
    PopulationCaptureState *s=population_state();
    if(begin<=0 || end<begin) {
        InterlockedIncrement(&s->clock_failures); s->report_partial=1; return 0;
    }
    return end-begin;
}
static uint32_t population_packet_slot(const PopulationCaptureState *s,uint32_t sequence,int queue)
{
    (void)s; (void)queue;
    return sequence-1U; /* Ticket index is the absolute bucket plus one. */
}
static uint32_t population_elapsed_at(const PopulationCaptureState *s,int64_t now)
{
    uint64_t elapsed;
    if(now<s->started || s->frequency<=0) return 0;
    elapsed=(uint64_t)(now-s->started);
    if(elapsed>=(uint64_t)s->frequency*(POPULATION_WINDOW_MS/1000U)) return POPULATION_WINDOW_MS;
    return (uint32_t)(elapsed*1000U/(uint64_t)s->frequency);
}
static uint32_t population_elapsed_ms(void)
{
    PopulationCaptureState *s=population_state();
    if(!s || !s->serial) return 0;
    return s->phase==1?population_elapsed_at(s,population_qpc()):s->elapsed_ms;
}
static const void *population_address(const void *base,uint32_t offset,size_t size)
{
    uintptr_t start=(uintptr_t)base;
    if(!start || offset>UINTPTR_MAX-start) return NULL;
    start+=offset;
    if(size>UINTPTR_MAX-start) return NULL;
    return (const void *)start;
}
static int population_copy(const void *base,uint32_t offset,void *out,size_t size)
{
    const void *at=population_address(base,offset,size);
    if(!at || !readable_memory(at,size)) return 0;
    memcpy(out,at,size);
    return 1;
}
static int population_copy_scoped(const void *base,uint32_t offset,void *out,size_t size,
    VehicleCopyRegionCache *scope)
{
    const void *at=population_address(base,offset,size);
    if(!at || !vehicle_copy_readable(scope,at,size)) return 0;
    memcpy(out,at,size); return 1;
}
static void population_copy_resource_before_scoped(ShadowResourceSnapshot *out,
    const void *candidate,VehicleCopyRegionCache *scope)
{
    const unsigned char *source=(const unsigned char *)candidate;
    memset(out,0,sizeof(*out));
    /* Preserve the complete spans required by copy_shadow_resource_before.
     * Permission results belong only to this callback's copy scope. */
    if(!population_address(source,0,SHADOW_CANDIDATE_BACKING_OFFSET+sizeof(void *)) ||
       !vehicle_copy_readable(scope,source,SHADOW_CANDIDATE_BACKING_OFFSET+sizeof(void *))) return;
    memcpy(&out->backing,source+SHADOW_CANDIDATE_BACKING_OFFSET,sizeof(void *));
    out->valid=SHADOW_RESOURCE_BACKING_READ;
    if(!population_address(out->backing,0,SHADOW_BACKING_RESOURCE_OFFSET+sizeof(void *)) ||
       !vehicle_copy_readable(scope,out->backing,SHADOW_BACKING_RESOURCE_OFFSET+sizeof(void *))) return;
    memcpy(&out->resource,(const unsigned char *)out->backing+SHADOW_BACKING_RESOURCE_OFFSET,sizeof(void *));
    out->valid|=SHADOW_RESOURCE_POINTER_READ;
    if(!population_address(out->resource,0,VEHICLE_RESOURCE_HEIGHT_OFFSET+sizeof(uint32_t)) ||
       !vehicle_copy_readable(scope,out->resource,VEHICLE_RESOURCE_HEIGHT_OFFSET+sizeof(uint32_t))) return;
    memcpy(&out->width,(const unsigned char *)out->resource+VEHICLE_RESOURCE_WIDTH_OFFSET,sizeof(uint32_t));
    memcpy(&out->height,(const unsigned char *)out->resource+VEHICLE_RESOURCE_HEIGHT_OFFSET,sizeof(uint32_t));
    out->valid|=SHADOW_RESOURCE_DIMENSIONS_READ;
}
static void population_cache(PopulationCacheCopy *out,const void *manager,
    const PopulationCaptureLayout *l)
{
    unsigned char *base=NULL;
    uint32_t i,shift;
    uint64_t capacity;
    unsigned char b0=0;
    if(((uintptr_t)manager&7U) || !population_copy(manager,l->manager_owner_vector,&base,sizeof(base)) ||
       !population_copy(manager,l->manager_owner_count,&out->count,4) ||
       !population_copy(manager,l->manager_owner_encoded_capacity,&out->encoded,4)) return;
    out->base=(uint64_t)(uintptr_t)base;
    shift=(out->encoded>>1U)&31U;
    capacity=(uint64_t)(out->encoded>>6U)<<shift;
    out->capacity=capacity>UINT32_MAX?UINT32_MAX:(uint32_t)capacity;
    out->valid=1;
    if(population_copy(manager,l->manager_cache_limit,&out->a8,4) &&
       population_copy(manager,l->manager_cache_dimension,&out->dimension,4) &&
       population_copy(manager,l->manager_new_owner_update,&b0,1) &&
       population_copy(manager,l->manager_scheduler,&out->b4,4)) {
        out->b0=b0; out->valid|=2;
    }
    if(out->count>POPULATION_OWNERS || out->count>capacity ||
       (out->count && (!base || ((uintptr_t)base&7U) || !l->owner_stride ||
        !population_address(base,(out->count-1U)*l->owner_stride,l->owner_stride)))) return;
    for(i=0;i<out->count;++i) {
        PopulationOwnerCopy *r=&out->owners[i];
        const void *owner=population_address(base,i*l->owner_stride,l->owner_stride);
        unsigned char initialized=0;
        if(!population_copy(owner,l->owner_identity,&r->identity,4) ||
           !population_copy(owner,l->owner_age,&r->age,4) ||
           !population_copy(owner,l->owner_initialized,&initialized,1)) return;
        r->initialized=initialized; r->valid=1;
        r->backing=(uint64_t)(uintptr_t)population_address(owner,l->owner_backing,1);
    }
    out->valid|=4;
}

/* This is a borrowed input copy, not a native predicate invocation. */
static PopulationGateInputs population_gate_context(const void *manager,
    const void *context,const PopulationCaptureProfile *p)
{
    PopulationGateInputs out={0};
    const PopulationCaptureLayout *l=&p->layout;
    void *config=NULL,*table=NULL,*object=NULL;
    unsigned char reduced=0;
    uint32_t index=0;
    uint64_t offset;
    if(population_copy(manager,l->manager_config,&config,sizeof(config)) &&
       population_copy(config,l->config_type3_enable,&out.type3_enabled,4) &&
       population_copy(config,l->config_coverage_threshold,&out.threshold_bits,4) &&
       population_copy(config,l->config_reduced,&reduced,1)) {
        out.reduced=reduced; out.valid|=PG_VALID_CONFIG;
    }
    if(population_copy(g_shadow_engine.bootstrap.disrupt_base,p->context_index_rva,&index,4) &&
       population_copy(context,0,&table,sizeof(table))) {
        offset=(uint64_t)l->context_table_slot_bias+(uint64_t)index*l->context_table_slot_stride;
        if(offset<=UINT32_MAX && population_copy(table,(uint32_t)offset,&object,sizeof(object)) &&
           population_copy(object,l->context_width,&out.width,4) &&
           population_copy(object,l->context_height,&out.height,4)) out.valid|=PG_VALID_CONTEXT;
    }
    if(population_copy(g_shadow_engine.bootstrap.disrupt_base,p->epsilon_rva,&out.epsilon_bits,4) &&
       population_copy(g_shadow_engine.bootstrap.disrupt_base,p->coverage_numerator_rva,&out.numerator_bits,4) &&
       out.epsilon_bits==p->epsilon_bits && out.numerator_bits==p->coverage_numerator_bits)
        out.valid|=PG_VALID_CONSTANTS;
    out.mxcsr=population_capture_mxcsr(); out.valid|=PG_VALID_MXCSR;
    return out;
}
static int population_candidate(PopulationCandidateCopy *out,const void *node,
    uint64_t *next,const PopulationCaptureLayout *l,PopulationGateInputs context,
    VehicleCopyRegionCache *scope)
{
    void *link=NULL,*descriptor=NULL,*spatial=NULL;
    unsigned char enabled=0;
    out->candidate=(uint64_t)(uintptr_t)node; out->cache_slot=-1;
    out->gate=context;
    if(((uintptr_t)node&7U) ||
       !population_copy_scoped(node,l->candidate_next,&link,sizeof(link),scope) ||
       !population_copy_scoped(node,l->candidate_descriptor,&descriptor,sizeof(descriptor),scope) ||
       !population_copy_scoped(node,l->candidate_spatial,&spatial,sizeof(spatial),scope)) return 0;
    *next=(uint64_t)(uintptr_t)link;
    out->descriptor=(uint64_t)(uintptr_t)descriptor;
    out->spatial=(uint64_t)(uintptr_t)spatial; out->valid=1;
    if(!((uintptr_t)descriptor&7U) &&
       population_copy_scoped(descriptor,l->descriptor_type,&out->gate.raw_type,4,scope) &&
       population_copy_scoped(descriptor,l->descriptor_enable,&enabled,1,scope) &&
       population_copy_scoped(descriptor,l->descriptor_factor,&out->gate.descriptor_factor_bits,4,scope) &&
       population_copy_scoped(descriptor,l->descriptor_flags,&out->gate.descriptor_flags,4,scope)) {
        out->gate.enabled=enabled; out->gate.valid|=PG_VALID_DESCRIPTOR;
    }
    if(population_copy_scoped(node,l->candidate_factor,&out->gate.candidate_factor_bits,4,scope) &&
       population_copy_scoped(node,l->candidate_coverage,&out->gate.coverage,4,scope))
        out->gate.valid|=PG_VALID_CANDIDATE;
    if(!((uintptr_t)spatial&7U) &&
       population_copy_scoped(spatial,l->spatial_descriptor_handle,&out->handle,8,scope))
        out->handle_valid=out->handle!=UINT64_MAX;
    out->gate_result=PG_UNKNOWN; /* Evaluation runs only on the worker. */
    return 1;
}

static PopulationTicket population_reserve(int queue,uint32_t *slot)
{
    PopulationCaptureState *s=population_state();
    volatile LONG *lock,*busy,*skipped,*attempted,*rate;
    uint32_t sequence,bucket,last;
    int64_t now;
    uint64_t elapsed;
    PopulationTicket ticket=0;
    int held=0;
    if(!s || !InterlockedCompareExchange(&s->active,0,0)) return 0;
    /* Pin even failed reservations until their loss counters are published.
     * The second active check prevents a delayed entrant writing frozen data. */
    InterlockedIncrement(&s->reservation_inflight);
    if(!InterlockedCompareExchange(&s->active,0,0)) goto done;
    lock=queue?&s->queue_lock:&s->manager_lock;
    busy=queue?&s->queue_busy:&s->manager_busy;
    skipped=queue?&s->queue_skipped:&s->manager_skipped;
    attempted=queue?&s->queue_attempted:&s->manager_attempted;
    rate=queue?&s->queue_rate_skipped:&s->manager_rate_skipped;
    if(InterlockedCompareExchange(lock,1,0)) {
        if(InterlockedCompareExchange(&s->active,0,0)) {
            InterlockedIncrement(attempted); InterlockedIncrement(skipped);
        }
        goto done;
    }
    held=1;
    if(!s->active) goto done;
    InterlockedIncrement(attempted); now=population_qpc();
    if(now<s->started || !now) {
        InterlockedIncrement(&s->clock_failures); s->report_partial=1;
        InterlockedIncrement(skipped); goto done;
    }
    elapsed=(uint64_t)(now-s->started);
    if(elapsed>=(uint64_t)s->frequency*(POPULATION_WINDOW_MS/1000U)) {
        InterlockedIncrement(rate); goto done;
    }
    bucket=(uint32_t)(elapsed*(1000U/POPULATION_SAMPLE_MS)/(uint64_t)s->frequency);
    last=queue?s->queue_bucket:s->manager_bucket;
    if(last!=UINT32_MAX && bucket<=last) {
        if(bucket<last) { InterlockedIncrement(&s->clock_failures); s->report_partial=1; }
        InterlockedIncrement(rate); goto done;
    }
    if(*busy) { InterlockedIncrement(skipped); goto done; }
    sequence=queue?++s->queue_sequence:++s->manager_sequence;
    *slot=bucket;
    ticket=((uint64_t)s->serial<<32)|(queue?0x80000000ULL:0)|(bucket+1U);
    if(queue) {
        s->queue_bucket=bucket;
        memset(&s->queues[*slot],0,sizeof(s->queues[*slot]));
        s->queues[*slot].ticket=ticket; s->queues[*slot].sequence=sequence;
        s->queue_ticket=ticket;
    } else {
        s->manager_bucket=bucket;
        memset(&s->managers[*slot],0,sizeof(s->managers[*slot]));
        s->managers[*slot].ticket=ticket; s->managers[*slot].sequence=sequence;
        s->manager_ticket=ticket;
    }
    InterlockedExchange(busy,1);
done:
    if(held) InterlockedExchange(lock,0);
    InterlockedDecrement(&s->reservation_inflight);
    return ticket;
}
static PopulationManagerPacket *population_manager_ticket(PopulationTicket ticket)
{
    PopulationCaptureState *s=population_state();
    uint32_t sequence=(uint32_t)ticket;
    if(!ticket || !s || !s->manager_busy || s->manager_ticket!=ticket || !sequence) return NULL;
    return &s->managers[population_packet_slot(s,sequence,0)];
}
static PopulationQueuePacket *population_queue_ticket(PopulationTicket ticket)
{
    PopulationCaptureState *s=population_state();
    uint32_t sequence=(uint32_t)ticket&0x7fffffffU;
    if(!ticket || !s || !s->queue_busy || s->queue_ticket!=ticket || !sequence) return NULL;
    return &s->queues[population_packet_slot(s,sequence,1)];
}
static void population_vehicle_identity(PopulationCaptureState *s,PopulationManagerPacket *m)
{
    VehicleLightDiagnosticState *d=&g_shadow_engine.vehicle_diagnostics;
    VehicleSelectionSnapshot *snapshot=&s->selection_snapshot;
    uint32_t i,j,bank;
    DWORD now=GetTickCount();
    memset(snapshot,0,offsetof(VehicleSelectionSnapshot,records));
    if(!d->selection_enabled || !vehicle_selection_lock()) return;
    snapshot->lock_acquired=1;
    for(bank=0;bank<VEHICLE_SELECTION_EPOCH_BANKS;++bank)
        vehicle_selection_bank_health(snapshot,bank,m->renderer_epoch);
    copy_vehicle_selection_records_locked(snapshot,m->renderer_epoch);
    vehicle_selection_unlock();
    snapshot->publisher_overlap=InterlockedCompareExchange(&d->selection_publish_inflight,0,0)!=0;
    m->identity_health=1U|(snapshot->gap_epochs?2U:0U)|
        (snapshot->overflow_epochs?4U:0U)|(snapshot->publisher_overlap?8U:0U);
    index_vehicle_selection(snapshot,&s->selection_index);
    for(i=0;i<m->copied;++i) {
        PopulationCandidateCopy *r=&m->rows[i];
        const VehicleSelectionRecord *match;
        uint32_t matches;
        r->entity_id=UINT64_MAX;
        if(!r->handle_valid || !(r->gate.valid&PG_VALID_DESCRIPTOR) || r->gate.raw_type!=3U) continue;
        for(j=0;j<m->copied;++j)
            if(m->rows[j].handle_valid && m->rows[j].handle==r->handle) ++r->handle_instances;
        match=match_vehicle_selection(snapshot,&s->selection_index,r->handle,&matches);
        r->identity_matches=matches;
        if(!match || matches!=1U || r->handle_instances!=1U) continue;
        r->vehicle=match->vehicle;
        r->entity_id=vehicle_selection_full_entity_id(snapshot,&s->selection_index,r->handle);
        r->published_manager=match->publish_manager_call;
        r->published_renderer=match->publish_renderer_epoch;
        r->identity_age_ms=now-match->tick;
        r->identity_valid=m->identity_health==1U && m->chain_complete && r->vehicle &&
            r->entity_id!=UINT64_MAX && r->identity_age_ms<=0x7fffffffU &&
            match->publish_manager_call<=m->call && match->publish_renderer_epoch<=m->renderer_epoch &&
            (int64_t)m->renderer_epoch-match->publish_renderer_epoch<=3;
    }
}
static PopulationTicket population_manager_begin(void *manager,void *candidates,
    uint32_t count,void *context,void *call_context,LONG call)
{
    uint32_t slot,i,j;
    PopulationTicket ticket=population_reserve(0,&slot);
    PopulationCaptureState *s;
    PopulationManagerPacket *m;
    PopulationGateInputs gate;
    uint64_t node=(uint64_t)(uintptr_t)candidates,next;
    int64_t started;
    VehicleCopyRegionCache scope={0};
    (void)call_context;
    if(!ticket) return 0;
    started=population_qpc(); s=population_state(); m=&s->managers[slot];
    m->begin=started; m->thread=GetCurrentThreadId(); m->call=call; m->count=count;
    m->renderer_epoch=InterlockedCompareExchange(&g_shadow_engine.renderer.renderer_calls,0,0);
    gate=population_gate_context(manager,context,s->profile);
    population_cache(&m->before,manager,&s->profile->layout);
    for(i=0;i<count && i<POPULATION_CANDIDATES && node;++i) {
        for(j=0;j<i;++j) if(m->rows[j].candidate==node) break;
        if(j<i) { ++m->duplicate_candidates; break; }
        if(!population_candidate(&m->rows[i],(const void *)(uintptr_t)node,
            &next,&s->profile->layout,gate,&scope)) break;
        ++m->copied; node=next;
    }
    m->chain_complete=m->copied==count && node==0;
    population_vehicle_identity(s,m);
    m->observer_ticks=population_observer_elapsed(started,population_qpc());
    return ticket;
}
static void population_manager_prepared(PopulationTicket ticket,const VehicleLimiterChain *chain)
{
    PopulationManagerPacket *m=population_manager_ticket(ticket);
    PopulationCaptureState *s=population_state();
    uint64_t node,next;
    uint32_t i,j,matches;
    int64_t started;
    VehicleCopyRegionCache scope={0};
    if(!m || m->prepared || !chain) return;
    started=population_qpc(); m->prepared=1; m->kept_count=chain->count;
    m->limiter_active=chain->active; m->limiter_fail_open=chain->fail_open;
    m->limiter_degraded=chain->degraded;
    node=(uint64_t)(uintptr_t)chain->head;
    /* The actually passed chain is authoritative, including disabled/fail-open. */
    for(i=0;i<chain->count && i<1024U && node;++i) {
        matches=0;
        for(j=0;j<m->copied;++j) if(m->rows[j].candidate==node) {
            ++matches; if(m->rows[j].kept) matches=2; m->rows[j].kept=1;
        }
        if(matches!=1 || (node&7U) || !population_copy_scoped((const void *)(uintptr_t)node,
            s->profile->layout.candidate_next,&next,sizeof(next),&scope)) break;
        node=next;
    }
    m->keep_complete=m->chain_complete && i==chain->count && !node;
    if(m->keep_complete) for(j=0;j<m->copied;++j) m->rows[j].keep_known=1;
    m->observer_ticks+=population_observer_elapsed(started,population_qpc());
}
static void population_manager_end(PopulationTicket ticket,void *manager,void *bindings)
{
    PopulationManagerPacket *m=population_manager_ticket(ticket);
    PopulationCaptureState *s=population_state();
    const PopulationCaptureLayout *l;
    void *data=NULL,*candidate=NULL,*owner=NULL;
    uint32_t i,j,count=0;
    unsigned char update;
    int64_t started;
    if(!m || InterlockedCompareExchange(&m->complete,-1,0)) return;
    started=population_qpc(); l=&s->profile->layout;
    population_cache(&m->after,manager,l);
    if(!((uintptr_t)bindings&7U) && population_copy(bindings,l->binding_header_data,&data,sizeof(data)) &&
       population_copy(bindings,l->binding_header_count,&count,4) &&
       count<=POPULATION_CANDIDATES && m->prepared && count<=m->kept_count &&
       (!count || (data && !((uintptr_t)data&7U) && l->binding_stride &&
        population_address(data,(count-1U)*l->binding_stride,l->binding_stride)))) {
        m->admitted=count;
        for(i=0;i<count;++i) {
            const void *binding=population_address(data,i*l->binding_stride,l->binding_stride);
            if(!population_copy(binding,l->binding_candidate,&candidate,sizeof(candidate)) ||
               !population_copy(binding,l->binding_owner,&owner,sizeof(owner)) ||
               !population_copy(binding,l->binding_update,&update,1)) break;
            for(j=0;j<m->copied;++j) if(m->rows[j].candidate==(uint64_t)(uintptr_t)candidate) break;
            if(j==m->copied) { ++m->binding_unknown; continue; }
            {
                PopulationCandidateCopy *r=&m->rows[j];
                uint64_t address=(uint64_t)(uintptr_t)owner,delta;
                r->admitted=1; ++r->binding_count; r->update=update;
                if(r->binding_count>1U) ++m->binding_unknown;
                if(r->keep_known && !r->kept) ++m->binding_unknown;
                if(owner) {
                    r->cache_slot=-2;
                    if((m->after.valid&4U) && address>=m->after.base) {
                        delta=address-m->after.base;
                        if(l->owner_stride && !(delta%l->owner_stride) &&
                           delta/l->owner_stride<m->after.count)
                            r->cache_slot=(int32_t)(delta/l->owner_stride);
                    }
                }
            }
        }
        m->admission_valid=i==count && !m->binding_unknown && m->chain_complete && m->keep_complete;
    }
    m->end=population_qpc(); m->observer_ticks+=population_observer_elapsed(started,m->end);
    InterlockedExchange(&m->complete,1); InterlockedExchange(&s->manager_busy,0);
}

static PopulationTicket population_queue_begin(void *queue,uint32_t original,
    uint32_t admitted,uint32_t requested_faces,uint32_t admitted_faces,LONG call)
{
    uint32_t slot,i;
    PopulationTicket ticket=population_reserve(1,&slot);
    PopulationCaptureState *s;
    PopulationQueuePacket *q;
    VehicleCopyRegionCache scope;
    int64_t started;
    if(!ticket) return 0;
    started=population_qpc(); memset(&scope,0,sizeof(scope));
    s=population_state(); q=&s->queues[slot];
    q->begin=started; q->thread=GetCurrentThreadId(); q->call=call;
    q->original=original; q->admitted=admitted;
    q->requested_faces=requested_faces; q->admitted_faces=admitted_faces;
    for(i=0;i<original && i<PHYSICAL_QUEUE_ENTRIES;++i) {
        const void *entry=population_address(queue,i*RENDER_QUEUE_ENTRY_BYTES,RENDER_QUEUE_ENTRY_BYTES);
        PopulationQueueCopy *r=&q->rows[i];
        void *candidate=NULL,*descriptor=NULL,*spatial=NULL;
        ShadowResourceSnapshot before;
        if(!population_copy_scoped(entry,VEHICLE_QUEUE_FACE_COUNT_OFFSET,&r->faces,4,&scope) ||
           !population_copy_scoped(entry,VEHICLE_QUEUE_WIDTH_OFFSET,&r->width,4,&scope) ||
           !population_copy_scoped(entry,VEHICLE_QUEUE_HEIGHT_OFFSET,&r->height,4,&scope)) break;
        r->valid=1; r->submitted=i<admitted; ++q->copied;
        r->native_width=r->width; r->native_height=r->height;
        if(r->faces>6U) { r->valid=0; break; }
        if(!r->submitted) continue;
        if(!population_copy_scoped(entry,VEHICLE_QUEUE_CANDIDATE_OFFSET,&candidate,sizeof(candidate),&scope)) continue;
        r->candidate=(uint64_t)(uintptr_t)candidate;
        if(population_copy_scoped(candidate,s->profile->layout.candidate_descriptor,&descriptor,sizeof(descriptor),&scope) &&
           population_copy_scoped(candidate,s->profile->layout.candidate_spatial,&spatial,sizeof(spatial),&scope)) {
            r->descriptor=(uint64_t)(uintptr_t)descriptor;
            r->spatial=(uint64_t)(uintptr_t)spatial; r->valid|=2;
            if(population_copy_scoped(spatial,s->profile->layout.spatial_descriptor_handle,&r->handle,8,&scope))
                r->handle_valid=r->handle!=UINT64_MAX;
        }
        population_copy_resource_before_scoped(&before,candidate,&scope);
        r->before_valid=before.valid; r->before_width=before.width; r->before_height=before.height;
        r->backing_before=(uint64_t)(uintptr_t)before.backing;
        r->resource_before=(uint64_t)(uintptr_t)before.resource;
    }
    q->observer_ticks=population_observer_elapsed(started,population_qpc());
    return ticket;
}
static void population_queue_prepared(PopulationTicket ticket,void *queue)
{
    PopulationQueuePacket *q=population_queue_ticket(ticket);
    VehicleCopyRegionCache scope;
    uint32_t i;
    int64_t started;
    if(!q || q->complete) return;
    started=population_qpc(); memset(&scope,0,sizeof(scope));
    for(i=0;i<q->copied;++i) {
        PopulationQueueCopy *r=&q->rows[i];
        const void *entry=population_address(queue,i*RENDER_QUEUE_ENTRY_BYTES,RENDER_QUEUE_ENTRY_BYTES);
        if(!r->valid || !r->submitted) continue;
        if(population_copy_scoped(entry,VEHICLE_QUEUE_WIDTH_OFFSET,&r->width,4,&scope) &&
           population_copy_scoped(entry,VEHICLE_QUEUE_HEIGHT_OFFSET,&r->height,4,&scope)) r->policy_sampled=1;
    }
    q->observer_ticks+=population_observer_elapsed(started,population_qpc());
}
static void population_queue_end(PopulationTicket ticket,void *queue)
{
    PopulationQueuePacket *q=population_queue_ticket(ticket);
    PopulationCaptureState *s=population_state();
    VehicleCopyRegionCache scope;
    uint32_t i;
    int64_t started;
    if(!q || InterlockedCompareExchange(&q->complete,-1,0)) return;
    started=population_qpc(); memset(&scope,0,sizeof(scope));
    for(i=0;i<q->copied;++i) {
        PopulationQueueCopy *r=&q->rows[i];
        const void *entry=population_address(queue,i*RENDER_QUEUE_ENTRY_BYTES,RENDER_QUEUE_ENTRY_BYTES);
        void *resource=NULL;
        if(!r->valid || !r->submitted ||
           !population_copy_scoped(entry,VEHICLE_QUEUE_RESOURCE_OFFSET,&resource,sizeof(resource),&scope)) continue;
        r->resource_after=(uint64_t)(uintptr_t)resource;
        if(population_copy_scoped(resource,VEHICLE_RESOURCE_WIDTH_OFFSET,&r->resource_width,4,&scope) &&
           population_copy_scoped(resource,VEHICLE_RESOURCE_HEIGHT_OFFSET,&r->resource_height,4,&scope)) r->valid|=4;
    }
    q->end=population_qpc(); q->observer_ticks+=population_observer_elapsed(started,q->end);
    InterlockedExchange(&q->complete,1); InterlockedExchange(&s->queue_busy,0);
}

static int population_profile_valid(const PopulationCaptureProfile *p)
{
    size_t i;
    if(!p || !g_shadow_engine.bootstrap.disrupt_base || !p->span_count ||
       !validate_vehicle_handle_link_sites() || !validate_vehicle_resolution_sites()) return 0;
    for(i=0;i<p->span_count;++i) {
        const PopulationProofSpan *span=&p->spans[i];
        const unsigned char *address;
        if(span->rva>UINT32_MAX) return 0;
        address=(const unsigned char *)population_address(
            g_shadow_engine.bootstrap.disrupt_base,(uint32_t)span->rva,span->size);
        if(!address || !vehicle_owner_image_range(address,span->size) ||
           !readable_memory(address,span->size) || memcmp(address,span->bytes,span->size)) return 0;
    }
    return 1;
}
static void population_finish(void)
{
    PopulationCaptureState *s=population_state();
    uint64_t scaled;
    uint32_t retained_bins;
    int64_t now;
    if(!s || s->phase!=1) return;
    InterlockedExchange(&s->active,0);
    now=population_qpc(); s->stopped=now;
    s->elapsed_ms=population_elapsed_at(s,now);
    s->stop_reason=s->elapsed_ms==POPULATION_WINDOW_MS?1U:2U;
    if(now<s->started || !now) { InterlockedIncrement(&s->clock_failures); s->report_partial=1; }
    else {
        scaled=(uint64_t)(now-s->started);
        if(scaled>=(uint64_t)s->frequency*(POPULATION_WINDOW_MS/1000U))
            s->expected_bins=POPULATION_PACKETS;
        else {
            scaled*=1000U/POPULATION_SAMPLE_MS;
            s->expected_bins=(uint32_t)(scaled/(uint64_t)s->frequency+
                (scaled%(uint64_t)s->frequency!=0));
        }
    }
    if((LONG)(GetTickCount()-s->end_tick)>=0) {
        if(s->elapsed_ms!=POPULATION_WINDOW_MS) {
            InterlockedIncrement(&s->clock_failures); s->report_partial=1;
        }
        s->elapsed_ms=POPULATION_WINDOW_MS; s->expected_bins=POPULATION_PACKETS;
        s->stop_reason=1; /* Tick deadline is a fallback, not a fabricated QPC. */
    }
    retained_bins=s->manager_bucket==UINT32_MAX?0U:s->manager_bucket+1U;
    if(s->queue_bucket!=UINT32_MAX && retained_bins<s->queue_bucket+1U)
        retained_bins=s->queue_bucket+1U;
    if(s->expected_bins<retained_bins) s->expected_bins=retained_bins;
    s->phase=2; s->notice=3;
}
static void population_mark(void)
{
    PopulationCaptureState *s=population_state();
    if(!s || s->phase!=1 || s->marked) return;
    s->marked_tick=GetTickCount(); s->marked_qpc=population_qpc();
    if(s->marked_qpc<s->started || !s->marked_qpc) {
        InterlockedIncrement(&s->clock_failures); s->report_partial=1;
    }
    s->marked=1; s->notice=2; /* Optional annotation never changes the deadline. */
}
static int population_close_artifact(PopulationCaptureState *s)
{
    DWORD error;
    if(s->artifact==INVALID_HANDLE_VALUE) return 1;
    if(CloseHandle(s->artifact)) { s->artifact=INVALID_HANDLE_VALUE; return 1; }
    error=GetLastError();
    if(!s->export_error) s->export_error=error?error:ERROR_INVALID_HANDLE;
    /* A false close does not transfer ownership. An explicit later Start can
     * retry cleanup; an already-invalid handle has no remaining owned resource. */
    if(error==ERROR_INVALID_HANDLE) s->artifact=INVALID_HANDLE_VALUE;
    return 0;
}
static int population_toggle(void)
{
    PopulationCaptureState *s=population_state();
    const PopulationCaptureProfile *p=g_runtime_state.selected?g_runtime_state.selected->population_capture:NULL;
    LARGE_INTEGER frequency;
    FILETIME filetime;
    uint32_t serial;
    int64_t started;
    if(s && s->phase) return 0;
    if(s && s->artifact!=INVALID_HANDLE_VALUE && !population_close_artifact(s)) {
        append_log("STAGE_POPULATION_UNAVAILABLE cleanupPending=1 error=%lu policyChanged=0",
            (unsigned long)s->export_error); return 0;
    }
    if(!population_profile_valid(p) || !QueryPerformanceFrequency(&frequency) ||
       frequency.QuadPart<=0 || frequency.QuadPart>INT64_MAX/60000 ||
       (started=population_qpc())<=0) {
        append_log("STAGE_POPULATION_UNAVAILABLE profileOrClockProof=0 policyChanged=0"); return 0;
    }
    if(!s) {
        s=(PopulationCaptureState *)calloc(1,sizeof(*s));
        if(!s) { append_log("STAGE_POPULATION_UNAVAILABLE allocationFailed=1 policyChanged=0"); return 0; }
        s->artifact=INVALID_HANDLE_VALUE;
        InterlockedExchangePointer((void *volatile *)&g_shadow_engine.population,s);
    }
    if(InterlockedCompareExchange(&s->manager_lock,1,0)) return 0;
    if(InterlockedCompareExchange(&s->queue_lock,1,0)) {
        InterlockedExchange(&s->manager_lock,0); return 0;
    }
    if(s->manager_busy || s->queue_busy || s->reservation_inflight || s->serial==UINT32_MAX) {
        InterlockedExchange(&s->queue_lock,0); InterlockedExchange(&s->manager_lock,0); return 0;
    }
    serial=s->serial+1U;
    memset((unsigned char *)s+offsetof(PopulationCaptureState,serial),0,
        sizeof(*s)-offsetof(PopulationCaptureState,serial));
    s->manager_skipped=s->queue_skipped=0;
    s->manager_attempted=s->queue_attempted=s->manager_rate_skipped=s->queue_rate_skipped=0;
    s->serial=serial; s->profile=p; s->frequency=frequency.QuadPart;
    /* Start after allocation/reset so the fixed window includes actual recording. */
    s->started=population_qpc(); s->started_tick=GetTickCount();
    if(s->started<=0) {
        s->artifact=INVALID_HANDLE_VALUE;
        InterlockedExchange(&s->queue_lock,0); InterlockedExchange(&s->manager_lock,0); return 0;
    }
    GetSystemTimeAsFileTime(&filetime);
    s->started_filetime=((uint64_t)filetime.dwHighDateTime<<32)|filetime.dwLowDateTime;
    s->artifact=INVALID_HANDLE_VALUE;
    s->manager_bucket=s->queue_bucket=UINT32_MAX;
    s->end_tick=s->started_tick+POPULATION_WINDOW_MS; s->phase=1; s->notice=1;
    InterlockedExchange(&s->active,1);
    InterlockedExchange(&s->queue_lock,0); InterlockedExchange(&s->manager_lock,0);
    append_log("STAGE_POPULATION_ARM serial=%u mode=wholeWindowSampled managerPackets=%u queuePackets=%u candidatesPerPacket=%u cacheOwners=%u storageBytes=%llu windowMs=%u sampleMs=%u autoSave=1 markRequired=0 overwrite=0 proof=GlobalCopiedFields noPolicyChange=1",
        s->serial,POPULATION_PACKETS,POPULATION_PACKETS,POPULATION_CANDIDATES,POPULATION_OWNERS,
        (unsigned long long)sizeof(*s),POPULATION_WINDOW_MS,POPULATION_SAMPLE_MS);
    return 1;
}
static int population_artifact_write(PopulationCaptureState *s,const void *data,uint32_t size,int checksum)
{
    DWORD written=0,error;
    int ok;
    if(s->artifact==INVALID_HANDLE_VALUE || s->export_error) return 0;
    ok=WriteFile(s->artifact,data,size,&written,NULL)!=0;
    if(written>size) { s->export_error=ERROR_INVALID_DATA; return 0; }
    if(checksum && written) s->export_crc=population_crc32_update(s->export_crc,data,written);
    s->export_bytes+=written;
    if(!ok || written!=size) {
        error=ok?ERROR_WRITE_FAULT:GetLastError();
        s->export_error=error?error:ERROR_WRITE_FAULT; return 0;
    }
    return 1;
}
static void population_header(PopulationCaptureState *s)
{
    PopulationCaptureFileHeader *h=&s->export_header;
    uint32_t i,j;
    memset(h,0,sizeof(*h)); memcpy(h->magic,"SEPOP01",7);
    h->schema=POPULATION_FILE_SCHEMA; h->header_bytes=sizeof(*h);
    h->endian=POPULATION_FILE_ENDIAN; h->record_header_bytes=sizeof(PopulationCaptureRecordHeader);
    h->manager_prefix=POPULATION_MANAGER_PREFIX_BYTES; h->candidate_bytes=POPULATION_CANDIDATE_BYTES;
    h->queue_prefix=POPULATION_QUEUE_PREFIX_BYTES; h->queue_row_bytes=POPULATION_QUEUE_ROW_BYTES;
    h->max_candidates=POPULATION_CANDIDATES; h->max_queue=PHYSICAL_QUEUE_ENTRIES;
    h->max_owners=POPULATION_OWNERS; h->buckets=POPULATION_PACKETS;
    h->period_ms=POPULATION_SAMPLE_MS; h->window_ms=POPULATION_WINDOW_MS;
    h->pid=GetCurrentProcessId(); h->serial=s->serial; h->session=g_shadow_engine.internal_tools.session;
    h->frequency=s->frequency; h->started=s->started; h->stopped=s->stopped;
    h->started_filetime=s->started_filetime; h->elapsed_ms=s->elapsed_ms; h->stop_reason=s->stop_reason;
    h->manager_attempted=(uint32_t)s->manager_attempted; h->queue_attempted=(uint32_t)s->queue_attempted;
    h->manager_rate_skipped=(uint32_t)s->manager_rate_skipped; h->queue_rate_skipped=(uint32_t)s->queue_rate_skipped;
    h->manager_contended=(uint32_t)s->manager_skipped; h->queue_contended=(uint32_t)s->queue_skipped;
    h->marked_qpc=s->marked_qpc;
    h->marked_elapsed_ms=s->marked?population_elapsed_at(s,s->marked_qpc):0U;
    h->expected_bins=s->expected_bins;
    memcpy(h->version,PATCH_VERSION,sizeof(PATCH_VERSION)<sizeof(h->version)?sizeof(PATCH_VERSION):sizeof(h->version)-1U);
    for(i=0;i<POPULATION_PACKETS;++i) {
        const PopulationManagerPacket *m=&s->managers[i];
        const PopulationQueuePacket *q=&s->queues[i];
        if(m->complete==1) {
            if(h->expected_bins<i+1U) h->expected_bins=i+1U;
            ++h->manager_count;
            if(m->copied>POPULATION_CANDIDATES || !m->chain_complete || !m->keep_complete ||
               !m->admission_valid || m->before.count>POPULATION_OWNERS || m->after.count>POPULATION_OWNERS)
                s->report_partial=1;
        }
        if(q->complete==1) {
            if(h->expected_bins<i+1U) h->expected_bins=i+1U;
            ++h->queue_count;
            if(q->copied>PHYSICAL_QUEUE_ENTRIES || q->copied<q->original) s->report_partial=1;
            for(j=0;j<q->copied && j<PHYSICAL_QUEUE_ENTRIES;++j)
                if(!(q->rows[j].valid&1U) || (q->rows[j].submitted &&
                   (!q->rows[j].policy_sampled || !(q->rows[j].valid&4U)))) s->report_partial=1;
        }
    }
    s->expected_bins=h->expected_bins;
    h->manager_missing=h->expected_bins>h->manager_count?h->expected_bins-h->manager_count:0U;
    h->queue_missing=h->expected_bins>h->queue_count?h->expected_bins-h->queue_count:0U;
    if(h->manager_missing || h->queue_missing || h->manager_count!=s->manager_sequence ||
       h->queue_count!=s->queue_sequence) s->report_partial=1;
    h->flags=(s->marked?1U:0U)|(s->report_partial?2U:0U)|(s->clock_failures?4U:0U);
}
static void population_coverage(PopulationCaptureState *s,int queue)
{
    uint32_t i,count=0;
    int64_t first=0,last=0,gap=0,total=0,peak=0;
    for(i=0;i<POPULATION_PACKETS;++i) {
        int64_t begin,cost;
        LONG complete;
        if(queue) {
            complete=s->queues[i].complete; begin=s->queues[i].begin; cost=s->queues[i].observer_ticks;
        } else {
            complete=s->managers[i].complete; begin=s->managers[i].begin; cost=s->managers[i].observer_ticks;
        }
        if(complete!=1) continue;
        if(!count) first=begin;
        else if(begin-last>gap) gap=begin-last;
        last=begin; total+=cost; if(cost>peak) peak=cost; ++count;
    }
    append_log("STAGE_POPULATION_COVERAGE serial=%u stream=%s retained=%u expectedBins=%u missingBins=%u firstQpc=%lld lastQpc=%lld maximumBetweenSampleGapMs=%.3f observerTotalMs=%.3f observerPeakMs=%.3f sampledOnly=1 causalManagerQueueJoin=0 gpuReadiness=notMeasured",
        s->serial,queue?"queue":"manager",count,s->expected_bins,
        queue?s->export_header.queue_missing:s->export_header.manager_missing,first,last,
        gap*1000.0/s->frequency,total*1000.0/s->frequency,peak*1000.0/s->frequency);
}
static int population_export_begin(PopulationCaptureState *s)
{
    int length;
    s->export_started=1; s->export_crc=0xFFFFFFFFU;
    population_header(s);
    length=_snwprintf(s->artifact_name,96,L"ShadowEnginePopulation-%lu-%016llX-%u.bin",
        (unsigned long)GetCurrentProcessId(),(unsigned long long)s->started_filetime,s->serial);
    if(length<=0 || length>=96 ||
       lstrlenW(g_shadow_engine.bootstrap.snapshot.bin_directory)+length>=MAX_PATH) {
        s->export_error=ERROR_FILENAME_EXCED_RANGE; return 0;
    }
    lstrcpynW(s->artifact_path,g_shadow_engine.bootstrap.snapshot.bin_directory,MAX_PATH);
    lstrcatW(s->artifact_path,s->artifact_name);
    s->artifact=CreateFileW(s->artifact_path,GENERIC_WRITE,FILE_SHARE_READ,NULL,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,NULL);
    if(s->artifact==INVALID_HANDLE_VALUE) {
        s->export_error=GetLastError(); if(!s->export_error) s->export_error=ERROR_OPEN_FAILED;
        return 0;
    }
    append_log("STAGE_POPULATION_BEGIN serial=%u mode=wholeWindowSampled elapsedMs=%u stopReason=%u marked=%u sampleMs=%u managerRetained=%u queueRetained=%u managerAttempted=%lu queueAttempted=%lu managerRateSkipped=%lu queueRateSkipped=%lu managerContended=%lu queueContended=%lu clockFailures=%ld reportPartial=%u file=%ls binarySchema=%u",
        s->serial,s->elapsed_ms,s->stop_reason,s->marked,POPULATION_SAMPLE_MS,
        s->export_header.manager_count,s->export_header.queue_count,
        (unsigned long)s->manager_attempted,(unsigned long)s->queue_attempted,
        (unsigned long)s->manager_rate_skipped,(unsigned long)s->queue_rate_skipped,
        (unsigned long)s->manager_skipped,(unsigned long)s->queue_skipped,
        (long)s->clock_failures,s->report_partial,s->artifact_name,POPULATION_FILE_SCHEMA);
    population_coverage(s,0); population_coverage(s,1);
    return population_artifact_write(s,&s->export_header,sizeof(s->export_header),1);
}
static int population_export_record(PopulationCaptureState *s)
{
    while(s->export_cursor<POPULATION_PACKETS*2U) {
        uint32_t cursor=s->export_cursor++,bucket=cursor/2U;
        PopulationCaptureRecordHeader record={0};
        const void *payload;
        uint32_t copied;
        if(cursor&1U) {
            const PopulationQueuePacket *q=&s->queues[bucket];
            if(q->complete!=1) continue;
            copied=q->copied;
            if(copied>PHYSICAL_QUEUE_ENTRIES) { s->export_error=ERROR_INVALID_DATA; return -1; }
            record.type=POPULATION_FILE_QUEUE; record.sequence=q->sequence;
            record.payload_bytes=POPULATION_QUEUE_PREFIX_BYTES+copied*POPULATION_QUEUE_ROW_BYTES;
            payload=q;
        } else {
            const PopulationManagerPacket *m=&s->managers[bucket];
            if(m->complete!=1) continue;
            copied=m->copied;
            if(copied>POPULATION_CANDIDATES) { s->export_error=ERROR_INVALID_DATA; return -1; }
            record.type=POPULATION_FILE_MANAGER; record.sequence=m->sequence;
            record.payload_bytes=POPULATION_MANAGER_PREFIX_BYTES+copied*POPULATION_CANDIDATE_BYTES;
            payload=m;
        }
        record.header_bytes=sizeof(record); record.bucket=bucket;
        if(!population_artifact_write(s,&record,sizeof(record),1) ||
           !population_artifact_write(s,payload,record.payload_bytes,1)) return -1;
        ++s->export_records;
        if(record.type==POPULATION_FILE_QUEUE) ++s->export_queues; else ++s->export_managers;
        return 0;
    }
    return 1;
}
static void population_export_finish(PopulationCaptureState *s,int complete)
{
    PopulationCaptureFileFooter footer={0};
    int artifact_flushed=0,artifact_closed,artifact_saved,log_flushed,log_summary;
    if(complete && !s->export_error) {
        if(s->export_managers!=s->export_header.manager_count || s->export_queues!=s->export_header.queue_count)
            s->export_error=ERROR_INVALID_DATA;
        else {
            memcpy(footer.magic,"SEPOEND",7); footer.schema=POPULATION_FILE_SCHEMA;
            footer.footer_bytes=sizeof(footer); footer.records=s->export_records;
            footer.managers=s->export_managers; footer.queues=s->export_queues;
            footer.crc32=s->export_crc^0xFFFFFFFFU; footer.bytes_before_footer=s->export_bytes;
            footer.total_bytes=s->export_bytes+sizeof(footer);
            footer.flags=s->export_header.flags; footer.expected_bins=s->expected_bins;
            if(population_artifact_write(s,&footer,sizeof(footer),0)) {
                artifact_flushed=FlushFileBuffers(s->artifact)!=0;
                if(!artifact_flushed) {
                    s->export_error=GetLastError();
                    if(!s->export_error) s->export_error=ERROR_WRITE_FAULT;
                }
            }
        }
    }
    artifact_closed=population_close_artifact(s);
    artifact_saved=complete && !s->export_error && artifact_flushed && artifact_closed;
    log_summary=append_log("STAGE_POPULATION_END serial=%u artifactSaved=%u records=%u managers=%u queues=%u bytes=%llu crc32=0x%08X reportPartial=%u error=%lu artifactFlush=%u artifactClose=%u cleanupPending=%u file=%ls",
        s->serial,(unsigned)artifact_saved,s->export_records,s->export_managers,s->export_queues,
        (unsigned long long)s->export_bytes,s->export_crc^0xFFFFFFFFU,s->report_partial,
        (unsigned long)s->export_error,(unsigned)artifact_flushed,(unsigned)artifact_closed,
        s->artifact!=INVALID_HANDLE_VALUE,s->artifact_name);
    append_log("STAGE_POPULATION_REPORT_COST serial=%u batches=%u totalMs=%.3f peakBatchMs=%.3f batchRecordLimit=%u batchByteLimit=%u budgetMs=%u budgetCheckedBetweenRecords=1",
        s->serial,s->report_batches,s->report_ticks*1000.0/s->frequency,
        s->report_peak_batch_ticks*1000.0/s->frequency,POPULATION_REPORT_BATCH_PACKETS,
        POPULATION_REPORT_BATCH_BYTES,POPULATION_REPORT_BUDGET_MS);
    log_flushed=flush_session_log();
    s->notice=artifact_saved && log_summary && log_flushed?4U:5U;
    s->phase=0;
    append_log("STAGE_POPULATION_SAVE_RESULT serial=%u saved=%u artifactSaved=%u notice=%u flushSucceeded=%u artifactFlush=%u artifactClose=%u reportPartial=%u bytes=%llu error=%lu scope=priorArtifactAndSummaryFlush markerDurabilityNotAsserted=1",
        s->serial,s->notice==4U,(unsigned)artifact_saved,s->notice,(unsigned)log_flushed,
        (unsigned)artifact_flushed,(unsigned)artifact_closed,s->report_partial,
        (unsigned long long)s->export_bytes,(unsigned long)s->export_error);
}
static void population_poll(void)
{
    PopulationCaptureState *s=population_state();
    if(!s || !s->phase) return;
    if(s->phase==1 && (population_elapsed_ms()>=POPULATION_WINDOW_MS ||
       (LONG)(GetTickCount()-s->end_tick)>=0)) population_finish();
    if(s->phase!=2 || InterlockedCompareExchange(&s->manager_lock,1,0)) return;
    if(InterlockedCompareExchange(&s->queue_lock,1,0)) { InterlockedExchange(&s->manager_lock,0); return; }
    if(!s->manager_busy && !s->queue_busy && !s->reservation_inflight) {
        uint32_t records;
        uint64_t bytes=s->export_bytes;
        int complete=0,result=0;
        int64_t started=population_qpc(),elapsed;
        if(!s->export_started && !population_export_begin(s)) result=-1;
        for(records=0;result>=0 && records<POPULATION_REPORT_BATCH_PACKETS;++records) {
            if(records && s->export_bytes-bytes>
               POPULATION_REPORT_BATCH_BYTES-sizeof(PopulationManagerPacket)-sizeof(PopulationCaptureRecordHeader)) break;
            result=population_export_record(s);
            if(result) { complete=result>0; break; }
            if(s->export_bytes-bytes>=POPULATION_REPORT_BATCH_BYTES ||
               population_qpc()-started>=s->frequency*POPULATION_REPORT_BUDGET_MS/1000) break;
        }
        elapsed=population_qpc()-started; s->report_ticks+=elapsed; ++s->report_batches;
        if(elapsed>s->report_peak_batch_ticks) s->report_peak_batch_ticks=elapsed;
        if(complete || result<0) population_export_finish(s,complete);
    }
    InterlockedExchange(&s->queue_lock,0); InterlockedExchange(&s->manager_lock,0);
}
