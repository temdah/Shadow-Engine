/* Actual read-only driver resolver over private allocated native-layout bytes.
 * ReadProcessMemory is real; the wrapper only counts calls and optionally changes
 * a fixture field after a successful copy to exercise revalidation races.
 * Static executable/profile proof is a separate gate, not simulated here. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
static unsigned reads,checks,mutation_reads,mutation_at,successful_lookup_reads,publication_id_reads;
static uint64_t read_signature;
static uintptr_t mutation_match;
static void *mutation_target;
static uint64_t mutation_value;
static size_t mutation_size;
static BOOL WINAPI fixture_read(HANDLE process,LPCVOID address,LPVOID output,
    SIZE_T size,SIZE_T *copied)
{
    BOOL ok=ReadProcessMemory(process,address,output,size,copied);
    ++reads;
    read_signature=(read_signature^(uint64_t)(uintptr_t)address)*1099511628211ULL;
    read_signature=(read_signature^(uint64_t)size)*1099511628211ULL;
    if(ok && (uintptr_t)address==mutation_match && ++mutation_reads==mutation_at)
        memcpy(mutation_target,&mutation_value,mutation_size);
    return ok;
}
#define ReadProcessMemory fixture_read
#include "../src/shadow_engine_patch.c"
#undef ReadProcessMemory
#define REQUIRE(value) do { assert(value); ++checks; } while(0)

#define PLAYER_ID 0x1357246800000042ULL
#define VEHICLE_ID 0x12345678ABCDEF01ULL
#define SECOND_VEHICLE_ID 0x87654321ABCDEF02ULL
typedef struct DriverFixture {
    unsigned char image[128],root[16],array[8],controller[16],data[24];
    unsigned char player_reference[16],pawn_reference[16],player_entity[0x900];
    unsigned char pawn_state[0x900],pawn[0xA0];
    unsigned char player_components[16],player_cache[0x50],cache_backing[24];
    unsigned char vehicle_reference[16],alternate_vehicle_reference[16],component_reference[16];
    unsigned char vehicle_entity[0x100],vehicle_components[16],vehicle_cache[0x50];
    unsigned char vehicle[0x220],second_vehicle[0x220],seats[3*0x90];
    uintptr_t root_pointer;
} DriverFixture;
static DriverIdentityProfile test_profile;
static RuntimeProfile test_runtime;

static void u32(void *where,uint32_t value) { memcpy(where,&value,4); }
static void u64(void *where,uint64_t value) { memcpy(where,&value,8); }
static void pointer(void *where,const void *value) { uintptr_t p=(uintptr_t)value; memcpy(where,&p,8); }

static void reset_fixture(DriverFixture *f)
{
    memset(f,0,sizeof(*f));
    mutation_match=0; mutation_target=NULL; mutation_reads=0; mutation_at=0;
    test_profile=g_global_driver_identity_profile;
    test_profile.pawn_class_id_rva=0x20; test_profile.vehicle_class_id_rva=0x24;
    test_runtime=g_runtime_profiles[0]; test_runtime.driver_identity=&test_profile;
    g_runtime_state.selected=&test_runtime; g_runtime_state.image_size=sizeof(f->image);
    g_shadow_engine.bootstrap.disrupt_base=f->image;
    g_shadow_engine.vehicle_diagnostics.driver_proof_ready=1;
    f->root_pointer=(uintptr_t)f->root;
    g_shadow_engine.vehicle_diagnostics.player_root_address=(unsigned char *)&f->root_pointer;
    u32(f->image+0x20,0x911DD85F); u32(f->image+0x24,0x8B611803);
    pointer(f->root,f->array); u32(f->root+8,1);
    pointer(f->array,f->controller); pointer(f->controller+8,f->data);
    pointer(f->data+16,f->player_reference);
    u64(f->player_reference,PLAYER_ID); pointer(f->player_reference+8,f->player_entity);
    memcpy(f->pawn_reference,f->player_reference,16);
    pointer(f->player_entity+0x68,f->player_components); u32(f->player_entity+0x70,1);
    pointer(f->player_entity+0x78,f->player_cache); pointer(f->player_components,f->pawn);
    u32(f->player_cache,0x911DD85F); u32(f->player_cache+4,0);
    pointer(f->pawn+0x10,f->player_reference);
    pointer(f->pawn+0x40,f->pawn_state); /* Realistic distinct state, not CEntity. */
    pointer(f->pawn+0x90,f->vehicle_reference);
    u64(f->vehicle_reference,VEHICLE_ID); pointer(f->vehicle_reference+8,f->vehicle_entity);
    memcpy(f->alternate_vehicle_reference,f->vehicle_reference,16);
    memcpy(f->component_reference,f->vehicle_reference,16);
    u32(f->vehicle_entity+0x60,0x10000); /* Native predicate accepts bit16, rejects bit3. */
    pointer(f->vehicle_entity+0x68,f->vehicle_components); u32(f->vehicle_entity+0x70,1);
    pointer(f->vehicle_entity+0x78,f->vehicle_cache); pointer(f->vehicle_components,f->vehicle);
    u32(f->vehicle_cache,0x8B611803); u32(f->vehicle_cache+4,0);
    pointer(f->vehicle+0x10,f->vehicle_reference);
    pointer(f->vehicle+0x1F8,f->seats); u32(f->vehicle+0x200,2);
    u32(f->seats+0x48,1); u64(f->seats+0x58,PLAYER_ID);
    u32(f->seats+0x90+0x48,2); u64(f->seats+0x90+0x58,UINT64_MAX);
}

