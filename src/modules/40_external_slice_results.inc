/* Module 40: generation-safe external SliceExecute result storage,
 * consumption, release, and bounded F8/F9 diagnostics. */
#include "../internal/overhead_probe.h"
static LifecycleRecordSnapshot *find_lifecycle_record(void *record,int create)
{
    uint32_t start,index;
    if(!record) return NULL;
    start=(uint32_t)(((uintptr_t)record>>4U)&(LIFECYCLE_RECORD_SLOTS-1U));
    for(index=0;index<LIFECYCLE_RECORD_SLOTS;++index) {
        LifecycleRecordSnapshot *slot=&g_shadow_engine.external_results.lifecycle_records[
            (start+index)&(LIFECYCLE_RECORD_SLOTS-1U)];
        void *current=(void *)slot->record;
        if(current==record) return slot;
        if(!current && create) {
            void *prior=InterlockedCompareExchangePointer(
                (PVOID volatile *)&slot->record,record,NULL);
            if(!prior || prior==record) return slot;
        }
    }
    if(create) InterlockedIncrement(&g_shadow_engine.external_results.lifecycle_record_slot_misses);
    return NULL;
}
static unsigned char *get_renderer_resource_context(void *renderer)
{
    unsigned char *bytes=(unsigned char *)renderer;
    unsigned char *context;
    if(!renderer || !readable_memory(bytes+0x13B0,sizeof(void *))) return NULL;
    context=*(unsigned char **)(bytes+0x13B0);
    if(!context || !readable_memory(context+0xBC8,sizeof(LONG))) return NULL;
    return context;
}

static uint32_t submit_external_resources_with_context(unsigned char active,
    unsigned char *context,void **resources,uint32_t count,int *submitted)
{
    LONG mode;
    if(submitted) *submitted=0;
    if(!context || !readable_memory(context+0xBC8,sizeof(LONG)) ||
       !resources || !count || count>EXTRA_SLICE_RESULT_COUNT ||
       !g_shadow_engine.hooks.original_resource_wrapper) return 0;
    mode=atomic_add_long((volatile LONG *)(context+0xBC8),1);
    {
        OVERHEAD_BEGIN(submit_probe,OH_RESOURCE_SUBMIT_NATIVE);
        g_shadow_engine.hooks.original_resource_wrapper(active,resources,count,NULL,0,context,0,
            (uint32_t)mode);
        OVERHEAD_END(submit_probe);
    }
    if(submitted) *submitted=1;
    return (uint32_t)mode;
}

static uint32_t submit_external_resources(unsigned char active,void *renderer,
    void **resources,uint32_t count,int *submitted)
{
    return submit_external_resources_with_context(active,
        get_renderer_resource_context(renderer),resources,count,submitted);
}

/* SliceExecute acquires the primary resource before publishing it through the
 * result array. A rejected non-null publication therefore still owns exactly
 * one unmatched native acquisition. Retire that acquisition on the same
 * worker path with the resource context carried beside the result pointer. */
static void retire_rejected_external_slice_result(void *result,
    unsigned char *resource_context)
{
    void *resources[1];
    int submitted=0;
    if(!result) {
        InterlockedIncrement(
            &g_shadow_engine.external_results.slice_result_external_rejected_null);
        return;
    }
    InterlockedIncrement(
        &g_shadow_engine.external_results.slice_result_external_rejected_nonnull);
    resources[0]=result;
    submit_external_resources_with_context(0,resource_context,resources,1,
        &submitted);
    if(!submitted) {
        InterlockedIncrement(
            &g_shadow_engine.external_results.slice_result_external_orphan_release_failures);
        return;
    }
    InterlockedIncrement(
        &g_shadow_engine.external_results.slice_result_external_orphan_release_resources);
}

static uint32_t count_mask_bits(uint32_t value)
{
    uint32_t count=0;
    while(value) {
        value&=value-1U;
        ++count;
    }
    return count;
}

static int wait_for_external_slice_writers(LifecycleRecordSnapshot *slot)
{
    uint32_t spin;
    for(spin=0;spin<65536U;++spin) {
        if(InterlockedCompareExchange(
            &slot->extra_slice_result_active_writers,0,0)==0) return 1;
    }
    return 0;
}

