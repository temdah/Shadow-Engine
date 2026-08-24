/* Module 40: generation-safe external SliceExecute result storage,
 * consumption, release, and bounded F8/F9 diagnostics. */
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
static LifecycleRecordSnapshot *find_existing_lifecycle_record_fast(void *record)
{
    uint32_t start,index;
    if(!record) return NULL;
    start=(uint32_t)(((uintptr_t)record>>4U)&(LIFECYCLE_RECORD_SLOTS-1U));
    for(index=0;index<LIFECYCLE_RECORD_SLOTS;++index) {
        LifecycleRecordSnapshot *slot=&g_shadow_engine.external_results.lifecycle_records[
            (start+index)&(LIFECYCLE_RECORD_SLOTS-1U)];
        void *current=(void *)slot->record;
        if(current==record) return slot;
        if(!current) return NULL;
    }
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

static uint32_t submit_external_resources(unsigned char active,void *renderer,
    void **resources,uint32_t count,int *submitted)
{
    unsigned char *context=get_renderer_resource_context(renderer);
    LONG mode;
    if(submitted) *submitted=0;
    if(!context || !resources || !count || count>EXTRA_SLICE_RESULT_COUNT ||
       !g_shadow_engine.hooks.original_resource_wrapper) return 0;
    mode=atomic_add_long((volatile LONG *)(context+0xBC8),1);
    g_shadow_engine.hooks.original_resource_wrapper(active,resources,count,NULL,0,context,0,
        (uint32_t)mode);
    if(submitted) *submitted=1;
    return (uint32_t)mode;
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

static void __fastcall store_external_slice_result(void *inline_results,
    uint32_t index,void *result)
{
    LifecycleRecordSnapshot *slot;
    void *record,*previous;
    LONG generation,builder_call,stored_generation,stored_builder;
    LONG bit,prior_mask;
    uint32_t external_index;
    if(!inline_results || index<NATIVE_SLICE_RESULT_COUNT ||
       index>=TOTAL_LOCAL_MAPS) {
        InterlockedIncrement(&g_shadow_engine.external_results.slice_result_external_store_failures);
        return;
    }
    record=(unsigned char *)inline_results-0x70U;
    slot=find_existing_lifecycle_record_fast(record);
    if(!slot) {
        InterlockedIncrement(&g_shadow_engine.external_results.slice_result_external_store_failures);
        return;
    }
    if(InterlockedCompareExchange(
       &slot->extra_slice_result_consumer_state,0,0)!=0) {
        InterlockedIncrement(&g_shadow_engine.external_results.slice_result_external_late_writes);
        InterlockedIncrement(&g_shadow_engine.external_results.slice_result_external_store_failures);
        return;
    }
    InterlockedIncrement(&slot->extra_slice_result_active_writers);
    if(InterlockedCompareExchange(
       &slot->extra_slice_result_consumer_state,0,0)!=0) {
        InterlockedDecrement(&slot->extra_slice_result_active_writers);
        InterlockedIncrement(&g_shadow_engine.external_results.slice_result_external_late_writes);
        InterlockedIncrement(&g_shadow_engine.external_results.slice_result_external_store_failures);
        return;
    }
    generation=InterlockedCompareExchange(&slot->generation,0,0);
    builder_call=InterlockedCompareExchange(&slot->builder_call,0,0);
    stored_generation=InterlockedCompareExchange(
        &slot->extra_slice_result_generation,generation,0);
    stored_builder=InterlockedCompareExchange(
        &slot->extra_slice_result_builder_call,builder_call,0);
    if(generation<=0 || builder_call<=0 ||
       (stored_generation!=0 && stored_generation!=generation) ||
       (stored_builder!=0 && stored_builder!=builder_call)) {
        InterlockedDecrement(&slot->extra_slice_result_active_writers);
        InterlockedIncrement(&g_shadow_engine.external_results.slice_result_external_stale_lineage);
        InterlockedIncrement(&g_shadow_engine.external_results.slice_result_external_store_failures);
        return;
    }
    external_index=index-NATIVE_SLICE_RESULT_COUNT;
    bit=(LONG)(1U<<external_index);
    previous=InterlockedExchangePointer(
        (PVOID volatile *)&slot->extra_slice_results[external_index],result);
    prior_mask=atomic_or_long(&slot->extra_slice_result_written_mask,bit);
    if(prior_mask&bit)
        InterlockedIncrement(&g_shadow_engine.external_results.slice_result_external_duplicate_indices);
    (void)previous;
    if(result) InterlockedIncrement(&g_shadow_engine.external_results.slice_result_external_nonnull);
    else InterlockedIncrement(&g_shadow_engine.external_results.slice_result_external_null);
    InterlockedDecrement(&slot->extra_slice_result_active_writers);
}

static int consume_external_slice_results(void *renderer,
    LifecycleRecordSnapshot *slot,int fallback)
{
    void *resources[EXTRA_SLICE_RESULT_COUNT];
    LONG generation,builder_call,stored_generation,stored_builder,mask;
    uint32_t i,j,count=0,result_count;
    int submitted=0;
    if(!slot) return 1;
    if(InterlockedCompareExchange(
       &slot->extra_slice_result_consumer_state,1,0)!=0) return 1;
    if(!wait_for_external_slice_writers(slot)) {
        InterlockedExchange(&slot->extra_slice_result_consumer_state,0);
        InterlockedIncrement(&g_shadow_engine.external_results.slice_result_external_release_failures);
        return 0;
    }
    mask=InterlockedCompareExchange(&slot->extra_slice_result_written_mask,0,0);
    if(!mask) {
        InterlockedExchange(&slot->extra_slice_result_consumer_state,2);
        return 1;
    }
    generation=InterlockedCompareExchange(&slot->generation,0,0);
    builder_call=InterlockedCompareExchange(&slot->builder_call,0,0);
    stored_generation=InterlockedCompareExchange(
        &slot->extra_slice_result_generation,0,0);
    stored_builder=InterlockedCompareExchange(
        &slot->extra_slice_result_builder_call,0,0);
    if(stored_generation!=generation || stored_builder!=builder_call) {
        InterlockedExchange(&slot->extra_slice_result_consumer_state,0);
        InterlockedIncrement(&g_shadow_engine.external_results.slice_result_external_stale_lineage);
        InterlockedIncrement(&g_shadow_engine.external_results.slice_result_external_release_failures);
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
    InterlockedExchange(&slot->extra_slice_result_builder_call,0);
    InterlockedExchange(&slot->extra_slice_result_consumer_state,2);
    atomic_add_long(&g_shadow_engine.external_results.slice_result_external_consumed_results,
        (LONG)result_count);
    if(fallback)
        InterlockedIncrement(&g_shadow_engine.external_results.slice_result_external_release_fallbacks);
    return 1;
}

/* v0.9.10 keeps only the lifecycle state required by external slice-result
 * routing. The older provenance, repair, and sidecar hooks remain below as
 * dormant research code, but are no longer installed. */
static void reset_external_slice_cycle(LifecycleRecordSnapshot *slot)
{
    LONG prior_consumer,pending_mask;
    uint32_t i;
    prior_consumer=InterlockedExchange(
        &slot->extra_slice_result_consumer_state,1);
    if(!wait_for_external_slice_writers(slot))
        InterlockedIncrement(&g_shadow_engine.external_results.slice_result_external_reset_pending);
    pending_mask=InterlockedExchange(
        &slot->extra_slice_result_written_mask,0);
    if(pending_mask || prior_consumer==1)
        InterlockedIncrement(&g_shadow_engine.external_results.slice_result_external_reset_pending);
    for(i=0;i<EXTRA_SLICE_RESULT_COUNT;++i)
        InterlockedExchangePointer(
            (PVOID volatile *)&slot->extra_slice_results[i],NULL);
    InterlockedExchange(&slot->extra_slice_result_generation,0);
    InterlockedExchange(&slot->extra_slice_result_builder_call,0);
    InterlockedExchange(&slot->extra_slice_result_consumer_state,0);
}

static void maybe_log_external_slice_summary(void)
{
    DWORD now=GetTickCount();
    LONG prior=InterlockedCompareExchange(
        &g_shadow_engine.external_results.last_external_slice_summary_tick,0,0);
    if((DWORD)(now-(DWORD)prior)<EXTERNAL_SLICE_SUMMARY_PERIOD_MS) return;
    if(InterlockedCompareExchange(&g_shadow_engine.external_results.last_external_slice_summary_tick,(LONG)now,
       prior)!=prior) return;
    append_log("STAGE_Y_SUMMARY intervalMs=%u guardState=%ld nativeResultCount=%u externalResultCount=%u nativeNonNullStores=%ld nativeNullStores=%ld externalNonNullStores=%ld externalNullStores=%ld externalConsumedResults=%ld externalReleaseEvents=%ld externalReleaseResources=%ld storeFailures=%ld releaseFailures=%ld staleLineage=%ld lateWrites=%ld duplicateIndices=%ld duplicateResources=%ld releaseFallbacks=%ld resetPending=%ld overwriteFieldsProtected=F8,F100,F108,F110,F118 fieldMutation=externalSliceResultRouting diagnostics=minimal",
        EXTERNAL_SLICE_SUMMARY_PERIOD_MS,(long)g_shadow_engine.external_results.slice_result_guard_state,
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
        (long)g_shadow_engine.external_results.slice_result_external_duplicate_indices,
        (long)g_shadow_engine.external_results.slice_result_external_duplicate_resources,
        (long)g_shadow_engine.external_results.slice_result_external_release_fallbacks,
        (long)g_shadow_engine.external_results.slice_result_external_reset_pending);
}

static void log_manual_external_slice_snapshot(const char *state,LONG serial)
{
    append_log("STAGE_Y_MANUAL state=%s serial=%ld guardState=%ld nativeResultCount=%u externalResultCount=%u nativeNonNullStores=%ld nativeNullStores=%ld externalNonNullStores=%ld externalNullStores=%ld externalConsumedResults=%ld externalReleaseEvents=%ld externalReleaseResources=%ld storeFailures=%ld releaseFailures=%ld staleLineage=%ld lateWrites=%ld duplicateIndices=%ld duplicateResources=%ld releaseFallbacks=%ld resetPending=%ld overwriteFieldsProtected=F8,F100,F108,F110,F118 fieldMutation=externalSliceResultRouting diagnostics=minimal",
        state,(long)serial,(long)g_shadow_engine.external_results.slice_result_guard_state,
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
        (long)g_shadow_engine.external_results.slice_result_external_duplicate_indices,
        (long)g_shadow_engine.external_results.slice_result_external_duplicate_resources,
        (long)g_shadow_engine.external_results.slice_result_external_release_fallbacks,
        (long)g_shadow_engine.external_results.slice_result_external_reset_pending);
}

static void *__fastcall hooked_external_render_record_constructor(void *record)
{
    void *result=g_shadow_engine.hooks.original_render_record_constructor(record);
    LONG call=atomic_add_long(&g_shadow_engine.external_results.lifecycle_constructor_calls,1)+1;
    LifecycleRecordSnapshot *slot=find_lifecycle_record(record,1);
    if(slot) {
        InterlockedIncrement(&slot->generation);
        InterlockedExchange(&slot->constructor_call,call);
        InterlockedExchange(&slot->builder_call,0);
        reset_external_slice_cycle(slot);
    }
    return result;
}

static void __fastcall hooked_external_frame_builder(void *argument1,
    void *argument2,void *record,uint64_t argument4,uint64_t argument5,
    uint64_t argument6,uint64_t argument7)
{
    LONG call=atomic_add_long(&g_shadow_engine.external_results.lifecycle_builder_calls,1)+1;
    LifecycleRecordSnapshot *slot=find_lifecycle_record(record,1);
    if(slot) {
        reset_external_slice_cycle(slot);
        InterlockedExchange(&slot->builder_call,call);
    }
    g_shadow_engine.hooks.original_frame_builder(argument1,argument2,record,argument4,argument5,
        argument6,argument7);
}

static void __fastcall hooked_external_post_call_object_renderers(
    void *argument1,uint64_t argument2,unsigned char argument3,int argument4,
    unsigned char argument5,uint64_t argument6,void *record,
    uint64_t argument8,uint64_t argument9,uint64_t argument10)
{
    LifecycleRecordSnapshot *slot=find_existing_lifecycle_record_fast(record);
    InterlockedIncrement(&g_shadow_engine.external_results.postcall_calls);
    g_shadow_engine.hooks.original_post_call_object_renderers(argument1,argument2,argument3,
        argument4,argument5,argument6,record,argument8,argument9,argument10);
    consume_external_slice_results(argument1,slot,0);
    maybe_log_external_slice_summary();
}

static void __fastcall hooked_external_render_record_release(void *argument1,
    void *record,unsigned char argument3)
{
    LifecycleRecordSnapshot *slot=find_existing_lifecycle_record_fast(record);
    InterlockedIncrement(&g_shadow_engine.external_results.lifecycle_release_calls);
    if(slot && InterlockedCompareExchange(
       &slot->extra_slice_result_written_mask,0,0)!=0)
        consume_external_slice_results(argument1,slot,1);
    g_shadow_engine.hooks.original_render_record_release(argument1,record,argument3);
}
