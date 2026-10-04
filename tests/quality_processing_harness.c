/* Current-unity Q1/Q2 vs frozen .75. Synthetic Windows-owned memory only.
 * Policy words are seeded directly; settings-owner transactions have a separate
 * fixture. No hooks, game, settings files, worker, ETW, or installation. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <assert.h>
static unsigned queries,checks,cases;
static const void *optional_proof_address;
static int reject_optional_proof;
static SIZE_T WINAPI quality_test_query(LPCVOID address,PMEMORY_BASIC_INFORMATION info,SIZE_T size)
{
    ++queries;
    if(reject_optional_proof && address==optional_proof_address) return 0;
    return VirtualQuery(address,info,size);
}
#define VirtualQuery quality_test_query
#include "../src/shadow_engine_patch.c"
#include "quality_reference_75.inc"
#undef VirtualQuery
#define CHECK(value) do { ++checks; assert(value); } while(0)
#define PAGE_BYTES 4096U
#define QUEUE_BYTES ((RENDER_QUEUE_ALLOCATION_BYTES+PAGE_BYTES-1U)&~(PAGE_BYTES-1U))
#define PAYLOAD_BYTES (128U*PAGE_BYTES)
#define COUNTERS 11U
static unsigned char *old_queue,*new_queue,*payload;
static unsigned char input[RENDER_QUEUE_ALLOCATION_BYTES];
static unsigned char candidate_objects[TOTAL_LOCAL_MAPS][0x20];
static VehicleQualityRecord record_seed[VEHICLE_SELECTION_EPOCH_BANKS][VEHICLE_SELECTION_SLOTS_PER_EPOCH];
static unsigned before_queries,after_queries;
typedef struct QualityCounters { LONG v[COUNTERS]; } QualityCounters;
static void protect(void *address,SIZE_T bytes,DWORD permission)
{ DWORD old; CHECK(VirtualProtect(address,bytes,permission,&old)); }
static QualityCounters counters(unsigned world)
{
    QualityCounters c={{0}};
    if(world) {
        WorldLightQualityState *w=&g_shadow_engine.world_quality;
        c.v[0]=w->mutated_calls; c.v[1]=w->mutated_entries;
        c.v[5]=w->skipped_ambiguous; c.v[6]=w->skipped_vehicle; c.v[7]=w->skipped_unreadable;
        c.v[8]=w->under_cap_entries; c.v[9]=w->inspected_entries; c.v[10]=w->fast_check_unavailable_calls;
    } else {
        VehicleLightDiagnosticState *d=&g_shadow_engine.vehicle_diagnostics;
        c.v[0]=d->quality_mutated_calls; c.v[1]=d->quality_mutated_entries;
        c.v[2]=d->quality_mutated_type1; c.v[3]=d->quality_mutated_type3;
        c.v[4]=d->quality_unknown_entries; c.v[5]=d->quality_ambiguous_entries;
        c.v[8]=d->quality_under_cap_entries; c.v[9]=d->quality_inspected_entries;
        c.v[10]=d->quality_fast_check_unavailable_calls;
    }
    return c;
}
static void clear_counters(void)
{
    VehicleLightDiagnosticState *d=&g_shadow_engine.vehicle_diagnostics;
    memset(&g_shadow_engine.world_quality,0,sizeof(g_shadow_engine.world_quality));
    d->quality_mutated_calls=d->quality_mutated_entries=d->quality_mutated_type1=d->quality_mutated_type3=0;
    d->quality_unknown_entries=d->quality_ambiguous_entries=0;
    d->quality_under_cap_entries=d->quality_inspected_entries=d->quality_fast_check_unavailable_calls=0;
    d->quality_lock_skips=d->world_quality_lock_skips=0;
}
static void identity_put(uint64_t handle,LONG epoch,uint64_t owner,int ambiguous)
{
    unsigned n,start=frozen75_vehicle_selection_hash(handle,0)&511U;
    for(n=0;n<512U;++n) {
        VehicleQualityRecord *r=&g_shadow_engine.vehicle_diagnostics.quality_records[(unsigned)epoch&3U][(start+n)&511U];
        if(r->epoch_tag==epoch+1 && r->descriptor_handle!=handle) continue;
        r->descriptor_handle=handle; r->vehicle=owner; r->distance_squared=25.0f;
        r->ambiguous=ambiguous; r->epoch_tag=epoch+1; return;
    }
    assert(0);
}
static void entry(unsigned i,unsigned char *descriptor,unsigned char *spatial,int type,uint64_t handle,
    uint32_t width,uint32_t height)
{
    unsigned char *e=input+(size_t)i*RENDER_QUEUE_ENTRY_BYTES;
    *(void **)(e+VEHICLE_QUEUE_CANDIDATE_OFFSET)=candidate_objects[i];
    *(void **)(candidate_objects[i]+8)=descriptor; *(void **)(candidate_objects[i]+16)=spatial;
    if(descriptor) memcpy(descriptor+8,&type,4);
    if(spatial) memcpy(spatial+VEHICLE_SPATIAL_DESCRIPTOR_HANDLE_OFFSET,&handle,8);
    *(uint32_t *)(e+VEHICLE_QUEUE_WIDTH_OFFSET)=width; *(uint32_t *)(e+VEHICLE_QUEUE_HEIGHT_OFFSET)=height;
}
static void reset(void)
{
    unsigned i;
    protect(payload,PAYLOAD_BYTES,PAGE_READWRITE);
    protect(old_queue,QUEUE_BYTES,PAGE_READWRITE); protect(new_queue,QUEUE_BYTES,PAGE_READWRITE);
    memset(payload,0xA5,PAYLOAD_BYTES); memset(input,0x5A,sizeof(input));
    memset(candidate_objects,0,sizeof(candidate_objects));
    memset(g_shadow_engine.vehicle_diagnostics.quality_records,0,sizeof(record_seed));
    g_shadow_engine.renderer.renderer_calls=20;
    g_shadow_engine.vehicle_diagnostics.selection_writer_active=0;
    g_shadow_engine.vehicle_diagnostics.selection_enabled=1;
    g_shadow_engine.policy_control.quality_ready=1;
    g_shadow_engine.policy_control.disabled=3L<<SHADOW_POLICY_RESOLUTION_SHIFT;
    g_shadow_engine.policy_control.world_configuration=1L|(3L<<SHADOW_WORLD_RESOLUTION_SHIFT);
    reject_optional_proof=0; optional_proof_address=NULL; clear_counters();
    for(i=0;i<TOTAL_LOCAL_MAPS;++i)
        entry(i,payload+i*0x100U,payload+i*0x100U+0x80U,3,UINT64_MAX,4096,4096);
}
static void run(unsigned world,int frozen,void *queue,uint32_t count)
{
    if(world) { if(frozen) frozen75_apply_world_shadow_quality(queue,count); else apply_world_shadow_quality(queue,count); }
    else { if(frozen) frozen75_apply_vehicle_shadow_quality(queue,count); else apply_vehicle_shadow_quality(queue,count); }
}
static int enabled(unsigned world,int null_queue)
{
    if(null_queue || !g_shadow_engine.policy_control.quality_ready ||
       g_shadow_engine.vehicle_diagnostics.selection_writer_active) return 0;
    if(world) return (g_shadow_engine.policy_control.world_configuration&SHADOW_WORLD_ENABLED)!=0;
    return g_shadow_engine.vehicle_diagnostics.selection_enabled &&
        !(g_shadow_engine.policy_control.disabled&SHADOW_POLICY_DISABLE_QUALITY);
}
/* Whole output and mutation accounting come from the complete frozen pass.
 * Expected scoped classifications independently replay only fixture-selected
 * inspected entries through that frozen classifier. Q1's predicate/helper and
 * reported counters are never used to derive the expectation. */