static void count_external_slice_rejection(ExternalRejectReason reason)
{
    InterlockedIncrement(&g_shadow_engine.external_results.rejection_reasons[reason]);
    InterlockedIncrement(&g_shadow_engine.external_results.slice_result_external_store_failures);
}

static void __fastcall store_external_slice_result(void *inline_results,
    uint32_t index,void *result,unsigned char *resource_context)
{
    LifecycleRecordSnapshot *slot;
    void *record;
    LONG generation,cycle,stored_generation,stored_cycle;
    ExternalRejectReason reason;
    LONG bit,prior_mask;
    uint32_t external_index;
    /* Full helper envelope includes nested observation and rejected-result
     * retirement. Its child category timings must not be added to this one. */
    OVERHEAD_BEGIN(store_probe,OH_EXTERNAL_STORE);
    if(!inline_results || index<NATIVE_SLICE_RESULT_COUNT ||
       index>=TOTAL_LOCAL_MAPS) {
        count_external_slice_rejection(EXTERNAL_REJECT_INVALID_INPUT);
        retire_rejected_external_slice_result(result,resource_context);
        OVERHEAD_END(store_probe);
        return;
    }
    record=(unsigned char *)inline_results-0x70U;
#if SHADOW_ENGINE_INTERNAL_DIAGNOSTICS
    intersection_observe(IX_COMPLETION,record,index,result,0,0);
#endif
    /* Lifecycle ownership is required even when optional observation is absent. */
    slot=find_existing_lifecycle_record_fast(record);
#if SHADOW_ENGINE_INTERNAL_DIAGNOSTICS
    {
        OVERHEAD_BEGIN(completion_probe,OH_COMPLETION_RECORD);
        completion_diagnostic_record_completion(inline_results,index,
            slot ? InterlockedCompareExchange(&slot->generation,0,0):0,
            slot ? InterlockedCompareExchange(&slot->producer_cycle,0,0):0,
            slot ? InterlockedCompareExchange(
                &slot->extra_slice_result_consumer_state,0,0):-1);
        OVERHEAD_END(completion_probe);
    }
#endif
    if(!slot) {
        count_external_slice_rejection(EXTERNAL_REJECT_MISSING_RECORD);
        retire_rejected_external_slice_result(result,resource_context);
        OVERHEAD_END(store_probe);
        return;
    }
    if(InterlockedCompareExchange(
       &slot->extra_slice_result_consumer_state,0,0)!=0) {
        InterlockedIncrement(&g_shadow_engine.external_results.slice_result_external_late_writes);
        count_external_slice_rejection(EXTERNAL_REJECT_CLOSED_CONSUMER);
        retire_rejected_external_slice_result(result,resource_context);
        OVERHEAD_END(store_probe);
        return;
    }
    InterlockedIncrement(&slot->extra_slice_result_active_writers);
    if(InterlockedCompareExchange(
       &slot->extra_slice_result_consumer_state,0,0)!=0) {
        InterlockedDecrement(&slot->extra_slice_result_active_writers);
        InterlockedIncrement(&g_shadow_engine.external_results.slice_result_external_late_writes);
        count_external_slice_rejection(EXTERNAL_REJECT_CLOSED_CONSUMER);
        retire_rejected_external_slice_result(result,resource_context);
        OVERHEAD_END(store_probe);
        return;
    }
    generation=InterlockedCompareExchange(&slot->generation,0,0);
    cycle=InterlockedCompareExchange(&slot->producer_cycle,0,0);
    stored_generation=InterlockedCompareExchange(
        &slot->extra_slice_result_generation,0,0);
    stored_cycle=InterlockedCompareExchange(&slot->extra_slice_result_cycle,0,0);
    reason=EXTERNAL_REJECT_REASON_COUNT;
    if(generation<=0) reason=EXTERNAL_REJECT_ZERO_GENERATION;
    else if(cycle<=0) reason=EXTERNAL_REJECT_UNOPENED_CYCLE;
    else if(!InterlockedCompareExchange(&slot->producer_cycle_ready,0,0))
        reason=EXTERNAL_REJECT_QUARANTINED_CYCLE;
    else if(stored_generation!=generation) reason=EXTERNAL_REJECT_GENERATION_MISMATCH;
    else if(stored_cycle!=cycle) reason=EXTERNAL_REJECT_CYCLE_MISMATCH;
    if(reason!=EXTERNAL_REJECT_REASON_COUNT) {
        InterlockedDecrement(&slot->extra_slice_result_active_writers);
        InterlockedIncrement(&g_shadow_engine.external_results.slice_result_external_stale_lineage);
        count_external_slice_rejection(reason);
        retire_rejected_external_slice_result(result,resource_context);
        OVERHEAD_END(store_probe);
        return;
    }
    external_index=index-NATIVE_SLICE_RESULT_COUNT;
    bit=(LONG)(1U<<external_index);
    prior_mask=atomic_or_long(&slot->extra_slice_result_written_mask,bit);
    if(prior_mask&bit) {
        InterlockedIncrement(&g_shadow_engine.external_results.slice_result_external_duplicate_indices);
        count_external_slice_rejection(EXTERNAL_REJECT_DUPLICATE_INDEX);
        InterlockedDecrement(&slot->extra_slice_result_active_writers);
        retire_rejected_external_slice_result(result,resource_context);
        OVERHEAD_END(store_probe);
        return;
    }
    InterlockedExchangePointer(
        (PVOID volatile *)&slot->extra_slice_results[external_index],result);
    if(result) InterlockedIncrement(&g_shadow_engine.external_results.slice_result_external_nonnull);
    else InterlockedIncrement(&g_shadow_engine.external_results.slice_result_external_null);
    InterlockedDecrement(&slot->extra_slice_result_active_writers);
    OVERHEAD_END(store_probe);
}

