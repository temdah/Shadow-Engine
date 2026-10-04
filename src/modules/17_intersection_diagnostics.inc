/* Module 17: opt-in, bounded copied-value provenance. No engine field writes,
 * resource retention, waiting, admission policy or worker-side engine reads. */
enum IntersectionKind {
    IX_CONSTRUCTOR=1,IX_BUILDER_ENTER,IX_LOOKUP,IX_BUILDER_EXIT,
    IX_PRODUCER,IX_PRODUCER_RETURN,IX_PRE_FINALIZER,IX_RELEASE,
    IX_QUEUE,IX_ENTRY,IX_COMPLETION,IX_MARK
};
typedef struct IntersectionBuilderScope {
    void *record,*previous;
    LONG capture,generation;
    int installed;
} IntersectionBuilderScope;

static int intersection_lock(void)
{
    IntersectionDiagnosticState *d=&g_shadow_engine.intersection;
    if(InterlockedCompareExchange(&d->lock,1,0)==0) return 1;
    InterlockedIncrement(&d->contention);
    return 0;
}
static void intersection_unlock(void)
{ InterlockedExchange(&g_shadow_engine.intersection.lock,0); }

/* Called only with the diagnostic try-lock; engine execution never waits. */
static void intersection_append(IntersectionEvent *event)
{
    IntersectionDiagnosticState *d=&g_shadow_engine.intersection;
    event->sequence=++d->sequence;
    event->tick=GetTickCount();
    event->thread=GetCurrentThreadId();
    d->events[(event->sequence-1U)%INTERSECTION_EVENTS]=*event;
    if(d->count<INTERSECTION_EVENTS) ++d->count;
    if(event->mismatch_mask) {
        if(d->anomaly_count<INTERSECTION_ANOMALIES)
            d->anomalies[d->anomaly_count++]=*event;
        else ++d->anomaly_overflow;
    }
}

static IntersectionRecord *intersection_record(void *record,LONG generation)
{
    IntersectionDiagnosticState *d=&g_shadow_engine.intersection;
    uint32_t start=(uint32_t)((uintptr_t)record>>4U)%INTERSECTION_RECORDS,i;
    for(i=0;i<INTERSECTION_RECORDS;++i) {
        IntersectionRecord *s=&d->records[(start+i)%INTERSECTION_RECORDS];
        if(s->capture!=d->capture || s->record==record) {
            if(s->capture!=d->capture || s->generation!=generation) {
                memset(s,0,sizeof(*s));
                s->record=record; s->generation=generation; s->capture=d->capture;
            }
            return s;
        }
    }
    InterlockedIncrement(&d->slot_misses);
    return NULL;
}