static void expect_current(DriverFixture *f,int expected)
{
    DriverIdentity result;
    memset(&result,0xA5,sizeof(result));
    REQUIRE(copy_current_driver_identity(&result)==expected);
    if(expected) {
        REQUIRE(result.owner==(uint64_t)(uintptr_t)f->vehicle);
        REQUIRE(result.entity_id==VEHICLE_ID && result.player_id==PLAYER_ID);
    } else REQUIRE(!result.owner && !result.entity_id && !result.player_id);
}

static void test_identity_and_seats(DriverFixture *f)
{
    DriverIdentity result;
    DriverFixture before;
    unsigned start=reads;
    reset_fixture(f); expect_current(f,1);
    successful_lookup_reads=reads-start;
    before=*f; expect_current(f,1);
    start=reads; REQUIRE(copy_vehicle_entity_id(f->vehicle)==VEHICLE_ID);
    publication_id_reads=reads-start;
    REQUIRE(!memcmp(f,&before,sizeof(*f)));
    /* CPawn state is unrelated to its owning entity, even when absent. */
    pointer(f->pawn+0x40,NULL); expect_current(f,1);
    pointer(f->pawn+0x40,(void *)(uintptr_t)1); expect_current(f,1);
    u32(f->image+0x20,0); expect_current(f,0);
    REQUIRE(g_shadow_engine.vehicle_diagnostics.driver_proof_ready==1);
    u32(f->image+0x20,0x911DD85F); expect_current(f,1);
    u32(f->image+0x24,0); expect_current(f,0);
    u32(f->image+0x24,0x8B611803); expect_current(f,1);
    pointer(f->pawn+0x90,NULL); expect_current(f,0); /* Exit/on foot. */
    pointer(f->pawn+0x90,f->vehicle_reference); expect_current(f,1); /* Entry. */
    u64(f->vehicle_reference,UINT64_MAX); expect_current(f,0);
    reset_fixture(f); pointer(f->pawn+0x10,f->pawn_reference);
    pointer(f->vehicle+0x10,f->component_reference); expect_current(f,1); /* Nonaliased equivalent refs. */
    u64(f->pawn_reference,PLAYER_ID^0x100000000ULL); expect_current(f,0);
    reset_fixture(f); pointer(f->pawn+0x10,f->pawn_reference);
    pointer(f->pawn_reference+8,f->vehicle_entity); expect_current(f,0);
    reset_fixture(f); pointer(f->vehicle+0x10,f->component_reference);
    u64(f->component_reference,VEHICLE_ID^0x100000000ULL); expect_current(f,0);
    reset_fixture(f); pointer(f->vehicle+0x10,f->component_reference);
    pointer(f->component_reference+8,f->player_entity); expect_current(f,0);
    reset_fixture(f); u32(f->vehicle_entity+0x60,0); expect_current(f,0);
    u32(f->vehicle_entity+0x60,0x10008); expect_current(f,0);
    u32(f->vehicle_entity+0x60,0x50000); expect_current(f,1); /* Unrelated flag is allowed. */
    reset_fixture(f); u64(f->seats+0x58,PLAYER_ID^0x100000000ULL);
    u64(f->seats+0x90+0x58,PLAYER_ID); expect_current(f,0); /* Passenger, same low32. */
    reset_fixture(f); u64(f->seats+0x58,UINT64_MAX);
    u32(f->seats+0x90+0x48,1); u64(f->seats+0x90+0x58,PLAYER_ID);
    expect_current(f,0); /* Later driver role cannot override empty first. */
    reset_fixture(f); u32(f->seats+0x48,2); u32(f->seats+0x90+0x48,1);
    u64(f->seats+0x90+0x58,PLAYER_ID); expect_current(f,1);
    reset_fixture(f); u32(f->vehicle+0x200,65); expect_current(f,0);
    reset_fixture(f); memcpy(f->second_vehicle,f->vehicle,sizeof(f->vehicle));
    pointer(f->vehicle_components,f->second_vehicle);
    u64(f->alternate_vehicle_reference,SECOND_VEHICLE_ID);
    pointer(f->pawn+0x90,f->alternate_vehicle_reference);
    pointer(f->second_vehicle+0x10,f->alternate_vehicle_reference);
    REQUIRE(copy_current_driver_identity(&result));
    REQUIRE(result.owner==(uint64_t)(uintptr_t)f->second_vehicle && result.entity_id==SECOND_VEHICLE_ID);
    /* Address reuse under a different full generation must use the new ID. */
    pointer(f->vehicle_components,f->vehicle); pointer(f->vehicle+0x10,f->alternate_vehicle_reference);
    REQUIRE(copy_current_driver_identity(&result));
    REQUIRE(result.owner==(uint64_t)(uintptr_t)f->vehicle && result.entity_id==SECOND_VEHICLE_ID);
    g_shadow_engine.vehicle_diagnostics.driver_proof_ready=0;
    REQUIRE(!copy_current_driver_identity(&result) && !result.owner);
    REQUIRE(copy_vehicle_entity_id(f->vehicle)==UINT64_MAX);
    REQUIRE(!copy_current_driver_identity(NULL));
    reset_fixture(f); test_runtime.driver_identity=NULL; expect_current(f,0);
}