static int consume_external_slice_results(void *renderer,
    LifecycleRecordSnapshot *slot)
{
    void *resources[EXTRA_SLICE_RESULT_COUNT];
    LONG generation,cycle,stored_generation,stored_cycle,mask;
    uint32_t i,j,count=0,result_count;
    int submitted=0;
    /* Includes bounded writer polling, context validation and native resource
     * submission. OH_RESOURCE_SUBMIT_NATIVE is an overlapping child interval. */
    OVERHEAD_BEGIN(consume_probe,OH_EXTERNAL_CONSUME);
    if(!slot) { OVERHEAD_END(consume_probe); return 1; }
    if(InterlockedCompareExchange(
       &slot->extra_slice_result_consumer_state,1,0)!=0) {
        OVERHEAD_END(consume_probe);
        return 1;
    }
    if(!wait_for_external_slice_writers(slot)) {
        InterlockedExchange(&slot->extra_slice_result_consumer_state,0);
        InterlockedIncrement(&g_shadow_engine.external_results.slice_result_external_release_failures);
        OVERHEAD_END(consume_probe);
        return 0;
    }
    mask=InterlockedCompareExchange(&slot->extra_slice_result_written_mask,0,0);
    if(!mask) {
        InterlockedExchange(&slot->extra_slice_result_consumer_state,2);
        OVERHEAD_END(consume_probe);
        return 1;
    }
    generation=InterlockedCompareExchange(&slot->generation,0,0);
    cycle=InterlockedCompareExchange(&slot->producer_cycle,0,0);
    stored_generation=InterlockedCompareExchange(
        &slot->extra_slice_result_generation,0,0);
    stored_cycle=InterlockedCompareExchange(&slot->extra_slice_result_cycle,0,0);
    if(stored_generation!=generation || stored_cycle!=cycle) {
        InterlockedExchange(&slot->extra_slice_result_consumer_state,0);
        InterlockedIncrement(&g_shadow_engine.external_results.slice_result_external_stale_lineage);
        InterlockedIncrement(&g_shadow_engine.external_results.slice_result_external_release_failures);
        OVERHEAD_END(consume_probe);
        return 0;
    }
    for(i=0;i<EXTRA_SLICE_RESULT_COUNT;++i) {
        void *resource;
        if(!(mask&(LONG)(1U<<i))) continue;
        resource=(void *)slot->extra_slice_results[i];
        if(!resource) continue;
        for(j=0;j<count;++j) if(resources[j]==resource) break;
        if(j<count)
            InterlockedIncrement(&g_shadow_engine.external_results.slice_result_external_duplicate_resources);
        resources[count++]=resource;
    }
    result_count=count_mask_bits((uint32_t)mask);
    if(count) {
        submit_external_resources(0,renderer,resources,count,&submitted);
        if(!submitted) {
            InterlockedExchange(&slot->extra_slice_result_consumer_state,0);
            InterlockedIncrement(&g_shadow_engine.external_results.slice_result_external_release_failures);
            OVERHEAD_END(consume_probe);
            return 0;
        }
        InterlockedIncrement(&g_shadow_engine.external_results.slice_result_external_release_events);
        atomic_add_long(&g_shadow_engine.external_results.slice_result_external_release_resources,(LONG)count);
    }
    for(i=0;i<EXTRA_SLICE_RESULT_COUNT;++i)
        InterlockedExchangePointer(
            (PVOID volatile *)&slot->extra_slice_results[i],NULL);
    InterlockedExchange(&slot->extra_slice_result_written_mask,0);
    InterlockedExchange(&slot->extra_slice_result_generation,0);
    InterlockedExchange(&slot->extra_slice_result_cycle,0);
    InterlockedExchange(&slot->extra_slice_result_consumer_state,2);
    atomic_add_long(&g_shadow_engine.external_results.slice_result_external_consumed_results,
        (LONG)result_count);
    OVERHEAD_END(consume_probe);
    return 1;
}

