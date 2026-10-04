/* Actual policy modules with only the native identity provider substituted.
 * The native provider has a separate proof/reader harness. Synthetic owned
 * candidates and queues; no hooks, workers, installation or game calls. */
#include "../src/modules/00_shared_config_state.inc"
#include "../src/modules/05_runtime_profiles.inc"
#include "../src/modules/10_runtime_primitives.inc"
#if SHADOW_ENGINE_OVERHEAD_MEASUREMENT
#include "../src/modules/12_overhead_measurement.inc"
#endif
#if SHADOW_ENGINE_INTERNAL_DIAGNOSTICS
#include "../src/modules/14_periodic_summary.inc"
#endif
#include "../src/modules/15_patch_transaction.inc"
#include "../src/modules/16_policy_controls.inc"
#include "../src/modules/17_saved_settings.inc"
#if SHADOW_ENGINE_INTERNAL_DIAGNOSTICS
#include "../src/modules/17_intersection_diagnostics.inc"
#endif
#include <assert.h>
typedef struct DriverIdentity { uint64_t owner,entity_id,player_id; } DriverIdentity;
static DriverIdentity fixture_driver;
static unsigned driver_calls,checks;
static int driver_available;
static uint64_t batch_entity_id=UINT64_MAX;
static void (*during_driver_read)(void);
static int copy_current_driver_identity(DriverIdentity *out)
{
    ++driver_calls;
    assert(!g_shadow_engine.vehicle_diagnostics.selection_writer_active);
    if(during_driver_read) during_driver_read();
    if(!driver_available) return 0;
    *out=fixture_driver; return 1;
}
static int copy_current_driver_identity_traced(DriverIdentity *out,DriverIdentityTrace *trace)
{
    int valid=copy_current_driver_identity(out);
    memset(trace,0,sizeof(*trace));
    trace->stage=valid?DRIVER_TRACE_SUCCESS:DRIVER_TRACE_PAWN_COMPONENT;
    trace->detail=valid?DRIVER_DETAIL_NONE:DRIVER_DETAIL_MISSING_CACHE;
    if(valid) {
        trace->owner=out->owner; trace->vehicle_id=out->entity_id;
        trace->player_id=trace->seat_player_id=out->player_id;
    }
    return valid;
}
static uint64_t copy_vehicle_entity_id(void *vehicle)
{ (void)vehicle; return batch_entity_id; }
static void initialize_vehicle_driver_identity(void) { }
#include "../src/modules/18_vehicle_owner_lifetime.inc"
#include "../src/modules/18_shadow_resolution_diagnostics.inc"
#include "../src/modules/18_vehicle_light_diagnostics.inc"
#include "../src/modules/19_world_light_quality.inc"
#if SHADOW_ENGINE_INTERNAL_DIAGNOSTICS
#include "../src/modules/19_external_completion_diagnostics.inc"
#include "../src/modules/19_workload_present.inc"
#include "../src/modules/19_workload_capture.inc"
#include "../src/modules/19_population_capture.inc"
#endif
#include "../src/modules/20_manager_owner_profile.inc"
#include "../src/modules/30_renderer_queue_diagnostics.inc"
#include "../src/modules/40_external_slice_results.inc"
#include "../src/modules/60_engine_expansion.inc"
#include "../src/modules/65_runtime_preflight.inc"
#if SHADOW_ENGINE_INTERNAL_DIAGNOSTICS
#include "../src/modules/66_lua_frame_compatibility.inc"
#endif
#include "../src/modules/70_bootstrap_orchestration.inc"
#if SHADOW_ENGINE_INTERNAL_DIAGNOSTICS
#include "../src/modules/75_internal_tools.inc"
#include "../src/modules/76_support_report.inc"
#endif
#include "../src/modules/80_runtime_entry.inc"
#define CHECK(x) do { ++checks; assert(x); } while(0)
#define DRIVER_OWNER 0xD000ULL
#define DRIVER_ID 0x300000007ULL
#define DRIVER_HANDLE 0x800000010ULL
static unsigned char nodes[32][0x20],descriptors[32][0x44],spatials[32][0x60];
static unsigned char queue[RENDER_QUEUE_ALLOCATION_BYTES];
static VehicleLimiterChain chain;
static VehicleCandidateSample complete_sample;