static void test_bounded_containers(DriverFixture *f)
{
    reset_fixture(f); pointer(f->player_entity+0x78,NULL); expect_current(f,0);
    reset_fixture(f); u32(f->player_cache+4,1); expect_current(f,0);
    reset_fixture(f); u32(f->player_entity+0x70,257); expect_current(f,0);
    reset_fixture(f); u32(f->player_cache,0x1234); expect_current(f,0);
    reset_fixture(f); u32(f->player_cache,0x1234);
    pointer(f->player_cache+0x40,f->cache_backing); u32(f->player_cache+0x48,2);
    u32(f->cache_backing,0x1234); u32(f->cache_backing+8,0x911DD85F);
    u32(f->cache_backing+12,0); expect_current(f,1);
    u32(f->player_cache+0x48,257); expect_current(f,0);
    reset_fixture(f); pointer(f->pawn+0x90,(void *)(uintptr_t)1); expect_current(f,0);
    reset_fixture(f); pointer(f->vehicle+0x1F8,(void *)(uintptr_t)1); expect_current(f,0);
    reset_fixture(f); u32(f->vehicle+0x200,0); expect_current(f,0);
    REQUIRE(sizeof(DriverSeatValue)==24U && offsetof(DriverSeatValue,occupant)==16U);
}

static void change_after_copy(uintptr_t match,void *target,uint64_t value,size_t size)
{
    mutation_match=match; mutation_target=target; mutation_value=value;
    mutation_size=size; mutation_reads=0; mutation_at=1;
}