static void compare(unsigned world,uint32_t count,int proof_expected,int null_queue)
{
    unsigned i,n=count>TOTAL_LOCAL_MAPS?TOTAL_LOCAL_MAPS:count,under=0;
    uint32_t cap=world?shadow_policy_world_resolution():shadow_policy_resolution();
    int active=enabled(world,null_queue);
    LONG token=g_shadow_engine.vehicle_diagnostics.selection_writer_active,old_skips,new_skips;
    QualityCounters full,expected,actual;
    memcpy(old_queue,input,sizeof(input)); memcpy(new_queue,input,sizeof(input));
    memcpy(record_seed,g_shadow_engine.vehicle_diagnostics.quality_records,sizeof(record_seed));
    clear_counters(); queries=0;
    run(world,1,null_queue?NULL:old_queue,count); before_queries=queries; full=counters(world);
    old_skips=world?g_shadow_engine.vehicle_diagnostics.world_quality_lock_skips:g_shadow_engine.vehicle_diagnostics.quality_lock_skips;
    CHECK(token==g_shadow_engine.vehicle_diagnostics.selection_writer_active);
    CHECK(!memcmp(record_seed,g_shadow_engine.vehicle_diagnostics.quality_records,sizeof(record_seed)));
    clear_counters();
    if(active) for(i=0;i<n;++i) {
        unsigned char *e=input+(size_t)i*RENDER_QUEUE_ENTRY_BYTES;
        uint32_t width=*(uint32_t *)(e+VEHICLE_QUEUE_WIDTH_OFFSET),height=*(uint32_t *)(e+VEHICLE_QUEUE_HEIGHT_OFFSET);
        if(proof_expected && width<=cap && height<=cap) ++under;
        else run(world,1,new_queue+(size_t)i*RENDER_QUEUE_ENTRY_BYTES,1);
    }
    expected=counters(world);
    for(i=0;i<4U;++i) expected.v[i]=full.v[i];
    expected.v[8]=(LONG)under; expected.v[9]=active?(LONG)(n-under):0;
    expected.v[10]=active && n && !proof_expected;
    memcpy(new_queue,input,sizeof(input)); clear_counters(); queries=0;
    optional_proof_address=new_queue+VEHICLE_QUEUE_WIDTH_OFFSET;
    run(world,0,null_queue?NULL:new_queue,count); after_queries=queries; actual=counters(world);
    new_skips=world?g_shadow_engine.vehicle_diagnostics.world_quality_lock_skips:g_shadow_engine.vehicle_diagnostics.quality_lock_skips;
    CHECK(!memcmp(old_queue,new_queue,sizeof(input)));
    CHECK(!memcmp(&expected,&actual,sizeof(expected)));
    CHECK(token==g_shadow_engine.vehicle_diagnostics.selection_writer_active && old_skips==new_skips);
    CHECK(!memcmp(record_seed,g_shadow_engine.vehicle_diagnostics.quality_records,sizeof(record_seed)));
    if(active && n && proof_expected && under==n) CHECK(after_queries==1);
    if(!active || !n) CHECK(after_queries==0);
    ++cases;
}
static void decisions(void)
{
    unsigned world,level,kind,x,y,identity;
    static const int types[]={-1,0,1,2,3,4,5};
    for(world=0;world<2;++world) for(level=1;level<=4;++level)
    for(kind=0;kind<ARRAY_COUNT(types);++kind) for(x=0;x<3;++x) for(y=0;y<3;++y)
    for(identity=0;identity<6;++identity) {
        uint32_t cap=256U<<level;
        reset();
        g_shadow_engine.policy_control.disabled=(LONG)level<<SHADOW_POLICY_RESOLUTION_SHIFT;
        g_shadow_engine.policy_control.world_configuration=1L|((LONG)level<<SHADOW_WORLD_RESOLUTION_SHIFT);
        entry(0,payload,payload+0x80U,types[kind],identity?17:UINT64_MAX,
            x==0?cap/2U:(x==1?cap:cap*2U),y==0?cap/2U:(y==1?cap:cap*2U));
        if(identity>=2) identity_put(17,20,identity==4?0:42,identity==3);
        if(identity==5) identity_put(17,19,99,0);
        compare(world,1,1,0);
    }
    for(world=0;world<2;++world) {
        reset(); compare(world,0,0,0); compare(world,30,1,1);
        g_shadow_engine.policy_control.quality_ready=0; compare(world,30,1,0);
        g_shadow_engine.policy_control.quality_ready=1;
        g_shadow_engine.vehicle_diagnostics.selection_writer_active=1; compare(world,30,1,0);
        reset(); g_shadow_engine.policy_control.disabled|=SHADOW_POLICY_DISABLE_QUALITY;
        g_shadow_engine.policy_control.world_configuration&=~SHADOW_WORLD_ENABLED;
        compare(world,30,1,0);
        reset(); compare(world,UINT32_MAX,1,0);
        *(void **)(input+VEHICLE_QUEUE_CANDIDATE_OFFSET)=NULL; compare(world,1,1,0);
        entry(0,NULL,payload+0x80U,3,17,4096,4096); compare(world,1,1,0);
        entry(0,payload,NULL,3,17,4096,4096); compare(world,1,1,0);
        reset(); reject_optional_proof=1; compare(world,30,0,0);
        CHECK(after_queries==before_queries+1U);
        reset(); g_shadow_engine.vehicle_diagnostics.selection_enabled=0;
        compare(world,30,1,0);
    }
    puts("PASS Q1/Q2 frozen75: both passes, four caps, independent lower/equal/higher axes, types -1..5, sentinel/missing/exact/ambiguous/null/conflicting owners, Original/readiness/token/null gates and count bounds; full queue bytes, mutations, scoped coverage and registry unchanged.");
}
static void memory_safety(void)
{
    unsigned world,mode,i;
    MEMORY_BASIC_INFORMATION info;
    for(world=0;world<2;++world) for(mode=0;mode<3;++mode) {
        unsigned char *q; QualityCounters c,old;
        reset(); q=new_queue+3U*PAGE_BYTES-VEHICLE_QUEUE_WIDTH_OFFSET;
        *(void **)(q+VEHICLE_QUEUE_CANDIDATE_OFFSET)=mode==0?NULL:candidate_objects[0];
        *(void **)(candidate_objects[0]+8)=NULL;
        *(uint32_t *)(q+VEHICLE_QUEUE_WIDTH_OFFSET-4U)=1;
        protect(new_queue+3U*PAGE_BYTES,PAGE_BYTES,mode==2?(PAGE_READWRITE|PAGE_GUARD):PAGE_NOACCESS);
        CHECK(readable_memory(q+VEHICLE_QUEUE_WIDTH_OFFSET-4U,4));
        run(world,1,q,1); old=counters(world); clear_counters(); queries=0; run(world,0,q,1);
        c=counters(world); CHECK(c.v[8]==0 && c.v[9]==1 && c.v[10]==1 && !c.v[1]);
        CHECK(!memcmp(c.v,old.v,8U*sizeof(LONG)));
        CHECK(queries==1);
        CHECK(VirtualQuery(new_queue+3U*PAGE_BYTES,&info,sizeof(info))!=0);
        if(mode==2) CHECK(info.Protect&PAGE_GUARD);
    }
    for(mode=0;mode<2;++mode) {
        reset(); protect(payload,PAGE_BYTES,mode==0?PAGE_NOACCESS:(PAGE_READWRITE|PAGE_GUARD));
        compare(1,1,1,0); CHECK(counters(1).v[7]==1);
        *(uint32_t *)(input+VEHICLE_QUEUE_WIDTH_OFFSET)=512;
        *(uint32_t *)(input+VEHICLE_QUEUE_HEIGHT_OFFSET)=512;
        compare(1,1,1,0); CHECK(counters(1).v[7]==0 && counters(1).v[8]==1);
        CHECK(VirtualQuery(payload,&info,sizeof(info))!=0);
        if(mode) CHECK(info.Protect&PAGE_GUARD);
    }
    /* A successful earlier proof grants nothing to a later pass. */
    for(world=0;world<2;++world) {
        unsigned char *q;
        reset(); q=new_queue+3U*PAGE_BYTES-VEHICLE_QUEUE_WIDTH_OFFSET;
        *(void **)(q+VEHICLE_QUEUE_CANDIDATE_OFFSET)=NULL;
        *(uint32_t *)(q+VEHICLE_QUEUE_WIDTH_OFFSET)=512;
        *(uint32_t *)(q+VEHICLE_QUEUE_HEIGHT_OFFSET)=512;
        run(world,0,q,1); CHECK(counters(world).v[8]==1);
        protect(new_queue+3U*PAGE_BYTES,PAGE_BYTES,PAGE_READWRITE|PAGE_GUARD);
        queries=0; run(world,0,q,1);
        CHECK(counters(world).v[8]==1 && counters(world).v[9]==1 && counters(world).v[10]==1);
        CHECK(queries==1 && VirtualQuery(new_queue+3U*PAGE_BYTES,&info,sizeof(info))!=0);
        CHECK(info.Protect&PAGE_GUARD);
    }
    /* Different readable/writable regions invalidate only the shortcut. The
     * complete old path must still cap valid entries in every region. */
    for(world=0;world<2;++world) {
        reset();
        for(i=0;i<30;++i) identity_put(UINT64_C(1000)+i,20,42,0);
        for(i=0;i<30;++i) entry(i,payload+i*0x100U,payload+i*0x100U+0x80U,3,
            world?UINT64_MAX:UINT64_C(1000)+i,4096,1024);
        protect(new_queue+4U*PAGE_BYTES,PAGE_BYTES,PAGE_EXECUTE_READWRITE);
        compare(world,30,0,0); CHECK(counters(world).v[1]==30);
        reset();
        for(i=0;i<30;++i) entry(i,payload+i*0x100U,payload+i*0x100U+0x80U,3,17,512,512);
        *(void **)(input+VEHICLE_QUEUE_CANDIDATE_OFFSET)=payload;
        protect(payload,PAYLOAD_BYTES,PAGE_NOACCESS);
        memcpy(new_queue,input,sizeof(input)); queries=0; run(world,0,new_queue,30);
        CHECK(counters(world).v[8]==30 && counters(world).v[9]==0 && queries==1);
        CHECK(!memcmp(input,new_queue,sizeof(input))); /* No candidate dereference. */
    }
    reset(); queries=0;
    CHECK(!quality_dimensions_readable(NULL,1)); CHECK(!quality_dimensions_readable(new_queue,0));
    CHECK(!quality_dimensions_readable(new_queue,UINT32_MAX));
    CHECK(!quality_dimensions_readable((void *)(UINTPTR_MAX-VEHICLE_QUEUE_WIDTH_OFFSET+1U),1));
    CHECK(!quality_dimensions_readable((void *)(UINTPTR_MAX-VEHICLE_QUEUE_WIDTH_OFFSET-4U),1));
    CHECK(!quality_dimensions_readable((void *)(UINTPTR_MAX-VEHICLE_QUEUE_WIDTH_OFFSET-16U),30));
    CHECK(!queries);
    puts("PASS optional span safety: readable face with guarded/no-access dimensions preserves null-candidate/descriptor legacy skips without consuming guard; split readable queue still mutates; under-cap guarded payload never read; zero/count/start/end overflow refuses before VirtualQuery.");
}
static void independent_passes(void)
{
    unsigned v,w,mode;
    for(v=0;v<=4;++v) for(w=0;w<=4;++w) for(mode=0;mode<3;++mode) {
        reset(); g_shadow_engine.policy_control.disabled=v?((LONG)v<<SHADOW_POLICY_RESOLUTION_SHIFT):
            SHADOW_POLICY_DISABLE_QUALITY|(3L<<SHADOW_POLICY_RESOLUTION_SHIFT);
        g_shadow_engine.policy_control.world_configuration=w?(1L|((LONG)w<<SHADOW_WORLD_RESOLUTION_SHIFT)):
            (3L<<SHADOW_WORLD_RESOLUTION_SHIFT);
        entry(0,payload,payload+0x80U,3,17,8192,1024);
        if(mode==0) identity_put(17,20,42,0);
        memcpy(old_queue,input,sizeof(input)); memcpy(new_queue,input,sizeof(input));
        frozen75_apply_vehicle_shadow_quality(old_queue,1); apply_vehicle_shadow_quality(new_queue,1);
        CHECK(!g_shadow_engine.vehicle_diagnostics.selection_writer_active);
        CHECK(!memcmp(old_queue,new_queue,sizeof(input)));
        /* Admit a real publisher between distinct pass tokens. The next pass
         * must see this publication, including newly conflicting ownership. */
        CHECK(vehicle_selection_lock());
        publish_vehicle_quality_identity(0,20,17,mode==0?99:42,25.0f);
        vehicle_selection_unlock();
        if(mode==2) g_shadow_engine.vehicle_diagnostics.selection_writer_active=1;
        frozen75_apply_world_shadow_quality(old_queue,1); apply_world_shadow_quality(new_queue,1);
        CHECK(!memcmp(old_queue,new_queue,sizeof(input)));
        CHECK(g_shadow_engine.vehicle_diagnostics.selection_writer_active==(mode==2));
        ++cases;
    }
    puts("PASS all 25 independent cap/Original combinations and real identity publication between separate pass tokens, including conflict and second-pass token refusal; frozen75 final queue matches.");
}
static void cumulative_coverage(void)
{
    unsigned world,fallback,i;
    for(world=0;world<2;++world) for(fallback=0;fallback<2;++fallback) {
        QualityCounters once,twice;
        reset();
        entry(0,payload,payload+0x80U,3,17,512,512);
        entry(1,payload+0x100U,payload+0x180U,3,17,4096,4096);
        entry(2,payload+0x200U,payload+0x280U,3,19,4096,4096);
        entry(3,payload+0x300U,payload+0x380U,3,23,4096,4096);
        entry(4,payload+0x400U,payload+0x480U,3,17,512,512);
        *(void **)(input+4U*RENDER_QUEUE_ENTRY_BYTES+VEHICLE_QUEUE_CANDIDATE_OFFSET)=NULL;
        *(void **)(input+5U*RENDER_QUEUE_ENTRY_BYTES+VEHICLE_QUEUE_CANDIDATE_OFFSET)=NULL;
        identity_put(17,20,42,0); identity_put(23,20,99,1);
        reject_optional_proof=(int)fallback;
        compare(world,6,!fallback,0); once=counters(world);
        memcpy(new_queue,input,sizeof(input)); run(world,0,new_queue,6); twice=counters(world);
        for(i=0;i<COUNTERS;++i) CHECK(twice.v[i]==2*once.v[i]);
        CHECK(!memcmp(old_queue,new_queue,sizeof(input)));
    }
    puts("PASS cumulative Q2 totals: repeated mixed exact/unknown/ambiguous/null queues with proof success and fallback preserve mutations and accumulate scoped classifications and coverage.");
}
static double timed(unsigned world,int frozen,unsigned loops)
{
    LARGE_INTEGER a,b,f; unsigned i,j; unsigned char *q=frozen?old_queue:new_queue;
    QueryPerformanceFrequency(&f); QueryPerformanceCounter(&a);
    for(i=0;i<loops;++i) {
        /* Restore just native dimensions each call; both timed sides pay this
         * same work so mixed/all-above cases cannot silently become no-op. */
        for(j=0;j<30;++j) memcpy(q+(size_t)j*RENDER_QUEUE_ENTRY_BYTES+VEHICLE_QUEUE_WIDTH_OFFSET,
            input+(size_t)j*RENDER_QUEUE_ENTRY_BYTES+VEHICLE_QUEUE_WIDTH_OFFSET,8);
        run(world,frozen,q,30);
    }
    QueryPerformanceCounter(&b); return (double)(b.QuadPart-a.QuadPart)*1000000.0/(double)f.QuadPart/(double)loops;
}
static void benchmarks(void)
{
    unsigned world,layout,i,r; static const char *names[]={"dense-no-op","mixed-half","all-above","fragmented-payload-above","failed-span-above"};
    puts("Q1 benchmark scope: sparse exact vehicle registry; world sentinel handles avoid identity lookup. These cheap synthetic baselines expose the extra query cost and do not predict the live identity mix.");
    for(world=0;world<2;++world) for(layout=0;layout<5;++layout) {
        double old_sum=0,new_sum=0;
        reset();
        for(i=0;i<30;++i) {
            unsigned width=(layout==0 || (layout==1 && i%2U==0))?1024:4096;
            unsigned char *d=layout==3?payload+2U*i*PAGE_BYTES:payload+i*0x100U;
            unsigned char *s=layout==3?payload+(2U*i+1U)*PAGE_BYTES:payload+i*0x100U+0x80U;
            entry(i,d,s,3,world?UINT64_MAX:UINT64_C(1000)+i,width,width);
            if(!world) identity_put(UINT64_C(1000)+i,20,42,0);
        }
        if(layout==3) for(i=0;i<60;++i) protect(payload+i*PAGE_BYTES,PAGE_BYTES,i&1U?PAGE_READONLY:PAGE_READWRITE);
        if(layout==4) protect(new_queue+4U*PAGE_BYTES,PAGE_BYTES,PAGE_EXECUTE_READWRITE);
        compare(world,30,layout!=4,0);
        timed(world,1,20); timed(world,0,20);
        for(r=0;r<6;++r) {
            if(r&1U) { new_sum+=timed(world,0,200); old_sum+=timed(world,1,200); }
            else { old_sum+=timed(world,1,200); new_sum+=timed(world,0,200); }
        }
        printf("Q1 full-pass %s %s: frozen75=%.3fus candidate=%.3fus VirtualQuery=%u->%u; six alternating rounds x200 calls, equal dimension restoration, fresh proof/cache, host-only not FPS.\n",
            world?"world":"vehicle",names[layout],old_sum/6.0,new_sum/6.0,before_queries,after_queries);
    }
}
int main(void)
{
    SYSTEM_INFO info;
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);
    GetSystemInfo(&info); CHECK(info.dwPageSize==PAGE_BYTES);
    old_queue=VirtualAlloc(NULL,QUEUE_BYTES,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    new_queue=VirtualAlloc(NULL,QUEUE_BYTES,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    payload=VirtualAlloc(NULL,PAYLOAD_BYTES,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    CHECK(old_queue && new_queue && payload);
    decisions(); memory_safety(); independent_passes(); cumulative_coverage(); benchmarks();
    CHECK(VirtualFree(old_queue,0,MEM_RELEASE)); CHECK(VirtualFree(new_queue,0,MEM_RELEASE)); CHECK(VirtualFree(payload,0,MEM_RELEASE));
    printf("PASS Q1/Q2 current-unity frozen75 differential: %u cases, %u checks.\n",cases,checks);
    return 0;
}