/* ExecuteFrameGraph continues submitting work after its native result-wrapper
 * batch. Runtime v2.0.4 proved that consuming there can close the generation
 * before external Slice writers finish. Drain at the function's finalizer
 * boundary instead, while RBX=renderer and RDI=record are still live.
 * This placement is not itself a job fence; producer-owned lineage and
 * runtime completion evidence establish the normal lifecycle ordering. */
static void __fastcall consume_external_slice_results_at_frame_graph_tail(
    void *renderer,void *record)
{
    LifecycleRecordSnapshot *slot;
    LONG pending;
    /* Tail envelope includes drain and ordinary summaries; those separately
     * measured child intervals are nonadditive with this lifecycle category. */
    OVERHEAD_BEGIN(tail_probe,OH_LIFECYCLE_PATCH);
    slot=find_existing_lifecycle_record_fast(record);
    pending=slot ? InterlockedCompareExchange(
        &slot->extra_slice_result_written_mask,0,0):0;
#if SHADOW_ENGINE_INTERNAL_DIAGNOSTICS
    {
        OVERHEAD_BEGIN(marker_probe,OH_COMPLETION_MARKERS);
        completion_diagnostic_mark_pre_finalizer(record);
        OVERHEAD_END(marker_probe);
    }
    intersection_observe(IX_PRE_FINALIZER,record,0,NULL,0,0);
#endif
    if(slot) {
        if(pending!=0)
            InterlockedIncrement(
                &g_shadow_engine.external_results.frame_graph_tail_drains);
        else
            InterlockedIncrement(
                &g_shadow_engine.external_results.frame_graph_tail_empty);
        /* A true tail boundary closes the generation even when no result has
         * arrived yet. Any post-tail completion is then rejected and retired
         * through its exact native resource context instead of being stranded. */
        consume_external_slice_results(renderer,slot);
    } else {
        InterlockedIncrement(
            &g_shadow_engine.external_results.frame_graph_tail_empty);
    }
#if !SHADOW_ENGINE_INTERNAL_DIAGNOSTICS
    /* Internal periodic summaries run on the existing tools worker. */
    maybe_log_external_slice_summary();
#endif
    OVERHEAD_END(tail_probe);
}

static int validate_frame_graph_pre_finalizer(void)
{
    static const unsigned char prefix[12]={
        0x44,0x0F,0xB7,0x45,0x68,
        0x48,0x8B,0x55,0x60,
        0x48,0x8B,0xCB
    };
    unsigned char *site=ENGINE_ADDRESS(
        FRAME_GRAPH_PRE_FINALIZER_RVA);
    int32_t relative;
    uint64_t target_rva;
    if(!readable_memory(site,17) ||
       memcmp(site,prefix,sizeof(prefix))!=0 || site[12]!=0xE8) return 0;
    memcpy(&relative,site+13,sizeof(relative));
    target_rva=RESOLVED_ENGINE_RVA(
        FRAME_GRAPH_PRE_FINALIZER_RVA)+17+(int64_t)relative;
    return target_rva==RESOLVED_ENGINE_RVA(FRAME_GRAPH_FINALIZER_RVA);
}

