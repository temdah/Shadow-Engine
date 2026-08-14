/* Module 50: resource-producer correlation, wrapper tracing, null compaction,
 * and acquire/release diagnostics. */
static void publish_resource_producer(LONG call,void *manager,void *caller,
    unsigned char active,uint32_t mode,uint32_t primary_count,
    uint32_t primary_valid,uint32_t secondary_count,uint32_t secondary_valid,
    uint64_t primary_null_mask,uint64_t secondary_null_mask)
{
    ResourceProducerSnapshot *slot=&g_resource_producers[
        (uint32_t)call&(RESOURCE_PRODUCER_SLOTS-1U)];
    uintptr_t caller_value=(uintptr_t)caller;
    uintptr_t base_value=(uintptr_t)g_disrupt_base;
    InterlockedExchange(&slot->serial,0);
    slot->manager=manager;
    slot->caller=caller;
    slot->caller_rva=(caller_value>=base_value &&
        caller_value<base_value+0x10000000ULL) ?
        (uint64_t)(caller_value-base_value):~(uint64_t)0;
    slot->mode=mode;
    slot->active=active ? 1U:0U;
    slot->primary_count=primary_count;
    slot->primary_valid=primary_valid;
    slot->secondary_count=secondary_count;
    slot->secondary_valid=secondary_valid;
    slot->primary_null_mask=primary_null_mask;
    slot->secondary_null_mask=secondary_null_mask;
    InterlockedExchange(&slot->serial,call);
}
static int find_resource_producer(ResourceProducerSnapshot *destination,
    void *manager,unsigned char active,uint32_t mode,uint32_t primary_count,
    uint32_t secondary_count)
{
    uint32_t index;
    LONG best=0;
    ResourceProducerSnapshot candidate;
    for(index=0;index<RESOURCE_PRODUCER_SLOTS;++index) {
        ResourceProducerSnapshot *slot=&g_resource_producers[index];
        LONG before=InterlockedCompareExchange(&slot->serial,0,0);
        LONG after;
        if(before<=0) continue;
        memcpy(&candidate,slot,sizeof(candidate));
        after=InterlockedCompareExchange(&slot->serial,0,0);
        if(before!=after || candidate.serial!=before) continue;
        if(candidate.manager!=manager || candidate.active!=(active ? 1U:0U) ||
           candidate.mode!=mode || candidate.primary_count!=primary_count ||
           candidate.secondary_count!=secondary_count) continue;
        if(before>best) {
            *destination=candidate;
            best=before;
        }
    }
    return best>0;
}
static int64_t __fastcall hooked_resource_wrapper(unsigned char active,
    void **primary,uint32_t primary_count,void **secondary,
    uint32_t secondary_count,unsigned char *context,uint64_t argument7,
    uint32_t mode)
{
    void *manager=NULL;
    void *caller=__builtin_return_address(0);
    LONG call=atomic_add_long(&g_resource_wrapper_calls,1)+1;
    uint32_t primary_valid=count_nonnull_objects(primary,primary_count);
    uint32_t secondary_valid=count_nonnull_objects(secondary,secondary_count);
    uint64_t primary_null_mask=build_null_mask(primary,primary_count);
    uint64_t secondary_null_mask=build_null_mask(secondary,secondary_count);
    if(context && readable_memory(context+0x20,sizeof(void *)))
        manager=*(void **)(context+0x20);
    publish_resource_producer(call,manager,caller,active,mode,primary_count,
        primary_valid,secondary_count,secondary_valid,primary_null_mask,
        secondary_null_mask);
    if(primary_valid!=primary_count || secondary_valid!=secondary_count)
        append_log("STAGE_M_RESOURCE_PRODUCER_NULL producerCall=%ld caller=%p callerRva=0x%08X%08X manager=%p direction=%s mode=%u primaryCount=%u primaryValid=%u primaryNullMask=0x%08X%08X secondaryCount=%u secondaryValid=%u secondaryNullMask=0x%08X%08X",
            (long)call,caller,
            (uint32_t)((((uintptr_t)caller>=(uintptr_t)g_disrupt_base) ?
                (uint64_t)((uintptr_t)caller-(uintptr_t)g_disrupt_base):
                ~(uint64_t)0)>>32),
            (uint32_t)(((uintptr_t)caller>=(uintptr_t)g_disrupt_base) ?
                (uint64_t)((uintptr_t)caller-(uintptr_t)g_disrupt_base):
                ~(uint64_t)0),manager,active ? "acquire":"release",mode,
            primary_count,primary_valid,(uint32_t)(primary_null_mask>>32),
            (uint32_t)primary_null_mask,secondary_count,secondary_valid,
            (uint32_t)(secondary_null_mask>>32),(uint32_t)secondary_null_mask);
    return g_original_resource_wrapper(active,primary,primary_count,secondary,
        secondary_count,context,argument7,mode);
}