static void intersection_observe(uint32_t kind,void *record,uint32_t field,
    void *value,uintptr_t caller,uint32_t key)
{
    IntersectionDiagnosticState *d=&g_shadow_engine.intersection;
    LifecycleRecordSnapshot *life;
    IntersectionRecord *s;
    IntersectionEvent e;
    uint32_t i;
    static const uint32_t offsets[5]={0xF8,0x100,0x108,0x110,0x118};
    static const uint32_t expected_fields[3]={0,1,3};
    if(InterlockedCompareExchange(&d->state,0,0)!=2 || !record) return;
    if(!intersection_lock()) return;
    if(d->state!=2) { intersection_unlock(); return; }
    life=find_existing_lifecycle_record_fast(record);
    if(!life || life->generation<=0) {
        InterlockedIncrement(&d->slot_misses); intersection_unlock(); return;
    }
    memset(&e,0,sizeof(e));
    e.kind=kind; e.record=record; e.generation=life->generation;
    e.cycle=life->producer_cycle; e.builder=life->builder_call;
    e.value=value; e.caller=caller; e.data[0]=field; e.data[1]=key;
    /* Completion carries identity/value only: do not add a dereference of a
     * raw job result-base pointer to the existing publication path. */
    if(kind==IX_COMPLETION) {
        intersection_append(&e); intersection_unlock(); return;
    }
    s=intersection_record(record,e.generation);
    if(!s) { intersection_unlock(); return; }
    if(kind==IX_CONSTRUCTOR || kind==IX_BUILDER_ENTER) {
        s->expected_mask=0; s->last_valid=0;
    }
    /* These boundaries own a live native 0x148-byte record. Copy qwords only;
     * pointed-to resources are never followed. Values are not an atomic tuple. */
    for(i=0;i<5;++i) {
        e.fields[i]=*(void *volatile *)((unsigned char *)record+offsets[i]);
        if(s->last_valid && e.fields[i]!=s->last[i]) e.changed_mask|=1U<<i;
        s->last[i]=e.fields[i];
    }
    s->last_valid=1;
    e.valid_mask=s->expected_mask;
    for(i=0;i<3;++i) {
        e.expected[i]=s->expected[i];
        /* Lookup return precedes native assignment of this particular field. */
        if((s->expected_mask&(1U<<expected_fields[i])) &&
           !(kind==IX_LOOKUP && field==expected_fields[i]) &&
           e.fields[expected_fields[i]]!=s->expected[i])
            e.mismatch_mask|=1U<<expected_fields[i];
        if(kind==IX_LOOKUP && field==expected_fields[i]) {
            s->expected[i]=value;
            s->expected_mask|=1U<<field;
        }
    }
    intersection_append(&e);
    intersection_unlock();
}

static void intersection_builder_enter(IntersectionBuilderScope *scope,void *record)
{
    IntersectionDiagnosticState *d=&g_shadow_engine.intersection;
    LifecycleRecordSnapshot *life;
    memset(scope,0,sizeof(*scope));
    if(InterlockedCompareExchange(&d->state,0,0)!=2 || !d->tls_valid) return;
    life=find_existing_lifecycle_record_fast(record);
    if(!life) return;
    scope->record=record; scope->capture=d->capture; scope->generation=life->generation;
    scope->previous=TlsGetValue(d->tls_index);
    scope->installed=TlsSetValue(d->tls_index,scope)!=0;
    if(scope->installed) intersection_observe(IX_BUILDER_ENTER,record,0,NULL,0,0);
}
static void intersection_builder_exit(IntersectionBuilderScope *scope)
{
    IntersectionDiagnosticState *d=&g_shadow_engine.intersection;
    LifecycleRecordSnapshot *life;
    if(!scope->installed) return;
    life=find_existing_lifecycle_record_fast(scope->record);
    if(scope->capture==d->capture && life && life->generation==scope->generation)
        intersection_observe(IX_BUILDER_EXIT,scope->record,0,NULL,0,0);
    TlsSetValue(d->tls_index,scope->previous);
}

/* Read-only identity query for other bounded observers; binding lifetime is
 * still owned by this observer's preflight/transaction. */
static int intersection_matches_lookup_target(const void *target)
{
    return target && target==g_shadow_engine.intersection.lookup_target;
}

static void *__fastcall hooked_intersection_lookup(void *manager,uint32_t key,
    void *descriptor)
{
    uintptr_t caller=(uintptr_t)__builtin_return_address(0);
    IntersectionDiagnosticState *d=&g_shadow_engine.intersection;
    void *result=g_shadow_engine.hooks.original_intersection_lookup(manager,key,descriptor);
    IntersectionBuilderScope *scope;
    LifecycleRecordSnapshot *life;
    uint32_t i;
    static const uint32_t fields[3]={1,0,3};
    if(InterlockedCompareExchange(&d->state,0,0)!=2 || !d->tls_valid) return result;
    for(i=0;i<3;++i) if(caller==d->lookup_returns[i]) break;
    if(i==3) return result;
    scope=(IntersectionBuilderScope *)TlsGetValue(d->tls_index);
    if(!scope || scope->capture!=d->capture) return result;
    life=find_existing_lifecycle_record_fast(scope->record);
    if(!life || life->generation!=scope->generation) return result;
    intersection_observe(IX_LOOKUP,scope->record,fields[i],result,
        caller-(uintptr_t)g_shadow_engine.bootstrap.disrupt_base,key);
    return result;
}