static int install_frame_graph_tail_relay(void)
{
    unsigned char body[64],expected[17];
    unsigned char *site=ENGINE_ADDRESS(
        FRAME_GRAPH_PRE_FINALIZER_RVA);
    uint64_t finalizer=(uint64_t)(uintptr_t)
        ENGINE_ADDRESS(FRAME_GRAPH_FINALIZER_RVA);
    uint64_t helper=(uint64_t)(uintptr_t)
        consume_external_slice_results_at_frame_graph_tail;
    size_t n=0;
    if(!validate_frame_graph_pre_finalizer()) return 0;
    memcpy(expected,site,sizeof(expected));

    body[n++]=0x48; body[n++]=0x8B; body[n++]=0xCB; /* mov rcx,rbx */
    body[n++]=0x48; body[n++]=0x8B; body[n++]=0xD7; /* mov rdx,rdi */
    body[n++]=0x48; body[n++]=0xB8;
    memcpy(body+n,&helper,8); n+=8;        /* mov rax,helper */
    body[n++]=0xFF; body[n++]=0xD0;       /* call rax */
    body[n++]=0x44; body[n++]=0x0F; body[n++]=0xB7; body[n++]=0x45;
    body[n++]=0x68;                         /* movzx r8d,[rbp+68] */
    body[n++]=0x48; body[n++]=0x8B; body[n++]=0x55; body[n++]=0x60;
                                             /* mov rdx,[rbp+60] */
    body[n++]=0x48; body[n++]=0x8B; body[n++]=0xCB; /* mov rcx,rbx */
    body[n++]=0x48; body[n++]=0xB8;
    memcpy(body+n,&finalizer,8); n+=8;      /* mov rax,native finalizer */
    body[n++]=0xFF; body[n++]=0xD0;         /* call rax */
    return install_inline_relay(FRAME_GRAPH_PRE_FINALIZER_RVA,
        expected,sizeof(expected),body,n);
}

/* Begin a new generation only after the prior frame-graph consumer has
 * retired its external results. Any overlap is recorded as a hard lifecycle
 * invariant violation for the runtime test log. */
static int reset_external_slice_cycle(LifecycleRecordSnapshot *slot)
{
    LONG prior_consumer;
    OVERHEAD_BEGIN(reset_probe,OH_EXTERNAL_RESET);
    prior_consumer=InterlockedCompareExchange(
        &slot->extra_slice_result_consumer_state,1,0);
    if(prior_consumer==2 && InterlockedCompareExchange(
       &slot->extra_slice_result_consumer_state,1,2)!=2) prior_consumer=1;
    InterlockedExchange(&slot->producer_cycle_ready,0);
    if(prior_consumer!=0 && prior_consumer!=2) {
        InterlockedIncrement(&g_shadow_engine.external_results.slice_result_external_reset_pending);
        OVERHEAD_END(reset_probe);
        return 0;
    }
    /* Never discard an owned pointer or spin for asynchronous jobs. A failed
     * reset quarantines publication but preserves the old cycle for drain. */
    if(InterlockedCompareExchange(&slot->extra_slice_result_active_writers,0,0) ||
       InterlockedCompareExchange(&slot->extra_slice_result_written_mask,0,0)) {
        InterlockedExchange(&slot->extra_slice_result_consumer_state,prior_consumer);
        InterlockedIncrement(&g_shadow_engine.external_results.slice_result_external_reset_pending);
        OVERHEAD_END(reset_probe);
        return 0;
    }
#if SHADOW_ENGINE_INTERNAL_DIAGNOSTICS
    {
        OVERHEAD_BEGIN(marker_probe,OH_COMPLETION_MARKERS);
        completion_diagnostic_mark_reset((void *)slot->record);
        OVERHEAD_END(marker_probe);
    }
#endif
    InterlockedExchange(&slot->extra_slice_result_generation,0);
    InterlockedExchange(&slot->extra_slice_result_cycle,0);
    InterlockedExchange(&slot->producer_cycle,0);
    InterlockedExchange(&slot->extra_slice_result_consumer_state,0);
    OVERHEAD_END(reset_probe);
    return 1;
}