static void publish_resource_null_snapshot(ResourceNullSnapshot *target,
    LONG event,LONG submit_call,void *manager,unsigned char active,
    void **primary,uint32_t primary_count,uint32_t primary_valid,
    void **secondary,uint32_t secondary_count,uint32_t secondary_valid,
    uint64_t primary_null_mask,uint64_t secondary_null_mask,
    uint64_t argument7,uint32_t mode)
{
    LONG context=InterlockedCompareExchange(&g_active_renderer_context,0,0);
    ResourceProducerSnapshot producer;
    int producer_found=find_resource_producer(&producer,manager,active,mode,
        primary_count,secondary_count);
    InterlockedExchange(&target->serial,0);
    target->submit_call=submit_call;
    target->renderer_context=context;
    target->renderer_call=InterlockedCompareExchange(&g_active_renderer_call,0,0);
    target->entries=(uint32_t)InterlockedCompareExchange(
        &g_active_renderer_entries,0,0);
    target->faces=(uint32_t)InterlockedCompareExchange(
        &g_active_renderer_faces,0,0);
    target->highest_map=(uint32_t)InterlockedCompareExchange(
        &g_active_renderer_highest_map,0,0);
    target->manager=manager;
    target->queue=g_active_renderer_queue;
    target->primary=primary;
    target->secondary=secondary;
    target->primary_count=primary_count;
    target->primary_valid=primary_valid;
    target->secondary_count=secondary_count;
    target->secondary_valid=secondary_valid;
    target->primary_null_mask=primary_null_mask;
    target->secondary_null_mask=secondary_null_mask;
    target->argument7=argument7;
    target->mode=mode;
    target->active=active ? 1U:0U;
    target->producer_found=producer_found;
    target->producer_call=producer_found ? producer.serial:0;
    target->producer_caller=producer_found ? producer.caller:NULL;
    target->producer_caller_rva=producer_found ? producer.caller_rva:
        ~(uint64_t)0;
    target->producer_primary_valid=producer_found ? producer.primary_valid:0;
    target->producer_secondary_valid=producer_found ?
        producer.secondary_valid:0;
    target->producer_primary_null_mask=producer_found ?
        producer.primary_null_mask:0;
    target->producer_secondary_null_mask=producer_found ?
        producer.secondary_null_mask:0;
    InterlockedExchange(&target->serial,event);
}

static void trace_resource_null_event(LONG event,LONG submit_call,void *manager,
    unsigned char active,void **primary,uint32_t primary_count,
    uint32_t primary_valid,void **secondary,uint32_t secondary_count,
    uint32_t secondary_valid,uint64_t primary_null_mask,
    uint64_t secondary_null_mask,uint64_t argument7,uint32_t mode)
{
    LONG context=InterlockedCompareExchange(&g_active_renderer_context,0,0);
    LONG renderer_call=InterlockedCompareExchange(&g_active_renderer_call,0,0);
    LONG entries=InterlockedCompareExchange(&g_active_renderer_entries,0,0);
    LONG faces=InterlockedCompareExchange(&g_active_renderer_faces,0,0);
    LONG highest=InterlockedCompareExchange(&g_active_renderer_highest_map,0,0);
    int emit=event<=(LONG)RESOURCE_TRACE_INITIAL_EVENTS ||
        (event%(LONG)RESOURCE_TRACE_PERIOD)==0;
    ResourceNullSnapshot *target=active ? &g_last_null_acquire:
                                          &g_last_null_release;
    if(context) InterlockedIncrement(&g_null_events_with_renderer_context);
    if(context && faces>(LONG)ORIGINAL_LOCAL_MAPS) {
        InterlockedIncrement(&g_null_events_with_extra_maps);
        if(InterlockedCompareExchange(&g_first_null_extra_logged,1,0)==0)
            emit=1;
    }
    publish_resource_null_snapshot(target,event,submit_call,manager,active,
        primary,primary_count,primary_valid,secondary,secondary_count,
        secondary_valid,primary_null_mask,secondary_null_mask,argument7,mode);
    if(emit)
        append_log("STAGE_M_RESOURCE_NULL event=%ld submitCall=%ld direction=%s rendererContext=%ld rendererCall=%ld queue=%p entries=%ld faces=%ld highestMap=%ld manager=%p mode=%u argument7High=0x%08X argument7Low=0x%08X primary=%p primaryCount=%u primaryValid=%u primaryNullMask=0x%08X%08X secondary=%p secondaryCount=%u secondaryValid=%u secondaryNullMask=0x%08X%08X producerFound=%ld producerCall=%ld producerCaller=%p producerCallerRva=0x%08X%08X producerPrimaryValid=%u producerPrimaryNullMask=0x%08X%08X producerSecondaryValid=%u producerSecondaryNullMask=0x%08X%08X",
            (long)event,(long)submit_call,active ? "acquire":"release",
            (long)context,(long)renderer_call,g_active_renderer_queue,
            (long)entries,(long)faces,(long)highest,manager,mode,
            (uint32_t)(argument7>>32),(uint32_t)argument7,primary,primary_count,
            primary_valid,(uint32_t)(primary_null_mask>>32),
            (uint32_t)primary_null_mask,secondary,secondary_count,
            secondary_valid,(uint32_t)(secondary_null_mask>>32),
            (uint32_t)secondary_null_mask,(long)target->producer_found,
            (long)target->producer_call,target->producer_caller,
            (uint32_t)(target->producer_caller_rva>>32),
            (uint32_t)target->producer_caller_rva,
            target->producer_primary_valid,
            (uint32_t)(target->producer_primary_null_mask>>32),
            (uint32_t)target->producer_primary_null_mask,
            target->producer_secondary_valid,
            (uint32_t)(target->producer_secondary_null_mask>>32),
            (uint32_t)target->producer_secondary_null_mask);
}

