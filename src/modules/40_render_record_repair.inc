/* Module 40: bounded render-record lineage tracking and the v0.8.9 same-cycle
 * F8/100 repair with symmetric displaced-resource sidecars. */
static LifecycleRecordSnapshot *find_lifecycle_record(void *record,int create)
{
    uint32_t start,index;
    if(!record) return NULL;
    start=(uint32_t)(((uintptr_t)record>>4U)&(LIFECYCLE_RECORD_SLOTS-1U));
    for(index=0;index<LIFECYCLE_RECORD_SLOTS;++index) {
        LifecycleRecordSnapshot *slot=&g_lifecycle_records[
            (start+index)&(LIFECYCLE_RECORD_SLOTS-1U)];
        void *current=(void *)slot->record;
        if(current==record) return slot;
        if(!current && create) {
            void *prior=InterlockedCompareExchangePointer(
                (PVOID volatile *)&slot->record,record,NULL);
            if(!prior || prior==record) return slot;
        }
    }
    if(create) InterlockedIncrement(&g_lifecycle_record_slot_misses);
    return NULL;
}
static LifecycleThreadContext *find_lifecycle_thread(int create)
{
    LONG thread_id=(LONG)GetCurrentThreadId();
    uint32_t index;
    for(index=0;index<LIFECYCLE_THREAD_SLOTS;++index) {
        LifecycleThreadContext *slot=&g_lifecycle_threads[index];
        LONG current=InterlockedCompareExchange(&slot->thread_id,0,0);
        if(current==thread_id) return slot;
        if(current==0 && create &&
           InterlockedCompareExchange(&slot->thread_id,thread_id,0)==0)
            return slot;
    }
    if(create) InterlockedIncrement(&g_lifecycle_thread_slot_misses);
    return NULL;
}
static int read_lifecycle_fields(void *record,LONG *flag,void **field_f8,
    void **field_100)
{
    unsigned char *bytes=(unsigned char *)record;
    if(flag) *flag=0;
    if(field_f8) *field_f8=NULL;
    if(field_100) *field_100=NULL;
    if(!record || !readable_memory(bytes+0x100,sizeof(void *))) return 0;
    if(flag) *flag=bytes[0x11] ? 1:0;
    if(field_f8) *field_f8=*(void **)(bytes+0xF8);
    if(field_100) *field_100=*(void **)(bytes+0x100);
    return 1;
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

static uint32_t submit_sidecar_resources(unsigned char active,void *renderer,
    void **resources,uint32_t count,int *submitted)
{
    unsigned char *context=get_renderer_resource_context(renderer);
    LONG mode;
    if(submitted) *submitted=0;
    if(!context || !resources || !count || count>2U ||
       !g_original_resource_wrapper) return 0;
    mode=atomic_add_long((volatile LONG *)(context+0xBC8),1);
    g_original_resource_wrapper(active,resources,count,NULL,0,context,0,
        (uint32_t)mode);
    if(submitted) *submitted=1;
    return (uint32_t)mode;
}

static int should_emit_lifecycle_event(LONG event)
{
    return event<=(LONG)LIFECYCLE_TRACE_INITIAL ||
           (event%(LONG)LIFECYCLE_TRACE_PERIOD)==0;
}

static void log_lifecycle_record(const char *phase,LONG event,void *record)
{
    LifecycleRecordSnapshot *slot=find_lifecycle_record(record,0);
    LONG flag=0;
    void *field_f8=NULL,*field_100=NULL;
    read_lifecycle_fields(record,&flag,&field_f8,&field_100);
    if(!slot) {
        append_log("STAGE_N_RECORD event=%ld phase=%s record=%p tracked=0 currentFlag=%ld currentF8=%p current100=%p outerRenderer=%ld faces=%ld",
            (long)event,phase,record,(long)flag,field_f8,field_100,
            (long)InterlockedCompareExchange(&g_active_renderer_call,0,0),
            (long)InterlockedCompareExchange(&g_active_renderer_faces,0,0));
        return;
    }
    append_log("STAGE_N_RECORD event=%ld phase=%s record=%p tracked=1 generation=%ld constructorCall=%ld currentFlag=%ld currentF8=%p current100=%p outerRenderer=%ld entries=%ld faces=%ld highestMap=%ld",
        (long)event,phase,record,
        (long)InterlockedCompareExchange(&slot->generation,0,0),
        (long)InterlockedCompareExchange(&slot->constructor_call,0,0),
        (long)flag,field_f8,field_100,
        (long)InterlockedCompareExchange(&g_active_renderer_call,0,0),
        (long)InterlockedCompareExchange(&g_active_renderer_entries,0,0),
        (long)InterlockedCompareExchange(&g_active_renderer_faces,0,0),
        (long)InterlockedCompareExchange(&g_active_renderer_highest_map,0,0));
    append_log("STAGE_N_LINEAGE event=%ld record=%p builderCall=%ld builderThread=%ld enterFlag=%ld enterF8=%p enter100=%p exitFlag=%ld exitF8=%p exit100=%p lookupF8Call=%ld lookupF8Thread=%ld lookupF8Result=%p lookup100Call=%ld lookup100Thread=%ld lookup100Result=%p renderCall=%ld renderThread=%ld renderFlag=%ld renderF8=%p render100=%p releaseCall=%ld releaseThread=%ld releaseFlag=%ld releaseF8=%p release100=%p",
        (long)event,record,
        (long)InterlockedCompareExchange(&slot->builder_call,0,0),
        (long)InterlockedCompareExchange(&slot->builder_thread,0,0),
        (long)InterlockedCompareExchange(&slot->builder_enter_flag,0,0),
        (void *)slot->builder_enter_f8,(void *)slot->builder_enter_100,
        (long)InterlockedCompareExchange(&slot->builder_exit_flag,0,0),
        (void *)slot->builder_exit_f8,(void *)slot->builder_exit_100,
        (long)InterlockedCompareExchange(&slot->lookup_f8_call,0,0),
        (long)InterlockedCompareExchange(&slot->lookup_f8_thread,0,0),
        (void *)slot->lookup_f8_result,
        (long)InterlockedCompareExchange(&slot->lookup_100_call,0,0),
        (long)InterlockedCompareExchange(&slot->lookup_100_thread,0,0),
        (void *)slot->lookup_100_result,
        (long)InterlockedCompareExchange(&slot->renderer_call,0,0),
        (long)InterlockedCompareExchange(&slot->renderer_thread,0,0),
        (long)InterlockedCompareExchange(&slot->renderer_flag,0,0),
        (void *)slot->renderer_f8,(void *)slot->renderer_100,
        (long)InterlockedCompareExchange(&slot->release_call,0,0),
        (long)InterlockedCompareExchange(&slot->release_thread,0,0),
        (long)InterlockedCompareExchange(&slot->release_flag,0,0),
        (void *)slot->release_f8,(void *)slot->release_100);
}

static void reset_lifecycle_cycle_fields(LifecycleRecordSnapshot *slot)
{
    InterlockedExchange(&slot->builder_exit_flag,0);
    InterlockedExchangePointer((PVOID volatile *)&slot->builder_exit_f8,NULL);
    InterlockedExchangePointer((PVOID volatile *)&slot->builder_exit_100,NULL);
    InterlockedExchange(&slot->lookup_f8_call,0);
    InterlockedExchange(&slot->lookup_100_call,0);
    InterlockedExchange(&slot->lookup_f8_thread,0);
    InterlockedExchange(&slot->lookup_100_thread,0);
    InterlockedExchangePointer((PVOID volatile *)&slot->lookup_f8_result,NULL);
    InterlockedExchangePointer((PVOID volatile *)&slot->lookup_100_result,NULL);
    InterlockedExchange(&slot->renderer_call,0);
    InterlockedExchange(&slot->renderer_thread,0);
    InterlockedExchange(&slot->renderer_flag,0);
    InterlockedExchangePointer((PVOID volatile *)&slot->renderer_f8,NULL);
    InterlockedExchangePointer((PVOID volatile *)&slot->renderer_100,NULL);
    InterlockedExchange(&slot->release_call,0);
    InterlockedExchange(&slot->release_thread,0);
    InterlockedExchange(&slot->release_flag,0);
    InterlockedExchangePointer((PVOID volatile *)&slot->release_f8,NULL);
    InterlockedExchangePointer((PVOID volatile *)&slot->release_100,NULL);
    if(InterlockedCompareExchange(&slot->sidecar_acquired,0,0)) {
        InterlockedIncrement(&g_sidecar_reset_while_acquired);
    } else {
        InterlockedExchange(&slot->sidecar_generation,0);
        InterlockedExchange(&slot->sidecar_count,0);
        InterlockedExchangePointer(
            (PVOID volatile *)&slot->sidecar_resource_0,NULL);
        InterlockedExchangePointer(
            (PVOID volatile *)&slot->sidecar_resource_1,NULL);
        InterlockedExchange(&slot->sidecar_acquire_mode,0);
        InterlockedExchange(&slot->sidecar_release_mode,0);
    }
}

static void reset_lifecycle_record(LifecycleRecordSnapshot *slot,LONG call)
{
    InterlockedIncrement(&slot->generation);
    InterlockedExchange(&slot->constructor_call,call);
    InterlockedExchange(&slot->builder_call,0);
    InterlockedExchange(&slot->builder_thread,0);
    InterlockedExchange(&slot->builder_enter_flag,0);
    InterlockedExchangePointer((PVOID volatile *)&slot->builder_enter_f8,NULL);
    InterlockedExchangePointer((PVOID volatile *)&slot->builder_enter_100,NULL);
    reset_lifecycle_cycle_fields(slot);
}

static void *__fastcall hooked_render_record_constructor(void *record)
{
    void *result=g_original_render_record_constructor(record);
    LONG call=atomic_add_long(&g_lifecycle_constructor_calls,1)+1;
    LifecycleRecordSnapshot *slot=find_lifecycle_record(record,1);
    if(slot) reset_lifecycle_record(slot,call);
    if(call<=64 || (call&0xFF)==0)
        log_lifecycle_record("constructor_exit",call,record);
    return result;
}

static void __fastcall hooked_frame_builder(void *argument1,void *argument2,
    void *record,uint64_t argument4,uint64_t argument5,uint64_t argument6,
    uint64_t argument7)
{
    LONG call=atomic_add_long(&g_lifecycle_builder_calls,1)+1;
    LONG flag=0;
    void *field_f8=NULL,*field_100=NULL;
    DWORD thread_id=GetCurrentThreadId();
    LifecycleRecordSnapshot *slot=find_lifecycle_record(record,1);
    LifecycleThreadContext *thread=find_lifecycle_thread(1);
    read_lifecycle_fields(record,&flag,&field_f8,&field_100);
    if(slot) {
        reset_lifecycle_cycle_fields(slot);
        InterlockedExchange(&slot->builder_call,call);
        InterlockedExchange(&slot->builder_thread,(LONG)thread_id);
        InterlockedExchange(&slot->builder_enter_flag,flag);
        InterlockedExchangePointer((PVOID volatile *)&slot->builder_enter_f8,
            field_f8);
        InterlockedExchangePointer((PVOID volatile *)&slot->builder_enter_100,
            field_100);
    }
    if(thread) {
        InterlockedExchangePointer((PVOID volatile *)&thread->record,record);
        InterlockedExchange(&thread->builder_call,call);
        InterlockedExchange(&thread->active,1);
    }
    g_original_frame_builder(argument1,argument2,record,argument4,argument5,
        argument6,argument7);
    read_lifecycle_fields(record,&flag,&field_f8,&field_100);
    if(slot) {
        InterlockedExchange(&slot->builder_exit_flag,flag);
        InterlockedExchangePointer((PVOID volatile *)&slot->builder_exit_f8,
            field_f8);
        InterlockedExchangePointer((PVOID volatile *)&slot->builder_exit_100,
            field_100);
    }
    if(thread) InterlockedExchange(&thread->active,0);
    if(flag && (!field_f8 || !field_100)) {
        LONG event=atomic_add_long(&g_lifecycle_builder_null_events,1)+1;
        InterlockedExchangePointer((PVOID volatile *)&g_last_lifecycle_null_record,
            record);
        if(should_emit_lifecycle_event(event))
            log_lifecycle_record("builder_exit_null",event,record);
    }
}

static void *__fastcall hooked_shared_texture_lookup(void *manager,uint32_t key,
    void *descriptor)
{
    void *caller=__builtin_return_address(0);
    void *result=g_original_shared_texture_lookup(manager,key,descriptor);
    uint64_t caller_rva=~(uint64_t)0;
    int field=0;
    LifecycleThreadContext *thread;
    LifecycleRecordSnapshot *slot;
    void *record=NULL;
    LONG call;
    uintptr_t caller_value=(uintptr_t)caller;
    uintptr_t base_value=(uintptr_t)g_disrupt_base;
    if(caller_value>=base_value && caller_value<base_value+0x10000000ULL)
        caller_rva=(uint64_t)(caller_value-base_value);
    if(caller_rva==FIELD_F8_LOOKUP_RETURN_RVA) field=0xF8;
    else if(caller_rva==FIELD_100_LOOKUP_RETURN_RVA) field=0x100;
    if(!field) return result;
    call=atomic_add_long(&g_lifecycle_lookup_calls,1)+1;
    thread=find_lifecycle_thread(0);
    if(thread && InterlockedCompareExchange(&thread->active,0,0))
        record=(void *)thread->record;
    slot=find_lifecycle_record(record,record!=NULL);
    if(field==0xF8) {
        InterlockedIncrement(&g_lifecycle_lookup_f8_calls);
        if(slot) {
            InterlockedExchange(&slot->lookup_f8_call,call);
            InterlockedExchange(&slot->lookup_f8_thread,
                (LONG)GetCurrentThreadId());
            InterlockedExchangePointer(
                (PVOID volatile *)&slot->lookup_f8_result,result);
        }
    } else {
        InterlockedIncrement(&g_lifecycle_lookup_100_calls);
        if(slot) {
            InterlockedExchange(&slot->lookup_100_call,call);
            InterlockedExchange(&slot->lookup_100_thread,
                (LONG)GetCurrentThreadId());
            InterlockedExchangePointer(
                (PVOID volatile *)&slot->lookup_100_result,result);
        }
    }
    if(!result || call<=(LONG)LIFECYCLE_LOOKUP_LOG_INITIAL ||
       (call%(LONG)LIFECYCLE_LOOKUP_LOG_PERIOD)==0)
        append_log("STAGE_N_LOOKUP call=%ld field=0x%X caller=%p callerRva=0x%08X%08X thread=%lu builderActive=%u record=%p manager=%p key=0x%08X descriptor=%p result=%p",
            (long)call,field,caller,(uint32_t)(caller_rva>>32),
            (uint32_t)caller_rva,(unsigned long)GetCurrentThreadId(),
            thread && InterlockedCompareExchange(&thread->active,0,0) ? 1U:0U,
            record,manager,key,descriptor,result);
    return result;
}

static uint32_t add_unique_sidecar_resource(void **resources,uint32_t count,
    void *resource)
{
    uint32_t index;
    if(!resource) return count;
    for(index=0;index<count;++index) if(resources[index]==resource) return count;
    if(count<2U) resources[count++]=resource;
    return count;
}

static void __fastcall hooked_render_record_acquire(void *argument1,
    void *argument2,void *record)
{
    LONG call=atomic_add_long(&g_lifecycle_renderer_calls,1)+1;
    LONG flag=0,generation_before=0,generation_after=0;
    LONG builder_call=0,builder_exit_flag=0,release_call=0;
    LONG lookup_f8_call=0,lookup_100_call=0;
    void *field_f8=NULL,*field_100=NULL,*saved_f8=NULL,*saved_100=NULL;
    void *lookup_f8=NULL,*lookup_100=NULL;
    void *final_f8=NULL,*final_100=NULL;
    void *sidecar[2]={NULL,NULL};
    uint32_t sidecar_count=0,sidecar_mode=0;
    int common_lineage=0,f8_verified=0,field_100_verified=0;
    int mismatch_f8=0,mismatch_100=0,restored_f8=0,restored_100=0;
    int rolled_back_f8=0,rolled_back_100=0;
    int rejected=0,sidecar_submitted=0;
    LifecycleRecordSnapshot *slot=find_lifecycle_record(record,1);
    read_lifecycle_fields(record,&flag,&field_f8,&field_100);
    if(slot) {
        InterlockedExchange(&slot->renderer_call,call);
        InterlockedExchange(&slot->renderer_thread,(LONG)GetCurrentThreadId());
        InterlockedExchange(&slot->renderer_flag,flag);
        InterlockedExchangePointer((PVOID volatile *)&slot->renderer_f8,
            field_f8);
        InterlockedExchangePointer((PVOID volatile *)&slot->renderer_100,
            field_100);
    }
    if(flag && (!field_f8 || !field_100)) {
        LONG null_event=atomic_add_long(&g_lifecycle_renderer_null_events,1)+1;
        InterlockedExchangePointer((PVOID volatile *)&g_last_lifecycle_null_record,
            record);
        if(should_emit_lifecycle_event(null_event))
            log_lifecycle_record("renderer_enter_null",null_event,record);
    }
    if(flag && slot) {
        generation_before=InterlockedCompareExchange(&slot->generation,0,0);
        builder_call=InterlockedCompareExchange(&slot->builder_call,0,0);
        builder_exit_flag=InterlockedCompareExchange(
            &slot->builder_exit_flag,0,0);
        release_call=InterlockedCompareExchange(&slot->release_call,0,0);
        lookup_f8_call=InterlockedCompareExchange(&slot->lookup_f8_call,0,0);
        lookup_100_call=InterlockedCompareExchange(&slot->lookup_100_call,0,0);
        saved_f8=(void *)slot->builder_exit_f8;
        saved_100=(void *)slot->builder_exit_100;
        lookup_f8=(void *)slot->lookup_f8_result;
        lookup_100=(void *)slot->lookup_100_result;
        generation_after=InterlockedCompareExchange(&slot->generation,0,0);
        common_lineage=generation_before==generation_after &&
            builder_call==call && builder_exit_flag==1 && release_call==0;
        f8_verified=common_lineage && lookup_f8_call>0 && saved_f8 &&
            lookup_f8==saved_f8;
        field_100_verified=common_lineage && lookup_100_call>0 && saved_100 &&
            lookup_100==saved_100;
        mismatch_f8=f8_verified && field_f8!=saved_f8;
        mismatch_100=field_100_verified && field_100!=saved_100;
    }
    if(flag && ((!field_f8 && !f8_verified) ||
       (!field_100 && !field_100_verified))) rejected=1;
    if(mismatch_f8 || mismatch_100 || rejected) {
        LONG event=atomic_add_long(&g_pre_acquire_repair_events,1)+1;
        if(mismatch_f8 && field_f8) {
            sidecar_count=add_unique_sidecar_resource(sidecar,sidecar_count,
                field_f8);
            InterlockedIncrement(&g_pre_acquire_nonnull_f8);
        }
        if(mismatch_100 && field_100) {
            sidecar_count=add_unique_sidecar_resource(sidecar,sidecar_count,
                field_100);
            InterlockedIncrement(&g_pre_acquire_nonnull_100);
        }
        if(InterlockedCompareExchange(&slot->sidecar_acquired,0,0)!=0 ||
           InterlockedCompareExchange(&slot->sidecar_count,0,0)!=0)
            rejected=1;
        if(sidecar_count && (!get_renderer_resource_context(argument1) ||
           !g_original_resource_wrapper)) {
            rejected=1;
            InterlockedIncrement(&g_sidecar_context_failures);
        }
        if(common_lineage &&
           (InterlockedCompareExchange(&slot->generation,0,0)!=generation_after ||
            InterlockedCompareExchange(&slot->builder_call,0,0)!=call ||
            InterlockedCompareExchange(&slot->release_call,0,0)!=0)) {
            common_lineage=0;
            f8_verified=0;
            field_100_verified=0;
            rejected=1;
        }
        if(!rejected && mismatch_f8) {
            void *prior=InterlockedCompareExchangePointer(
                (PVOID volatile *)((unsigned char *)record+0xF8),saved_f8,
                field_f8);
            restored_f8=prior==field_f8 || prior==saved_f8;
        }
        if(!rejected && mismatch_100) {
            void *prior=InterlockedCompareExchangePointer(
                (PVOID volatile *)((unsigned char *)record+0x100),saved_100,
                field_100);
            restored_100=prior==field_100 || prior==saved_100;
        }
        read_lifecycle_fields(record,NULL,&final_f8,&final_100);
        if(mismatch_f8 && (!restored_f8 || final_f8!=saved_f8)) {
            rejected=1;
            InterlockedIncrement(&g_pre_acquire_repair_postwrite_failure);
        }
        if(mismatch_100 && (!restored_100 || final_100!=saved_100)) {
            rejected=1;
            InterlockedIncrement(&g_pre_acquire_repair_postwrite_failure);
        }
        if(!rejected && sidecar_count) {
            InterlockedExchange(&slot->sidecar_generation,generation_after);
            InterlockedExchangePointer(
                (PVOID volatile *)&slot->sidecar_resource_0,sidecar[0]);
            InterlockedExchangePointer(
                (PVOID volatile *)&slot->sidecar_resource_1,sidecar[1]);
            InterlockedExchange(&slot->sidecar_count,(LONG)sidecar_count);
            sidecar_mode=submit_sidecar_resources(1,argument1,sidecar,
                sidecar_count,&sidecar_submitted);
            if(sidecar_submitted) {
                InterlockedExchange(&slot->sidecar_acquire_mode,
                    (LONG)sidecar_mode);
                InterlockedExchange(&slot->sidecar_acquired,1);
                InterlockedIncrement(&g_sidecar_acquire_events);
                atomic_add_long(&g_sidecar_acquire_resources,
                    (LONG)sidecar_count);
            } else {
                rejected=1;
                InterlockedIncrement(&g_sidecar_context_failures);
            }
        }
        if(rejected && !sidecar_submitted) {
            if(mismatch_f8 && restored_f8) {
                void *prior=InterlockedCompareExchangePointer(
                    (PVOID volatile *)((unsigned char *)record+0xF8),field_f8,
                    saved_f8);
                rolled_back_f8=prior==saved_f8 || prior==field_f8;
            }
            if(mismatch_100 && restored_100) {
                void *prior=InterlockedCompareExchangePointer(
                    (PVOID volatile *)((unsigned char *)record+0x100),field_100,
                    saved_100);
                rolled_back_100=prior==saved_100 || prior==field_100;
            }
            if(InterlockedCompareExchange(&slot->sidecar_acquired,0,0)==0) {
                InterlockedExchange(&slot->sidecar_count,0);
                InterlockedExchange(&slot->sidecar_generation,0);
                InterlockedExchangePointer(
                    (PVOID volatile *)&slot->sidecar_resource_0,NULL);
                InterlockedExchangePointer(
                    (PVOID volatile *)&slot->sidecar_resource_1,NULL);
            }
            read_lifecycle_fields(record,NULL,&final_f8,&final_100);
        }
        if(!rejected && mismatch_f8)
            InterlockedIncrement(&g_pre_acquire_repair_f8);
        if(!rejected && mismatch_100)
            InterlockedIncrement(&g_pre_acquire_repair_100);
        if(rejected) {
            InterlockedIncrement(&g_pre_acquire_repair_rejected);
            if(!common_lineage)
                InterlockedIncrement(&g_pre_acquire_repair_stale);
            else
                InterlockedIncrement(&g_pre_acquire_repair_unverified);
        }
        InterlockedExchangePointer((PVOID volatile *)&g_last_lifecycle_null_record,
            record);
        append_log("STAGE_P_PRE_ACQUIRE_REPAIR event=%ld record=%p rendererCall=%ld generationBefore=%ld generationAfter=%ld builderCall=%ld builderExitFlag=%ld releaseCall=%ld rawF8=%p raw100=%p savedF8=%p saved100=%p lookupF8=%p lookup100=%p commonLineage=%u mismatchF8=%u mismatch100=%u restoredF8=%u restored100=%u rolledBackF8=%u rolledBack100=%u sidecarCount=%u sidecar0=%p sidecar1=%p sidecarSubmitted=%u sidecarMode=%u rejected=%u finalF8=%p final100=%p",
            (long)event,record,(long)call,(long)generation_before,
            (long)generation_after,(long)builder_call,(long)builder_exit_flag,
            (long)release_call,field_f8,field_100,saved_f8,saved_100,
            lookup_f8,lookup_100,common_lineage ? 1U:0U,
            mismatch_f8 ? 1U:0U,mismatch_100 ? 1U:0U,
            restored_f8 ? 1U:0U,restored_100 ? 1U:0U,
            rolled_back_f8 ? 1U:0U,rolled_back_100 ? 1U:0U,sidecar_count,
            sidecar[0],sidecar[1],sidecar_submitted ? 1U:0U,sidecar_mode,
            rejected ? 1U:0U,final_f8,final_100);
    }
    g_original_render_record_acquire(argument1,argument2,record);
}

static void __fastcall hooked_render_record_release(void *argument1,
    void *record,unsigned char argument3)
{
    LONG call=atomic_add_long(&g_lifecycle_release_calls,1)+1;
    LONG flag=0,generation=0,sidecar_generation=0,sidecar_count=0;
    LONG builder_call=0,sidecar_acquired=0;
    void *field_f8=NULL,*field_100=NULL,*sidecar[2]={NULL,NULL};
    uint32_t release_mode=0;
    int eligible=0,submitted=0;
    LifecycleRecordSnapshot *slot=find_lifecycle_record(record,1);
    read_lifecycle_fields(record,&flag,&field_f8,&field_100);
    if(slot) {
        generation=InterlockedCompareExchange(&slot->generation,0,0);
        builder_call=InterlockedCompareExchange(&slot->builder_call,0,0);
        sidecar_generation=InterlockedCompareExchange(
            &slot->sidecar_generation,0,0);
        sidecar_count=InterlockedCompareExchange(&slot->sidecar_count,0,0);
        sidecar_acquired=InterlockedCompareExchange(&slot->sidecar_acquired,0,0);
        sidecar[0]=(void *)slot->sidecar_resource_0;
        sidecar[1]=(void *)slot->sidecar_resource_1;
        eligible=sidecar_acquired==1 && sidecar_count>0 && sidecar_count<=2 &&
            sidecar_generation==generation && builder_call==call &&
            sidecar[0]!=NULL;
        InterlockedExchange(&slot->release_call,call);
        InterlockedExchange(&slot->release_thread,(LONG)GetCurrentThreadId());
        InterlockedExchange(&slot->release_flag,flag);
        InterlockedExchangePointer((PVOID volatile *)&slot->release_f8,
            field_f8);
        InterlockedExchangePointer((PVOID volatile *)&slot->release_100,
            field_100);
    }
    if(flag && (!field_f8 || !field_100)) {
        LONG event=atomic_add_long(&g_lifecycle_release_null_events,1)+1;
        InterlockedExchangePointer((PVOID volatile *)&g_last_lifecycle_null_record,
            record);
        if(should_emit_lifecycle_event(event))
            log_lifecycle_record("release_enter_null",event,record);
    }
    g_original_render_record_release(argument1,record,argument3);
    if(eligible) {
        release_mode=submit_sidecar_resources(0,argument1,sidecar,
            (uint32_t)sidecar_count,&submitted);
        if(submitted) {
            InterlockedExchange(&slot->sidecar_release_mode,(LONG)release_mode);
            InterlockedExchange(&slot->sidecar_acquired,0);
            InterlockedIncrement(&g_sidecar_release_events);
            atomic_add_long(&g_sidecar_release_resources,sidecar_count);
            append_log("STAGE_P_SIDECAR_RELEASE record=%p releaseCall=%ld generation=%ld builderCall=%ld sidecarCount=%ld sidecar0=%p sidecar1=%p acquireMode=%ld releaseMode=%u submitted=1",
                record,(long)call,(long)generation,(long)builder_call,
                (long)sidecar_count,sidecar[0],sidecar[1],
                (long)slot->sidecar_acquire_mode,release_mode);
            InterlockedExchange(&slot->sidecar_count,0);
            InterlockedExchange(&slot->sidecar_generation,0);
            InterlockedExchangePointer(
                (PVOID volatile *)&slot->sidecar_resource_0,NULL);
            InterlockedExchangePointer(
                (PVOID volatile *)&slot->sidecar_resource_1,NULL);
        } else {
            InterlockedIncrement(&g_sidecar_context_failures);
            InterlockedIncrement(&g_sidecar_symmetry_failures);
            append_log("STAGE_P_SIDECAR_RELEASE record=%p releaseCall=%ld generation=%ld builderCall=%ld sidecarCount=%ld sidecar0=%p sidecar1=%p submitted=0 reason=context",
                record,(long)call,(long)generation,(long)builder_call,
                (long)sidecar_count,sidecar[0],sidecar[1]);
        }
    } else if(sidecar_acquired || sidecar_count) {
        InterlockedIncrement(&g_sidecar_symmetry_failures);
        append_log("STAGE_P_SIDECAR_RELEASE record=%p releaseCall=%ld generation=%ld sidecarGeneration=%ld builderCall=%ld sidecarCount=%ld sidecarAcquired=%ld submitted=0 reason=lineage",
            record,(long)call,(long)generation,(long)sidecar_generation,
            (long)builder_call,(long)sidecar_count,(long)sidecar_acquired);
    }
}

static void log_manual_lifecycle_snapshot(const char *state,LONG serial)
{
    void *record=(void *)g_last_lifecycle_null_record;
    append_log("STAGE_P_MANUAL state=%s serial=%ld repairEvents=%ld repairedF8=%ld repaired100=%ld nonnullF8=%ld nonnull100=%ld repairRejected=%ld repairStale=%ld repairUnverified=%ld repairPostwriteFailure=%ld sidecarAcquireEvents=%ld sidecarAcquireResources=%ld sidecarReleaseEvents=%ld sidecarReleaseResources=%ld sidecarContextFailures=%ld sidecarSymmetryFailures=%ld sidecarResetWhileAcquired=%ld",
        state,(long)serial,(long)g_pre_acquire_repair_events,
        (long)g_pre_acquire_repair_f8,(long)g_pre_acquire_repair_100,
        (long)g_pre_acquire_nonnull_f8,(long)g_pre_acquire_nonnull_100,
        (long)g_pre_acquire_repair_rejected,
        (long)g_pre_acquire_repair_stale,
        (long)g_pre_acquire_repair_unverified,
        (long)g_pre_acquire_repair_postwrite_failure,
        (long)g_sidecar_acquire_events,(long)g_sidecar_acquire_resources,
        (long)g_sidecar_release_events,(long)g_sidecar_release_resources,
        (long)g_sidecar_context_failures,(long)g_sidecar_symmetry_failures,
        (long)g_sidecar_reset_while_acquired);
    if(!record) {
        append_log("STAGE_N_MANUAL state=%s serial=%ld available=0 constructorCalls=%ld builderCalls=%ld lookupCalls=%ld lookupF8Calls=%ld lookup100Calls=%ld renderCalls=%ld releaseCalls=%ld builderNullEvents=%ld rendererNullEvents=%ld releaseNullEvents=%ld recordSlotMisses=%ld threadSlotMisses=%ld repairEvents=%ld repairedF8=%ld repaired100=%ld repairRejected=%ld repairStale=%ld repairUnverified=%ld repairPostwriteFailure=%ld",
            state,(long)serial,(long)g_lifecycle_constructor_calls,
            (long)g_lifecycle_builder_calls,(long)g_lifecycle_lookup_calls,
            (long)g_lifecycle_lookup_f8_calls,
            (long)g_lifecycle_lookup_100_calls,(long)g_lifecycle_renderer_calls,
            (long)g_lifecycle_release_calls,
            (long)g_lifecycle_builder_null_events,
            (long)g_lifecycle_renderer_null_events,
            (long)g_lifecycle_release_null_events,
            (long)g_lifecycle_record_slot_misses,
            (long)g_lifecycle_thread_slot_misses,
            (long)g_pre_acquire_repair_events,
            (long)g_pre_acquire_repair_f8,
            (long)g_pre_acquire_repair_100,
            (long)g_pre_acquire_repair_rejected,
            (long)g_pre_acquire_repair_stale,
            (long)g_pre_acquire_repair_unverified,
            (long)g_pre_acquire_repair_postwrite_failure);
        return;
    }
    append_log("STAGE_N_MANUAL state=%s serial=%ld available=1 lastNullRecord=%p constructorCalls=%ld builderCalls=%ld lookupCalls=%ld lookupF8Calls=%ld lookup100Calls=%ld renderCalls=%ld releaseCalls=%ld builderNullEvents=%ld rendererNullEvents=%ld releaseNullEvents=%ld recordSlotMisses=%ld threadSlotMisses=%ld repairEvents=%ld repairedF8=%ld repaired100=%ld repairRejected=%ld repairStale=%ld repairUnverified=%ld repairPostwriteFailure=%ld",
        state,(long)serial,record,(long)g_lifecycle_constructor_calls,
        (long)g_lifecycle_builder_calls,(long)g_lifecycle_lookup_calls,
        (long)g_lifecycle_lookup_f8_calls,
        (long)g_lifecycle_lookup_100_calls,(long)g_lifecycle_renderer_calls,
        (long)g_lifecycle_release_calls,
        (long)g_lifecycle_builder_null_events,
        (long)g_lifecycle_renderer_null_events,
        (long)g_lifecycle_release_null_events,
        (long)g_lifecycle_record_slot_misses,
        (long)g_lifecycle_thread_slot_misses,
        (long)g_pre_acquire_repair_events,
        (long)g_pre_acquire_repair_f8,
        (long)g_pre_acquire_repair_100,
        (long)g_pre_acquire_repair_rejected,
        (long)g_pre_acquire_repair_stale,
        (long)g_pre_acquire_repair_unverified,
        (long)g_pre_acquire_repair_postwrite_failure);
    log_lifecycle_record(state,serial,record);
}