static int begin_external_slice_producer_cycle(void *record)
{
    LifecycleRecordSnapshot *slot=find_existing_lifecycle_record_fast(record);
    LONG cycle;
    if(!slot || InterlockedCompareExchange(&slot->generation,0,0)<=0) {
        if(slot) InterlockedExchange(&slot->producer_cycle_ready,0);
        InterlockedIncrement(&g_shadow_engine.external_results.producer_cycle_open_failures);
        return 0;
    }
    /* A prior producer must have reached its consumer, even when it produced
     * no external result. Fail publication closed without clearing ownership. */
    if((InterlockedCompareExchange(&slot->producer_cycle,0,0)>0 &&
        InterlockedCompareExchange(&slot->extra_slice_result_consumer_state,0,0)!=2) ||
       !reset_external_slice_cycle(slot)) {
        InterlockedExchange(&slot->producer_cycle_ready,0);
        InterlockedIncrement(&g_shadow_engine.external_results.producer_cycle_open_failures);
        return 0;
    }
    cycle=atomic_add_long(&g_shadow_engine.external_results.lifecycle_producer_calls,1)+1;
    InterlockedExchange(&slot->producer_cycle,cycle);
    InterlockedExchange(&slot->extra_slice_result_generation,
        InterlockedCompareExchange(&slot->generation,0,0));
    InterlockedExchange(&slot->extra_slice_result_cycle,cycle);
    /* Release publication last, before the native scheduling call. */
    InterlockedExchange(&slot->producer_cycle_ready,1);
    return 1;
}

static void log_external_slice_reasons(const char *state,LONG serial)
{
    ExternalResultState *results=&g_shadow_engine.external_results;
    append_log("STAGE_Y_REASONS state=%s serial=%ld lineageKey=constructorProducer cycleOpens=%ld cycleOpenFailures=%ld invalidInput=%ld missingRecord=%ld closedConsumer=%ld zeroGeneration=%ld unopenedCycle=%ld generationMismatch=%ld cycleMismatch=%ld duplicateIndex=%ld quarantinedCycle=%ld",
        state,(long)serial,(long)results->lifecycle_producer_calls,
        (long)results->producer_cycle_open_failures,
        (long)results->rejection_reasons[EXTERNAL_REJECT_INVALID_INPUT],
        (long)results->rejection_reasons[EXTERNAL_REJECT_MISSING_RECORD],
        (long)results->rejection_reasons[EXTERNAL_REJECT_CLOSED_CONSUMER],
        (long)results->rejection_reasons[EXTERNAL_REJECT_ZERO_GENERATION],
        (long)results->rejection_reasons[EXTERNAL_REJECT_UNOPENED_CYCLE],
        (long)results->rejection_reasons[EXTERNAL_REJECT_GENERATION_MISMATCH],
        (long)results->rejection_reasons[EXTERNAL_REJECT_CYCLE_MISMATCH],
        (long)results->rejection_reasons[EXTERNAL_REJECT_DUPLICATE_INDEX],
        (long)results->rejection_reasons[EXTERNAL_REJECT_QUARANTINED_CYCLE]);
}