static void __fastcall hooked_resource_submit(void *manager,unsigned char active,
    void **primary,uint32_t primary_count,void **secondary,
    uint32_t secondary_count,uint64_t argument7,uint32_t mode)
{
    void *primary_scratch[64],*secondary_scratch[64];
    LONG submit_call=atomic_add_long(&g_resource_submit_calls,1)+1;
    uint32_t primary_valid=count_nonnull_objects(primary,primary_count);
    uint32_t secondary_valid=count_nonnull_objects(secondary,secondary_count);
    uint32_t dropped_total=(primary_count-primary_valid)+
                           (secondary_count-secondary_valid);
    uint64_t primary_null_mask=build_null_mask(primary,primary_count);
    uint64_t secondary_null_mask=build_null_mask(secondary,secondary_count);
    if(dropped_total) {
        LONG event=atomic_add_long(&g_null_resource_events,1)+1;
        trace_resource_null_event(event,submit_call,manager,active,
            primary,primary_count,primary_valid,secondary,secondary_count,
            secondary_valid,primary_null_mask,secondary_null_mask,argument7,mode);
    }
    if(active) atomic_add_long(&g_null_acquire_drops,(LONG)dropped_total);
    else atomic_add_long(&g_null_release_drops,(LONG)dropped_total);
    if(!primary && primary_count) {
        atomic_add_long(&g_null_submit_drops,(LONG)primary_count);
        primary_count=0;
    }
    if(!secondary && secondary_count) {
        atomic_add_long(&g_null_submit_drops,(LONG)secondary_count);
        secondary_count=0;
    }
    if(primary && primary_valid!=primary_count && primary_count<=64U &&
       readable_memory(primary,(size_t)primary_count*sizeof(void *))) {
        uint32_t read_index,write_index=0,dropped=primary_count-primary_valid;
        for(read_index=0;read_index<primary_count;++read_index)
            if(primary[read_index]) primary_scratch[write_index++]=primary[read_index];
        primary=primary_scratch;
        primary_count=primary_valid;
        atomic_add_long(&g_null_submit_drops,(LONG)dropped);
    }
    if(secondary && secondary_valid!=secondary_count && secondary_count<=64U &&
       readable_memory(secondary,(size_t)secondary_count*sizeof(void *))) {
        uint32_t read_index,write_index=0,dropped=secondary_count-secondary_valid;
        for(read_index=0;read_index<secondary_count;++read_index)
            if(secondary[read_index]) secondary_scratch[write_index++]=secondary[read_index];
        secondary=secondary_scratch;
        secondary_count=secondary_valid;
        atomic_add_long(&g_null_submit_drops,(LONG)dropped);
    }
    if(!primary_count && !secondary_count) return;
    g_original_resource_submit(manager,active,primary,primary_count,secondary,
        secondary_count,argument7,mode);
}