/* Complete read-only recovery: all three mapped calls must reach one function
 * and their following native stores must target the expected record fields. */
static int validate_intersection_sites(void)
{
    IntersectionDiagnosticState *d=&g_shadow_engine.intersection;
    const RuntimeProfile *p=g_runtime_state.selected;
    static const uint64_t returns[3]={FIELD_100_LOOKUP_RETURN_RVA,
        FIELD_F8_LOOKUP_RETURN_RVA,FIELD_110_LOOKUP_RETURN_RVA};
    static const uint32_t stores[3]={12,0,4},offsets[3]={0x100,0xF8,0x110};
    unsigned char *target=NULL;
    uintptr_t recovered[3];
    uint32_t i;
    if(!p) return 0;
    for(i=0;i<3;++i) {
        unsigned char *ret=ENGINE_ADDRESS(returns[i]),*candidate;
        unsigned char expected[7]={0x49,0x89,0,0,0,0,0};
        int32_t rel;
        if(!readable_memory(ret-5,32) || ret[-5]!=0xE8) return 0;
        memcpy(&rel,ret-4,4); candidate=ret+rel;
        expected[2]=p->intersection_record_modrm;
        memcpy(expected+3,&offsets[i],4);
        if(memcmp(ret+stores[i],expected,sizeof(expected))!=0) return 0;
        if(target && target!=candidate) return 0;
        target=candidate; recovered[i]=(uintptr_t)ret;
    }
    if(!readable_memory(target,15) ||
       memcmp(target,p->intersection_lookup_prologue,15)!=0) return 0;
    d->lookup_target=target;
    memcpy(d->lookup_returns,recovered,sizeof(recovered));
    return 1;
}
static int install_intersection_observer(void)
{
    IntersectionDiagnosticState *d=&g_shadow_engine.intersection;
    if(!validate_intersection_sites()) return 0;
    d->queue_work=(IntersectionQueueWorkFn)GetProcAddress(
        GetModuleHandleW(L"kernel32.dll"),"QueueUserWorkItem");
    if(!d->queue_work) return 0;
    d->tls_index=TlsAlloc();
    if(d->tls_index==TLS_OUT_OF_INDEXES) return 0;
    d->tls_valid=1;
    return install_detour(d->lookup_target,
        g_runtime_state.selected->intersection_lookup_prologue,15,
        hooked_intersection_lookup,
        (void **)&g_shadow_engine.hooks.original_intersection_lookup);
}
static void rollback_intersection_observer(void)
{
    IntersectionDiagnosticState *d=&g_shadow_engine.intersection;
    if(d->tls_valid) TlsFree(d->tls_index);
    d->tls_valid=0;
}

static void intersection_queue(void *record,void *queue,uint32_t original,
    uint32_t admitted,uint32_t requested,uint32_t admitted_faces)
{
    IntersectionDiagnosticState *d=&g_shadow_engine.intersection;
    IntersectionEvent e;
    LifecycleRecordSnapshot *life;
    uint32_t index,face,cursor=0;
    int detailed;
    if(InterlockedCompareExchange(&d->state,0,0)!=2 || !queue) return;
    if(!intersection_lock()) return;
    if(d->state!=2) { intersection_unlock(); return; }
    memset(&e,0,sizeof(e));
    e.kind=IX_QUEUE; e.record=record; e.queue=queue;
    life=find_existing_lifecycle_record_fast(record);
    if(life) { e.generation=life->generation; e.cycle=life->producer_cycle; e.builder=life->builder_call; }
    e.data[0]=original; e.data[1]=admitted;
    e.data[2]=requested; e.data[3]=admitted_faces;
    intersection_append(&e);
    detailed=(DWORD)(e.tick-d->last_queue_sample)>=100U;
    if(detailed) d->last_queue_sample=e.tick;
    if(!detailed && original==admitted) { intersection_unlock(); return; }
    for(index=0;index<original && index<PHYSICAL_QUEUE_ENTRIES;++index) {
        unsigned char *entry=(unsigned char *)queue+(size_t)index*RENDER_QUEUE_ENTRY_BYTES;
        uint32_t count=*(uint32_t *)(entry+0x20A0);
        if(count>6U) break;
        if(detailed || index>=admitted) {
            e.kind=IX_ENTRY; e.value=entry;
            e.data[0]=index; e.data[1]=count; e.data[2]=cursor;
            e.data[3]=cursor+count; e.data[4]=index>=admitted;
            e.data[5]=cursor<TOTAL_LOCAL_MAPS ? g_shadow_engine.resources.shadow_map_ids[cursor]:0;
            memset(e.faces,0,sizeof(e.faces));
            for(face=0;face<count;++face)
                e.faces[face]=*(void **)(entry+0x7D8+(size_t)face*0x820U);
            intersection_append(&e);
        }
        cursor+=count;
    }
    intersection_unlock();
}

