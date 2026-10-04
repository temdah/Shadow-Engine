/* Section 3 / Build A: current publisher versus frozen .77 working files.
 * Synthetic scalar registries only. No hooks, native pointers, game, workers,
 * filesystem writes or new production instrumentation. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <assert.h>
static unsigned checks,cases,tag_atomics,other_atomics,lock_attempts;
static unsigned total_old_tags,total_new_tags;
static uintptr_t quality_begin,quality_end;
static volatile LONG *registry_token;
static void count_atomic(volatile LONG *address)
{
    uintptr_t value=(uintptr_t)address;
    if(value>=quality_begin && value<quality_end) {
        assert(registry_token && *registry_token==1);
        ++tag_atomics;
    } else ++other_atomics;
}
static LONG WINAPI fixture_compare(volatile LONG *address,LONG exchange,LONG comparand)
{
    count_atomic(address);
    if(address==registry_token && exchange==1 && comparand==0) ++lock_attempts;
    return InterlockedCompareExchange(address,exchange,comparand);
}
static LONG WINAPI fixture_exchange(volatile LONG *address,LONG value)
{
    count_atomic(address);
    return InterlockedExchange(address,value);
}
#undef InterlockedCompareExchange
#undef InterlockedExchange
#define InterlockedCompareExchange fixture_compare
#define InterlockedExchange fixture_exchange
#include "../src/shadow_engine_patch.c"
#include "reference/quality_publisher_v77.inc"
#undef InterlockedCompareExchange
#undef InterlockedExchange
#define CHECK(value) do { ++checks; assert(value); } while(0)
static VehicleLightDiagnosticState seed,expected;

static void reset(void)
{
    VehicleLightDiagnosticState *d=&g_shadow_engine.vehicle_diagnostics;
    memset(d,0,sizeof(*d));
    d->selection_enabled=1;
    g_shadow_engine.renderer.renderer_calls=20;
    g_shadow_engine.renderer.manager_calls=37;
    g_shadow_engine.policy_control.disabled=SHADOW_POLICY_DISABLE_LIMITER;
    registry_token=&d->selection_writer_active;
    quality_begin=(uintptr_t)d->quality_records;
    quality_end=quality_begin+sizeof(d->quality_records);
}
static void scope_fill(VehicleBatchScope *scope,unsigned count,uint64_t first,uint64_t owner)
{
    unsigned i;
    memset(scope,0,sizeof(*scope));
    assert(count<=VEHICLE_BATCH_PUBLICATION_MAX);
    scope->pending_count=count;
    for(i=0;i<count;++i) {
        VehicleSelectionRecord *r=&scope->pending[i];
        r->descriptor_handle=first+i;
        r->vehicle=owner;
        r->distance_squared=4.0f+(float)i;
        r->position[0]=(float)i; r->position[1]=-3.5f; r->position[2]=12.0f;
        r->position_valid=i&1U;
        r->tick=0xFFFFFFF0U+i;
        r->observations=1;
    }
}
static void lookup_compare(uint64_t handle,LONG epoch)
{
    uint64_t old_owner=UINT64_MAX,new_owner=UINT64_MAX;
    float old_distance=-123.0f,new_distance=-123.0f;
    int old_result,new_result;
    CHECK(vehicle_selection_lock());
    old_result=frozen77_lookup_vehicle_quality_identity(handle,epoch,&old_owner,&old_distance);
    new_result=lookup_vehicle_quality_identity(handle,epoch,&new_owner,&new_distance);
    CHECK(old_result==new_result && old_owner==new_owner);
    CHECK(!memcmp(&old_distance,&new_distance,sizeof(old_distance)));
    vehicle_selection_unlock();
}
static unsigned compare_batch(const VehicleBatchScope *scope)
{
    VehicleLightDiagnosticState *d=&g_shadow_engine.vehicle_diagnostics;
    unsigned old_tags,old_other,old_locks,i;
    memcpy(&seed,d,sizeof(seed));
    tag_atomics=other_atomics=lock_attempts=0;
    frozen77_publish_vehicle_selection_batch(scope);
    old_tags=tag_atomics; old_other=other_atomics; old_locks=lock_attempts;
    memcpy(&expected,d,sizeof(expected));
    memcpy(d,&seed,sizeof(seed));
    tag_atomics=other_atomics=lock_attempts=0;
    publish_vehicle_selection_batch(scope);
    CHECK(!memcmp(&expected,d,sizeof(expected)));
    CHECK(tag_atomics==0);
    CHECK(other_atomics==old_other && lock_attempts==old_locks);
    CHECK(d->selection_writer_active==seed.selection_writer_active);
    total_old_tags+=old_tags; total_new_tags+=tag_atomics;
    if(!d->selection_writer_active) {
        for(i=0;i<scope->pending_count;++i) {
            lookup_compare(scope->pending[i].descriptor_handle,g_shadow_engine.renderer.renderer_calls);
            lookup_compare(scope->pending[i].descriptor_handle,g_shadow_engine.renderer.renderer_calls+4);
        }
        lookup_compare(0xFEED12345678ULL,g_shadow_engine.renderer.renderer_calls);
        CHECK(!memcmp(&expected,d,sizeof(expected)));
    }
    ++cases;
    return old_tags;
}
static void basic_cases(void)
{
    VehicleBatchScope scope;
    VehicleLightDiagnosticState *d=&g_shadow_engine.vehicle_diagnostics;
    unsigned i;
    reset(); scope_fill(&scope,2,0x100000011ULL,0xA000);
    CHECK(!shadow_policy_enabled(SHADOW_POLICY_DISABLE_LIMITER));
    CHECK(compare_batch(&scope)==6);
    CHECK(d->selection_bank_count[0]==2);
    scope.pending[0].distance_squared=256.0f;
    scope.pending[0].tick=0x00000010U;
    scope.pending[1].position_valid=1;
    CHECK(compare_batch(&scope)==6);
    for(i=0;i<2;++i) scope.pending[i].vehicle=0xB000;
    CHECK(compare_batch(&scope)==6);
    /* Reobserving the original owner cannot clear same-epoch ambiguity. */
    for(i=0;i<2;++i) scope.pending[i].vehicle=0xA000;
    compare_batch(&scope);
    CHECK(vehicle_selection_lock());
    CHECK(lookup_vehicle_quality_identity(scope.pending[0].descriptor_handle,20,NULL,NULL)==-1);
    vehicle_selection_unlock();
    /* Four live banks and then bank reuse; differing owners remain conflicts. */
    for(i=21;i<=29;++i) {
        g_shadow_engine.renderer.renderer_calls=(LONG)i;
        scope.pending[0].vehicle=0xA000+i;
        compare_batch(&scope);
    }
    reset(); scope_fill(&scope,64,0x200000000ULL,0x100);
    for(i=0;i<64;++i) scope.pending[i].descriptor_handle=scope.pending[0].descriptor_handle;
    CHECK(compare_batch(&scope)==192);
    CHECK(d->selection_bank_count[0]==1);
    reset(); scope_fill(&scope,1,UINT64_MAX,0);
    compare_batch(&scope);
    CHECK(vehicle_selection_lock());
    CHECK(lookup_vehicle_quality_identity(UINT64_MAX,20,NULL,NULL)==-1);
    vehicle_selection_unlock();
}
static void collision_and_capacity_cases(void)
{
    VehicleBatchScope scope;
    VehicleLightDiagnosticState *d=&g_shadow_engine.vehicle_diagnostics;
    unsigned i,n=0;
    uint64_t handle;
    reset(); scope_fill(&scope,64,0,0x123);
    for(handle=1;n<64;++handle)
        if((frozen77_vehicle_selection_hash(handle,0)&511U)==511U)
            scope.pending[n++].descriptor_handle=handle;
    CHECK(compare_batch(&scope)==2208); /* 1+...+64 probes plus two stores each. */
    compare_batch(&scope);
    scope.pending[63].vehicle=0x456;
    compare_batch(&scope);
    reset();
    for(i=0;i<8;++i) {
        scope_fill(&scope,64,1U+i*64U,0x123);
        compare_batch(&scope);
    }
    CHECK(d->selection_bank_count[0]==512);
    scope_fill(&scope,1,9000,0x456);
    CHECK(compare_batch(&scope)==0);
    CHECK(d->selection_overflow==1);
    /* Capacity reservation remains conservative even for an existing record. */
    scope_fill(&scope,1,1,0x123);
    CHECK(compare_batch(&scope)==0);
    CHECK(d->selection_overflow==2);
    g_shadow_engine.renderer.renderer_calls=24;
    CHECK(compare_batch(&scope)==3);
    CHECK(d->selection_bank_count[0]==1);
    /* Quality-table exhaustion is independently reachable for direct helper
     * testing; the whole-batch reservation normally prevents this state. */
    reset();
    for(i=0;i<512;++i) {
        VehicleQualityRecord *r=&d->quality_records[0][i];
        r->epoch_tag=21; r->descriptor_handle=i+1U; r->vehicle=0x123;
    }
    memcpy(&seed,d,sizeof(seed));
    CHECK(vehicle_selection_lock()); tag_atomics=0;
    frozen77_publish_vehicle_quality_identity(0,20,9000,0x456,1.0f);
    CHECK(tag_atomics==512); vehicle_selection_unlock();
    memcpy(&expected,d,sizeof(expected)); memcpy(d,&seed,sizeof(seed));
    CHECK(vehicle_selection_lock()); tag_atomics=0;
    publish_vehicle_quality_identity(0,20,9000,0x456,1.0f);
    CHECK(tag_atomics==0); vehicle_selection_unlock();
    CHECK(!memcmp(&expected,d,sizeof(expected)) && d->quality_overflow==1);
    ++cases;
}
static void refusal_cases(void)
{
    VehicleBatchScope scope;
    VehicleLightDiagnosticState *d=&g_shadow_engine.vehicle_diagnostics;
    reset(); scope_fill(&scope,0,1,2); CHECK(compare_batch(&scope)==0);
    scope_fill(&scope,2,1,2); d->selection_enabled=0;
    CHECK(compare_batch(&scope)==0 && !d->selection_gaps);
    d->selection_enabled=1; d->selection_writer_active=1;
    CHECK(compare_batch(&scope)==0);
    CHECK(d->selection_gaps==1 && d->selection_publish_inflight==0);
    CHECK(d->selection_gap_epoch_tag[0]==21);
    d->selection_writer_active=0; scope.pending_overflow=1;
    CHECK(compare_batch(&scope)==0 && d->selection_overflow==1);
    CHECK(d->selection_bank_count[0]==0 && d->selection_publish_inflight==0);
    scope.pending_overflow=0;
    compare_batch(&scope);
    /* An already active publisher is preserved around this nested counter use. */
    d->selection_publish_inflight=1;
    compare_batch(&scope);
    CHECK(d->selection_publish_inflight==1);
    reset(); g_shadow_engine.renderer.renderer_calls=0;
    compare_batch(&scope);
}
int main(void)
{
    basic_cases(); collision_and_capacity_cases(); refusal_cases();
    printf("publication bookkeeping: %u cases, %u checks passed; batch quality-tag atomics %u -> %u; outer token and full state preserved (diagnostics=%d).\n",
        cases,checks,total_old_tags,total_new_tags,SHADOW_ENGINE_INTERNAL_DIAGNOSTICS);
    return 0;
}
