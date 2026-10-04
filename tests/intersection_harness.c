/* Runs the production recorder with synthetic records. No game or hooks. */
#include "../src/shadow_engine_patch.c"
#include <assert.h>
#include <stdlib.h>

static unsigned char record_a[0x200],record_b[0x200];
static unsigned char queue_bytes[RENDER_QUEUE_ALLOCATION_BYTES];
static int key_down,work_requests;
static SHORT WINAPI fake_key(int key) { return key==VK_F7 && key_down ? (SHORT)0x8000:0; }
static BOOL WINAPI reject_work(LPTHREAD_START_ROUTINE fn,PVOID arg,ULONG flags)
{ (void)fn; (void)arg; (void)flags; ++work_requests; return FALSE; }
static void *__fastcall fake_constructor(void *record) { return record; }
static void *__fastcall fake_lookup(void *manager,uint32_t key,void *descriptor)
{ assert(manager==record_a && key==23); return descriptor; }
static IntersectionEvent *last_event(void)
{
    IntersectionDiagnosticState *d=&g_shadow_engine.intersection;
    return &d->events[(d->sequence-1U)%INTERSECTION_EVENTS];
}

static void test_readonly_preflight(void)
{
    static const uint32_t returns[3]={0x3DDD9C,0x3DDFC0,0x3DE059};
    static const uint32_t stores[3]={12,0,4},offsets[3]={0x100,0xF8,0x110};
    unsigned char *image=VirtualAlloc(NULL,0x2100000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    unsigned char *target=image+0x2000000,*ret;
    uint32_t p,i;
    int32_t relative;
    PatchTransaction transaction;
    PatchRollbackResult rollback;
    assert(image);
    g_shadow_engine.bootstrap.disrupt_base=image;
    for(p=0;p<ARRAY_COUNT(g_runtime_profiles);++p) {
        select_runtime_profile_state(&g_runtime_profiles[p]);
        memcpy(target,g_runtime_profiles[p].intersection_lookup_prologue,15);
        for(i=0;i<3;++i) {
            ret=ENGINE_ADDRESS(returns[i]); relative=(int32_t)(target-ret);
            ret[-5]=0xE8; memcpy(ret-4,&relative,4);
            ret[stores[i]]=0x49; ret[stores[i]+1]=0x89;
            ret[stores[i]+2]=g_runtime_profiles[p].intersection_record_modrm;
            memcpy(ret+stores[i]+3,&offsets[i],4);
        }
        assert(validate_intersection_sites());
        assert(g_shadow_engine.intersection.lookup_target==target);
        for(i=0;i<3;++i) {
            ret=ENGINE_ADDRESS(returns[i]);
            ret[-5]=0x90; assert(!validate_intersection_sites()); ret[-5]=0xE8;
            ret[stores[i]+3]^=1; assert(!validate_intersection_sites()); ret[stores[i]+3]^=1;
            ret[-4]^=1; assert(!validate_intersection_sites()); ret[-4]^=1;
        }
        target[0]^=1; assert(!validate_intersection_sites()); target[0]^=1;
        assert(validate_intersection_sites());
    }
    /* Exercise the real observer's transaction, TLS and published trampoline
     * on private synthetic bytes only, including failure before target write. */
    for(i=0;i<2;++i) {
        assert(patch_transaction_begin(&transaction,"observer-test",i==0?0:-1));
        assert(install_intersection_observer()==(i!=0));
        assert(g_shadow_engine.intersection.tls_valid);
        assert(g_shadow_engine.hooks.original_intersection_lookup);
        rollback=patch_transaction_rollback(&transaction);
        assert(!rollback.failed_restores && rollback.released_allocations==1);
        rollback_intersection_observer();
        assert(!g_shadow_engine.intersection.tls_valid);
        assert(!g_shadow_engine.hooks.original_intersection_lookup);
        assert(memcmp(target,g_runtime_state.selected->intersection_lookup_prologue,15)==0);
    }
    clear_runtime_profile(); assert(!validate_intersection_sites());
    g_shadow_engine.bootstrap.disrupt_base=NULL;
    VirtualFree(image,0,MEM_RELEASE);
    puts("PASS actual lookup preflight: five synthetic layouts, wrong call/store/target/prologue and unknown profile reject without executable writes (synthetic A4EE is not runtime proof)");
    puts("PASS actual observer installation on synthetic bytes: prepublished trampoline, failed-write and successful-install rollback, original bytes restored, binding cleared, TLS released");
}

int main(int argc,char **argv)
{
    IntersectionDiagnosticState *d=&g_shadow_engine.intersection;
    LifecycleRecordSnapshot *life;
    IntersectionBuilderScope outer,inner;
    IntersectionEvent e;
    unsigned char before[sizeof(record_a)];
    uint32_t i,sequence;
    LONG generation;
    HANDLE file;
    memset(&g_shadow_engine,0,sizeof(g_shadow_engine));
    test_readonly_preflight();
    g_shadow_engine.hooks.original_render_record_constructor=fake_constructor;
    g_shadow_engine.hooks.original_intersection_lookup=fake_lookup;
    g_shadow_engine.hooks.get_async_key_state=fake_key;
    d->queue_work=reject_work;
    d->tls_index=TlsAlloc(); assert(d->tls_index!=TLS_OUT_OF_INDEXES); d->tls_valid=1;
    hooked_external_render_record_constructor(record_a);
    hooked_external_render_record_constructor(record_b);
    life=find_existing_lifecycle_record_fast(record_a); assert(life);
    generation=life->generation;
    assert(hooked_intersection_lookup(record_a,23,record_b)==record_b);
    intersection_observe(IX_RELEASE,record_a,0,NULL,0,0); assert(d->sequence==0);
    atomic_or_long(&d->requests,1); intersection_poll(0,0); assert(d->state==2 && d->capture==1);
    intersection_poll(0,0); assert(d->capture==1); /* held key is not a new command */
    key_down=0; intersection_poll(0,0);
    intersection_builder_enter(&outer,record_a);
    assert(TlsGetValue(d->tls_index)==&outer);
    intersection_builder_enter(&inner,record_b);
    intersection_builder_exit(&inner); assert(TlsGetValue(d->tls_index)==&outer);
    intersection_observe(IX_LOOKUP,record_a,0,record_b,0x3DDFC0,23);
    assert(last_event()->mismatch_mask==0); /* assignment has not happened yet */
    *(void **)(record_a+0xF8)=record_b;
    intersection_builder_exit(&outer); assert(TlsGetValue(d->tls_index)==NULL);
    assert(last_event()->valid_mask==1 && last_event()->mismatch_mask==0);
    memcpy(before,record_a,sizeof(before));
    intersection_observe(IX_PRE_FINALIZER,record_a,0,NULL,0,0);
    assert(memcmp(before,record_a,sizeof(before))==0);
    *(void **)(record_a+0xF8)=NULL;
    intersection_observe(IX_RELEASE,record_a,0,NULL,0,0);
    assert(last_event()->mismatch_mask==1 && d->anomaly_count==1);
    intersection_observe(IX_LOOKUP,record_a,0,NULL,0,23);
    intersection_observe(IX_RELEASE,record_a,0,NULL,0,0);
    assert(last_event()->valid_mask==1 && !last_event()->mismatch_mask); /* valid null */
    ++life->generation;
    intersection_observe(IX_RELEASE,record_a,0,NULL,0,0);
    assert(!last_event()->valid_mask && life->generation==generation+1);
    intersection_builder_enter(&outer,record_a); ++life->generation;
    sequence=d->sequence; intersection_builder_exit(&outer);
    assert(d->sequence==sequence && TlsGetValue(d->tls_index)==NULL);

    /* A completion identity need not point to readable native record bytes. */
    find_lifecycle_record((void *)0x12340,1)->generation=1;
    intersection_observe(IX_COMPLETION,(void *)0x12340,17,record_b,0,0);
    assert(last_event()->record==(void *)0x12340 && !last_event()->valid_mask);
    sequence=d->sequence;
    d->lock=1; intersection_observe(IX_RELEASE,record_a,0,NULL,0,0); d->lock=0;
    assert(d->sequence==sequence && d->contention>0);
    intersection_observe(IX_COMPLETION,(void *)0x56780,17,NULL,0,0);
    assert(d->sequence==sequence && d->slot_misses==1);

    for(i=0;i<PHYSICAL_QUEUE_ENTRIES;++i) {
        unsigned char *entry=queue_bytes+(size_t)i*RENDER_QUEUE_ENTRY_BYTES;
        *(uint32_t *)(entry+0x20A0)=1;
        *(void **)(entry+0x7D8)=record_b;
    }
    d->last_queue_sample=GetTickCount();
    intersection_queue(record_a,queue_bytes,PHYSICAL_QUEUE_ENTRIES,TOTAL_LOCAL_MAPS,
                       PHYSICAL_QUEUE_ENTRIES,TOTAL_LOCAL_MAPS);
    assert(last_event()->kind==IX_ENTRY && last_event()->data[0]==TOTAL_LOCAL_MAPS);
    assert(last_event()->data[4]==1 && last_event()->data[5]==0);
    assert(last_event()->faces[0]==record_b);
    memset(&e,0,sizeof(e)); e.mismatch_mask=1;
    for(i=0;i<INTERSECTION_EVENTS+150U;++i) intersection_append(&e);
    assert(d->count==INTERSECTION_EVENTS && d->anomaly_count==INTERSECTION_ANOMALIES);
    assert(d->anomaly_overflow>0 && last_event()->sequence==d->sequence);
    /* Formatter must handle every copied field without truncation. */
    file=CreateFileW(L"NUL",GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_EXISTING,0,NULL);
    assert(file!=INVALID_HANDLE_VALUE && intersection_write_event(file,last_event(),0));
    CloseHandle(file);

    intersection_poll(1,0); assert(d->marker_seen && last_event()->kind==IX_MARK);
    intersection_poll(0,1); assert(d->state==3 && work_requests==1);
    sequence=d->sequence;
    intersection_observe(IX_RELEASE,record_a,0,NULL,0,0);
    intersection_poll(0,0); assert(work_requests==1 && sequence==d->sequence);
    intersection_poll(0,1); assert(work_requests==2); /* explicit retry only */
    d->state=4; key_down=1; intersection_poll(0,0); assert(d->state==4);
    key_down=0; intersection_poll(0,0); d->state=0;
    atomic_or_long(&d->requests,1); intersection_poll(0,0); intersection_poll(0,0);
    assert(d->capture==2 && d->sequence==0);
    intersection_observe(IX_COMPLETION,(void *)0x12340,17,record_b,0,0);
    d->started=GetTickCount()-INTERSECTION_TIMEOUT_MS;
    intersection_poll(0,0); assert(d->state==3 && work_requests==3);
    if(argc==2) {
        DWORD started=GetTickCount();
        assert(mbstowcs(g_shadow_engine.bootstrap.snapshot.bin_directory,
            argv[1],MAX_PATH)<MAX_PATH);
        d->queue_work=(IntersectionQueueWorkFn)GetProcAddress(
            GetModuleHandleW(L"kernel32.dll"),"QueueUserWorkItem");
        assert(d->queue_work); intersection_schedule_flush();
        while(d->state==4 && GetTickCount()-started<5000U) Sleep(1);
        assert(d->state==0);
        puts("PASS real thread-pool flush of frozen copies to supplied evidence directory");
    }
    rollback_intersection_observer(); assert(!d->tls_valid);
    puts("PASS actual intersection recorder: disabled passthrough, key edges, TLS nesting, lookup-before-store, valid null, mismatch, generation reuse, no engine writes, completion identity-only, contention, unknown records, dropped entries, bounded ring/anomalies, formatting, mark/freeze, explicit retry, worker exclusion, timeout, TLS cleanup");
    return 0;
}