static void reset(unsigned limit)
{
    VehicleLightDiagnosticState *d=&g_shadow_engine.vehicle_diagnostics;
    memset(d,0,sizeof(*d));
    memset(&chain,0,sizeof(chain));
    memset(nodes,0,sizeof(nodes)); memset(descriptors,0,sizeof(descriptors));
    memset(spatials,0,sizeof(spatials)); memset(queue,0,sizeof(queue));
    d->selection_enabled=1; d->driver_proof_ready=1;
    g_shadow_engine.renderer.renderer_calls=20;
    g_shadow_engine.renderer.manager_calls=30;
    g_shadow_engine.policy_control.disabled=((LONG)(limit?limit:4)<<SHADOW_POLICY_LIMIT_SHIFT)|
        (1L<<SHADOW_POLICY_RESOLUTION_SHIFT);
    if(limit==0 || limit==10) g_shadow_engine.policy_control.disabled|=SHADOW_POLICY_DISABLE_LIMITER;
    if(limit==10) g_shadow_engine.policy_control.disabled|=SHADOW_POLICY_TEN_UNLIMITED;
    g_shadow_engine.policy_control.world_configuration=(2L<<SHADOW_WORLD_RESOLUTION_SHIFT)|SHADOW_WORLD_ENABLED;
    g_shadow_engine.policy_control.quality_ready=1;
    fixture_driver.owner=DRIVER_OWNER; fixture_driver.entity_id=DRIVER_ID;
    fixture_driver.player_id=0x900000001ULL;
    driver_available=1; driver_calls=0; during_driver_read=NULL;
}
static void publish(uint64_t owner,uint64_t entity_id,uint64_t handle,unsigned lights,float distance)
{
    VehicleBatchScope scope;
    memset(&scope,0,sizeof(scope)); scope.pending_count=lights;
    for(unsigned i=0;i<lights;++i) {
        VehicleSelectionRecord *r=&scope.pending[i];
        r->vehicle=owner; r->vehicle_entity_id=entity_id; r->descriptor_handle=handle+i;
        r->distance_squared=distance; r->tick=GetTickCount(); r->observations=1;
    }
    publish_vehicle_selection_batch(&scope);
}
static void candidate(unsigned i,int type,uint64_t handle,unsigned count)
{
    *(void **)nodes[i]=i+1<count?nodes[i+1]:NULL;
    *(void **)(nodes[i]+8)=descriptors[i]; *(void **)(nodes[i]+16)=spatials[i];
    *(int32_t *)(descriptors[i]+8)=type;
    memcpy(spatials[i]+VEHICLE_SPATIAL_DESCRIPTOR_HANDLE_OFFSET,&handle,sizeof(handle));
}
static unsigned scene(unsigned driver_lights,uint64_t driver_id)
{
    unsigned count=driver_lights+12;
    publish(DRIVER_OWNER,driver_id,DRIVER_HANDLE,driver_lights,10000.0f);
    for(unsigned i=0;i<driver_lights;++i) candidate(i,3,DRIVER_HANDLE+i,count);
    for(unsigned owner=0;owner<5;++owner) {
        uint64_t handle=0x900000020ULL+owner*2;
        publish(0x1000+owner*0x100,0x400000100ULL+owner,handle,2,4.0f+(float)owner*4);
        candidate(driver_lights+owner*2,3,handle,count);
        candidate(driver_lights+owner*2+1,3,handle+1,count);
    }
    candidate(count-2,2,0x500000077ULL,count); /* world remains untouched */
    candidate(count-1,1,DRIVER_HANDLE+8,count);
    publish(DRIVER_OWNER,driver_id,DRIVER_HANDLE+8,1,10000.0f);
    return count;
}
static void check_links(unsigned count)
{
    restore_vehicle_limiter_chain(&chain);
    for(unsigned i=0;i<count;++i) CHECK(*(void **)nodes[i]==(i+1<count?nodes[i+1]:NULL));
}
static void test_selection(void)
{
    unsigned count;
    reset(1); count=scene(2,DRIVER_ID);
    prepare_vehicle_limiter_chain(&chain,nodes[0],count,31,0);
    CHECK(driver_calls==1 && chain.sample.driver_valid && chain.sample.driver_headlight_lights==2);
    CHECK(chain.sample.driver_owner==DRIVER_OWNER && chain.sample.driver_entity_id==DRIVER_ID);
    CHECK(chain.complete_vehicles==5 && chain.count==6 && chain.suppressed==8);
    for(unsigned i=0;i<2;++i) {
        CHECK(chain.sample.records[i].selection_driver && chain.sample.records[i].selection_would_keep);
        CHECK(chain.sample.records[i].selection_rank==0 && !chain.sample.records[i].selection_group_complete);
        CHECK(chain.sample.records[i].selection_role==VEHICLE_SELECTION_DRIVER_ROLE);
    }
    CHECK(g_shadow_engine.vehicle_diagnostics.residency_slots[0].vehicle==0x1000);
    CHECK(g_shadow_engine.vehicle_diagnostics.residency_slots[0].vehicle_entity_id==0x400000100ULL);
    check_links(count);
    /* Switching driven vehicles invalidates the previous exemption immediately. */
    fixture_driver.owner=0x1000; fixture_driver.entity_id=0x400000100ULL;
    prepare_vehicle_limiter_chain(&chain,nodes[0],count,32,0);
    CHECK(!chain.sample.records[0].selection_driver && chain.sample.records[2].selection_driver);
    CHECK(!chain.sample.records[0].selection_would_keep);
    CHECK(g_shadow_engine.vehicle_diagnostics.residency_slots[0].vehicle==0x1100);
    check_links(count);
    reset(1); count=scene(1,DRIVER_ID);
    prepare_vehicle_limiter_chain(&chain,nodes[0],count,31,0);
    CHECK(chain.sample.driver_headlight_lights==1 && chain.complete_vehicles==5);
    CHECK(chain.sample.records[0].selection_driver && chain.count==5);
    check_links(count);
    /* Nonwaiting residency fallback still admits driver plus traffic N+2. */
    reset(1); count=scene(2,DRIVER_ID);
    g_shadow_engine.vehicle_diagnostics.residency_lock=1;
    prepare_vehicle_limiter_chain(&chain,nodes[0],count,31,0);
    CHECK(!chain.residency_applied && chain.count==10 && chain.sample.records[0].selection_driver);
    g_shadow_engine.vehicle_diagnostics.residency_lock=0; check_links(count);
    /* Zero and ten keep the small bypass and never ask for driver identity. */
    for(unsigned limit=0;limit<=10;limit+=10) {
        reset(limit); count=scene(2,DRIVER_ID);
        prepare_vehicle_limiter_chain(&chain,nodes[0],count,31,0);
        CHECK(!chain.enabled && !chain.evaluated && !driver_calls && chain.count==count);
    }
    /* Pointer equality alone, unknown IDs and unavailable driver proof grant no exemption. */
    for(unsigned kind=0;kind<3;++kind) {
        reset(1); count=scene(2,kind==1?UINT64_MAX:DRIVER_ID+1);
        if(kind==2) driver_available=0;
        prepare_vehicle_limiter_chain(&chain,nodes[0],count,31,0);
        CHECK(!chain.sample.records[0].selection_driver && !chain.sample.driver_headlight_lights);
        CHECK(chain.complete_vehicles==6 && !chain.sample.records[0].selection_would_keep);
        check_links(count);
    }
}
static void test_residency_transition(void)
{
    VehicleLightDiagnosticState *d=&g_shadow_engine.vehicle_diagnostics;
    unsigned count;
    reset(1); count=scene(2,DRIVER_ID);
    prepare_vehicle_limiter_chain(&chain,nodes[0],count,31,0); check_links(count);
    complete_sample=chain.sample;
    d->residency_slots[0].vehicle=DRIVER_OWNER;
    d->residency_slots[0].vehicle_entity_id=DRIVER_ID;
    d->residency_slots[0].role=1;
    d->residency_slots[1].vehicle=0xB000;
    d->residency_slots[1].vehicle_entity_id=0x400000123ULL;
    d->residency_slots[1].role=3;
    chain.degraded=1; chain.sample=complete_sample;
    CHECK(apply_vehicle_residency_policy(&chain));
    CHECK(!d->residency_slots[0].vehicle && d->residency_slots[1].vehicle==0xB000);
    CHECK(chain.sample.records[0].selection_driver && chain.sample.records[0].selection_would_keep);
    CHECK(chain.sample.records[0].selection_role==VEHICLE_SELECTION_DRIVER_ROLE);
    /* A recycled address with another full ID is not deleted as this driver. */
    d->residency_slots[0].vehicle=DRIVER_OWNER;
    d->residency_slots[0].vehicle_entity_id=DRIVER_ID-1;
    chain.sample=complete_sample;
    CHECK(apply_vehicle_residency_policy(&chain));
    CHECK(d->residency_slots[0].vehicle==DRIVER_OWNER);
    /* On exit the same headlights return to ordinary finite-limit policy. */
    driver_available=0; chain.degraded=0;
    prepare_vehicle_limiter_chain(&chain,nodes[0],count,32,0);
    CHECK(!chain.sample.driver_valid && !chain.sample.records[0].selection_driver);
    CHECK(chain.complete_vehicles==6); check_links(count);
}
static void quality_entry(unsigned i,int type,uint64_t handle,uint32_t width,uint32_t height)
{
    unsigned char *entry=queue+(size_t)i*RENDER_QUEUE_ENTRY_BYTES;
    candidate(i,type,handle,30);
    *(void **)(entry+VEHICLE_QUEUE_CANDIDATE_OFFSET)=nodes[i];
    *(uint32_t *)(entry+VEHICLE_QUEUE_WIDTH_OFFSET)=width;
    *(uint32_t *)(entry+VEHICLE_QUEUE_HEIGHT_OFFSET)=height;
}
static uint32_t width(unsigned i)
{ return *(uint32_t *)(queue+(size_t)i*RENDER_QUEUE_ENTRY_BYTES+VEHICLE_QUEUE_WIDTH_OFFSET); }
static uint32_t height(unsigned i)
{ return *(uint32_t *)(queue+(size_t)i*RENDER_QUEUE_ENTRY_BYTES+VEHICLE_QUEUE_HEIGHT_OFFSET); }
static void test_quality(void)
{
    reset(0);
    publish(DRIVER_OWNER,DRIVER_ID,DRIVER_HANDLE,2,4.0f);
    publish(0x1000,0x400000100ULL,0x800000030ULL,1,4.0f);
    quality_entry(0,3,DRIVER_HANDLE,4096,2048);
    quality_entry(1,1,DRIVER_HANDLE+1,2048,4096);
    quality_entry(2,3,0x800000030ULL,4096,256);
    quality_entry(3,2,0xF00000017ULL,4096,4096);
    apply_vehicle_shadow_quality(queue,4);
    CHECK(!driver_calls && width(0)==512 && height(0)==512);
    CHECK(width(1)==512 && height(1)==512);
    CHECK(width(2)==512 && height(2)==256 && width(3)==4096);
    CHECK(g_shadow_engine.vehicle_diagnostics.quality_mutated_entries==3);
    apply_world_shadow_quality(queue,4);
    CHECK(!driver_calls && width(0)==512 && height(1)==512 && width(3)==1024);
    /* Optional under-cap proof avoids both identity inspection and driver read. */
    driver_calls=0;
    quality_entry(0,3,DRIVER_HANDLE,512,256);
    quality_entry(1,1,DRIVER_HANDLE+1,256,512);
    apply_vehicle_shadow_quality(queue,2);
    CHECK(!driver_calls && width(0)==512 && height(1)==512);
    /* Unknown full ID uses the existing cap and needs no driver lookup. */
    reset(0); publish(DRIVER_OWNER,UINT64_MAX,DRIVER_HANDLE,1,4.0f);
    quality_entry(0,3,DRIVER_HANDLE,4096,4096); apply_vehicle_shadow_quality(queue,1);
    CHECK(!driver_calls && width(0)==512 && height(0)==512);
    for(unsigned kind=0;kind<3;++kind) {
        reset(0); publish(DRIVER_OWNER,DRIVER_ID,DRIVER_HANDLE,1,4.0f);
        if(kind==0) fixture_driver.entity_id=DRIVER_ID+1;
        if(kind==1) fixture_driver.owner=DRIVER_OWNER+8;
        if(kind==2) driver_available=0;
        quality_entry(0,3,DRIVER_HANDLE,4096,4096); apply_vehicle_shadow_quality(queue,1);
        CHECK(!driver_calls && width(0)==512 && height(0)==512);
    }
    reset(0); publish(DRIVER_OWNER,DRIVER_ID,DRIVER_HANDLE,1,4.0f);
    quality_entry(0,3,DRIVER_HANDLE,4096,4096);
    g_shadow_engine.vehicle_diagnostics.selection_writer_active=1;
    apply_vehicle_shadow_quality(queue,1);
    CHECK(!driver_calls && width(0)==4096);
    g_shadow_engine.vehicle_diagnostics.selection_writer_active=0;
    g_shadow_engine.policy_control.disabled|=SHADOW_POLICY_DISABLE_QUALITY;
    apply_vehicle_shadow_quality(queue,1); CHECK(!driver_calls && width(0)==4096);
}
static void test_identity_conflicts(void)
{
    unsigned count;
    for(unsigned same_epoch=0;same_epoch<2;++same_epoch) {
        VehicleSelectionSnapshot snapshot;
        VehicleCandidateSample sample;
        reset(1); count=scene(2,DRIVER_ID-1);
        if(!same_epoch) g_shadow_engine.renderer.renderer_calls=21;
        publish(DRIVER_OWNER,DRIVER_ID,DRIVER_HANDLE,2,4.0f);
        prepare_vehicle_limiter_chain(&chain,nodes[0],count,31,0);
        CHECK(!chain.sample.records[0].selection_driver);
        CHECK(chain.sample.records[0].selection_entity_id==UINT64_MAX);
        check_links(count);
        memset(&sample,0,sizeof(sample)); sample.tick=GetTickCount(); sample.manager_call=31;
        sample.renderer_epoch=g_shadow_engine.renderer.renderer_calls; sample.copied_candidates=count;
        for(unsigned i=0;i<count;++i) CHECK(copy_vehicle_candidate(&sample.records[i],nodes[i]));
        snapshot_vehicle_selection(&snapshot,sample.renderer_epoch);
        classify_vehicle_selection_sample(&sample,&snapshot);
        CHECK(!sample.records[0].selection_driver && sample.records[0].selection_entity_id==UINT64_MAX);
        quality_entry(0,3,DRIVER_HANDLE,4096,4096); driver_calls=0;
        apply_vehicle_shadow_quality(queue,1);
        CHECK(!driver_calls && width(0)==512);
    }
    /* Full ID zero is valid when the native reader proves it. */
    reset(1); fixture_driver.entity_id=0;
    count=scene(2,0); prepare_vehicle_limiter_chain(&chain,nodes[0],count,31,0);
    CHECK(chain.sample.records[0].selection_driver); check_links(count);
}
static void test_extra_slot_toggle(void)
{
    reset(1);
    CHECK(vehicle_selection_extra_slots()==2U);
#if SHADOW_ENGINE_INTERNAL_DIAGNOSTICS
    unsigned count=scene(2,DRIVER_ID);
    VehicleLightDiagnosticState *d=&g_shadow_engine.vehicle_diagnostics;
    CHECK(!vehicle_selection_set_extra_slots(1));
    CHECK(vehicle_selection_set_extra_slots(0));
    CHECK(vehicle_selection_extra_slots()==0 && d->residency_reset_requested);
    /* The ranked fallback must use the same requested quota on lock miss. */
    d->residency_lock=1;
    prepare_vehicle_limiter_chain(&chain,nodes[0],count,31,0);
    CHECK(!chain.residency_applied && chain.count==6 && chain.suppressed==8);
    CHECK(chain.sample.selection_extra_slots==0 && chain.sample.records[0].selection_driver);
    check_links(count); d->residency_lock=0;
    prepare_vehicle_limiter_chain(&chain,nodes[0],count,32,0);
    CHECK(chain.residency_residents==1 && d->residency_extra_slots==0);
    CHECK(chain.count==6 && !d->residency_slots[1].vehicle);
    check_links(count);
    CHECK(vehicle_selection_set_extra_slots(2));
    d->residency_lock=1;
    prepare_vehicle_limiter_chain(&chain,nodes[0],count,33,0);
    CHECK(chain.count==10 && chain.sample.selection_extra_slots==2);
    check_links(count); d->residency_lock=0;
    prepare_vehicle_limiter_chain(&chain,nodes[0],count,34,0); check_links(count);
    d->residency_slots[1].vehicle=0x1100;
    d->residency_slots[1].vehicle_entity_id=0x400000101ULL;
    CHECK(vehicle_selection_set_extra_slots(0));
    chain.degraded=1;
    CHECK(apply_vehicle_residency_policy(&chain));
    CHECK(d->residency_extra_slots==0 && !d->residency_slots[1].vehicle);
    CHECK(d->residency_slots[0].vehicle==0x1000 && chain.sample.records[2].selection_would_keep);
    CHECK(chain.sample.records[0].selection_driver && chain.sample.records[0].selection_would_keep);
    CHECK(!d->residency_reset_requested);
    CHECK(vehicle_selection_set_extra_slots(0) && !d->residency_reset_requested);
    for(unsigned limit=0;limit<=10;limit+=10) {
        reset(limit); count=scene(2,DRIVER_ID); vehicle_selection_set_extra_slots(0);
        prepare_vehicle_limiter_chain(&chain,nodes[0],count,35,0);
        CHECK(!chain.enabled && !chain.evaluated && !driver_calls && chain.count==count);
    }
    reset(1); CHECK(vehicle_selection_extra_slots()==2);
#endif
}