static void maybe_log_external_slice_summary(void)
{
    DWORD now=GetTickCount();
    LONG prior=InterlockedCompareExchange(
        &g_shadow_engine.external_results.last_external_slice_summary_tick,0,0);
    if((DWORD)(now-(DWORD)prior)<EXTERNAL_SLICE_SUMMARY_PERIOD_MS) return;
    if(InterlockedCompareExchange(&g_shadow_engine.external_results.last_external_slice_summary_tick,(LONG)now,
       prior)!=prior) return;
    append_log("STAGE_Y_SUMMARY intervalMs=%u guardState=%ld frameGraphTailDrains=%ld frameGraphTailEmpty=%ld nativeResultCount=%u externalResultCount=%u nativeNonNullStores=%ld nativeNullStores=%ld externalNonNullStores=%ld externalNullStores=%ld externalConsumedResults=%ld externalReleaseEvents=%ld externalReleaseResources=%ld storeFailures=%ld releaseFailures=%ld staleLineage=%ld lateWrites=%ld rejectedNonNull=%ld rejectedNull=%ld orphanReleaseResources=%ld orphanReleaseFailures=%ld duplicateIndices=%ld duplicateResources=%ld resetPending=%ld fieldMutation=externalSliceResultRouting diagnostics=minimal",
        EXTERNAL_SLICE_SUMMARY_PERIOD_MS,(long)g_shadow_engine.external_results.slice_result_guard_state,
        (long)g_shadow_engine.external_results.frame_graph_tail_drains,
        (long)g_shadow_engine.external_results.frame_graph_tail_empty,
        NATIVE_SLICE_RESULT_COUNT,EXTRA_SLICE_RESULT_COUNT,
        (long)g_shadow_engine.external_results.slice_result_native_nonnull,
        (long)g_shadow_engine.external_results.slice_result_native_null,
        (long)g_shadow_engine.external_results.slice_result_external_nonnull,
        (long)g_shadow_engine.external_results.slice_result_external_null,
        (long)g_shadow_engine.external_results.slice_result_external_consumed_results,
        (long)g_shadow_engine.external_results.slice_result_external_release_events,
        (long)g_shadow_engine.external_results.slice_result_external_release_resources,
        (long)g_shadow_engine.external_results.slice_result_external_store_failures,
        (long)g_shadow_engine.external_results.slice_result_external_release_failures,
        (long)g_shadow_engine.external_results.slice_result_external_stale_lineage,
        (long)g_shadow_engine.external_results.slice_result_external_late_writes,
        (long)g_shadow_engine.external_results.slice_result_external_rejected_nonnull,
        (long)g_shadow_engine.external_results.slice_result_external_rejected_null,
        (long)g_shadow_engine.external_results.slice_result_external_orphan_release_resources,
        (long)g_shadow_engine.external_results.slice_result_external_orphan_release_failures,
        (long)g_shadow_engine.external_results.slice_result_external_duplicate_indices,
        (long)g_shadow_engine.external_results.slice_result_external_duplicate_resources,
        (long)g_shadow_engine.external_results.slice_result_external_reset_pending);
    log_external_slice_reasons("SUMMARY",0);
}

static void log_manual_external_slice_snapshot(const char *state,LONG serial)
{
    append_log("STAGE_Y_MANUAL state=%s serial=%ld guardState=%ld frameGraphTailDrains=%ld frameGraphTailEmpty=%ld nativeResultCount=%u externalResultCount=%u nativeNonNullStores=%ld nativeNullStores=%ld externalNonNullStores=%ld externalNullStores=%ld externalConsumedResults=%ld externalReleaseEvents=%ld externalReleaseResources=%ld storeFailures=%ld releaseFailures=%ld staleLineage=%ld lateWrites=%ld rejectedNonNull=%ld rejectedNull=%ld orphanReleaseResources=%ld orphanReleaseFailures=%ld duplicateIndices=%ld duplicateResources=%ld resetPending=%ld fieldMutation=externalSliceResultRouting diagnostics=minimal",
        state,(long)serial,(long)g_shadow_engine.external_results.slice_result_guard_state,
        (long)g_shadow_engine.external_results.frame_graph_tail_drains,
        (long)g_shadow_engine.external_results.frame_graph_tail_empty,
        NATIVE_SLICE_RESULT_COUNT,EXTRA_SLICE_RESULT_COUNT,
        (long)g_shadow_engine.external_results.slice_result_native_nonnull,
        (long)g_shadow_engine.external_results.slice_result_native_null,
        (long)g_shadow_engine.external_results.slice_result_external_nonnull,
        (long)g_shadow_engine.external_results.slice_result_external_null,
        (long)g_shadow_engine.external_results.slice_result_external_consumed_results,
        (long)g_shadow_engine.external_results.slice_result_external_release_events,
        (long)g_shadow_engine.external_results.slice_result_external_release_resources,
        (long)g_shadow_engine.external_results.slice_result_external_store_failures,
        (long)g_shadow_engine.external_results.slice_result_external_release_failures,
        (long)g_shadow_engine.external_results.slice_result_external_stale_lineage,
        (long)g_shadow_engine.external_results.slice_result_external_late_writes,
        (long)g_shadow_engine.external_results.slice_result_external_rejected_nonnull,
        (long)g_shadow_engine.external_results.slice_result_external_rejected_null,
        (long)g_shadow_engine.external_results.slice_result_external_orphan_release_resources,
        (long)g_shadow_engine.external_results.slice_result_external_orphan_release_failures,
        (long)g_shadow_engine.external_results.slice_result_external_duplicate_indices,
        (long)g_shadow_engine.external_results.slice_result_external_duplicate_resources,
        (long)g_shadow_engine.external_results.slice_result_external_reset_pending);
    log_external_slice_reasons(state,serial);
#if SHADOW_ENGINE_INTERNAL_DIAGNOSTICS
    completion_diagnostic_log_manual(state,serial);
#endif
}