static int intersection_write(HANDLE file,const char *line)
{
    DWORD written,length=(DWORD)strlen(line);
    return WriteFile(file,line,length,&written,NULL) && written==length;
}
static int intersection_write_event(HANDLE file,const IntersectionEvent *e,int anomaly)
{
    char line[1024];
    int n=_snprintf(line,sizeof(line),
        "IX_EVENT anomaly=%d seq=%u tick=%lu thread=%lu kind=%u record=%p gen=%ld cycle=%ld builder=%ld valid=0x%X changed=0x%X mismatch=0x%X fields=%p,%p,%p,%p,%p expected=%p,%p,%p value=%p caller=0x%llX queue=%p data=%u,%u,%u,%u,%u,%u faces=%p,%p,%p,%p,%p,%p\r\n",
        anomaly,e->sequence,(unsigned long)e->tick,(unsigned long)e->thread,e->kind,
        e->record,(long)e->generation,(long)e->cycle,(long)e->builder,
        e->valid_mask,e->changed_mask,e->mismatch_mask,
        e->fields[0],e->fields[1],e->fields[2],e->fields[3],e->fields[4],
        e->expected[0],e->expected[1],e->expected[2],e->value,
        (unsigned long long)e->caller,e->queue,e->data[0],e->data[1],e->data[2],
        e->data[3],e->data[4],e->data[5],e->faces[0],e->faces[1],e->faces[2],
        e->faces[3],e->faces[4],e->faces[5]);
    return n>0 && (size_t)n<sizeof(line) && intersection_write(file,line);
}
static DWORD WINAPI intersection_flush(void *unused)
{
    IntersectionDiagnosticState *d=&g_shadow_engine.intersection;
    wchar_t path[MAX_PATH],name[96];
    char line[768];
    HANDLE file=INVALID_HANDLE_VALUE;
    uint32_t i,start;
    int ok=0;
    (void)unused;
    /* Frozen state excludes every writer and rearm until this worker finishes. */
    lstrcpynW(path,g_shadow_engine.bootstrap.snapshot.bin_directory,MAX_PATH);
    _snwprintf(name,96,L"ShadowEngineIntersection-%lu-%ld.log",
        (unsigned long)GetCurrentProcessId(),(long)d->capture);
    if(lstrlenW(path)+lstrlenW(name)<MAX_PATH) {
        lstrcatW(path,name);
        file=CreateFileW(path,GENERIC_WRITE,FILE_SHARE_READ,NULL,CREATE_ALWAYS,
            FILE_ATTRIBUTE_NORMAL,NULL);
    }
    if(file!=INVALID_HANDLE_VALUE) {
        _snprintf(line,sizeof(line),
            "IX_BEGIN version=%s capture=%ld started=%lu marked=%lu markerSeen=%d total=%u retained=%u overwritten=%u anomalies=%u anomalyOverflow=%u contention=%ld slotMisses=%ld nonAtomicFieldTuple=1 cpuOnly=1 queueMapRanges=derivedPrefix\r\n",
            PATCH_VERSION,(long)d->capture,(unsigned long)d->started,
            (unsigned long)d->marked,d->marker_seen,d->sequence,d->count,
            d->sequence-d->count,d->anomaly_count,d->anomaly_overflow,
            (long)d->contention,(long)d->slot_misses);
        ok=intersection_write(file,line);
        start=d->sequence-d->count;
        for(i=0;ok && i<d->count;++i)
            ok=intersection_write_event(file,&d->events[(start+i)%INTERSECTION_EVENTS],0);
        for(i=0;ok && i<d->anomaly_count;++i)
            ok=intersection_write_event(file,&d->anomalies[i],1);
        if(ok) ok=intersection_write(file,"IX_END complete=1\r\n");
        CloseHandle(file);
    }
    if(!ok) InterlockedIncrement(&d->flush_failures);
    append_log("STAGE_INTERSECTION_CAPTURE_END capture=%ld complete=%d events=%u anomalies=%u contention=%ld slotMisses=%ld action=%s file=ShadowEngineIntersection-%lu-%ld.log",
        (long)d->capture,ok,d->count,d->anomaly_count,(long)d->contention,
        (long)d->slot_misses,ok?"idle":"F9Retry",
        (unsigned long)GetCurrentProcessId(),(long)d->capture);
    InterlockedExchange(&d->state,ok?0:3);
    return 0;
}
static void intersection_schedule_flush(void)
{
    IntersectionDiagnosticState *d=&g_shadow_engine.intersection;
    if(InterlockedCompareExchange(&d->state,4,3)!=3) return;
    if(!d->queue_work(intersection_flush,NULL,WT_EXECUTEDEFAULT)) {
        InterlockedIncrement(&d->flush_failures);
        InterlockedExchange(&d->state,3);
        append_log("STAGE_INTERSECTION_FLUSH_QUEUE_FAILED capture=%ld action=F9Retry",
            (long)d->capture);
    }
}