static void test_quality_trace(void)
{
    VehicleQueueSample sample;
    for(unsigned kind=0;kind<5;++kind) {
        reset(0); memset(&sample,0,sizeof(sample)); sample.copied_entries=1;
        publish(DRIVER_OWNER,kind==1?UINT64_MAX:DRIVER_ID,DRIVER_HANDLE,1,4.0f);
        if(kind==2) driver_available=0;
        if(kind==3) fixture_driver.entity_id++;
        quality_entry(0,3,DRIVER_HANDLE,kind==4?512:4096,512);
        apply_vehicle_shadow_quality_with_sample(queue,1,&sample);
        CHECK(sample.quality_evaluated && !sample.quality_lock_skipped);
        CHECK(!driver_calls && width(0)==512 && height(0)==512);
        if(kind==4) {
            CHECK(sample.records[0].quality_decision==QUALITY_DECISION_UNDER_CAP);
        } else {
            CHECK(sample.records[0].quality_decision==QUALITY_DECISION_CAPPED);
            CHECK(sample.records[0].quality_owner==DRIVER_OWNER);
        }
    }
    reset(0); memset(&sample,0,sizeof(sample)); sample.copied_entries=1;
    g_shadow_engine.vehicle_diagnostics.selection_writer_active=1;
    apply_vehicle_shadow_quality_with_sample(queue,1,&sample);
    CHECK(sample.quality_lock_skipped && !driver_calls);
}

static unsigned char bg_root[16],bg_controller[16],bg_data[24],bg_reference[16],bg_entity[96];
static void *bg_root_pointer,*bg_array_pointer;
int main(void)
{
    test_selection(); test_residency_transition(); test_quality(); test_identity_conflicts();
    test_extra_slot_toggle(); test_quality_trace();
    printf("PASS driver policy diagnostics=%d checks=%u; fresh limiter owner/full-ID join, traffic exclusion, degraded removal, uniform vehicle quality with zero driver-provider calls and bypass gates.\n",
        SHADOW_ENGINE_INTERNAL_DIAGNOSTICS,checks);
    return 0;
}