static void test_rechecks_and_unreadable(DriverFixture *f)
{
    unsigned char *inaccessible=VirtualAlloc(NULL,4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    DWORD prior;
    uint64_t value;
    REQUIRE(inaccessible!=NULL);
    REQUIRE(VirtualProtect(inaccessible,4096,PAGE_NOACCESS,&prior));
    reset_fixture(f); pointer(f->player_entity+0x78,inaccessible); expect_current(f,0);
    reset_fixture(f); pointer(f->pawn+0x90,inaccessible); expect_current(f,0);
    REQUIRE(!driver_copy((uintptr_t)inaccessible,&value,8));
    REQUIRE(!driver_copy(UINTPTR_MAX-3U,&value,8));
    REQUIRE(!driver_copy(1,&value,8)); REQUIRE(!driver_copy((uintptr_t)f,&value,0));
    REQUIRE(VirtualProtect(inaccessible,4096,prior,&prior)); REQUIRE(VirtualFree(inaccessible,0,MEM_RELEASE));
    reset_fixture(f);
    change_after_copy((uintptr_t)f->player_reference,f->player_reference,PLAYER_ID+0x100000000ULL,8);
    expect_current(f,0); REQUIRE(mutation_reads>0);
    reset_fixture(f);
    change_after_copy((uintptr_t)f->player_cache,f->player_cache+4,1,4);
    expect_current(f,0); REQUIRE(mutation_reads>0);
    reset_fixture(f);
    change_after_copy((uintptr_t)f->player_components,f->player_components,0,8);
    expect_current(f,0); REQUIRE(mutation_reads>0);
    reset_fixture(f);
    change_after_copy((uintptr_t)f->pawn+0x90,f->pawn+0x90,0,8);
    expect_current(f,0); REQUIRE(mutation_reads>0);
    reset_fixture(f);
    change_after_copy((uintptr_t)f->vehicle_reference,f->vehicle_reference,SECOND_VEHICLE_ID,8);
    expect_current(f,0); REQUIRE(mutation_reads>0);
    reset_fixture(f);
    change_after_copy((uintptr_t)f->seats+0x48,f->seats+0x58,PLAYER_ID+0x100000000ULL,8);
    expect_current(f,0); REQUIRE(mutation_reads>0);
    reset_fixture(f);
    change_after_copy((uintptr_t)f->data+16,f->seats+0x58,PLAYER_ID+0x100000000ULL,8);
    mutation_at=2; expect_current(f,0); REQUIRE(mutation_reads==2);
    reset_fixture(f);
}

static void expect_trace(DriverFixture *f,int expected,unsigned stage,unsigned detail)
{
    DriverFixture before=*f,after;
    DriverIdentity plain,traced;
    DriverIdentityTrace trace;
    unsigned start=reads,plain_reads,initial_mutations=mutation_reads,plain_mutations;
    uint64_t plain_signature;
    int plain_result,traced_result;
    read_signature=1469598103934665603ULL;
    plain_result=copy_current_driver_identity(&plain);
    plain_reads=reads-start; plain_signature=read_signature;
    plain_mutations=mutation_reads; after=*f;
    *f=before; mutation_reads=initial_mutations;
    start=reads; read_signature=1469598103934665603ULL;
    memset(&trace,0xA5,sizeof(trace));
    traced_result=copy_current_driver_identity_traced(&traced,&trace);
    REQUIRE(plain_result==expected && traced_result==expected);
    REQUIRE(!memcmp(&plain,&traced,sizeof(plain)));
    REQUIRE(reads-start==plain_reads && read_signature==plain_signature);
    REQUIRE(mutation_reads==plain_mutations && !memcmp(f,&after,sizeof(after)));
    REQUIRE(trace.stage==stage && trace.detail==detail);
    REQUIRE(strcmp(driver_identity_stage_name(stage),"unknown"));
    REQUIRE(strcmp(driver_identity_detail_name(detail),"unknown"));
    if(expected) {
        REQUIRE(trace.player_id==PLAYER_ID && trace.vehicle_id==VEHICLE_ID);
        REQUIRE(trace.seat_player_id==PLAYER_ID && trace.owner==plain.owner);
    }
}

static void test_trace_diagnostics(DriverFixture *f)
{
    DriverIdentityTrace trace;
    reset_fixture(f); expect_trace(f,1,DRIVER_TRACE_SUCCESS,DRIVER_DETAIL_NONE);
    g_shadow_engine.vehicle_diagnostics.driver_proof_ready=0;
    expect_trace(f,0,DRIVER_TRACE_PROOF,DRIVER_DETAIL_MISSING);
    reset_fixture(f); u32(f->image+0x24,0);
    expect_trace(f,0,DRIVER_TRACE_METADATA,DRIVER_DETAIL_METADATA_MISMATCH);
    reset_fixture(f); pointer(f->controller+8,NULL);
    expect_trace(f,0,DRIVER_TRACE_PLAYER_CHAIN,DRIVER_DETAIL_MISSING);
    reset_fixture(f); u64(f->player_reference,UINT64_MAX);
    expect_trace(f,0,DRIVER_TRACE_PLAYER_REFERENCE,DRIVER_DETAIL_FULL_ID_INVALID);
    reset_fixture(f); pointer(f->player_entity+0x78,NULL);
    expect_trace(f,0,DRIVER_TRACE_PAWN_COMPONENT,DRIVER_DETAIL_MISSING_CACHE);
    reset_fixture(f); u32(f->player_cache,0x1234);
    expect_trace(f,0,DRIVER_TRACE_PAWN_COMPONENT,DRIVER_DETAIL_CACHE_CLASS_MISSING);
    reset_fixture(f); u32(f->player_cache+4,1);
    expect_trace(f,0,DRIVER_TRACE_PAWN_COMPONENT,DRIVER_DETAIL_CACHE_INDEX);
    reset_fixture(f); pointer(f->player_entity+0x78,(void *)(uintptr_t)1);
    expect_trace(f,0,DRIVER_TRACE_PAWN_COMPONENT,DRIVER_DETAIL_READ);
    reset_fixture(f); u32(f->player_entity+0x70,257);
    expect_trace(f,0,DRIVER_TRACE_PAWN_COMPONENT,DRIVER_DETAIL_COUNT_BOUND);
    reset_fixture(f); pointer(f->pawn+0x10,f->pawn_reference); u64(f->pawn_reference,PLAYER_ID^0x100000000ULL);
    expect_trace(f,0,DRIVER_TRACE_PAWN_REFERENCE,DRIVER_DETAIL_ID_MISMATCH);
    reset_fixture(f); pointer(f->pawn+0x90,NULL);
    expect_trace(f,0,DRIVER_TRACE_VEHICLE_REFERENCE,DRIVER_DETAIL_MISSING);
    reset_fixture(f); u64(f->vehicle_reference,UINT64_MAX);
    expect_trace(f,0,DRIVER_TRACE_VEHICLE_REFERENCE,DRIVER_DETAIL_FULL_ID_INVALID);
    reset_fixture(f); u32(f->vehicle_entity+0x60,0x10008);
    expect_trace(f,0,DRIVER_TRACE_VEHICLE_FLAGS,DRIVER_DETAIL_ENTITY_FLAGS);
    reset_fixture(f); pointer(f->vehicle_entity+0x78,NULL);
    expect_trace(f,0,DRIVER_TRACE_VEHICLE_COMPONENT,DRIVER_DETAIL_MISSING_CACHE);
    reset_fixture(f); pointer(f->vehicle+0x10,f->player_reference);
    expect_trace(f,0,DRIVER_TRACE_COMPONENT_REFERENCE,DRIVER_DETAIL_ID_MISMATCH);
    reset_fixture(f); u64(f->seats+0x58,PLAYER_ID^0x100000000ULL);
    expect_trace(f,0,DRIVER_TRACE_DRIVER_SEAT,DRIVER_DETAIL_SEAT_OCCUPANT_MISMATCH);
    reset_fixture(f); u32(f->seats+0x48,2);
    expect_trace(f,0,DRIVER_TRACE_DRIVER_SEAT,DRIVER_DETAIL_SEAT_NOT_FOUND);
    reset_fixture(f);
    change_after_copy((uintptr_t)f->player_components,f->player_components,0,8);
    expect_trace(f,0,DRIVER_TRACE_PAWN_COMPONENT,DRIVER_DETAIL_READ_OR_CHANGED);
    reset_fixture(f);
    change_after_copy((uintptr_t)f->pawn+0x90,f->pawn+0x90,0,8);
    expect_trace(f,0,DRIVER_TRACE_RECHECK,DRIVER_DETAIL_READ_OR_CHANGED);
    reset_fixture(f);
    change_after_copy((uintptr_t)f->vehicle_reference,f->vehicle_reference,SECOND_VEHICLE_ID,8);
    expect_trace(f,0,DRIVER_TRACE_RECHECK,DRIVER_DETAIL_READ_OR_CHANGED);
    reset_fixture(f);
    change_after_copy((uintptr_t)f->vehicle_entity+0x60,f->vehicle_entity+0x60,0,4);
    expect_trace(f,0,DRIVER_TRACE_RECHECK,DRIVER_DETAIL_READ_OR_CHANGED);
    reset_fixture(f);
    change_after_copy((uintptr_t)f->seats+0x48,f->seats+0x58,PLAYER_ID+0x100000000ULL,8);
    expect_trace(f,0,DRIVER_TRACE_DRIVER_SEAT,DRIVER_DETAIL_READ_OR_CHANGED);
    reset_fixture(f);
    change_after_copy((uintptr_t)f->data+16,f->seats+0x58,PLAYER_ID+0x100000000ULL,8);
    mutation_at=2;
    expect_trace(f,0,DRIVER_TRACE_RECHECK,DRIVER_DETAIL_SEAT_OCCUPANT_MISMATCH);
    reset_fixture(f); pointer(f->pawn+0x10,f->pawn_reference);
    change_after_copy((uintptr_t)f->pawn_reference,f->pawn_reference,PLAYER_ID+0x100000000ULL,8);
    expect_trace(f,0,DRIVER_TRACE_RECHECK,DRIVER_DETAIL_READ_OR_CHANGED);
    reset_fixture(f); pointer(f->vehicle+0x10,f->component_reference);
    change_after_copy((uintptr_t)f->component_reference,f->component_reference,SECOND_VEHICLE_ID,8);
    expect_trace(f,0,DRIVER_TRACE_RECHECK,DRIVER_DETAIL_READ_OR_CHANGED);
    REQUIRE(!copy_current_driver_identity_traced(NULL,&trace));
    REQUIRE(trace.stage==DRIVER_TRACE_NONE && trace.detail==DRIVER_DETAIL_INVALID_OUTPUT);
    REQUIRE(!strcmp(driver_identity_stage_name(999),"unknown"));
    REQUIRE(!strcmp(driver_identity_detail_name(999),"unknown"));
    reset_fixture(f);
}

typedef struct DriverQueueFixture {
    unsigned char reference[16],entity[0x100];
    unsigned char components[2][VEHICLE_BATCH_ELEMENT_BYTES];
    unsigned char lights[2][VEHICLE_OWNER_LIGHT_BYTES];
    unsigned char nodes[2][0x20],descriptors[2][0x44],spatials[2][0x60];
    unsigned char queue[RENDER_QUEUE_ALLOCATION_BYTES];
    unsigned char batch_target[0x200],skeleton[8],world[16];
    VehicleQueueSample sample;
    VehicleLimiterChain limiter;
    DriverFixture *driver;
    unsigned batch_calls;
} DriverQueueFixture;
static DriverQueueFixture *active_queue_fixture;

/* Only the native batch body and its update return carrier are simulated.
 * The real wrapper, TLS scope, post-update observer, full-ID copy, publisher,
 * driver resolver, limiter and sampled quality path operate on allocated fixture bytes.
 * This neither installs a hook nor proves the executable's call boundary. */
static void __fastcall fixture_native_batch(void *vehicle,float delta,
    void *skeleton,void *world,float distance_squared)
{
    DriverQueueFixture *q=active_queue_fixture;
    VehicleLightDiagnosticState *d=&g_shadow_engine.vehicle_diagnostics;
    VehicleBatchScope *scope=(VehicleBatchScope *)TlsGetValue(d->batch_tls);
    unsigned index=vehicle==q->driver->vehicle?0U:1U;
    REQUIRE(vehicle==(index?q->driver->second_vehicle:q->driver->vehicle));
    REQUIRE(delta==0.0125f && skeleton==q->skeleton && world==q->world);
    REQUIRE(distance_squared==16.0f+(float)index);
    REQUIRE(scope && scope->vehicle==vehicle && scope->pending_count==0U);
    observe_vehicle_selection(q->components[index],skeleton,world,
        (uintptr_t)d->light_batch_target+VEHICLE_BATCH_RETURN_OFFSET);
    REQUIRE(scope->pending_count==1U && !scope->pending_overflow);
    ++q->batch_calls;
}

static void test_batch_driver_quality(DriverFixture *f)
{
    enum { MATCHED, MISSING_DRIVER_CACHE, MISSING_BATCH_ID };
    const uint64_t handles[]={0x800000010ULL,0x900000020ULL};
    DriverQueueFixture *q=VirtualAlloc(NULL,sizeof(*q),MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    VehicleLightDiagnosticState *d=&g_shadow_engine.vehicle_diagnostics;
    unsigned scenario,type,index,start;
    REQUIRE(q!=NULL);
    for(type=1U;type<=3U;type+=2U) for(scenario=MATCHED;scenario<=MISSING_BATCH_ID;++scenario) {
        memset(d,0,sizeof(*d)); reset_fixture(f);
        memset(q,0,sizeof(*q)); q->driver=f; active_queue_fixture=q;
        d->selection_enabled=1; d->batch_enabled=1;
        d->batch_tls=TlsAlloc(); REQUIRE(d->batch_tls!=TLS_OUT_OF_INDEXES);
        d->batch_tls_valid=1; d->light_batch_target=q->batch_target;
        d->original_light_batch=fixture_native_batch;
        g_shadow_engine.renderer.renderer_calls=20;
        g_shadow_engine.renderer.manager_calls=30;
        g_shadow_engine.policy_control.disabled=(4L<<SHADOW_POLICY_LIMIT_SHIFT)|
            (1L<<SHADOW_POLICY_RESOLUTION_SHIFT);
        g_shadow_engine.policy_control.quality_ready=1;
        REQUIRE(shadow_policy_resolution()==512U);
        u64(q->reference,SECOND_VEHICLE_ID); pointer(q->reference+8,q->entity);
        pointer(f->second_vehicle+DRIVER_REFERENCE_OFFSET,q->reference);
        if(scenario==MISSING_BATCH_ID) pointer(f->vehicle+DRIVER_REFERENCE_OFFSET,NULL);
        for(index=0;index<2U;++index) {
            unsigned char *owner=index?f->second_vehicle:f->vehicle;
            unsigned char *entry=q->queue+(size_t)index*RENDER_QUEUE_ENTRY_BYTES;
            uint64_t published_owner=0;
            unsigned slot,published_matches=0;
            pointer(owner+VEHICLE_BATCH_ARRAY_OFFSET,q->components[index]);
            u32(owner+VEHICLE_BATCH_COUNT_OFFSET,1U);
            pointer(q->components[index]+VEHICLE_OWNER_LIGHT_OFFSET,q->lights[index]);
            u64(q->lights[index]+VEHICLE_OWNER_DESCRIPTOR_HANDLE_OFFSET,handles[index]);
            hooked_vehicle_light_batch(owner,0.0125f,q->skeleton,q->world,16.0f+(float)index);
            REQUIRE(TlsGetValue(d->batch_tls)==NULL);
            REQUIRE(vehicle_selection_lock());
            REQUIRE(lookup_vehicle_quality_identity(handles[index],20,&published_owner,NULL)==1);
            REQUIRE(published_owner==(uint64_t)(uintptr_t)owner);
            for(slot=0;slot<VEHICLE_SELECTION_SLOTS_PER_EPOCH;++slot) {
                const VehicleSelectionRecord *record=&d->selection_records[
                    20U&(VEHICLE_SELECTION_EPOCH_BANKS-1U)][slot];
                if(!record->observations || record->publish_renderer_epoch!=20 ||
                   record->descriptor_handle!=handles[index]) continue;
                REQUIRE(record->vehicle==(uint64_t)(uintptr_t)owner);
                REQUIRE(record->vehicle_entity_id==(index?SECOND_VEHICLE_ID:
                    (scenario==MISSING_BATCH_ID?UINT64_MAX:VEHICLE_ID)));
                ++published_matches;
            }
            REQUIRE(published_matches==1U);
            vehicle_selection_unlock();
            pointer(q->nodes[index]+8,q->descriptors[index]);
            pointer(q->nodes[index],index?NULL:q->nodes[1]);
            pointer(q->nodes[index]+16,q->spatials[index]);
            u32(q->descriptors[index]+8,type);
            u64(q->spatials[index]+VEHICLE_SPATIAL_DESCRIPTOR_HANDLE_OFFSET,handles[index]);
            pointer(entry+VEHICLE_QUEUE_CANDIDATE_OFFSET,q->nodes[index]);
            u32(entry+VEHICLE_QUEUE_WIDTH_OFFSET,4096U);
            u32(entry+VEHICLE_QUEUE_HEIGHT_OFFSET,4096U);
        }
        REQUIRE(q->batch_calls==2U);
        REQUIRE(d->selection_bank_count[20U&(VEHICLE_SELECTION_EPOCH_BANKS-1U)]==2U);
        /* Neither a missing publication ID nor an unavailable driver component
         * changes the cap on an otherwise exact vehicle handle. */
        pointer(f->vehicle+DRIVER_REFERENCE_OFFSET,f->vehicle_reference);
        if(scenario==MISSING_DRIVER_CACHE) pointer(f->vehicle_entity+0x78,NULL);
        q->sample.copied_entries=2U;
        start=reads;
        apply_vehicle_shadow_quality_with_sample(q->queue,2U,&q->sample);
        REQUIRE(reads==start); /* Includes every real RPM call in the unity build. */
        REQUIRE(q->sample.quality_evaluated && !q->sample.quality_lock_skipped);
        REQUIRE(q->sample.records[0].quality_owner==(uint64_t)(uintptr_t)f->vehicle);
        REQUIRE(q->sample.records[1].quality_owner==(uint64_t)(uintptr_t)f->second_vehicle);
        for(index=0;index<2U;++index) {
            unsigned char *entry=q->queue+(size_t)index*RENDER_QUEUE_ENTRY_BYTES;
            REQUIRE(*(uint32_t *)(entry+VEHICLE_QUEUE_WIDTH_OFFSET)==512U);
            REQUIRE(*(uint32_t *)(entry+VEHICLE_QUEUE_HEIGHT_OFFSET)==512U);
            REQUIRE(q->sample.records[index].quality_decision==QUALITY_DECISION_CAPPED);
        }
        if(type==3U && scenario==MATCHED) {
            start=reads;
            prepare_vehicle_limiter_chain(&q->limiter,q->nodes[0],2U,31,0);
            REQUIRE(reads>start && q->limiter.sample.driver_valid);
            REQUIRE(q->limiter.sample.driver_owner==(uint64_t)(uintptr_t)f->vehicle);
            REQUIRE(q->limiter.sample.driver_entity_id==VEHICLE_ID);
            REQUIRE(q->limiter.sample.driver_headlight_lights==1U);
            REQUIRE(q->limiter.sample.records[0].selection_driver);
            REQUIRE(q->limiter.sample.records[0].selection_would_keep);
            REQUIRE(q->limiter.sample.records[0].selection_role==VEHICLE_SELECTION_DRIVER_ROLE);
            REQUIRE(!q->limiter.sample.records[1].selection_driver);
            restore_vehicle_limiter_chain(&q->limiter);
            REQUIRE(*(void **)q->nodes[0]==q->nodes[1] && !*(void **)q->nodes[1]);
        }
        REQUIRE(TlsFree(d->batch_tls)); d->batch_tls_valid=0;
    }
    active_queue_fixture=NULL;
    memset(d,0,sizeof(*d)); reset_fixture(f);
    REQUIRE(VirtualFree(q,0,MEM_RELEASE));
}

static void benchmark_driver_reads(DriverFixture *f)
{
    LARGE_INTEGER begin,end,frequency;
    DriverIdentity result;
    char enabled[4];
    unsigned kind,iteration;
    volatile uint64_t observed=0;
    const unsigned warmup=1000,iterations=10000;
    const char *names[]={"current_driver","publication_id","proof_disabled"};
    if(GetEnvironmentVariableA("SHADOW_ENGINE_DRIVER_BENCHMARK",enabled,sizeof(enabled))!=1U ||
       enabled[0]!='1') return;
    assert(QueryPerformanceFrequency(&frequency));
    reset_fixture(f);
    for(kind=0;kind<3U;++kind) {
        g_shadow_engine.vehicle_diagnostics.driver_proof_ready=kind==2U?0:1;
        for(iteration=0;iteration<warmup+iterations;++iteration) {
            if(iteration==warmup) assert(QueryPerformanceCounter(&begin));
            if(kind==1U) observed^=copy_vehicle_entity_id(f->vehicle);
            else observed+=(unsigned)copy_current_driver_identity(&result);
        }
        assert(QueryPerformanceCounter(&end));
        printf("SYNTHETIC driver reader: path=%s iterations=%u warmup=%u mean_us=%.6f diagnostics=%d; private fixture, counted real RPM, not game cost or FPS\n",
            names[kind],iterations,warmup,
            (end.QuadPart-begin.QuadPart)*1000000.0/(frequency.QuadPart*iterations),
            SHADOW_ENGINE_INTERNAL_DIAGNOSTICS);
    }
    assert(observed==warmup+iterations);
}

int main(void)
{
    DriverFixture *fixture=VirtualAlloc(NULL,sizeof(*fixture),MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    REQUIRE(fixture!=NULL);
    test_identity_and_seats(fixture);
    test_bounded_containers(fixture); test_rechecks_and_unreadable(fixture);
    test_trace_diagnostics(fixture);
    test_batch_driver_quality(fixture);
    {
        unsigned verification_reads=reads;
        benchmark_driver_reads(fixture);
        reads=verification_reads; /* Printed verification count excludes timing. */
    }
    REQUIRE(VirtualFree(fixture,0,MEM_RELEASE));
    printf("PASS driver identity: %u checks, %u real self-process reads, diagnostics=%d, successfulLookupReads=%u, publicationIdReads=%u; full IDs/direct reference/flags/seats/cache bounds/races; actual batch publication + sampled quality with zero RPM + fresh limiter role5 (native batch body stubbed); static profile and game acceptance separate\n",
        checks,reads,SHADOW_ENGINE_INTERNAL_DIAGNOSTICS,successful_lookup_reads,publication_id_reads);
    return 0;
}