/* Commands: bit 1 arm/stop (F7), bit 2 event mark (F8), bit 4 finish (F9).
 * Requests survive a contended poll. No key changes the rendering policy. */
static void intersection_poll(int f8,int f9)
{
    IntersectionDiagnosticState *d=&g_shadow_engine.intersection;
    LONG commands;
    DWORD now;
    int armed=0,flush=0;
    if(!d->tls_valid) return;
    if(f8) atomic_or_long(&d->requests,2);
    if(f9) atomic_or_long(&d->requests,4);
    if(d->state==0 && !d->requests) return;
    if(!intersection_lock()) return;
    commands=InterlockedExchange(&d->requests,0);
    now=GetTickCount();
    if(d->state==0 && (commands&1)) {
        ++d->capture;
        d->sequence=0; d->count=0; d->anomaly_count=0; d->anomaly_overflow=0;
        d->contention=0; d->slot_misses=0; d->marker_seen=0;
        d->started=now; d->marked=0; d->last_queue_sample=now-100U;
        InterlockedExchange(&d->state,2); armed=1;
    } else if(d->state==2 && (commands&1)) {
        InterlockedExchange(&d->state,3); flush=1;
    } else if(d->state==3 && (commands&4)) flush=1;
    if(d->state==2 && (commands&2) && !d->marker_seen) {
        IntersectionEvent e;
        memset(&e,0,sizeof(e)); e.kind=IX_MARK;
        d->marked=now; d->marker_seen=1; intersection_append(&e);
    }
    if(d->state==2 && ((commands&4) || now-d->started>=INTERSECTION_TIMEOUT_MS ||
       (d->marker_seen && now-d->marked>=INTERSECTION_POST_MS))) {
        InterlockedExchange(&d->state,3); flush=1;
    }
    intersection_unlock();
    if(armed) append_log("STAGE_INTERSECTION_ARMED capture=%ld timeoutMs=%u postMarkMs=%u ringEvents=%u policyChange=0",
        (long)d->capture,INTERSECTION_TIMEOUT_MS,INTERSECTION_POST_MS,INTERSECTION_EVENTS);
    if(flush) intersection_schedule_flush();
}
