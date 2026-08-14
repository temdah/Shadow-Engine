/* Module 40: bounded F8/100/110 render-record provenance plus a behavior-neutral
 * mid-builder checkpoint and the retained v0.8.9 F8/100 repair with symmetric
 * displaced-resource sidecars. */
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
    void **field_100,void **field_110)
{
    unsigned char *bytes=(unsigned char *)record;
    if(flag) *flag=0;
    if(field_f8) *field_f8=NULL;
    if(field_100) *field_100=NULL;
    if(field_110) *field_110=NULL;
    if(!record || !readable_memory(bytes+0x110,sizeof(void *))) return 0;
    if(flag) *flag=bytes[0x11] ? 1:0;
    if(field_f8) *field_f8=*(void **)(bytes+0xF8);
    if(field_100) *field_100=*(void **)(bytes+0x100);
    if(field_110) *field_110=*(void **)(bytes+0x110);
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

static int read_lifecycle_neighbors(void *record,void **field_f8,
    void **field_100,void **field_108,void **field_110,void **field_118)
{
    unsigned char *bytes=(unsigned char *)record;
    if(field_f8) *field_f8=NULL;
    if(field_100) *field_100=NULL;
    if(field_108) *field_108=NULL;
    if(field_110) *field_110=NULL;
    if(field_118) *field_118=NULL;
    if(!record || !readable_memory(bytes+0xF8,0x28U)) return 0;
    if(field_f8) *field_f8=*(void **)(bytes+0xF8);
    if(field_100) *field_100=*(void **)(bytes+0x100);
    if(field_108) *field_108=*(void **)(bytes+0x108);
    if(field_110) *field_110=*(void **)(bytes+0x110);
    if(field_118) *field_118=*(void **)(bytes+0x118);
    return 1;
}

static int should_emit_success_event(LONG event)
{
    return event>0 && ((uint32_t)event<=SUCCESS_TRACE_INITIAL ||
        ((uint32_t)event%SUCCESS_TRACE_PERIOD)==0U);
}

static void log_lifecycle_record(const char *phase,LONG event,void *record)
{
    LifecycleRecordSnapshot *slot=find_lifecycle_record(record,0);
    LONG flag=0;
    void *field_f8=NULL,*field_100=NULL,*field_110=NULL;
    read_lifecycle_fields(record,&flag,&field_f8,&field_100,&field_110);
    if(!slot) {
        append_log("STAGE_N_RECORD event=%ld phase=%s record=%p tracked=0 currentFlag=%ld currentF8=%p current100=%p current110=%p outerRenderer=%ld faces=%ld",
            (long)event,phase,record,(long)flag,field_f8,field_100,field_110,
            (long)InterlockedCompareExchange(&g_active_renderer_call,0,0),
            (long)InterlockedCompareExchange(&g_active_renderer_faces,0,0));
        return;
    }
    append_log("STAGE_N_RECORD event=%ld phase=%s record=%p tracked=1 generation=%ld constructorCall=%ld currentFlag=%ld currentF8=%p current100=%p current110=%p outerRenderer=%ld entries=%ld faces=%ld highestMap=%ld",
        (long)event,phase,record,
        (long)InterlockedCompareExchange(&slot->generation,0,0),
        (long)InterlockedCompareExchange(&slot->constructor_call,0,0),
        (long)flag,field_f8,field_100,field_110,
        (long)InterlockedCompareExchange(&g_active_renderer_call,0,0),
        (long)InterlockedCompareExchange(&g_active_renderer_entries,0,0),
        (long)InterlockedCompareExchange(&g_active_renderer_faces,0,0),
        (long)InterlockedCompareExchange(&g_active_renderer_highest_map,0,0));
    append_log("STAGE_N_LINEAGE event=%ld record=%p builderCall=%ld builderThread=%ld enterFlag=%ld enterF8=%p enter100=%p enter110=%p exitFlag=%ld exitF8=%p exit100=%p exit108=%p exit110=%p exit118=%p lookupF8Call=%ld lookupF8Thread=%ld lookupF8Result=%p lookup100Call=%ld lookup100Thread=%ld lookup100Result=%p lookup110Call=%ld lookup110Thread=%ld lookup110Result=%p checkpoint110Valid=%ld checkpoint110Call=%ld checkpointF8=%p checkpoint100=%p checkpoint108=%p checkpoint110=%p checkpoint118=%p renderCall=%ld renderThread=%ld renderFlag=%ld renderF8=%p render100=%p render110=%p releaseCall=%ld releaseThread=%ld releaseFlag=%ld releaseF8=%p release100=%p release110=%p",
        (long)event,record,
        (long)InterlockedCompareExchange(&slot->builder_call,0,0),
        (long)InterlockedCompareExchange(&slot->builder_thread,0,0),
        (long)InterlockedCompareExchange(&slot->builder_enter_flag,0,0),
        (void *)slot->builder_enter_f8,(void *)slot->builder_enter_100,
        (void *)slot->builder_enter_110,
        (long)InterlockedCompareExchange(&slot->builder_exit_flag,0,0),
        (void *)slot->builder_exit_f8,(void *)slot->builder_exit_100,
        (void *)slot->builder_exit_108,(void *)slot->builder_exit_110,
        (void *)slot->builder_exit_118,
        (long)InterlockedCompareExchange(&slot->lookup_f8_call,0,0),
        (long)InterlockedCompareExchange(&slot->lookup_f8_thread,0,0),
        (void *)slot->lookup_f8_result,
        (long)InterlockedCompareExchange(&slot->lookup_100_call,0,0),
        (long)InterlockedCompareExchange(&slot->lookup_100_thread,0,0),
        (void *)slot->lookup_100_result,
        (long)InterlockedCompareExchange(&slot->lookup_110_call,0,0),
        (long)InterlockedCompareExchange(&slot->lookup_110_thread,0,0),
        (void *)slot->lookup_110_result,
        (long)InterlockedCompareExchange(&slot->checkpoint_110_valid,0,0),
        (long)InterlockedCompareExchange(&slot->checkpoint_110_call,0,0),
        (void *)slot->checkpoint_110_f8,(void *)slot->checkpoint_110_100,
        (void *)slot->checkpoint_110_108,(void *)slot->checkpoint_110_110,
        (void *)slot->checkpoint_110_118,
        (long)InterlockedCompareExchange(&slot->renderer_call,0,0),
        (long)InterlockedCompareExchange(&slot->renderer_thread,0,0),
        (long)InterlockedCompareExchange(&slot->renderer_flag,0,0),
        (void *)slot->renderer_f8,(void *)slot->renderer_100,
        (void *)slot->renderer_110,
        (long)InterlockedCompareExchange(&slot->release_call,0,0),
        (long)InterlockedCompareExchange(&slot->release_thread,0,0),
        (long)InterlockedCompareExchange(&slot->release_flag,0,0),
        (void *)slot->release_f8,(void *)slot->release_100,
        (void *)slot->release_110);
}

static void reset_lifecycle_cycle_fields(LifecycleRecordSnapshot *slot)
{
    InterlockedExchange(&slot->builder_exit_flag,0);
    InterlockedExchangePointer((PVOID volatile *)&slot->builder_exit_f8,NULL);
    InterlockedExchangePointer((PVOID volatile *)&slot->builder_exit_100,NULL);
    InterlockedExchangePointer((PVOID volatile *)&slot->builder_exit_108,NULL);
    InterlockedExchangePointer((PVOID volatile *)&slot->builder_exit_110,NULL);
    InterlockedExchangePointer((PVOID volatile *)&slot->builder_exit_118,NULL);
    InterlockedExchange(&slot->lookup_f8_call,0);
    InterlockedExchange(&slot->lookup_100_call,0);
    InterlockedExchange(&slot->lookup_110_call,0);
    InterlockedExchange(&slot->lookup_f8_thread,0);
    InterlockedExchange(&slot->lookup_100_thread,0);
    InterlockedExchange(&slot->lookup_110_thread,0);
    InterlockedExchangePointer((PVOID volatile *)&slot->lookup_f8_result,NULL);
    InterlockedExchangePointer((PVOID volatile *)&slot->lookup_100_result,NULL);
    InterlockedExchangePointer((PVOID volatile *)&slot->lookup_110_result,NULL);
    InterlockedExchange(&slot->checkpoint_110_valid,0);
    InterlockedExchange(&slot->checkpoint_110_call,0);
    InterlockedExchangePointer((PVOID volatile *)&slot->checkpoint_110_f8,NULL);
    InterlockedExchangePointer((PVOID volatile *)&slot->checkpoint_110_100,NULL);
    InterlockedExchangePointer((PVOID volatile *)&slot->checkpoint_110_108,NULL);
    InterlockedExchangePointer((PVOID volatile *)&slot->checkpoint_110_110,NULL);
    InterlockedExchangePointer((PVOID volatile *)&slot->checkpoint_110_118,NULL);
    InterlockedExchange(&slot->tail_checkpoint_call,0);
    InterlockedExchange(&slot->tail_checkpoint_caller_rva,0);
    InterlockedExchange(&slot->tail_divergence_reported,0);
    InterlockedExchangePointer((PVOID volatile *)&slot->tail_checkpoint_f8,NULL);
    InterlockedExchange(&slot->renderer_call,0);
    InterlockedExchange(&slot->renderer_thread,0);
    InterlockedExchange(&slot->renderer_flag,0);
    InterlockedExchangePointer((PVOID volatile *)&slot->renderer_f8,NULL);
    InterlockedExchangePointer((PVOID volatile *)&slot->renderer_100,NULL);
    InterlockedExchangePointer((PVOID volatile *)&slot->renderer_110,NULL);
    InterlockedExchange(&slot->release_call,0);
    InterlockedExchange(&slot->release_thread,0);
    InterlockedExchange(&slot->release_flag,0);
    InterlockedExchangePointer((PVOID volatile *)&slot->release_f8,NULL);
    InterlockedExchangePointer((PVOID volatile *)&slot->release_100,NULL);
    InterlockedExchangePointer((PVOID volatile *)&slot->release_110,NULL);
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
    InterlockedExchangePointer((PVOID volatile *)&slot->builder_enter_110,NULL);
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
    void *field_f8=NULL,*field_100=NULL,*field_108=NULL,*field_110=NULL;
    void *field_118=NULL,*checkpoint_f8=NULL,*lookup_f8=NULL;
    DWORD thread_id=GetCurrentThreadId();
    LifecycleRecordSnapshot *slot=find_lifecycle_record(record,1);
    LifecycleThreadContext *thread=find_lifecycle_thread(1);
    read_lifecycle_fields(record,&flag,&field_f8,&field_100,&field_110);
    if(slot) {
        reset_lifecycle_cycle_fields(slot);
        InterlockedExchange(&slot->builder_call,call);
        InterlockedExchange(&slot->builder_thread,(LONG)thread_id);
        InterlockedExchange(&slot->builder_enter_flag,flag);
        InterlockedExchangePointer((PVOID volatile *)&slot->builder_enter_f8,
            field_f8);
        InterlockedExchangePointer((PVOID volatile *)&slot->builder_enter_100,
            field_100);
        InterlockedExchangePointer((PVOID volatile *)&slot->builder_enter_110,
            field_110);
    }
    if(thread) {
        InterlockedExchangePointer((PVOID volatile *)&thread->record,record);
        InterlockedExchange(&thread->builder_call,call);
        InterlockedExchange(&thread->active,1);
    }
    g_original_frame_builder(argument1,argument2,record,argument4,argument5,
        argument6,argument7);
    read_lifecycle_fields(record,&flag,&field_f8,&field_100,&field_110);
    read_lifecycle_neighbors(record,NULL,NULL,&field_108,NULL,&field_118);
    if(slot) {
        InterlockedExchange(&slot->builder_exit_flag,flag);
        InterlockedExchangePointer((PVOID volatile *)&slot->builder_exit_f8,
            field_f8);
        InterlockedExchangePointer((PVOID volatile *)&slot->builder_exit_100,
            field_100);
        InterlockedExchangePointer((PVOID volatile *)&slot->builder_exit_108,
            field_108);
        InterlockedExchangePointer((PVOID volatile *)&slot->builder_exit_110,
            field_110);
        InterlockedExchangePointer((PVOID volatile *)&slot->builder_exit_118,
            field_118);
    }
    if(thread) InterlockedExchange(&thread->active,0);
    if(slot && InterlockedCompareExchange(&slot->checkpoint_110_valid,0,0)) {
        checkpoint_f8=(void *)slot->checkpoint_110_f8;
        lookup_f8=(void *)slot->lookup_f8_result;
        if(lookup_f8 && checkpoint_f8!=lookup_f8) {
            LONG event=atomic_add_long(&g_f8_before_110_divergence_events,1)+1;
            InterlockedExchangePointer(
                (PVOID volatile *)&g_last_lifecycle_null_record,record);
            append_log("STAGE_S_INDEXED_WRITER event=%ld field=0xF8 record=%p builderCall=%ld lookupF8=%p checkpointF8=%p checkpoint100=%p checkpoint108=%p checkpoint110=%p checkpoint118=%p exitF8=%p exit100=%p exit108=%p exit110=%p exit118=%p firstInvalidBoundary=f8_store_to_110_lookup_return",
                (long)event,record,(long)call,lookup_f8,checkpoint_f8,
                (void *)slot->checkpoint_110_100,
                (void *)slot->checkpoint_110_108,
                (void *)slot->checkpoint_110_110,
                (void *)slot->checkpoint_110_118,field_f8,field_100,
                field_108,field_110,field_118);
            log_lifecycle_record("f8_before_110_divergence",event,record);
        } else if(lookup_f8 && checkpoint_f8==lookup_f8 &&
                  field_f8!=checkpoint_f8) {
            LONG event=atomic_add_long(&g_f8_after_110_divergence_events,1)+1;
            InterlockedExchangePointer(
                (PVOID volatile *)&g_last_lifecycle_null_record,record);
            append_log("STAGE_T_BUILDER_TAIL event=%ld field=0xF8 record=%p builderCall=%ld lookupF8=%p checkpointF8=%p checkpoint100=%p checkpoint108=%p checkpoint110=%p checkpoint118=%p lastTailCheckpointCall=%ld lastTailCheckpointRva=0x%08lX lastTailCheckpointF8=%p exitF8=%p exit100=%p exit108=%p exit110=%p exit118=%p firstInvalidBoundary=last_tail_lookup_return_to_builder_exit",
                (long)event,record,(long)call,lookup_f8,checkpoint_f8,
                (void *)slot->checkpoint_110_100,
                (void *)slot->checkpoint_110_108,
                (void *)slot->checkpoint_110_110,
                (void *)slot->checkpoint_110_118,
                (long)InterlockedCompareExchange(&slot->tail_checkpoint_call,0,0),
                (unsigned long)InterlockedCompareExchange(
                    &slot->tail_checkpoint_caller_rva,0,0),
                (void *)slot->tail_checkpoint_f8,field_f8,field_100,
                field_108,field_110,field_118);
            log_lifecycle_record("f8_after_110_divergence",event,record);
        }
    }
    if(slot && InterlockedCompareExchange(&slot->lookup_110_call,0,0)>0 &&
       slot->lookup_110_result && field_110!=(void *)slot->lookup_110_result) {
        LONG event=atomic_add_long(&g_builder_110_divergence_events,1)+1;
        InterlockedExchangePointer((PVOID volatile *)&g_last_lifecycle_null_record,
            record);
        append_log("STAGE_R_PROVENANCE event=%ld phase=builder_exit field=0x110 record=%p builderCall=%ld lookupCall=%ld lookupResult=%p observed=%p firstInvalidBoundary=lookup_to_builder_exit",
            (long)event,record,(long)call,
            (long)InterlockedCompareExchange(&slot->lookup_110_call,0,0),
            (void *)slot->lookup_110_result,field_110);
        log_lifecycle_record("builder_exit_110_divergence",event,record);
    }
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
    else if(caller_rva==FIELD_110_LOOKUP_RETURN_RVA) field=0x110;
    if(!field && caller_rva>FIELD_110_LOOKUP_RETURN_RVA &&
       caller_rva<=FRAME_BUILDER_END_RVA) {
        void *current_f8=NULL;
        void *previous_f8=NULL;
        LONG previous_rva=0;
        LONG tail_call=atomic_add_long(&g_builder_tail_lookup_checkpoints,1)+1;
        thread=find_lifecycle_thread(0);
        if(thread && InterlockedCompareExchange(&thread->active,0,0))
            record=(void *)thread->record;
        slot=find_lifecycle_record(record,0);
        if(slot && InterlockedCompareExchange(&slot->checkpoint_110_valid,0,0)) {
            read_lifecycle_neighbors(record,&current_f8,NULL,NULL,NULL,NULL);
            previous_f8=(void *)slot->tail_checkpoint_f8;
            previous_rva=InterlockedCompareExchange(
                &slot->tail_checkpoint_caller_rva,0,0);
            if(slot->lookup_f8_result &&
               previous_f8==(void *)slot->lookup_f8_result &&
               current_f8!=previous_f8 &&
               InterlockedCompareExchange(
                   &slot->tail_divergence_reported,1,0)==0) {
                LONG event=atomic_add_long(
                    &g_builder_tail_lookup_divergence_events,1)+1;
                InterlockedExchangePointer(
                    (PVOID volatile *)&g_last_lifecycle_null_record,record);
                append_log("STAGE_T_BUILDER_TAIL event=%ld field=0xF8 record=%p builderCall=%ld checkpointCall=%ld previousLookupReturnRva=0x%08lX currentLookupReturnRva=0x%08X previousF8=%p observedF8=%p firstInvalidBoundary=tail_lookup_return_to_tail_lookup_return",
                    (long)event,record,
                    thread ? (long)InterlockedCompareExchange(
                        &thread->builder_call,0,0):0L,
                    (long)tail_call,(unsigned long)previous_rva,
                    (uint32_t)caller_rva,previous_f8,current_f8);
            }
            InterlockedExchange(&slot->tail_checkpoint_call,tail_call);
            InterlockedExchange(&slot->tail_checkpoint_caller_rva,
                (LONG)caller_rva);
            InterlockedExchangePointer(
                (PVOID volatile *)&slot->tail_checkpoint_f8,current_f8);
        }
        return result;
    }
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
    } else if(field==0x100) {
        InterlockedIncrement(&g_lifecycle_lookup_100_calls);
        if(slot) {
            InterlockedExchange(&slot->lookup_100_call,call);
            InterlockedExchange(&slot->lookup_100_thread,
                (LONG)GetCurrentThreadId());
            InterlockedExchangePointer(
                (PVOID volatile *)&slot->lookup_100_result,result);
        }
    } else {
        void *checkpoint_f8=NULL,*checkpoint_100=NULL,*checkpoint_108=NULL;
        void *checkpoint_110=NULL,*checkpoint_118=NULL;
        int checkpoint_valid=read_lifecycle_neighbors(record,&checkpoint_f8,
            &checkpoint_100,&checkpoint_108,&checkpoint_110,&checkpoint_118);
        InterlockedIncrement(&g_lifecycle_lookup_110_calls);
        if(slot) {
            InterlockedExchange(&slot->lookup_110_call,call);
            InterlockedExchange(&slot->lookup_110_thread,
                (LONG)GetCurrentThreadId());
            InterlockedExchangePointer(
                (PVOID volatile *)&slot->lookup_110_result,result);
            InterlockedExchange(&slot->checkpoint_110_call,call);
            InterlockedExchangePointer(
                (PVOID volatile *)&slot->checkpoint_110_f8,checkpoint_f8);
            InterlockedExchangePointer(
                (PVOID volatile *)&slot->checkpoint_110_100,checkpoint_100);
            InterlockedExchangePointer(
                (PVOID volatile *)&slot->checkpoint_110_108,checkpoint_108);
            InterlockedExchangePointer(
                (PVOID volatile *)&slot->checkpoint_110_110,checkpoint_110);
            InterlockedExchangePointer(
                (PVOID volatile *)&slot->checkpoint_110_118,checkpoint_118);
            InterlockedExchange(&slot->checkpoint_110_valid,
                checkpoint_valid ? 1:0);
            InterlockedExchange(&slot->tail_checkpoint_call,call);
            InterlockedExchange(&slot->tail_checkpoint_caller_rva,
                (LONG)caller_rva);
            InterlockedExchangePointer(
                (PVOID volatile *)&slot->tail_checkpoint_f8,checkpoint_f8);
        }
        if(!result) {
            LONG event=atomic_add_long(&g_lookup_110_null_events,1)+1;
            InterlockedExchangePointer(
                (PVOID volatile *)&g_last_lifecycle_null_record,record);
            append_log("STAGE_R_PROVENANCE event=%ld phase=lookup_return field=0x110 record=%p builderCall=%ld callerRva=0x%08X%08X manager=%p key=0x%08X descriptor=%p result=%p firstInvalidBoundary=lookup_result_null",
                (long)event,record,
                thread ? (long)InterlockedCompareExchange(
                    &thread->builder_call,0,0) : 0L,
                (uint32_t)(caller_rva>>32),(uint32_t)caller_rva,manager,key,
                descriptor,result);
            log_lifecycle_record("lookup_110_null",event,record);
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

static void maybe_log_provenance_summary(void)
{
    DWORD now=GetTickCount();
    LONG prior=InterlockedCompareExchange(&g_last_provenance_summary_tick,0,0);
    if((DWORD)(now-(DWORD)prior)<PROVENANCE_SUMMARY_PERIOD_MS) return;
    if(InterlockedCompareExchange(&g_last_provenance_summary_tick,(LONG)now,
       prior)!=prior) return;
    append_log("STAGE_T_SUMMARY intervalMs=%u constructorCalls=%ld builderCalls=%ld lookupCalls=%ld lookupF8Calls=%ld lookup100Calls=%ld lookup110Calls=%ld lookup110Null=%ld f8Before110Divergence=%ld f8After110Divergence=%ld tailLookupCheckpoints=%ld tailLookupDivergence=%ld rendererCalls=%ld releaseCalls=%ld builder110Divergence=%ld renderer110Divergence=%ld release110Divergence=%ld repairEvents=%ld repairedF8=%ld repaired100=%ld repairRejected=%ld sidecarAcquireEvents=%ld sidecarAcquireResources=%ld sidecarReleaseEvents=%ld sidecarReleaseResources=%ld sidecarContextFailures=%ld sidecarSymmetryFailures=%ld",
        PROVENANCE_SUMMARY_PERIOD_MS,(long)g_lifecycle_constructor_calls,
        (long)g_lifecycle_builder_calls,(long)g_lifecycle_lookup_calls,
        (long)g_lifecycle_lookup_f8_calls,(long)g_lifecycle_lookup_100_calls,
        (long)g_lifecycle_lookup_110_calls,(long)g_lookup_110_null_events,
        (long)g_f8_before_110_divergence_events,
        (long)g_f8_after_110_divergence_events,
        (long)g_builder_tail_lookup_checkpoints,
        (long)g_builder_tail_lookup_divergence_events,
        (long)g_lifecycle_renderer_calls,
        (long)g_lifecycle_release_calls,(long)g_builder_110_divergence_events,
        (long)g_renderer_110_divergence_events,
        (long)g_release_110_divergence_events,
        (long)g_pre_acquire_repair_events,(long)g_pre_acquire_repair_f8,
        (long)g_pre_acquire_repair_100,(long)g_pre_acquire_repair_rejected,
        (long)g_sidecar_acquire_events,(long)g_sidecar_acquire_resources,
        (long)g_sidecar_release_events,(long)g_sidecar_release_resources,
        (long)g_sidecar_context_failures,(long)g_sidecar_symmetry_failures);
}

static void __fastcall hooked_render_record_acquire(void *argument1,
    void *argument2,void *record)
{
    LONG call=atomic_add_long(&g_lifecycle_renderer_calls,1)+1;
    LONG flag=0,generation_before=0,generation_after=0;
    LONG builder_call=0,builder_exit_flag=0,release_call=0;
    LONG lookup_f8_call=0,lookup_100_call=0,lookup_110_call=0;
    void *field_f8=NULL,*field_100=NULL,*field_108=NULL,*field_110=NULL;
    void *field_118=NULL;
    void *saved_f8=NULL,*saved_100=NULL,*saved_110=NULL;
    void *lookup_f8=NULL,*lookup_100=NULL,*lookup_110=NULL;
    void *final_f8=NULL,*final_100=NULL;
    void *sidecar[2]={NULL,NULL};
    uint32_t sidecar_count=0,sidecar_mode=0;
    int common_lineage=0,f8_verified=0,field_100_verified=0;
    int mismatch_f8=0,mismatch_100=0,restored_f8=0,restored_100=0;
    int rolled_back_f8=0,rolled_back_100=0;
    int rejected=0,sidecar_submitted=0;
    LifecycleRecordSnapshot *slot=find_lifecycle_record(record,1);
    read_lifecycle_fields(record,&flag,&field_f8,&field_100,&field_110);
    read_lifecycle_neighbors(record,NULL,NULL,&field_108,NULL,&field_118);
    maybe_log_provenance_summary();
    if(slot) {
        InterlockedExchange(&slot->renderer_call,call);
        InterlockedExchange(&slot->renderer_thread,(LONG)GetCurrentThreadId());
        InterlockedExchange(&slot->renderer_flag,flag);
        InterlockedExchangePointer((PVOID volatile *)&slot->renderer_f8,
            field_f8);
        InterlockedExchangePointer((PVOID volatile *)&slot->renderer_100,
            field_100);
        InterlockedExchangePointer((PVOID volatile *)&slot->renderer_110,
            field_110);
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
        lookup_110_call=InterlockedCompareExchange(&slot->lookup_110_call,0,0);
        saved_f8=(void *)slot->builder_exit_f8;
        saved_100=(void *)slot->builder_exit_100;
        saved_110=(void *)slot->builder_exit_110;
        lookup_f8=(void *)slot->lookup_f8_result;
        lookup_100=(void *)slot->lookup_100_result;
        lookup_110=(void *)slot->lookup_110_result;
        generation_after=InterlockedCompareExchange(&slot->generation,0,0);
        common_lineage=generation_before==generation_after &&
            builder_call==call && builder_exit_flag==1 && release_call==0;
        f8_verified=common_lineage && lookup_f8_call>0 && saved_f8 &&
            lookup_f8==saved_f8;
        field_100_verified=common_lineage && lookup_100_call>0 && saved_100 &&
            lookup_100==saved_100;
        mismatch_f8=f8_verified && field_f8!=saved_f8;
        mismatch_100=field_100_verified && field_100!=saved_100;
        if(common_lineage && lookup_110_call>0 && saved_110 &&
           lookup_110==saved_110 && field_110!=saved_110) {
            LONG event=atomic_add_long(&g_renderer_110_divergence_events,1)+1;
            InterlockedExchangePointer(
                (PVOID volatile *)&g_last_lifecycle_null_record,record);
            append_log("STAGE_S_INDEXED_WRITER event=%ld phase=renderer_enter field=0x110 record=%p rendererCall=%ld generation=%ld builderCall=%ld lookupCall=%ld lookupResult=%p builderExit=%p observed=%p exitF8=%p exit100=%p exit108=%p exit118=%p observedF8=%p observed100=%p observed108=%p observed118=%p firstInvalidBoundary=builder_exit_to_renderer",
                (long)event,record,(long)call,(long)generation_after,
                (long)builder_call,(long)lookup_110_call,lookup_110,saved_110,
                field_110,(void *)slot->builder_exit_f8,
                (void *)slot->builder_exit_100,(void *)slot->builder_exit_108,
                (void *)slot->builder_exit_118,field_f8,field_100,field_108,
                field_118);
            log_lifecycle_record("renderer_enter_110_divergence",event,record);
        }
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
        read_lifecycle_fields(record,NULL,&final_f8,&final_100,NULL);
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
            read_lifecycle_fields(record,NULL,&final_f8,&final_100,NULL);
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
        if(rejected || should_emit_success_event(event))
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
    void *field_f8=NULL,*field_100=NULL,*field_110=NULL,*renderer_110=NULL;
    void *sidecar[2]={NULL,NULL};
    uint32_t release_mode=0;
    int eligible=0,submitted=0;
    LifecycleRecordSnapshot *slot=find_lifecycle_record(record,1);
    read_lifecycle_fields(record,&flag,&field_f8,&field_100,&field_110);
    if(slot) {
        generation=InterlockedCompareExchange(&slot->generation,0,0);
        builder_call=InterlockedCompareExchange(&slot->builder_call,0,0);
        sidecar_generation=InterlockedCompareExchange(
            &slot->sidecar_generation,0,0);
        sidecar_count=InterlockedCompareExchange(&slot->sidecar_count,0,0);
        sidecar_acquired=InterlockedCompareExchange(&slot->sidecar_acquired,0,0);
        sidecar[0]=(void *)slot->sidecar_resource_0;
        sidecar[1]=(void *)slot->sidecar_resource_1;
        renderer_110=(void *)slot->renderer_110;
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
        InterlockedExchangePointer((PVOID volatile *)&slot->release_110,
            field_110);
    }
    if(slot && renderer_110 && field_110!=renderer_110) {
        LONG event=atomic_add_long(&g_release_110_divergence_events,1)+1;
        InterlockedExchangePointer((PVOID volatile *)&g_last_lifecycle_null_record,
            record);
        append_log("STAGE_R_PROVENANCE event=%ld phase=release_enter field=0x110 record=%p releaseCall=%ld generation=%ld builderCall=%ld rendererValue=%p observed=%p firstInvalidBoundary=renderer_to_release",
            (long)event,record,(long)call,(long)generation,(long)builder_call,
            renderer_110,field_110);
        log_lifecycle_record("release_enter_110_divergence",event,record);
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
            LONG release_event;
            InterlockedIncrement(&g_sidecar_release_events);
            release_event=InterlockedCompareExchange(
                &g_sidecar_release_events,0,0);
            atomic_add_long(&g_sidecar_release_resources,sidecar_count);
            if(should_emit_success_event(release_event))
                append_log("STAGE_P_SIDECAR_RELEASE event=%ld record=%p releaseCall=%ld generation=%ld builderCall=%ld sidecarCount=%ld sidecar0=%p sidecar1=%p acquireMode=%ld releaseMode=%u submitted=1",
                    (long)release_event,record,(long)call,(long)generation,
                    (long)builder_call,(long)sidecar_count,sidecar[0],sidecar[1],
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
