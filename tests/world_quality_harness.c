/* Historical frozen74-to75 H5 differential fixture. Q1/Q2 current-source
 * coverage is in quality_processing_harness.c. Synthetic queues and Windows-owned
 * allocations only: no game, hooks, worker, ETW, or file output. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <assert.h>
static unsigned queries,checks,cases,identity_cases;
static int fail_queries;
static volatile LONG *changing_tag;
static LONG replacement_tag;
static unsigned changing_reads;
static SIZE_T WINAPI fixture_query(LPCVOID address,PMEMORY_BASIC_INFORMATION info,SIZE_T bytes)
{
    ++queries;
    if(fail_queries) return 0;
    return VirtualQuery(address,info,bytes);
}
static LONG WINAPI fixture_atomic_read(volatile LONG *value,LONG exchange,LONG comparand)
{
    if(value==changing_tag && exchange==0 && comparand==0 && ++changing_reads==2U)
        InterlockedExchange(value,replacement_tag);
    return InterlockedCompareExchange(value,exchange,comparand);
}
#define VirtualQuery fixture_query
#undef InterlockedCompareExchange
#define InterlockedCompareExchange fixture_atomic_read
#include "../src/shadow_engine_patch.c"
#include "world_quality_reference_74.inc"
#include "quality_reference_75.inc"
#undef InterlockedCompareExchange
#undef VirtualQuery
#define CHECK(value) do { ++checks; assert(value); } while(0)
#define PAGE_BYTES 4096U
#define MEMORY_BYTES (128U*PAGE_BYTES)
static unsigned char input[RENDER_QUEUE_ALLOCATION_BYTES];
static unsigned char reference_queue[RENDER_QUEUE_ALLOCATION_BYTES];
static unsigned char candidate_queue[RENDER_QUEUE_ALLOCATION_BYTES];
static unsigned char candidates[TOTAL_LOCAL_MAPS][0x20];
static unsigned char *memory;
static VehicleQualityRecord identity_seed[VEHICLE_SELECTION_EPOCH_BANKS][VEHICLE_SELECTION_SLOTS_PER_EPOCH];
static VehicleQualityRecord identity_after[VEHICLE_SELECTION_EPOCH_BANKS][VEHICLE_SELECTION_SLOTS_PER_EPOCH];
static unsigned reference_queries,candidate_queries;

static void protect(void *address,size_t size,DWORD protection)
{
    DWORD previous;
    CHECK(VirtualProtect(address,size,protection,&previous));
}
static void identity_clear(void)
{ memset(g_shadow_engine.vehicle_diagnostics.quality_records,0,sizeof(identity_seed)); }
static VehicleQualityRecord *identity_put(uint64_t handle,LONG epoch,uint64_t owner,
    float distance,unsigned ambiguous)
{
    unsigned i,start=vehicle_selection_hash(handle,0)&(VEHICLE_SELECTION_SLOTS_PER_EPOCH-1U);
    unsigned bank=(unsigned)epoch&(VEHICLE_SELECTION_EPOCH_BANKS-1U);
    for(i=0;i<VEHICLE_SELECTION_SLOTS_PER_EPOCH;++i) {
        VehicleQualityRecord *record=&g_shadow_engine.vehicle_diagnostics.quality_records[bank]
            [(start+i)&(VEHICLE_SELECTION_SLOTS_PER_EPOCH-1U)];
        if(record->epoch_tag==epoch+1 && record->descriptor_handle!=handle) continue;
        record->descriptor_handle=handle; record->vehicle=owner;
        record->distance_squared=distance; record->ambiguous=ambiguous; record->epoch_tag=epoch+1;
        return record;
    }
    assert(0); return NULL;
}
static unsigned char *queue_entry(unsigned index)
{ return input+(size_t)index*RENDER_QUEUE_ENTRY_BYTES; }
static void entry_payload(unsigned index,unsigned char *descriptor,unsigned char *spatial,
    int type,uint64_t handle,uint32_t width,uint32_t height)
{
    unsigned char *entry=queue_entry(index);
    *(void **)(entry+VEHICLE_QUEUE_CANDIDATE_OFFSET)=candidates[index];
    *(void **)(candidates[index]+8)=descriptor;
    *(void **)(candidates[index]+0x10)=spatial;
    if(descriptor) memcpy(descriptor+8,&type,sizeof(type));
    if(spatial) memcpy(spatial+VEHICLE_SPATIAL_DESCRIPTOR_HANDLE_OFFSET,&handle,sizeof(handle));
    *(uint32_t *)(entry+VEHICLE_QUEUE_WIDTH_OFFSET)=width;
    *(uint32_t *)(entry+VEHICLE_QUEUE_HEIGHT_OFFSET)=height;
}
static void reset_inputs(void)
{
    unsigned i;
    protect(memory,MEMORY_BYTES,PAGE_READWRITE);
    memset(memory,0x6D,MEMORY_BYTES); memset(input,0xA5,sizeof(input));
    memset(candidates,0,sizeof(candidates)); identity_clear();
    g_shadow_engine.renderer.renderer_calls=20;
    g_shadow_engine.policy_control.world_configuration=SHADOW_WORLD_ENABLED|(3L<<SHADOW_WORLD_RESOLUTION_SHIFT);
    g_shadow_engine.policy_control.quality_ready=1;
    g_shadow_engine.vehicle_diagnostics.selection_writer_active=0;
    fail_queries=0; changing_tag=NULL; changing_reads=0;
    for(i=0;i<TOTAL_LOCAL_MAPS;++i)
        entry_payload(i,memory+i*0x100U,memory+i*0x100U+0x80U,3,UINT64_MAX,4096,4096);
}
static void compare_world(uint32_t count,int null_queue)
{
    WorldLightQualityState seed={7,11,13,17,19},expected;
    LONG token=g_shadow_engine.vehicle_diagnostics.selection_writer_active,expected_token,expected_skips;
    memcpy(reference_queue,input,sizeof(input)); memcpy(candidate_queue,input,sizeof(input));
    memcpy(identity_seed,g_shadow_engine.vehicle_diagnostics.quality_records,sizeof(identity_seed));
    g_shadow_engine.world_quality=seed;
    g_shadow_engine.vehicle_diagnostics.world_quality_lock_skips=23;
    queries=changing_reads=0;
    reference_apply_world_shadow_quality(null_queue?NULL:reference_queue,count);
    reference_queries=queries; expected=g_shadow_engine.world_quality;
    expected_token=g_shadow_engine.vehicle_diagnostics.selection_writer_active;
    expected_skips=g_shadow_engine.vehicle_diagnostics.world_quality_lock_skips;
    memcpy(identity_after,g_shadow_engine.vehicle_diagnostics.quality_records,sizeof(identity_after));
    memcpy(g_shadow_engine.vehicle_diagnostics.quality_records,identity_seed,sizeof(identity_seed));
    g_shadow_engine.world_quality=seed;
    g_shadow_engine.vehicle_diagnostics.world_quality_lock_skips=23;
    g_shadow_engine.vehicle_diagnostics.selection_writer_active=token;
    queries=changing_reads=0;
    frozen75_apply_world_shadow_quality(null_queue?NULL:candidate_queue,count);
    candidate_queries=queries;
    CHECK(!memcmp(reference_queue,candidate_queue,sizeof(input)));
    CHECK(!memcmp(&expected,&g_shadow_engine.world_quality,sizeof(expected)));
    CHECK(expected_token==g_shadow_engine.vehicle_diagnostics.selection_writer_active);
    CHECK(expected_skips==g_shadow_engine.vehicle_diagnostics.world_quality_lock_skips);
    CHECK(!memcmp(identity_after,g_shadow_engine.vehicle_diagnostics.quality_records,sizeof(identity_after)));
    ++cases;
}
static void compare_identity(uint64_t handle,LONG epoch,int expected)
{
    uint64_t old_owner=0xDEADBEEFULL,new_owner=old_owner;
    float old_distance=-123.5f,new_distance=old_distance;
    int old_result,new_result;
    memcpy(identity_seed,g_shadow_engine.vehicle_diagnostics.quality_records,sizeof(identity_seed));
    changing_reads=0;
    old_result=reference_lookup_vehicle_quality_identity(handle,epoch,&old_owner,&old_distance);
    memcpy(identity_after,g_shadow_engine.vehicle_diagnostics.quality_records,sizeof(identity_after));
    memcpy(g_shadow_engine.vehicle_diagnostics.quality_records,identity_seed,sizeof(identity_seed));
    changing_reads=0;
    new_result=lookup_vehicle_quality_identity(handle,epoch,&new_owner,&new_distance);
    CHECK(old_result==new_result && new_result==expected);
    CHECK(old_owner==new_owner && !memcmp(&old_distance,&new_distance,sizeof(float)));
    CHECK(!memcmp(identity_after,g_shadow_engine.vehicle_diagnostics.quality_records,sizeof(identity_after)));
    if(expected<=0) CHECK(new_owner==0xDEADBEEFULL && new_distance==-123.5f);
    if(!changing_tag) {
        CHECK(reference_lookup_vehicle_quality_identity(handle,epoch,NULL,NULL)==expected);
        CHECK(lookup_vehicle_quality_identity(handle,epoch,NULL,NULL)==expected);
    }
    ++identity_cases;
}
static void test_classification_and_gates(void)
{
    unsigned resolution,kind,dw,dh,identity;
    static const int types[]={-1,0,1,2,3,4,5};
    reset_inputs();
    for(resolution=1;resolution<=4;++resolution) {
        uint32_t cap=256U<<resolution;
        g_shadow_engine.policy_control.world_configuration=1L|((LONG)resolution<<SHADOW_WORLD_RESOLUTION_SHIFT);
        for(kind=0;kind<ARRAY_COUNT(types);++kind)
            for(dw=0;dw<3;++dw) for(dh=0;dh<3;++dh) for(identity=0;identity<5;++identity) {
                identity_clear();
                entry_payload(0,memory,memory+0x80U,types[kind],identity?17:UINT64_MAX,
                    dw==0?cap/2:(dw==1?cap:cap*2),dh==0?cap/2:(dh==1?cap:cap*2));
                if(identity>=2) identity_put(17,20,identity==4?0:42,5.0f,identity==3);
                compare_world(1,0);
            }
    }
    reset_inputs(); compare_world(0,0); CHECK(!reference_queries && !candidate_queries);
    compare_world(30,1); CHECK(!reference_queries && !candidate_queries);
    g_shadow_engine.policy_control.quality_ready=0;
    compare_world(30,0); CHECK(!reference_queries && !candidate_queries);
    g_shadow_engine.policy_control.quality_ready=1;
    g_shadow_engine.policy_control.world_configuration=0;
    compare_world(30,0); CHECK(!reference_queries && !candidate_queries);
    g_shadow_engine.policy_control.world_configuration=1;
    g_shadow_engine.vehicle_diagnostics.selection_writer_active=1;
    compare_world(30,0); CHECK(!reference_queries && !candidate_queries);
    CHECK(g_shadow_engine.vehicle_diagnostics.world_quality_lock_skips==24);
    reset_inputs(); compare_world(UINT32_MAX,0);
    CHECK(g_shadow_engine.world_quality.mutated_entries==41); /* Exactly30 entries. */
    *(void **)(queue_entry(0)+VEHICLE_QUEUE_CANDIDATE_OFFSET)=NULL; compare_world(1,0);
    CHECK(!reference_queries && !candidate_queries);
    entry_payload(0,NULL,memory+0x80U,3,UINT64_MAX,1024,1024); compare_world(1,0);
    CHECK(!reference_queries && !candidate_queries);
    entry_payload(0,memory,NULL,3,UINT64_MAX,1024,1024); compare_world(1,0);
    CHECK(!reference_queries && !candidate_queries);
    reset_inputs();
    for(kind=0;kind<30;++kind) {
        uint64_t handle=kind%5U ? 100U+kind:UINT64_MAX;
        entry_payload(kind,memory+kind*0x100U,memory+kind*0x100U+0x80U,
            types[kind%ARRAY_COUNT(types)],handle,1024U<<(kind%3U),1024U<<((kind/3U)%3U));
        if(kind%5U>=2U) identity_put(handle,20,42,4.0f,kind%5U==3U);
        if(kind%5U==4U) identity_put(handle,19,999,9.0f,0);
    }
    compare_world(30,0);
    puts("PASS world decisions: types -1..5, four caps, independent lower/equal/higher dimensions, sentinel/missing/exact/ambiguous/null owners, gates/token refusal, null pointers and count clamp; full queue bytes and counters identical.");
}
static void test_identity_banks(void)
{
    uint64_t handle=17,collision;
    unsigned bank,index;
    VehicleQualityRecord *r;
    reset_inputs(); compare_identity(handle,20,0); compare_identity(handle,-1,0);
    for(bank=0;bank<4;++bank) {
        identity_clear(); identity_put(handle,20-(LONG)bank,42,4.0f+(float)bank,0);
        compare_identity(handle,20,1);
    }
    for(bank=0;bank<4;++bank) identity_put(handle,20-(LONG)bank,42,4.0f+(float)bank,0);
    compare_identity(handle,20,1);
    {
        uint64_t owner=0; float distance=0;
        CHECK(lookup_vehicle_quality_identity(handle,20,&owner,&distance)==1 && owner==42 && distance==4.0f);
    }
    identity_put(handle,18,999,99.0f,0); compare_identity(handle,20,-1);
    identity_clear(); identity_put(handle,16,42,4.0f,0); compare_identity(handle,20,0);
    identity_clear(); identity_put(handle,21,42,4.0f,0); compare_identity(handle,20,0);
    identity_clear(); identity_put(handle,20,42,4.0f,1); compare_identity(handle,20,-1);
    identity_clear(); identity_put(handle,20,0,4.0f,0); compare_identity(handle,20,-1);
    for(collision=handle+1; (vehicle_selection_hash(collision,0)&511U)!=(vehicle_selection_hash(handle,0)&511U); ++collision) { }
    identity_clear(); identity_put(collision,20,99,9.0f,0); identity_put(handle,20,42,4.0f,0);
    compare_identity(handle,20,1); compare_identity(collision,20,1);
    identity_clear();
    for(bank=0;bank<4;++bank) for(index=0;index<VEHICLE_SELECTION_SLOTS_PER_EPOCH;++index) {
        r=&g_shadow_engine.vehicle_diagnostics.quality_records[bank][index];
        r->epoch_tag=(LONG)bank+17; r->descriptor_handle=100000U+index; r->vehicle=42;
    }
    compare_identity(handle,19,0); /* All four accepted banks are full and missing. */
    identity_clear(); r=identity_put(handle,20,42,4.0f,0);
    changing_tag=&r->epoch_tag; replacement_tag=0;
    compare_identity(handle,20,0);
    r->epoch_tag=21;
    entry_payload(0,memory,memory+0x80U,3,handle,4096,4096);
    compare_world(1,0); /* Same atomic replacement through the complete world pass. */
    CHECK(changing_reads==2 && g_shadow_engine.world_quality.mutated_entries==12);
    changing_tag=NULL;
    puts("PASS identity lookup: all four banks, collisions/full-bank misses, expiry/future tags, conflicting owners, newest distance, NULL outputs and tag replacement between atomic reads.");
}
static void test_memory_boundaries(void)
{
    MEMORY_BASIC_INFORMATION info;
    reset_inputs(); protect(memory,PAGE_BYTES,PAGE_NOACCESS); compare_world(1,0);
    CHECK(g_shadow_engine.world_quality.skipped_unreadable==20);
    reset_inputs(); protect(memory,PAGE_BYTES,PAGE_READWRITE|PAGE_GUARD); compare_world(1,0);
    CHECK(VirtualQuery(memory,&info,sizeof(info)) && (info.Protect&PAGE_GUARD));
    reset_inputs(); CHECK(VirtualFree(memory,PAGE_BYTES,MEM_DECOMMIT)); compare_world(1,0);
    CHECK(VirtualAlloc(memory,PAGE_BYTES,MEM_COMMIT,PAGE_READWRITE)==memory);
    reset_inputs(); entry_payload(0,memory+PAGE_BYTES-0x44U,memory+2U*PAGE_BYTES,3,UINT64_MAX,4096,1024);
    protect(memory+PAGE_BYTES,PAGE_BYTES,PAGE_READONLY); compare_world(1,0);
    CHECK(g_shadow_engine.world_quality.mutated_entries==12);
    reset_inputs(); entry_payload(0,memory+PAGE_BYTES-0x20U,memory+2U*PAGE_BYTES,3,UINT64_MAX,1024,1024);
    protect(memory+PAGE_BYTES,PAGE_BYTES,PAGE_READONLY); compare_world(1,0);
    CHECK(g_shadow_engine.world_quality.skipped_unreadable==20);
    reset_inputs(); entry_payload(0,memory+2U*PAGE_BYTES,memory+PAGE_BYTES-0x54U,3,UINT64_MAX,4096,4096);
    protect(memory+PAGE_BYTES,PAGE_BYTES,PAGE_READONLY); compare_world(1,0);
    CHECK(g_shadow_engine.world_quality.skipped_unreadable==20);
    reset_inputs(); entry_payload(0,memory+2U*PAGE_BYTES,memory+PAGE_BYTES-0x58U,3,UINT64_MAX,4096,4096);
    protect(memory+PAGE_BYTES,PAGE_BYTES,PAGE_READONLY); compare_world(1,0);
    CHECK(g_shadow_engine.world_quality.mutated_entries==12);
    reset_inputs(); fail_queries=1; compare_world(30,0);
    CHECK(reference_queries==30 && candidate_queries==30 && g_shadow_engine.world_quality.skipped_unreadable==49);
    fail_queries=0;
    reset_inputs(); compare_world(30,0); CHECK(candidate_queries<=2);
    protect(memory,PAGE_BYTES,PAGE_NOACCESS); compare_world(30,0);
    CHECK(g_shadow_engine.world_quality.skipped_unreadable==35); /* First16 payloads now unreadable. */
    protect(memory,PAGE_BYTES,PAGE_READWRITE); compare_world(30,0);
    CHECK(g_shadow_engine.world_quality.skipped_unreadable==19);
    reset_inputs();
    *(void **)(candidates[0]+8)=(void *)(UINTPTR_MAX-2U); compare_world(1,0);
    CHECK(!reference_queries && !candidate_queries && g_shadow_engine.world_quality.skipped_unreadable==20);
    puts("PASS memory validation: no-access, untouched guard, uncommitted, exact-end/straddling descriptor and handle spans, uncached query failure and fresh-pass permission invalidation.");
}
static void test_dense_and_fragmented_queries(void)
{
    unsigned i;
    reset_inputs(); compare_world(30,0);
    CHECK(reference_queries==60 && candidate_queries==1);
    printf("PASS dense30 query reduction: %u -> %u VirtualQuery calls; identical queue and counters.\n",reference_queries,candidate_queries);
    for(i=0;i<30;++i) entry_payload(i,memory+(29U-i)*0x100U,memory+(29U-i)*0x100U+0x80U,3,UINT64_MAX,4096,4096);
    /* VirtualQuery describes the queried page and following matching pages;
     * descending into the preceding page can require its own successful query. */
    compare_world(30,0); CHECK(reference_queries==60 && candidate_queries<=2);
    printf("PASS descending dense30: %u -> %u queries across two payload pages.\n",reference_queries,candidate_queries);
    reset_inputs();
    for(i=0;i<30;++i) entry_payload(i,memory+i*0x80U,memory+64U*PAGE_BYTES+i*0x80U,3,UINT64_MAX,4096,4096);
    protect(memory+64U*PAGE_BYTES,64U*PAGE_BYTES,PAGE_READONLY);
    compare_world(30,0); CHECK(reference_queries==60 && candidate_queries==2);
    printf("PASS two-region30 query reduction: %u -> %u VirtualQuery calls.\n",reference_queries,candidate_queries);
    reset_inputs();
    for(i=0;i<30;++i) entry_payload(i,memory+2U*i*PAGE_BYTES,memory+(2U*i+1U)*PAGE_BYTES,3,UINT64_MAX,4096,4096);
    for(i=0;i<60;++i) protect(memory+i*PAGE_BYTES,PAGE_BYTES,i&1U?PAGE_READONLY:PAGE_READWRITE);
    compare_world(30,0); CHECK(reference_queries==60 && candidate_queries==60);
    printf("PASS fragmented30 worst case: %u -> %u queries; identical queue/counters, no fabricated saving.\n",reference_queries,candidate_queries);
    for(i=0;i<30;++i) {
        *(void **)(candidates[i]+8)=memory+2U*(29U-i)*PAGE_BYTES;
        *(void **)(candidates[i]+0x10)=memory+(2U*(29U-i)+1U)*PAGE_BYTES;
    }
    compare_world(30,0); CHECK(reference_queries==60 && candidate_queries==60);
}
static double timed_world(int reference,unsigned loops)
{
    LARGE_INTEGER start,end,frequency;
    unsigned i;
    QueryPerformanceFrequency(&frequency); QueryPerformanceCounter(&start);
    for(i=0;i<loops;++i) {
        if(reference) reference_apply_world_shadow_quality(reference_queue,30);
        else frozen75_apply_world_shadow_quality(candidate_queue,30);
    }
    QueryPerformanceCounter(&end);
    return (double)(end.QuadPart-start.QuadPart)*1000000.0/(double)frequency.QuadPart/(double)loops;
}
static void report_timing(const char *label)
{
    unsigned round;
    double old_total=0,new_total=0;
    compare_world(30,0);
    timed_world(1,20); timed_world(0,20);
    for(round=0;round<6;++round) {
        if(round&1U) { new_total+=timed_world(0,200); old_total+=timed_world(1,200); }
        else { old_total+=timed_world(1,200); new_total+=timed_world(0,200); }
    }
    printf("Historical H5 %s CPU timing, 6 alternating rounds x200 calls including fresh cache initialization: frozen74=%.3fus frozen75=%.3fus; host-only microbenchmark, not game FPS.\n",label,old_total/6.0,new_total/6.0);
}
static void report_synthetic_timing(void)
{
    unsigned i;
    reset_inputs(); report_timing("dense30");
    reset_inputs();
    for(i=0;i<30;++i) entry_payload(i,memory+2U*i*PAGE_BYTES,memory+(2U*i+1U)*PAGE_BYTES,3,UINT64_MAX,4096,4096);
    for(i=0;i<60;++i) protect(memory+i*PAGE_BYTES,PAGE_BYTES,i&1U?PAGE_READONLY:PAGE_READWRITE);
    report_timing("fragmented30");
}
int main(void)
{
    SYSTEM_INFO info;
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);
    GetSystemInfo(&info); CHECK(info.dwPageSize==PAGE_BYTES);
    memory=VirtualAlloc(NULL,MEMORY_BYTES,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE); CHECK(memory!=NULL);
    test_classification_and_gates(); test_identity_banks(); test_memory_boundaries();
    test_dense_and_fragmented_queries(); report_synthetic_timing();
    CHECK(VirtualFree(memory,0,MEM_RELEASE));
    printf("PASS historical H5 frozen74-to75 comparison: %u world cases, %u identity cases, %u checks.\n",cases,identity_cases,checks);
    return 0;
}