static void *__fastcall hooked_external_render_record_constructor(void *record)
{
    void *result=g_shadow_engine.hooks.original_render_record_constructor(record);
    LONG call;
    LifecycleRecordSnapshot *slot;
    OVERHEAD_BEGIN(constructor_probe,OH_LIFECYCLE_PATCH);
    call=atomic_add_long(&g_shadow_engine.external_results.lifecycle_constructor_calls,1)+1;
    slot=find_lifecycle_record(record,1);
    if(slot && reset_external_slice_cycle(slot)) {
        InterlockedIncrement(&slot->generation);
        InterlockedExchange(&slot->constructor_call,call);
        InterlockedExchange(&slot->builder_call,0);
    }
#if SHADOW_ENGINE_INTERNAL_DIAGNOSTICS
    intersection_observe(IX_CONSTRUCTOR,record,0,NULL,0,0);
#endif
    OVERHEAD_END(constructor_probe);
    return result;
}

static void __fastcall hooked_external_frame_builder(void *argument1,
    void *argument2,void *record,uint64_t argument4,uint64_t argument5,
    uint64_t argument6,uint64_t argument7)
{
#if SHADOW_ENGINE_INTERNAL_DIAGNOSTICS
    IntersectionBuilderScope scope;
#endif
    LONG call;
    LifecycleRecordSnapshot *slot;
    OVERHEAD_BEGIN(builder_probe,OH_LIFECYCLE_PATCH);
    call=atomic_add_long(&g_shadow_engine.external_results.lifecycle_builder_calls,1)+1;
    slot=find_lifecycle_record(record,1);
    if(slot) {
        /* Builder identity is diagnostic only: Slice jobs can already be
         * publishing into the producer-owned cycle at this point. */
        InterlockedExchange(&slot->builder_call,call);
    }
#if SHADOW_ENGINE_INTERNAL_DIAGNOSTICS
    intersection_builder_enter(&scope,record);
#endif
    OVERHEAD_PAUSE(builder_probe);
    g_shadow_engine.hooks.original_frame_builder(argument1,argument2,record,argument4,argument5,
        argument6,argument7);
    OVERHEAD_RESUME(builder_probe);
#if SHADOW_ENGINE_INTERNAL_DIAGNOSTICS
    intersection_builder_exit(&scope);
#endif
    OVERHEAD_END(builder_probe);
}

static void __fastcall hooked_external_render_record_release(void *argument1,
    void *record,unsigned char argument3)
{
    OVERHEAD_BEGIN(release_probe,OH_LIFECYCLE_PATCH);
    InterlockedIncrement(&g_shadow_engine.external_results.lifecycle_release_calls);
#if SHADOW_ENGINE_INTERNAL_DIAGNOSTICS
    intersection_observe(IX_RELEASE,record,0,NULL,0,0);
#endif
    /* This native hook precedes some external Slice completions. It remains a
     * lifecycle observation point only; normal sidecar consumption belongs at
     * the later frame-graph pre-finalizer boundary. */
    OVERHEAD_END(release_probe);
    g_shadow_engine.hooks.original_render_record_release(argument1,record,argument3);
}
