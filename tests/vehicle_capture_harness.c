/* Actual F10 implementation on synthetic data. Never installs a game hook. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
static LPTHREAD_START_ROUTINE queued_flush;
static int reject_thread;
static HANDLE WINAPI capture_test_thread(LPSECURITY_ATTRIBUTES security,SIZE_T size,
    LPTHREAD_START_ROUTINE fn,LPVOID arg,DWORD flags,LPDWORD id)
{
    (void)security; (void)size; (void)arg; (void)flags; (void)id;
    if(reject_thread) return NULL;
    queued_flush=fn;
    return CreateEvent(NULL,TRUE,FALSE,NULL);
}
#define CreateThread capture_test_thread
static unsigned tls_set_calls,tls_fail_call;
static BOOL WINAPI capture_test_tls_set(DWORD index,LPVOID value)
{
    ++tls_set_calls;
    if(tls_fail_call && tls_set_calls==tls_fail_call) return FALSE;
    return TlsSetValue(index,value);
}
#define TlsSetValue capture_test_tls_set
#include "../src/shadow_engine_patch.c"
#undef CreateThread
#undef TlsSetValue
#include <assert.h>
#include "saved_settings_fixture.h"

static unsigned char queue_data[RENDER_QUEUE_ALLOCATION_BYTES];
static unsigned char candidate[0x60],descriptor[0x44],spatial[0x58],resource[0xB0];
static unsigned char queue_copy[sizeof(queue_data)];
static SHORT WINAPI no_key(int key) { (void)key; return 0; }

/* Existing single-observation fixtures model a one-component native batch. */
static void observe_and_publish_selection(void *component,void *skeleton,
    void *transform,uintptr_t caller)
{
    VehicleBatchScope *scope=TlsGetValue(g_shadow_engine.vehicle_diagnostics.batch_tls);
    if(scope) { scope->pending_count=0; scope->pending_overflow=0; }
    observe_vehicle_selection(component,skeleton,transform,caller);
    if(scope) publish_vehicle_selection_batch(scope);
}

static void test_preflight(void)
{
    unsigned char *image=VirtualAlloc(NULL,0x2100000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    unsigned char *renderer,*lookup=image+0x2000000;
    uint32_t p,i,v;
    int32_t delta;
    assert(image);
    g_shadow_engine.bootstrap.disrupt_base=image;
    g_shadow_engine.intersection.lookup_target=lookup;
    for(p=0;p<ARRAY_COUNT(g_runtime_profiles);++p) {
        select_runtime_profile_state(&g_runtime_profiles[p]);
        renderer=ENGINE_ADDRESS(RENDER_QUEUE_PROCESS_RVA);
        for(i=0;i<ARRAY_COUNT(g_vehicle_queue_proofs);++i) {
            const VehicleDiagnosticProof *proof=&g_vehicle_queue_proofs[i];
            memcpy(renderer+proof->offset,proof->bytes,proof->length);
        }
        renderer[0x339]=0xE8; delta=(int32_t)(lookup-(renderer+0x33E));
        memcpy(renderer+0x33A,&delta,4);
        for(v=0;v<ARRAY_COUNT(g_vehicle_resource_proofs);++v) {
            const VehicleResourceProof *proof=&g_vehicle_resource_proofs[v];
            memset(lookup,0,0x100);
            memcpy(lookup+proof->load_offset,proof->loads,proof->load_length);
            memcpy(lookup+proof->store_offset,proof->stores,8);
            assert(validate_vehicle_resolution_sites());
            lookup[proof->store_offset]^=1; assert(!validate_vehicle_resolution_sites());
            lookup[proof->store_offset]^=1;
            lookup[proof->load_offset]^=1; assert(!validate_vehicle_resolution_sites());
            lookup[proof->load_offset]^=1;
        }
        for(i=0;i<ARRAY_COUNT(g_vehicle_queue_proofs);++i) {
            renderer[g_vehicle_queue_proofs[i].offset]^=1;
            assert(!validate_vehicle_resolution_sites());
            renderer[g_vehicle_queue_proofs[i].offset]^=1;
        }
        renderer[0x339]=0x90; assert(!validate_vehicle_resolution_sites()); renderer[0x339]=0xE8;
        g_shadow_engine.intersection.lookup_target=lookup+1;
        assert(!validate_vehicle_resolution_sites());
        g_shadow_engine.intersection.lookup_target=lookup;
    }
    clear_runtime_profile(); assert(!validate_vehicle_resolution_sites());
    g_shadow_engine.bootstrap.disrupt_base=NULL;
    g_shadow_engine.intersection.lookup_target=NULL;
    VirtualFree(image,0,MEM_RELEASE);
    puts("PASS diagnostic preflight: complete instruction spans, both metadata variants, corrupt sites/call/target reject (synthetic A4EE is not live proof)");
}

static void prepare_capture(int extended)
{
    VehicleLightDiagnosticState *d=&g_shadow_engine.vehicle_diagnostics;
    memset(d,0,sizeof(*d)); queued_flush=NULL;
    d->state=2; d->serial=1; d->resolution_enabled=extended;
    d->started_tick=GetTickCount()-1600U;
    memset(queue_data,0,sizeof(queue_data)); memset(candidate,0,sizeof(candidate));
    memset(descriptor,0,sizeof(descriptor)); memset(spatial,0,sizeof(spatial));
    memset(resource,0,sizeof(resource));
    *(void **)(candidate+8)=descriptor; *(void **)(candidate+0x10)=spatial;
    *(int32_t *)(descriptor+8)=3; *(uint32_t *)(descriptor+0x40)=0x80;
    *(void **)spatial=(void *)(uintptr_t)0x1234;
    *(float *)(spatial+0x14)=0.5f;
    *(void **)(queue_data+0x10)=candidate;
    *(uint32_t *)(queue_data+0x20A0)=1;
    *(uint32_t *)(queue_data+0x20A4)=4096;
    *(uint32_t *)(queue_data+0x20A8)=4096;
    *(void **)(queue_data+0x20B0)=resource;
    *(uint32_t *)(resource+0x40)=2048; *(uint32_t *)(resource+0x44)=1024;
    memcpy(queue_copy,queue_data,sizeof(queue_data));
}

static unsigned char owner_components[VEHICLE_OWNER_MAX_RECORDS+1U][0x120];
static unsigned char owner_light[0x60],owner_spatial[0x20];
static unsigned owner_updates,owner_transforms;
static int owner_nested,owner_freeze,owner_skip_transform;
static void __fastcall fake_owner_transform(void *light,void *where,void *matrix)
{
    assert(light==owner_light && where==owner_spatial && matrix==(void *)(uintptr_t)0x123);
    ++owner_transforms;
}
static void __fastcall fake_owner_update(void *component,void *skeleton,void *world,
    float delta,unsigned char force)
{
    assert(skeleton==(void *)(uintptr_t)0x456 && world==(void *)(uintptr_t)0x789);
    assert(delta==0.0125f && force==0xAB);
    ++owner_updates;
    if(owner_nested) {
        owner_nested=0;
        hooked_vehicle_light_update(component,skeleton,world,delta,force);
    }
    if(owner_freeze) g_shadow_engine.vehicle_diagnostics.state=4;
    if(!owner_skip_transform) hooked_vehicle_light_transform(owner_light,owner_spatial,(void *)(uintptr_t)0x123);
}
static void owner_call(unsigned index)
{
    hooked_vehicle_light_update(owner_components[index],(void *)(uintptr_t)0x456,
        (void *)(uintptr_t)0x789,0.0125f,0xAB);
}
static void test_owner_capture(void)
{
    VehicleLightDiagnosticState *d=&g_shadow_engine.vehicle_diagnostics;
    VehicleOwnerScope stale;
    unsigned i;
    prepare_capture(0);
    d->owner_tls=TlsAlloc(); assert(d->owner_tls!=TLS_OUT_OF_INDEXES);
    d->owner_tls_valid=1; d->owner_enabled=1; d->started_tick=GetTickCount();
    d->original_light_update=fake_owner_update; d->original_light_transform=fake_owner_transform;
    memset(owner_components,0,sizeof(owner_components));
    memset(owner_light,0,sizeof(owner_light));
    *(void **)owner_spatial=(void *)(uintptr_t)0x87654321;
    *(uint64_t *)(owner_light+0x20)=0x1234567800000042ULL;
    for(i=0;i<ARRAY_COUNT(owner_components);++i) {
        *(void **)(owner_components[i]+0x10)=owner_light;
        *(int32_t *)(owner_components[i]+0x11C)=17;
    }
    owner_nested=1; owner_updates=owner_transforms=0;
    owner_call(0);
    assert(owner_updates==2 && owner_transforms==2 && d->owner_count==1);
    assert(d->owner_records[0].transform_seen==1); /* Nested dedup must mask outer scope. */
    assert(d->owner_records[0].spatial_root==(void *)(uintptr_t)0x87654321);
    assert(d->owner_records[0].spatial_handle==0x1234567800000042ULL);
    assert(d->owner_records[0].valid==7 && d->owner_records[0].bone_index==17);
    assert(TlsGetValue(d->owner_tls)==NULL);
    owner_call(0); assert(d->owner_count==1 && d->owner_records[0].transform_seen==1);
    d->started_tick=GetTickCount()-510U; owner_call(0);
    assert(d->owner_count==2 && d->owner_records[1].epoch==1);
    stale.serial=d->serial-1; stale.index=0; stale.light=owner_light;
    TlsSetValue(d->owner_tls,&stale);
    hooked_vehicle_light_transform(owner_light,owner_spatial,(void *)(uintptr_t)0x123);
    assert(d->owner_records[0].transform_seen==1);
    stale.serial=d->serial; stale.light=NULL;
    hooked_vehicle_light_transform(owner_light,owner_spatial,(void *)(uintptr_t)0x123);
    assert(d->owner_records[0].transform_mismatch==1);
    TlsSetValue(d->owner_tls,NULL);
    d->writer_active=1; owner_call(1); d->writer_active=0;
    assert(d->owner_count==2); /* Native update/transform still pass through. */
    for(i=1;i<ARRAY_COUNT(owner_components);++i) owner_call(i);
    assert(d->owner_count==VEHICLE_OWNER_RECORDS_PER_WINDOW+1U && d->owner_overflow>0);
    assert(d->owner_window_records[0]==1 && d->owner_window_records[1]==VEHICLE_OWNER_RECORDS_PER_WINDOW);
    d->owner_count=0; ++d->serial; d->started_tick=GetTickCount();
    memset(d->owner_window_records,0,sizeof(d->owner_window_records));
    *(uint64_t *)(owner_light+0x20)=0x9999999900000042ULL;
    owner_call(0); assert(d->owner_count==1);
    assert(d->owner_records[0].spatial_handle==0x9999999900000042ULL);
    owner_freeze=1; owner_call(1); owner_freeze=0;
    assert(d->state==4 && d->owner_records[1].transform_seen==0);
    assert(TlsGetValue(d->owner_tls)==NULL);
    /* Native memory can vanish before the frozen worker. It uses only copies. */
    memset(owner_components,0xFF,sizeof(owner_components)); memset(owner_light,0xFF,sizeof(owner_light));
    flush_vehicle_candidate_capture(NULL);
    rollback_vehicle_owner_observer(); assert(!d->owner_enabled && !d->owner_tls_valid);
    puts("PASS owner ABI, native passthrough, scoped transform identity, nested dedup, epoch/cap bounds, stale serial exclusion, pointer reuse and frozen copies");
}

static void test_owner_retry(void)
{
    VehicleLightDiagnosticState *d=&g_shadow_engine.vehicle_diagnostics;
    VehicleOwnerRecord *r;
    prepare_capture(0); d->started_tick=GetTickCount();
    d->owner_tls=TlsAlloc(); assert(d->owner_tls!=TLS_OUT_OF_INDEXES);
    d->owner_tls_valid=1; d->owner_enabled=1;
    d->original_light_update=fake_owner_update; d->original_light_transform=fake_owner_transform;
    memset(owner_components,0,sizeof(owner_components)); memset(owner_light,0,sizeof(owner_light));
    *(void **)(owner_components[0]+0x10)=owner_light;
    owner_skip_transform=1; owner_call(0); owner_skip_transform=0;
    r=&d->owner_records[0]; assert(d->owner_count==1 && !r->transform_seen);
    /* Synthetic clock stamps, never sleeps or forces the native update flag. */
    r->last_probe_tick=GetTickCount(); owner_call(0);
    assert(!r->transform_seen && r->update_attempts==1);
    r->last_probe_tick=GetTickCount()-VEHICLE_OWNER_RETRY_MS;
    *(uint64_t *)(owner_light+0x28)=99; owner_call(0);
    assert(!r->transform_seen && !r->descriptor_handle && r->update_attempts==2);
    *(uint64_t *)(owner_light+0x28)=0;
    r->last_probe_tick=GetTickCount()-VEHICLE_OWNER_RETRY_MS;
    *(void **)(owner_components[0]+0x10)=(void *)(uintptr_t)1; owner_call(0);
    assert(!r->transform_seen && r->update_attempts==3);
    *(void **)(owner_components[0]+0x10)=owner_light;
    r->last_probe_tick=GetTickCount()-VEHICLE_OWNER_RETRY_MS;
    owner_call(0); assert(r->transform_seen==1 && r->update_attempts==4 && d->owner_count==1);
    r->last_probe_tick=GetTickCount()-VEHICLE_OWNER_RETRY_MS;
    owner_call(0); assert(r->transform_seen==1 && r->update_attempts==4);
    rollback_vehicle_owner_observer(); d->state=0;
    puts("PASS bounded delayed transform retry: throttle, unchanged original arguments, changed handle/light rejection, same record reuse and stop after success");
}

static void test_focused_owner_windows(void)
{
    VehicleLightDiagnosticState *d=&g_shadow_engine.vehicle_diagnostics;
    VehicleOwnerScope scope;
    VehicleQueueSample *sample;
    unsigned epoch,i;
    prepare_capture(1); d->started_tick=GetTickCount();
    d->owner_enabled=1; d->owner_focused=1;
    memset(owner_components,0,sizeof(owner_components)); memset(owner_light,0,sizeof(owner_light));
    for(i=0;i<ARRAY_COUNT(owner_components);++i) *(void **)(owner_components[i]+0x10)=owner_light;
    assert(!begin_vehicle_owner_record(&scope,owner_components[0],NULL,NULL,0,0,0xABC));
    assert(!d->owner_count); /* No renderer snapshot: no broad first-frame fill. */
    for(epoch=0;epoch<VEHICLE_CAPTURE_SAMPLES;++epoch) {
        sample=&d->queue_samples[epoch];
        sample->copied_entries=2;
        for(i=0;i<2;++i) {
            sample->records[i].submitted=1; sample->records[i].valid=3; sample->records[i].descriptor_handle_valid=1;
            sample->records[i].descriptor_handle=0x1234567800000042ULL;
        }
        d->queue_sample_count=(LONG)epoch+1; d->owner_window_tick[epoch]=GetTickCount();
        *(uint64_t *)(owner_light+0x28)=0x9999999900000042ULL;
        for(i=0;i<ARRAY_COUNT(owner_components);++i)
            assert(!begin_vehicle_owner_record(&scope,owner_components[i],NULL,NULL,0,0,0xABC));
        assert(d->owner_window_unmatched[epoch]==ARRAY_COUNT(owner_components));
        assert(!d->owner_window_records[epoch]); /* Low index equality is not a match. */
        *(uint64_t *)(owner_light+0x28)=UINT64_MAX;
        assert(!begin_vehicle_owner_record(&scope,owner_components[0],NULL,NULL,0,0,0xABC));
        *(uint64_t *)(owner_light+0x28)=0x1234567800000042ULL;
        assert(vehicle_owner_handle_matches(sample,*(uint64_t *)(owner_light+0x28))==2);
        for(i=0;i<VEHICLE_OWNER_RECORDS_PER_WINDOW;++i) {
            assert(begin_vehicle_owner_record(&scope,owner_components[i],NULL,NULL,0,0,0xABC));
            assert(scope.index==epoch*VEHICLE_OWNER_RECORDS_PER_WINDOW+i);
            assert(d->owner_records[scope.index].epoch==epoch);
        }
        assert(!begin_vehicle_owner_record(&scope,owner_components[i],NULL,NULL,0,0,0xABC));
        assert(d->owner_window_overflow[epoch]==1);
        assert(d->owner_count==(epoch+1U)*VEHICLE_OWNER_RECORDS_PER_WINDOW);
        d->owner_window_tick[epoch]=GetTickCount()-VEHICLE_OWNER_WINDOW_MS;
        assert(!begin_vehicle_owner_record(&scope,owner_components[0],NULL,NULL,0,0,0xABC));
        d->owner_window_tick[epoch]=GetTickCount();
        d->owner_window_probes[epoch]=VEHICLE_OWNER_PROBES_PER_WINDOW;
        assert(!begin_vehicle_owner_record(&scope,(void *)(uintptr_t)1,NULL,NULL,0,0,0xABC));
    }
    assert(d->owner_count==VEHICLE_OWNER_MAX_RECORDS);
    d->sample_count=VEHICLE_CAPTURE_SAMPLES;
    d->owner_window_tick[3]=GetTickCount(); vehicle_capture_close_if_complete();
    assert(d->state==2); /* Last window can collect after final renderer returns. */
    d->owner_window_tick[3]=GetTickCount()-VEHICLE_OWNER_WINDOW_MS;
    g_shadow_engine.hooks.get_async_key_state=no_key;
    d->writer_active=1; poll_vehicle_candidate_capture_key(); assert(d->state==2);
    d->writer_active=0; poll_vehicle_candidate_capture_key(); assert(d->state==4 && queued_flush);
    memset(owner_components,0xFF,sizeof(owner_components)); memset(owner_light,0xFF,sizeof(owner_light));
    queued_flush(NULL); assert(d->state==0);
    /* Incomplete proof retains broad sampling but reserves each epoch's quota. */
    prepare_capture(0); d->owner_enabled=1; d->started_tick=GetTickCount();
    memset(owner_components,0,sizeof(owner_components));
    for(epoch=0;epoch<VEHICLE_CAPTURE_SAMPLES;++epoch) {
        d->started_tick=GetTickCount()-epoch*VEHICLE_CAPTURE_INTERVAL_MS;
        for(i=0;i<VEHICLE_OWNER_RECORDS_PER_WINDOW;++i)
            assert(begin_vehicle_owner_record(&scope,owner_components[i],NULL,NULL,0,0,0xABC));
        assert(!begin_vehicle_owner_record(&scope,owner_components[i],NULL,NULL,0,0,0xABC));
    }
    assert(d->owner_count==VEHICLE_OWNER_MAX_RECORDS);
    puts("PASS focused owner coverage: unrelated population excluded, full handles, shared instances, separate four-window quotas, no eviction, probe/time bounds, final-window grace, frozen copies and broad fallback");
}

static void set_identity_owner(VehicleOwnerRecord *r,uintptr_t component,
    uintptr_t light,uintptr_t skeleton,uint64_t handle,uint32_t epoch)
{
    memset(r,0,sizeof(*r));
    r->component=(void *)component; r->light=(void *)light;
    r->skeleton=(void *)skeleton; r->descriptor_handle=handle;
    r->epoch=epoch; r->valid=3U;
}

static void test_identity_lineage(void)
{
    VehicleLightDiagnosticState *d=&g_shadow_engine.vehicle_diagnostics;
    prepare_capture(1); d->identity_count=0; d->identity_next_lineage=0;
    d->serial=1; d->owner_count=4;
    set_identity_owner(&d->owner_records[0],0x100U,0x200U,0xA000U,0xAAU,0);
    set_identity_owner(&d->owner_records[1],0x100U,0x200U,0xA000U,0xAAU,1);
    set_identity_owner(&d->owner_records[2],0x110U,0x210U,0xA000U,0xBBU,0);
    set_identity_owner(&d->owner_records[3],0x110U,0x210U,0xA000U,0xBBU,1);
    update_vehicle_identity_registry(d,1);
    assert(d->identity_count==2 && d->identity_records[0].last_sample_mask==3U);
    assert(d->identity_records[0].captures_seen==1 && d->identity_records[0].total_sample_observations==2);
    assert(d->identity_records[1].last_sample_mask==3U);
    d->serial=2; d->owner_count=1;
    set_identity_owner(&d->owner_records[0],0x100U,0x200U,0xA000U,0xAAU,2);
    update_vehicle_identity_registry(d,2);
    assert(d->identity_count==2 && d->identity_records[0].captures_seen==2);
    assert(d->identity_records[0].last_sample_mask==4U);
    assert(d->identity_records[1].absent_captures==1 && d->identity_records[1].consecutive_absent==1);
    d->serial=3; d->owner_count=1;
    set_identity_owner(&d->owner_records[0],0x100U,0x300U,0xB000U,0xCCU,0);
    update_vehicle_identity_registry(d,3);
    assert(d->identity_count==3 && d->identity_records[2].lineage_id==3);
    assert(d->identity_records[0].absent_captures==1);
    d->serial=4; d->owner_count=1;
    set_identity_owner(&d->owner_records[0],0x120U,0x400U,0xC000U,0xAAU,0);
    update_vehicle_identity_registry(d,4);
    assert(d->identity_count==4 && d->identity_records[3].descriptor_handle==0xAAU);
    assert(d->identity_records[2].absent_captures==1);
    puts("PASS copied-token lineage: per-capture dedup, skeleton grouping inputs, continuity, observational absence, component change and prior-handle reuse leads");
}

static void test_handle_link(void)
{
    unsigned char *image=VirtualAlloc(NULL,0x3000000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    unsigned char *copy;
    unsigned p;
    VehicleQueueTicket ticket;
    VehicleLightDiagnosticState *d=&g_shadow_engine.vehicle_diagnostics;
    assert(image); g_shadow_engine.bootstrap.disrupt_base=image;
    for(p=0;p<ARRAY_COUNT(g_runtime_profiles);++p) {
        select_runtime_profile_state(&g_runtime_profiles[p]);
        if(!g_runtime_profiles[p].vehicle_spatial_copy_rva) {
            assert(!validate_vehicle_handle_link_sites()); continue;
        }
        copy=image+g_runtime_profiles[p].vehicle_spatial_copy_rva;
        memcpy(copy,g_vehicle_link_copy_prefix,sizeof(g_vehicle_link_copy_prefix)-1);
        memcpy(copy+0x9E,g_vehicle_link_copy_tail,sizeof(g_vehicle_link_copy_tail)-1);
        assert(validate_vehicle_handle_link_sites());
        copy[0]^=1; assert(!validate_vehicle_handle_link_sites()); copy[0]^=1;
        copy[0xC0]^=1; assert(!validate_vehicle_handle_link_sites()); copy[0xC0]^=1;
        g_runtime_state.image_size=g_runtime_profiles[p].vehicle_spatial_copy_rva+0xD5;
        assert(!validate_vehicle_handle_link_sites());
    }
    clear_runtime_profile(); assert(!validate_vehicle_handle_link_sites());
    g_shadow_engine.bootstrap.disrupt_base=NULL; VirtualFree(image,0,MEM_RELEASE);
    prepare_capture(1); d->handle_link_enabled=1;
    *(uint64_t *)(spatial+0x50)=0xAB00000042ULL;
    ticket=begin_vehicle_queue_sample(queue_data,1,1); assert(ticket.capture.active);
    assert(d->queue_samples[0].records[0].descriptor_handle_valid);
    assert(d->queue_samples[0].records[0].descriptor_handle==0xAB00000042ULL);
    *(uint64_t *)(spatial+0x50)=UINT64_MAX;
    finish_vehicle_queue_sample(ticket);
    assert(d->queue_samples[0].records[0].descriptor_handle==0xAB00000042ULL);
    ticket=begin_vehicle_queue_sample(queue_data,1,1); finish_vehicle_queue_sample(ticket);
    assert(!d->queue_samples[1].records[0].descriptor_handle_valid);
    prepare_capture(1); /* Optional read disabled: keep old capture fields. */
    ticket=begin_vehicle_queue_sample(queue_data,1,1); finish_vehicle_queue_sample(ticket);
    assert(d->queue_samples[0].records[0].valid==15 && !d->queue_samples[0].records[0].descriptor_handle_valid);
    d->handle_link_enabled=1;
    { VehicleQueueRecord unreadable;
      memset(&unreadable,0,sizeof(unreadable)); unreadable.light.spatial=(void *)(uintptr_t)1;
      copy_vehicle_descriptor_handle(&unreadable); assert(!unreadable.descriptor_handle_valid); }
    assert(!memcmp(queue_copy,queue_data,sizeof(queue_data)));
    d->state=0;
    puts("PASS spatial handle link: native proof rejection, no transform needed, copied identity, invalid/unreadable exclusion, legacy fallback and no queue writes");
}

static void fill_batch_proof(unsigned char *batch)
{
    memcpy(batch,g_vehicle_batch_prefix,sizeof(g_vehicle_batch_prefix)-1);
    memcpy(batch+0xD9,g_vehicle_batch_array,sizeof(g_vehicle_batch_array)-1);
    memcpy(batch+0x16D,g_vehicle_batch_restore,sizeof(g_vehicle_batch_restore)-1);
    memcpy(batch+0x18C,g_vehicle_batch_dispatch,sizeof(g_vehicle_batch_dispatch)-1);
}

static void test_batch_preflight(void)
{
    unsigned char *image=VirtualAlloc(NULL,0x2100000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    unsigned p,i;
    const unsigned offsets[]={0,0xD9,0x16D,0x18C};
    assert(image); g_shadow_engine.bootstrap.disrupt_base=image;
    for(p=0;p<ARRAY_COUNT(g_runtime_profiles);++p) {
        unsigned char *batch;
        select_runtime_profile_state(&g_runtime_profiles[p]);
        if(!g_runtime_profiles[p].vehicle_light_batch_rva) {
            assert(!validate_vehicle_batch_site()); continue;
        }
        batch=image+g_runtime_profiles[p].vehicle_light_batch_rva;
        fill_batch_proof(batch); assert(validate_vehicle_batch_site());
        for(i=0;i<ARRAY_COUNT(offsets);++i) {
            batch[offsets[i]]^=1; assert(!validate_vehicle_batch_site());
            assert(!g_shadow_engine.vehicle_diagnostics.light_batch_target);
            batch[offsets[i]]^=1;
        }
        g_runtime_state.image_size=1; assert(!validate_vehicle_batch_site());
    }
    clear_runtime_profile(); assert(!validate_vehicle_batch_site());
    g_shadow_engine.bootstrap.disrupt_base=NULL; VirtualFree(image,0,MEM_RELEASE);
    puts("PASS batch preflight: four mapped data profiles, A4EE disabled, every span/short image rejects (synthetic)");
}

static unsigned char batch_vehicle[2][0x160],batch_components[2][0x260];
static unsigned batch_depth,batch_calls;
static int batch_nest;
static void __fastcall fake_vehicle_batch(void *vehicle,float delta,void *skeleton,
    void *world,float distance_squared)
{
    VehicleLightDiagnosticState *d=&g_shadow_engine.vehicle_diagnostics;
    VehicleOwnerRecord r;
    VehicleBatchScope *scope;
    VehicleOwnerScope record_scope;
    uintptr_t token=(uintptr_t)vehicle;
    unsigned index=vehicle==batch_vehicle[1]?1U:0U;
    assert(delta==0.0125f && skeleton==(void *)(uintptr_t)0x456);
    assert(world==(void *)(uintptr_t)0x789 && distance_squared==123.25f);
    ++batch_calls;
    if(d->state!=2 || !d->batch_enabled) return;
    scope=(VehicleBatchScope *)TlsGetValue(d->batch_tls);
    assert(scope && (uintptr_t)scope->vehicle==token);
    if(batch_nest && !batch_depth) {
        ++batch_depth;
        hooked_vehicle_light_batch(batch_vehicle[1],delta,skeleton,world,distance_squared);
        --batch_depth;
        assert(TlsGetValue(d->batch_tls)==scope);
    }
    memset(&r,0,sizeof(r)); r.component=batch_components[index]+0x130;
    r.skeleton=skeleton; r.world_transform=world;
    r.update_caller=(uintptr_t)d->light_batch_target+VEHICLE_BATCH_RETURN_OFFSET;
    copy_vehicle_batch_evidence(&r);
    assert(r.vehicle==vehicle && r.vehicle_flags==63 && r.vehicle_count==2 && r.vehicle_index==1);
    if(d->owner_enabled) {
        assert(begin_vehicle_owner_record(&record_scope,r.component,skeleton,world,delta,0,r.update_caller));
        assert(d->owner_records[record_scope.index].vehicle==vehicle);
        assert(d->owner_records[record_scope.index].vehicle_flags==63);
    }
    r.vehicle_flags=0; r.component=(unsigned char *)r.component+1;
    copy_vehicle_batch_evidence(&r); assert(r.vehicle_flags==31);
    r.component=batch_components[index]+0x260; copy_vehicle_batch_evidence(&r); assert(r.vehicle_flags==31);
    r.component=(void *)(uintptr_t)1; copy_vehicle_batch_evidence(&r); assert(r.vehicle_flags==31);
    r.component=batch_components[index]; r.update_caller=1;
    copy_vehicle_batch_evidence(&r); assert(r.vehicle_flags==25);
    r.update_caller=(uintptr_t)d->light_batch_target+VEHICLE_BATCH_RETURN_OFFSET;
    r.skeleton=NULL; copy_vehicle_batch_evidence(&r); assert(r.vehicle_flags==21);
    r.skeleton=skeleton; r.world_transform=NULL;
    copy_vehicle_batch_evidence(&r); assert(r.vehicle_flags==13);
    r.world_transform=world; scope->serial=d->serial-1; r.vehicle_flags=0;
    copy_vehicle_batch_evidence(&r); assert(!r.vehicle_flags); scope->serial=d->serial;
    scope->vehicle=(void *)(uintptr_t)1;
    copy_vehicle_batch_evidence(&r); assert(r.vehicle_flags==29);
    scope->vehicle=vehicle;
}

static void test_batch_scope(void)
{
    VehicleLightDiagnosticState *d=&g_shadow_engine.vehicle_diagnostics;
    unsigned i;
    prepare_capture(0); d->started_tick=GetTickCount();
    d->batch_tls=TlsAlloc(); assert(d->batch_tls!=TLS_OUT_OF_INDEXES);
    d->batch_tls_valid=1; d->batch_enabled=1; d->original_light_batch=fake_vehicle_batch;
    d->owner_enabled=1;
    d->light_batch_target=(unsigned char *)(uintptr_t)0x1000;
    for(i=0;i<2;++i) {
        *(void **)batch_vehicle[i]=(void *)(uintptr_t)(0x1110+i);
        *(void **)(batch_vehicle[i]+0x148)=batch_components[i];
        *(uint32_t *)(batch_vehicle[i]+0x150)=2;
    }
    batch_nest=1; batch_calls=batch_depth=0;
    hooked_vehicle_light_batch(batch_vehicle[0],0.0125f,(void *)(uintptr_t)0x456,(void *)(uintptr_t)0x789,123.25f);
    assert(batch_calls==2 && TlsGetValue(d->batch_tls)==NULL);
    d->state=0;
    hooked_vehicle_light_batch((void *)(uintptr_t)1,0.0125f,(void *)(uintptr_t)0x456,(void *)(uintptr_t)0x789,123.25f);
    assert(batch_calls==3 && TlsGetValue(d->batch_tls)==NULL);
    /* Inject entry and restoration failures independently. A failed restore
     * must disable future reads of the expired borrowed stack scope. */
    d->state=2; d->owner_enabled=0; batch_nest=0;
    for(i=1;i<=2;++i) {
        d->batch_enabled=1; tls_set_calls=0; tls_fail_call=i;
        hooked_vehicle_light_batch(batch_vehicle[0],0.0125f,(void *)(uintptr_t)0x456,(void *)(uintptr_t)0x789,123.25f);
        assert(!d->batch_enabled && d->batch_tls_failures==i);
        TlsSetValue(d->batch_tls,NULL);
    }
    tls_fail_call=0;
    rollback_vehicle_owner_observer(); assert(!d->batch_enabled && !d->batch_tls_valid);
    puts("PASS batch float/stack ABI, two nested owners, capture integration, TLS restoration/faults, array bounds/alignment, caller/skeleton/transform/serial exclusions and idle passthrough");
}

static unsigned char selection_vehicles[6][0x160];
static unsigned char selection_components[6][0x260];
static unsigned char selection_lights[6][2][0x30];
static unsigned char selection_candidates[13][0x18];
static unsigned char selection_descriptors[13][0x44];
static unsigned char selection_spatials[13][0x58];

static void test_nearest_vehicle_selection(void)
{
    VehicleLightDiagnosticState *d=&g_shadow_engine.vehicle_diagnostics;
    VehicleBatchScope scope;
    VehicleCaptureTicket ticket;
    VehicleCandidateSample *sample;
    VehicleCandidateSample delta_sample;
    VehicleSelectionSnapshot delta_snapshot;
    VehicleLimiterChain limiter_chain;
    unsigned char *node;
    uint64_t handle;
    unsigned vehicle,light,index=0,kept=0;
    prepare_capture(0);
    d->batch_tls=TlsAlloc(); assert(d->batch_tls!=TLS_OUT_OF_INDEXES);
    d->batch_tls_valid=1; d->batch_enabled=1; d->selection_enabled=1;
    d->light_batch_target=(unsigned char *)(uintptr_t)0x1000;
    memset(selection_vehicles,0,sizeof(selection_vehicles));
    memset(selection_components,0,sizeof(selection_components));
    memset(selection_lights,0,sizeof(selection_lights));
    memset(selection_candidates,0,sizeof(selection_candidates));
    memset(selection_descriptors,0,sizeof(selection_descriptors));
    memset(selection_spatials,0,sizeof(selection_spatials));
    g_shadow_engine.renderer.manager_calls=10;
    for(vehicle=0;vehicle<6;++vehicle) {
        *(void **)(selection_vehicles[vehicle]+VEHICLE_BATCH_ARRAY_OFFSET)=selection_components[vehicle];
        *(uint32_t *)(selection_vehicles[vehicle]+VEHICLE_BATCH_COUNT_OFFSET)=2;
        scope.serial=d->serial; scope.vehicle=selection_vehicles[vehicle];
        scope.skeleton=(void *)(uintptr_t)0x456; scope.world_transform=(void *)(uintptr_t)0x789;
        scope.distance_squared=(float)(vehicle+1U);
        TlsSetValue(d->batch_tls,&scope);
        for(light=0;light<2;++light,++index) {
            handle=0x100000000ULL+(uint64_t)index;
            *(void **)(selection_components[vehicle]+light*VEHICLE_BATCH_ELEMENT_BYTES+
                VEHICLE_OWNER_LIGHT_OFFSET)=selection_lights[vehicle][light];
            *(uint64_t *)(selection_lights[vehicle][light]+
                VEHICLE_OWNER_DESCRIPTOR_HANDLE_OFFSET)=handle;
            observe_and_publish_selection(selection_components[vehicle]+
                light*VEHICLE_BATCH_ELEMENT_BYTES,scope.skeleton,scope.world_transform,
                (uintptr_t)d->light_batch_target+VEHICLE_BATCH_RETURN_OFFSET);
            *(void **)(selection_candidates[index]+8)=selection_descriptors[index];
            *(void **)(selection_candidates[index]+0x10)=selection_spatials[index];
            *(int32_t *)(selection_descriptors[index]+8)=3;
            *(uint64_t *)(selection_spatials[index]+
                VEHICLE_SPATIAL_DESCRIPTOR_HANDLE_OFFSET)=handle;
            if(index) *(void **)selection_candidates[index-1]=selection_candidates[index];
        }
    }
    /* A world Spotlight3 candidate has no vehicle publication and must remain unknown. */
    *(void **)(selection_candidates[12]+8)=selection_descriptors[12];
    *(void **)(selection_candidates[12]+0x10)=selection_spatials[12];
    *(int32_t *)(selection_descriptors[12]+8)=3;
    *(uint64_t *)(selection_spatials[12]+VEHICLE_SPATIAL_DESCRIPTOR_HANDLE_OFFSET)=0xABCDEFULL;
    *(void **)selection_candidates[11]=selection_candidates[12];
    TlsSetValue(d->batch_tls,NULL);
    ticket=begin_vehicle_candidate_sample(selection_candidates[0],13,11);
    assert(ticket.active); sample=&d->samples[0];
    assert(sample->selection_snapshot_records==12 && sample->selection_spotlight3==13);
    assert(sample->manager_call==11 && sample->selection_snapshot_vehicles==6);
    assert(sample->selection_snapshot_age_le_16==12 &&
           !sample->selection_snapshot_age_17_50 &&
           !sample->selection_snapshot_age_51_250 &&
           !sample->selection_snapshot_age_over_250);
    assert(sample->selection_exact_matches==12 && sample->selection_exact_vehicles==6 &&
           sample->selection_exact_complete_vehicles==6);
    assert(sample->selection_current_matches==12 && sample->selection_unknown==1);
    assert(sample->renderer_epoch==0 && sample->selection_renderer_same==12 &&
           !sample->selection_renderer_previous && !sample->selection_renderer_two_to_three &&
           !sample->selection_renderer_older && !sample->selection_renderer_future &&
           sample->selection_renderer_recent_matches==12);
    assert(!sample->selection_delta_zero && sample->selection_delta_one==12 &&
           !sample->selection_delta_two_to_four && !sample->selection_delta_five_plus &&
           !sample->selection_delta_future);
    assert(!sample->selection_older_matches && !sample->selection_ambiguous);
    assert(sample->selection_vehicles==6 && !sample->selection_partial_vehicles);
    assert(sample->selection_would_keep_lights==12);
    for(index=0;index<12;++index) {
        assert(sample->records[index].selection_group_complete);
        assert(sample->records[index].selection_manager_delta==1);
        assert(sample->records[index].selection_renderer_delta==0);
        assert(sample->records[index].selection_renderer_recent);
        assert(sample->records[index].selection_group_lights==2);
        if(sample->records[index].selection_would_keep) ++kept;
        assert(sample->records[index].selection_rank==index/2+1U);
        assert(sample->records[index].selection_would_keep);
    }
    assert(kept==12 && !sample->records[12].selection_matches &&
           !sample->records[12].selection_would_keep);
    prepare_vehicle_limiter_chain(&limiter_chain,selection_candidates[0],13,11,0);
    assert(limiter_chain.evaluated && limiter_chain.active &&
           !limiter_chain.fail_open);
    assert(limiter_chain.captured==13 && limiter_chain.complete_vehicles==6);
    assert(limiter_chain.classified==12 && limiter_chain.suppressed==4);
    assert(limiter_chain.count==9 && limiter_chain.head==selection_candidates[0]);
    node=(unsigned char *)limiter_chain.head;
    for(index=0;index<8;++index) {
        assert(node==selection_candidates[index]);
        node=*(unsigned char **)node;
    }
    assert(node==selection_candidates[12] && *(void **)node==NULL);
    restore_vehicle_limiter_chain(&limiter_chain);
    for(index=0;index<12;++index)
        assert(*(void **)selection_candidates[index]==selection_candidates[index+1]);
    assert(*(void **)selection_candidates[12]==NULL);
    assert(ShadowEngine_GetControlApiVersion()==2U);
    assert(ShadowEngine_GetVehicleHeadlightLimiterEnabled()==1);
    assert(ShadowEngine_SetVehicleHeadlightLimiterEnabled(0)==1);
    assert(ShadowEngine_GetVehicleHeadlightLimiterEnabled()==0);
    prepare_vehicle_limiter_chain(&limiter_chain,selection_candidates[0],13,11,0);
    assert(!limiter_chain.active && !limiter_chain.fail_open &&
           limiter_chain.count==13 && d->limiter_bypass_disabled==1);
    assert(ShadowEngine_SetVehicleHeadlightLimiterEnabled(1)==1);
    assert(ShadowEngine_GetVehicleHeadlightLimiterEnabled()==1);
    memset(&delta_sample,0,sizeof(delta_sample));
    delta_sample.tick=GetTickCount(); delta_sample.manager_call=11;
    delta_sample.renderer_epoch=0;
    delta_sample.copied_candidates=13;
    for(index=0;index<13;++index)
        assert(copy_vehicle_candidate(&delta_sample.records[index],selection_candidates[index]));
    snapshot_vehicle_selection(&delta_snapshot,0); assert(delta_snapshot.count==12);
    for(index=0;index<12;++index) {
        static const LONG publish_calls[6]={11,10,9,6,12,10};
        static const LONG publish_epochs[6]={0,-1,-2,-4,1,0};
        unsigned owner;
        for(owner=0;owner<6;++owner)
            if(delta_snapshot.records[index].vehicle==
               (uint64_t)(uintptr_t)selection_vehicles[owner]) break;
        assert(owner<6);
        delta_snapshot.records[index].publish_manager_call=publish_calls[owner];
        delta_snapshot.records[index].publish_renderer_epoch=publish_epochs[owner];
    }
    classify_vehicle_selection_sample(&delta_sample,&delta_snapshot);
    assert(delta_sample.selection_delta_zero==2 && delta_sample.selection_delta_one==4 &&
           delta_sample.selection_delta_two_to_four==2 &&
           delta_sample.selection_delta_five_plus==2 &&
           delta_sample.selection_delta_future==2);
    assert(delta_sample.selection_current_matches==4);
    assert(delta_sample.selection_older_matches==6);
    assert(delta_sample.selection_renderer_same==4);
    assert(delta_sample.selection_renderer_previous==2);
    assert(delta_sample.selection_renderer_two_to_three==2);
    assert(delta_sample.selection_renderer_older==2);
    assert(delta_sample.selection_renderer_future==2);
    assert(delta_sample.selection_renderer_recent_matches==8);
    assert(delta_sample.selection_renderer_nonrecent_matches==4);
    assert(delta_sample.selection_vehicles==4);
    assert(delta_sample.selection_would_keep_lights==8);
    finish_vehicle_candidate_sample(ticket,NULL,&limiter_chain);
    /* Wrong scope never publishes; contention and bounded replacement are explicit. */
    scope.vehicle=selection_vehicles[0]; scope.skeleton=(void *)(uintptr_t)0x456;
    scope.world_transform=(void *)(uintptr_t)0x789; scope.distance_squared=1.0f;
    TlsSetValue(d->batch_tls,&scope);
    observe_and_publish_selection(selection_components[0],NULL,scope.world_transform,
        (uintptr_t)d->light_batch_target+VEHICLE_BATCH_RETURN_OFFSET);
    assert(d->selection_bank_count[0]==12);
    d->selection_writer_active=1;
    observe_and_publish_selection(selection_components[0],scope.skeleton,scope.world_transform,
        (uintptr_t)d->light_batch_target+VEHICLE_BATCH_RETURN_OFFSET);
    d->selection_writer_active=0; assert(d->selection_gaps==1);
    snapshot_vehicle_selection(&delta_snapshot,0);
    assert(delta_snapshot.lock_acquired && delta_snapshot.gap_epochs==1 &&
           !delta_snapshot.overflow_epochs);
    for(index=d->selection_bank_count[0];index<VEHICLE_SELECTION_SLOTS_PER_EPOCH;++index) {
        VehicleBatchScope fill;
        memset(&fill,0,sizeof(fill)); fill.pending_count=1;
        fill.pending[0].descriptor_handle=0x900000000ULL+index;
        fill.pending[0].vehicle=0xA00000000ULL+index;
        fill.pending[0].tick=GetTickCount();
        publish_vehicle_selection_batch(&fill);
    }
    *(uint64_t *)(selection_lights[0][0]+VEHICLE_OWNER_DESCRIPTOR_HANDLE_OFFSET)=0x777777ULL;
    observe_and_publish_selection(selection_components[0],scope.skeleton,scope.world_transform,
        (uintptr_t)d->light_batch_target+VEHICLE_BATCH_RETURN_OFFSET);
    assert(d->selection_bank_count[0]==VEHICLE_SELECTION_SLOTS_PER_EPOCH &&
           d->selection_overflow==1 && !d->selection_rotations);
    snapshot_vehicle_selection(&delta_snapshot,0);
    assert(delta_snapshot.lock_acquired && delta_snapshot.gap_epochs==1 &&
           delta_snapshot.overflow_epochs==1);
    g_shadow_engine.renderer.renderer_calls=1;
    *(uint64_t *)(selection_lights[0][0]+VEHICLE_OWNER_DESCRIPTOR_HANDLE_OFFSET)=0x888888ULL;
    observe_and_publish_selection(selection_components[0],scope.skeleton,scope.world_transform,
        (uintptr_t)d->light_batch_target+VEHICLE_BATCH_RETURN_OFFSET);
    assert(d->selection_bank_epoch[1]==1 && d->selection_bank_count[1]==1 &&
           d->selection_rotations==1);
    snapshot_vehicle_selection(&delta_snapshot,1);
    assert(delta_snapshot.gap_epochs==1 && delta_snapshot.overflow_epochs==1);
    snapshot_vehicle_selection(&delta_snapshot,4);
    assert(!delta_snapshot.gap_epochs && !delta_snapshot.overflow_epochs);
    TlsSetValue(d->batch_tls,NULL); rollback_vehicle_owner_observer(); d->state=0;
    puts("PASS hybrid-six limiter: generation-aware renderer windows, epoch-scoped contention/saturation, runtime control, exact coherent groups, native-distance rank, transactional chain restoration and world fail-open");
}

static void test_vehicle_batch_publication(void)
{
    VehicleLightDiagnosticState *d=&g_shadow_engine.vehicle_diagnostics;
    VehicleBatchScope scope;
    VehicleSelectionSnapshot snapshot;
    uint64_t owner=0;
    LONG saved_epoch=g_shadow_engine.renderer.renderer_calls;
    unsigned i;
    prepare_capture(0);
    memset(&scope,0,sizeof(scope));
    d->selection_enabled=1;
    scope.pending_count=2;
    for(i=0;i<2;++i) {
        scope.pending[i].descriptor_handle=0x200000900ULL+i;
        scope.pending[i].vehicle=0xA000;
        scope.pending[i].distance_squared=25.0f;
        scope.pending[i].tick=GetTickCount();
    }
    /* Simulate renderer advancement while native code updates the two bulbs.
     * Staged records are invisible until the group commit, then share one epoch. */
    g_shadow_engine.renderer.renderer_calls=12;
    assert(!lookup_vehicle_quality_identity(scope.pending[0].descriptor_handle,12,&owner,NULL));
    g_shadow_engine.renderer.renderer_calls=16;
    publish_vehicle_selection_batch(&scope);
    snapshot_vehicle_selection(&snapshot,16);
    assert(snapshot.count==2 && d->selection_bank_count[0]==2);
    for(i=0;i<2;++i) {
        assert(snapshot.records[i].publish_renderer_epoch==16);
        assert(lookup_vehicle_quality_identity(scope.pending[i].descriptor_handle,16,&owner,NULL)==1);
        assert(owner==0xA000);
    }
    /* Full generation equality: a reused slot does not inherit this owner. */
    assert(!lookup_vehicle_quality_identity(0x300000900ULL,16,&owner,NULL));
    scope.pending[0].descriptor_handle=0x200000A00ULL;
    scope.pending[1].descriptor_handle=0x200000A01ULL;
    scope.pending_overflow=1;
    publish_vehicle_selection_batch(&scope);
    assert(d->selection_bank_count[0]==2 && d->selection_overflow==1);
    scope.pending_overflow=0;
    d->selection_writer_active=1;
    publish_vehicle_selection_batch(&scope);
    d->selection_writer_active=0;
    assert(d->selection_bank_count[0]==2 && d->selection_gaps==1);
    d->selection_bank_count[0]=VEHICLE_SELECTION_SLOTS_PER_EPOCH-1;
    publish_vehicle_selection_batch(&scope);
    assert(d->selection_overflow==2);
    for(i=0;i<2;++i)
        assert(!lookup_vehicle_quality_identity(scope.pending[i].descriptor_handle,16,&owner,NULL));
    assert(!d->selection_publish_inflight && !d->selection_writer_active);
    prepare_capture(0);
    g_shadow_engine.renderer.renderer_calls=saved_epoch;
    puts("PASS atomic vehicle publication: invisible staging, shared commit epoch, generation exclusion, contention and whole-group overflow rejection");
}

static void test_selection_index_equivalence(void)
{
    VehicleSelectionSnapshot snapshot;
    VehicleSelectionIndex index;
    VehicleCandidateRecord full,compact;
    unsigned round,i,h,linear_matches,indexed_matches,visited=0;
    for(round=0;round<8;++round) {
        memset(&snapshot,0,sizeof(snapshot));
        snapshot.count=round?VEHICLE_SELECTION_SNAPSHOT_RECORDS:0;
        for(i=0;i<snapshot.count;++i) {
            VehicleSelectionRecord *r=&snapshot.records[i];
            r->descriptor_handle=0x100000000ULL+(i%193);
            r->vehicle=0x9000U+(i%193);
            if(round&1U) r->vehicle+=i%3;
            r->publish_renderer_epoch=(LONG)((i*7+round)%13)-3;
            r->tick=(DWORD)((i*5+round)%19);
        }
        index_vehicle_selection(&snapshot,&index);
        for(h=0;h<230;++h) {
            const VehicleSelectionRecord *expected=NULL,*actual;
            uint64_t handle=0x100000000ULL+h;
            linear_matches=0;
            for(i=0;i<snapshot.count;++i) {
                const VehicleSelectionRecord *r=&snapshot.records[i];
                if(r->descriptor_handle!=handle) continue;
                if(!expected) { expected=r; linear_matches=1; continue; }
                if(r->vehicle!=expected->vehicle) { linear_matches=2; continue; }
                if(r->publish_renderer_epoch>expected->publish_renderer_epoch ||
                   (r->publish_renderer_epoch==expected->publish_renderer_epoch &&
                    r->tick>expected->tick)) expected=r;
            }
            actual=match_vehicle_selection(&snapshot,&index,handle,&indexed_matches);
            assert(actual==expected && indexed_matches==linear_matches);
            if(round==1) {
                unsigned bucket=vehicle_selection_hash(handle,0)&
                    (VEHICLE_SELECTION_SLOTS_PER_EPOCH-1U);
                for(i=index.heads[bucket];i!=UINT32_MAX;i=index.next[i]) ++visited;
            }
        }
    }
    assert(visited<230U*VEHICLE_SELECTION_SNAPSHOT_RECORDS/8U);
    memset(candidate,0,sizeof(candidate)); memset(descriptor,0,sizeof(descriptor));
    memset(spatial,0,sizeof(spatial));
    *(void **)(candidate+8)=descriptor; *(void **)(candidate+0x10)=spatial;
    *(int32_t *)(descriptor+8)=3; *(uint32_t *)(descriptor+0x40)=0x80;
    *(float *)(spatial+0x14)=17.0f;
    *(uint64_t *)(spatial+VEHICLE_SPATIAL_DESCRIPTOR_HANDLE_OFFSET)=0x200000901ULL;
    memset(&full,0,sizeof(full)); memset(&compact,0,sizeof(compact));
    assert(copy_vehicle_candidate(&full,candidate));
    assert(copy_vehicle_candidate_fields(&compact,candidate,0));
    assert(full.descriptor_handle==compact.descriptor_handle &&
        full.descriptor_handle_valid==compact.descriptor_handle_valid &&
        full.renderer_type==compact.renderer_type && full.next==compact.next &&
        full.descriptor==compact.descriptor && full.spatial==compact.spatial);
    assert(full.observed_x==17.0f && full.descriptor_flags==0x80);
    assert(compact.observed_x==0.0f && compact.descriptor_flags==0);
    printf("PASS indexed lookup equivalence:1840 queries, empty/full snapshots, collisions, owners, epoch/tick ties; bucket visits=%u versus linear=%u; compact policy fields unchanged\n",
        visited,230U*VEHICLE_SELECTION_SNAPSHOT_RECORDS);
}

typedef struct SelectionProjectionFixture {
    VehicleSelectionSnapshot full,summary;
    VehicleSelectionProjection projection;
    VehicleSelectionIndex index;
    VehicleCandidateSample reference,projected;
} SelectionProjectionFixture;

static void publish_projection_record(LONG epoch,uint64_t handle,uint64_t owner,
    DWORD tick,float distance_squared)
{
    VehicleBatchScope scope;
    memset(&scope,0,sizeof(scope)); scope.pending_count=1;
    scope.pending[0].descriptor_handle=handle; scope.pending[0].vehicle=owner;
    scope.pending[0].tick=tick; scope.pending[0].distance_squared=distance_squared;
    scope.pending[0].position_valid=1;
    scope.pending[0].position[0]=distance_squared;
    scope.pending[0].position[1]=2.0f; scope.pending[0].position[2]=3.0f;
    g_shadow_engine.renderer.renderer_calls=epoch;
    g_shadow_engine.renderer.manager_calls=epoch+40;
    publish_vehicle_selection_batch(&scope);
}

static void projection_candidate(VehicleCandidateSample *sample,unsigned index,
    uint64_t handle,int renderer_type)
{
    VehicleCandidateRecord *candidate=&sample->records[index];
    memset(candidate,0,sizeof(*candidate));
    candidate->renderer_type=renderer_type;
    candidate->descriptor_handle_valid=1; candidate->descriptor_handle=handle;
    if(sample->copied_candidates<=index) sample->copied_candidates=index+1;
}

/* The full snapshot/index remains the independent oracle. Compare complete
 * winning records, not just owners, so encounter-order and update ties matter. */
static void assert_projection_equivalence(SelectionProjectionFixture *fixture,
    const VehicleCandidateSample *input)
{
    VehicleLightDiagnosticState *d=&g_shadow_engine.vehicle_diagnostics;
    LONG gaps_before=d->selection_gaps,gaps_after;
    unsigned i;
    snapshot_vehicle_selection(&fixture->full,input->renderer_epoch);
    gaps_after=d->selection_gaps;
    /* Both alternatives see the same failure-counter starting state. */
    d->selection_gaps=gaps_before;
    memset(&fixture->projection,0xA5,sizeof(fixture->projection));
    snapshot_vehicle_selection_projection(&fixture->summary,&fixture->projection,input);
    assert(d->selection_gaps==gaps_after);
    assert(!memcmp(&fixture->full,&fixture->summary,
        offsetof(VehicleSelectionSnapshot,records)));
    if(fixture->projection.fallback_full_snapshot) {
        assert(!memcmp(fixture->full.records,fixture->summary.records,
            fixture->full.count*sizeof(fixture->full.records[0])));
    } else {
        index_vehicle_selection(&fixture->full,&fixture->index);
        for(i=0;i<input->copied_candidates;++i) {
            const VehicleCandidateRecord *candidate=&input->records[i];
            const VehicleSelectionRecord *expected=NULL;
            uint32_t matches=0;
            if(candidate->renderer_type==3 && candidate->descriptor_handle_valid)
                expected=match_vehicle_selection(&fixture->full,&fixture->index,
                    candidate->descriptor_handle,&matches);
            assert(fixture->projection.matches[i]==matches);
            if(expected) assert(!memcmp(expected,&fixture->projection.records[i],sizeof(*expected)));
        }
    }
    memcpy(&fixture->reference,input,sizeof(*input));
    memcpy(&fixture->projected,input,sizeof(*input));
    classify_vehicle_selection_fields(&fixture->reference,&fixture->full,0);
    classify_vehicle_selection_projected_fields(&fixture->projected,&fixture->summary,
        &fixture->projection,0);
    assert(!memcmp(&fixture->reference,&fixture->projected,sizeof(*input)));
    for(i=0;i<input->copied_candidates;++i)
        if(input->records[i].renderer_type!=3)
            assert(!memcmp(&input->records[i],&fixture->projected.records[i],
                sizeof(input->records[i])));
}

/* Opt in with SHADOW_ENGINE_SELECTION_BENCHMARK=1. This is only the patch-owned
 * ownership snapshot/classifier stage, not native pointer validation, manager
 * preparation as a whole, shadow rendering, or a prediction of in-game FPS. */
static void benchmark_selection_projection(SelectionProjectionFixture *fixture,
    const VehicleCandidateSample *input)
{
    char enabled[2];
    LARGE_INTEGER frequency,start,end;
    LONGLONG full_ticks=0,projected_ticks=0;
    unsigned iteration,pass,fallbacks=0;
    const unsigned iterations=100;
    if(GetEnvironmentVariableA("SHADOW_ENGINE_SELECTION_BENCHMARK",enabled,sizeof(enabled))!=1 ||
       enabled[0]!='1' || !QueryPerformanceFrequency(&frequency)) return;
    for(iteration=0;iteration<iterations;++iteration) {
        /* Copies and equivalence checks are outside the timed region. Alternate
         * the first path to avoid always giving one path the warmest cache. */
        memcpy(&fixture->reference,input,sizeof(*input));
        memcpy(&fixture->projected,input,sizeof(*input));
        for(pass=0;pass<2;++pass) {
            if((iteration+pass)&1U) {
                QueryPerformanceCounter(&start);
                snapshot_vehicle_selection_projection(&fixture->summary,&fixture->projection,input);
                classify_vehicle_selection_projected_fields(&fixture->projected,&fixture->summary,
                    &fixture->projection,0);
                QueryPerformanceCounter(&end);
                projected_ticks+=end.QuadPart-start.QuadPart;
                fallbacks+=fixture->projection.fallback_full_snapshot!=0;
            } else {
                QueryPerformanceCounter(&start);
                snapshot_vehicle_selection(&fixture->full,input->renderer_epoch);
                classify_vehicle_selection_fields(&fixture->reference,&fixture->full,0);
                QueryPerformanceCounter(&end);
                full_ticks+=end.QuadPart-start.QuadPart;
            }
        }
        assert(!memcmp(&fixture->reference,&fixture->projected,sizeof(*input)));
    }
    printf("INFO offline ownership snapshot/classification: iterations=%u candidates=%u records=%u full_mean=%.3f us projected_mean=%.3f us fallback=%u; excludes native validation, rendering and FPS\n",
        iterations,input->copied_candidates,fixture->full.count,
        (double)full_ticks*1000000.0/(double)frequency.QuadPart/iterations,
        (double)projected_ticks*1000000.0/(double)frequency.QuadPart/iterations,fallbacks);
}

static void test_selection_projection_equivalence(void)
{
    VehicleLightDiagnosticState *d=&g_shadow_engine.vehicle_diagnostics;
    SelectionProjectionFixture *fixture=calloc(1,sizeof(*fixture));
    VehicleCandidateSample *sample=calloc(1,sizeof(*sample));
    VehicleBatchScope scope;
    uint32_t saved_heads[VEHICLE_SELECTION_EPOCH_BANKS][VEHICLE_SELECTION_SLOTS_PER_EPOCH];
    uint32_t saved_next[VEHICLE_SELECTION_EPOCH_BANKS][VEHICLE_SELECTION_SLOTS_PER_EPOCH];
    uint64_t collision_handles[VEHICLE_SELECTION_SLOTS_PER_EPOCH];
    LONG saved_epoch=g_shadow_engine.renderer.renderer_calls;
    LONG saved_manager=g_shadow_engine.renderer.manager_calls;
    uint64_t handle=0x100001234ULL,collider,low_owner=0,high_owner=0;
    unsigned i,bank,offset,limit;
    assert(fixture && sample);
    prepare_capture(0); sample->tick=200; sample->manager_call=100; sample->renderer_epoch=5;
    projection_candidate(sample,0,handle,3);
    assert_projection_equivalence(fixture,sample); /* Disabled/empty clears poisoned matches. */
    d->selection_enabled=1;
    assert_projection_equivalence(fixture,sample);
    assert(!fixture->summary.count && fixture->summary.lock_acquired);

    publish_projection_record(5,handle,0xA000,100,25.0f);
    publish_projection_record(5,handle+1,0xA000,100,25.0f);
    projection_candidate(sample,1,handle+1,3);
    projection_candidate(sample,2,handle,1);
    projection_candidate(sample,3,handle+1,0);
    projection_candidate(sample,4,0xDEADBEEF,3); /* Unknown world light stays unknown. */
    projection_candidate(sample,5,handle,3); sample->records[5].descriptor_handle_valid=0;
    assert_projection_equivalence(fixture,sample);
    assert(fixture->summary.count==2 && fixture->projected.selection_would_keep_lights==2);
    assert(!fixture->projection.matches[2] && !fixture->projection.matches[3] &&
        !fixture->projection.matches[4] && !fixture->projection.matches[5]);
    publish_projection_record(5,handle,0xA000,110,4.0f);
    publish_projection_record(5,handle,0xA000,105,9.0f);
    assert_projection_equivalence(fixture,sample);
    assert(fixture->summary.count==2 && fixture->projection.records[0].observations==3 &&
        fixture->projection.records[0].tick==105 && fixture->projection.records[0].distance_squared==9.0f);

    /* Old conflicting owners must remain ambiguous before any freshness test. */
    prepare_capture(0); d->selection_enabled=1;
    publish_projection_record(0,handle,0xA000,100,10.0f);
    publish_projection_record(5,handle,0xB000,120,1.0f);
    assert_projection_equivalence(fixture,sample);
    assert(fixture->projection.matches[0]==2 && fixture->projection.records[0].vehicle==0xA000);
    assert(fixture->projected.selection_ambiguous==1 && !fixture->projected.records[0].selection_would_keep);

    prepare_capture(0); d->selection_enabled=1;
    publish_projection_record(7,handle,0xA000,120,4.0f);
    publish_projection_record(10,handle,0xA000,110,9.0f);
    sample->renderer_epoch=8;
    assert_projection_equivalence(fixture,sample);
    assert(fixture->projection.matches[0]==1 && fixture->projection.records[0].publish_renderer_epoch==10);
    assert(fixture->projected.selection_renderer_future==1 && !fixture->projected.records[0].selection_renderer_recent);

    /* Publish the higher physical slot first. Matching must still encounter
     * the lower slot first when epoch/tick tie and owners conflict. */
    prepare_capture(0); d->selection_enabled=1; sample->renderer_epoch=2;
    for(i=1;i<10000 && (!low_owner || !high_owner);++i) {
        unsigned slot=vehicle_selection_hash(handle,i)&(VEHICLE_SELECTION_SLOTS_PER_EPOCH-1U);
        if(slot<64 && !low_owner) low_owner=i;
        if(slot>=384 && !high_owner) high_owner=i;
    }
    assert(low_owner && high_owner);
    publish_projection_record(2,handle,high_owner,100,1.0f);
    publish_projection_record(2,handle,low_owner,100,2.0f);
    assert_projection_equivalence(fixture,sample);
    assert(fixture->projection.matches[0]==2 && fixture->projection.records[0].vehicle==low_owner);

    /* Deliberate projection-bucket collision plus same slot/different generation. */
    collider=0x400000000ULL;
    while((vehicle_selection_hash(collider,0)&(VEHICLE_SELECTION_SLOTS_PER_EPOCH-1U))!=
          (vehicle_selection_hash(handle,0)&(VEHICLE_SELECTION_SLOTS_PER_EPOCH-1U))) ++collider;
    prepare_capture(0); d->selection_enabled=1;
    publish_projection_record(2,handle,0xA000,100,1.0f);
    publish_projection_record(2,collider,0xB000,100,2.0f);
    publish_projection_record(2,handle+0x100000000ULL,0xC000,100,3.0f);
    projection_candidate(sample,1,collider,3);
    projection_candidate(sample,4,handle+0x100000000ULL,3);
    assert_projection_equivalence(fixture,sample);
    assert(fixture->projection.records[0].vehicle==0xA000 &&
        fixture->projection.records[1].vehicle==0xB000 && fixture->projection.records[4].vehicle==0xC000);
    publish_projection_record(6,handle+7,0xD000,150,5.0f);
    sample->renderer_epoch=6; projection_candidate(sample,5,handle+7,3);
    assert_projection_equivalence(fixture,sample);
    assert(fixture->summary.count==1 && !fixture->projection.matches[0] &&
        !fixture->projection.matches[1] && !fixture->projection.matches[4] && fixture->projection.matches[5]==1);

    /* Fill every bank exclusively through committed groups. Repeated handles,
     * conflicting owners and duplicate candidates exercise the full-sized oracle. */
    prepare_capture(0); d->selection_enabled=1; memset(sample,0,sizeof(*sample));
    sample->renderer_epoch=5; sample->tick=200; sample->manager_call=100;
    for(bank=0;bank<VEHICLE_SELECTION_EPOCH_BANKS;++bank) {
        g_shadow_engine.renderer.renderer_calls=(LONG)bank+2;
        g_shadow_engine.renderer.manager_calls=(LONG)bank+40;
        for(offset=0;offset<VEHICLE_SELECTION_SLOTS_PER_EPOCH;offset+=VEHICLE_BATCH_PUBLICATION_MAX) {
            memset(&scope,0,sizeof(scope)); scope.pending_count=VEHICLE_BATCH_PUBLICATION_MAX;
            for(i=0;i<scope.pending_count;++i) {
                VehicleSelectionRecord *record=&scope.pending[i];
                unsigned ordinal=offset+i;
                record->descriptor_handle=0x500000000ULL+ordinal;
                record->vehicle=0x9000+ordinal/2;
                if(bank==1 && !(ordinal%17)) ++record->vehicle;
                record->tick=100+bank; record->distance_squared=(float)(ordinal/2+1);
            }
            publish_vehicle_selection_batch(&scope);
        }
    }
    for(i=0;i<VEHICLE_CAPTURE_MAX_CANDIDATES;++i)
        projection_candidate(sample,i,0x500000000ULL+(i%31 ? i:i/2),i%19 ? 3:1);
    for(limit=1;limit<=10;++limit) {
        assert(shadow_policy_set_vehicle_limit(limit));
        assert_projection_equivalence(fixture,sample);
        assert(fixture->summary.count==VEHICLE_SELECTION_SNAPSHOT_RECORDS);
    }
    benchmark_selection_projection(fixture,sample);
    memcpy(saved_heads,d->selection_heads,sizeof(saved_heads));
    memcpy(saved_next,d->selection_next,sizeof(saved_next));
    memset(&scope,0,sizeof(scope)); scope.pending_count=1;
    scope.pending[0].descriptor_handle=0x600000000ULL; scope.pending[0].vehicle=0xE000;
    scope.pending[0].tick=200;
    publish_vehicle_selection_batch(&scope); /* No free capacity: publish no index entry. */
    assert(d->selection_overflow==1 && !memcmp(saved_heads,d->selection_heads,sizeof(saved_heads)) &&
        !memcmp(saved_next,d->selection_next,sizeof(saved_next)));
    projection_candidate(sample,1,scope.pending[0].descriptor_handle,3);
    assert_projection_equivalence(fixture,sample);
    assert(fixture->summary.overflow_epochs==1 && !fixture->projection.matches[1]);
    scope.pending_overflow=1; publish_vehicle_selection_batch(&scope);
    assert(d->selection_overflow==2 && !memcmp(saved_heads,d->selection_heads,sizeof(saved_heads)) &&
        !memcmp(saved_next,d->selection_next,sizeof(saved_next)));

    d->selection_publish_inflight=1;
    d->selection_gap_epoch_tag[2]=3; /* Epoch2 is still in the proven recent window. */
    assert_projection_equivalence(fixture,sample);
    assert(fixture->summary.publisher_overlap && fixture->summary.gap_epochs==1 &&
        fixture->summary.overflow_epochs==1);
    d->selection_publish_inflight=0; d->selection_writer_active=1;
    assert_projection_equivalence(fixture,sample);
    assert(!fixture->summary.lock_acquired && !fixture->summary.count && fixture->summary.gaps==1);
    d->selection_writer_active=0;
    assert_projection_equivalence(fixture,sample);
    assert(fixture->summary.lock_acquired && fixture->summary.count==VEHICLE_SELECTION_SNAPSHOT_RECORDS);

    /* Adversarial handles all share one projection bucket. Even distinct
     * candidate queries must trigger the bounded full-snapshot fallback. */
    prepare_capture(0); d->selection_enabled=1; memset(sample,0,sizeof(*sample));
    sample->renderer_epoch=9; sample->tick=200; sample->manager_call=100;
    collider=0x700000000ULL;
    for(i=0;i<VEHICLE_SELECTION_SLOTS_PER_EPOCH;++i) {
        while(vehicle_selection_hash(collider,0)&(VEHICLE_SELECTION_SLOTS_PER_EPOCH-1U)) ++collider;
        collision_handles[i]=collider++;
    }
    g_shadow_engine.renderer.renderer_calls=9;
    g_shadow_engine.renderer.manager_calls=49;
    for(offset=0;offset<VEHICLE_SELECTION_SLOTS_PER_EPOCH;offset+=VEHICLE_BATCH_PUBLICATION_MAX) {
        memset(&scope,0,sizeof(scope)); scope.pending_count=VEHICLE_BATCH_PUBLICATION_MAX;
        for(i=0;i<scope.pending_count;++i) {
            VehicleSelectionRecord *record=&scope.pending[i];
            record->descriptor_handle=collision_handles[offset+i];
            record->vehicle=0xF000+(offset+i)/2;
            record->tick=100; record->distance_squared=(float)((offset+i)/2+1);
        }
        publish_vehicle_selection_batch(&scope);
    }
    for(i=0;i<VEHICLE_CAPTURE_MAX_CANDIDATES;++i)
        projection_candidate(sample,i,collision_handles[i],3);
    assert_projection_equivalence(fixture,sample);
    assert(fixture->projection.fallback_full_snapshot &&
        fixture->summary.count==VEHICLE_SELECTION_SLOTS_PER_EPOCH);
    /* Repeated candidate handles retain the same ambiguity/fail-open result
     * when the bound, rather than a complete projection, ends lookup. */
    for(i=0;i<VEHICLE_CAPTURE_MAX_CANDIDATES;++i)
        projection_candidate(sample,i,collision_handles[0],3);
    assert_projection_equivalence(fixture,sample);
    assert(fixture->projection.fallback_full_snapshot &&
        fixture->projected.selection_ambiguous==VEHICLE_CAPTURE_MAX_CANDIDATES);
    assert(shadow_policy_set_vehicle_limit(VEHICLE_SELECTION_STABLE_VEHICLES));
    prepare_capture(0); g_shadow_engine.renderer.renderer_calls=saved_epoch;
    g_shadow_engine.renderer.manager_calls=saved_manager;
    free(sample); free(fixture);
    puts("PASS candidate projection: full-snapshot/sample equivalence, old conflicts, future epochs, slot order, full handles, updates, rotation, atomic overflow, lock/overlap, all-limit full-bank stress and bounded collision fallback");
}

static void test_protected_admission_summary(void)
{
    VehicleCandidateSample captured;
    VehicleLimiterChain chain;
    VehicleQueueSample queue;
    uint32_t i;
    memset(&captured,0,sizeof(captured));
    memset(&chain,0,sizeof(chain));
    memset(&queue,0,sizeof(queue));
    chain.enabled=1; chain.evaluated=1; chain.active=1; chain.suppressed=4;
    chain.sample.copied_candidates=10;
    captured.copied_candidates=10;
    for(i=0;i<8;++i) {
        VehicleCandidateRecord *source=&chain.sample.records[i];
        VehicleCandidateRecord *record=&captured.records[i];
        source->candidate=record->candidate=(void *)(uintptr_t)(0x1000U+i*0x10U);
        source->renderer_type=record->renderer_type=3;
        source->selection_vehicle=0x5000U+i/2U;
        source->selection_would_keep=1;
        record->descriptor_handle=0x7000U+i;
        record->descriptor_handle_valid=1;
        record->admitted=i!=7U;
    }
    for(i=8;i<10;++i) {
        VehicleCandidateRecord *source=&chain.sample.records[i];
        VehicleCandidateRecord *record=&captured.records[i];
        source->candidate=record->candidate=(void *)(uintptr_t)(0x1000U+i*0x10U);
        source->renderer_type=record->renderer_type=1;
        source->selection_vehicle=0x9000U;
        source->selection_vehicle_type1=1;
        source->selection_matches=1;
        source->selection_candidate_instances=1;
        source->selection_renderer_recent=1;
        record->descriptor_handle=0x7000U+i;
        record->descriptor_handle_valid=1;
        record->admitted=1;
    }
    captured.type1_candidates=2;
    summarize_vehicle_policy_admission(&captured,&chain);
    assert(captured.limiter_enabled && captured.limiter_evaluated &&
           captured.limiter_active && captured.limiter_suppressed==4);
    assert(captured.protected_headlight_lights==8 &&
           captured.protected_headlight_admitted_lights==7 &&
           captured.protected_headlight_vehicles==4);
    assert(captured.protected_headlight_fully_admitted_vehicles==3 &&
           captured.protected_headlight_partially_admitted_vehicles==1 &&
           !captured.protected_headlight_not_admitted_vehicles);
    assert(captured.vehicle_type1_candidates==2 &&
           captured.vehicle_type1_admitted_candidates==2 &&
           captured.vehicle_type1_vehicles==1 &&
           captured.vehicle_type1_admitted_vehicles==1 &&
           captured.protected_loss_with_vehicle_type1_admitted);
    queue.tick=captured.tick=100;
    queue.copied_entries=2;
    queue.records[0].submitted=1;
    queue.records[0].descriptor_handle_valid=1;
    queue.records[0].descriptor_handle=captured.records[0].descriptor_handle;
    queue.records[0].face_count=6;
    queue.records[1].submitted=1;
    queue.records[1].descriptor_handle_valid=1;
    queue.records[1].descriptor_handle=captured.records[8].descriptor_handle;
    queue.records[1].face_count=1;
    correlate_vehicle_queue_window(&captured,&queue);
    assert(captured.queue_protected_handle_matches==1 &&
           captured.queue_protected_faces==6 &&
           captured.queue_vehicle_type1_handle_matches==1 &&
           captured.queue_vehicle_type1_faces==1 &&
           !captured.queue_handle_ambiguous &&
           !captured.queue_correlation_tick_delta_ms);
    puts("PASS protected admission summary, vehicle-owned type-1 separation and same-window face correlation");
}

static void populate_residency_sample(VehicleLimiterChain *chain,DWORD tick)
{
    uint32_t vehicle,light,index=0;
    memset(chain,0,sizeof(*chain));
    chain->sample.tick=tick;
    chain->sample.copied_candidates=14;
    for(vehicle=0;vehicle<7;++vehicle)
        for(light=0;light<2;++light,++index) {
            VehicleCandidateRecord *record=&chain->sample.records[index];
            record->renderer_type=3;
            record->selection_vehicle=0xA000U+vehicle;
            record->descriptor_handle=0x100000000ULL+vehicle*2U+light;
            record->selection_distance_squared=(float)((vehicle+1U)*25U);
            record->selection_group_complete=1;
            record->selection_renderer_recent=1;
            record->selection_rank=vehicle+1U;
            record->selection_would_keep=vehicle<4U;
        }
    chain->sample.selection_would_keep_lights=8;
}

static void test_capture_analysis(void)
{
    VehicleCandidateRecord a,b;
    VehicleCandidateSample sample;
    double rate=0;
    memset(&a,0,sizeof(a)); memset(&sample,0,sizeof(sample));
    a.descriptor_handle_valid=1; a.descriptor_handle=123; a.selection_vehicle=456;
    a.selection_matches=1; a.selection_candidate_instances=1;
    a.selection_renderer_recent=1; a.selection_distance_squared=100;
    b=a; b.selection_distance_squared=25;
    assert(vehicle_capture_rope(&a,1000,&b,1500,&rate) && rate== -150.0);
    assert(vehicle_capture_rope(&b,1000,&a,1500,&rate) && rate==150.0);
    assert(vehicle_capture_rope(&a,1000,&a,1500,&rate) && rate==0.0);
    b.descriptor_handle=124;
    assert(!vehicle_capture_rope(&a,1000,&b,1500,&rate));
    b=a; b.selection_vehicle=457;
    assert(!vehicle_capture_rope(&a,1000,&b,1500,&rate));
    b=a; b.selection_renderer_recent=0;
    assert(!vehicle_capture_rope(&a,1000,&b,1500,&rate));
    b=a; b.selection_candidate_instances=2;
    assert(!vehicle_capture_rope(&a,1000,&b,1500,&rate));
    b=a;
    assert(!vehicle_capture_rope(&a,1000,&b,1001,&rate));
    assert(!vehicle_capture_rope(&a,1000,&b,4000,&rate));
    assert(vehicle_capture_rope(&a,0xFFFFFF00U,&b,244U,&rate));
    assert(vehicle_capture_elapsed_us(100,200,1000)==100000.0);
    assert(vehicle_capture_elapsed_us(100,99,1000)== -1.0);
    assert(vehicle_capture_elapsed_us(100,200,0)== -1.0);
    assert(!strcmp(vehicle_capture_admission_reason(&sample,&a),"nativeAdmissionUnavailable"));
    sample.admission_observed=1;
    assert(!strcmp(vehicle_capture_admission_reason(&sample,&a),"limiterOffNativeOmitted"));
    sample.limiter_enabled=1; a.selection_group_complete=1; a.policy_protected=1;
    assert(!strcmp(vehicle_capture_admission_reason(&sample,&a),"policyKeptNativeOmitted"));
    a.policy_protected=0; sample.residency_degraded=1;
    assert(!strcmp(vehicle_capture_admission_reason(&sample,&a),"notKeptDuringDegradedEvidence"));
    a.admitted=1;
    assert(!strcmp(vehicle_capture_admission_reason(&sample,&a),"nativeAdmitted"));
    puts("PASS copied capture analysis: closing/opening/stationary, identity/freshness/interval rejection, tick wrap, QPC validity and admission reasons");
}

static void test_vehicle_residency_policy(void)
{
    VehicleLightDiagnosticState *d=&g_shadow_engine.vehicle_diagnostics;
    VehicleLimiterChain chain;
    uint32_t i;
    assert(shadow_policy_set_vehicle_limit(4));
    d->residency_lock=0; d->residency_reset_requested=1;
    populate_residency_sample(&chain,1000U);
    assert(apply_vehicle_residency_policy(&chain));
    assert(chain.residency_initialized && chain.residency_residents==4);
    assert(chain.sample.selection_would_keep_lights==8);
    assert(!d->residency_slots[4].vehicle && !d->residency_slots[5].vehicle);

    /* No incoming timer: a new clearly nearest car enters on this call. */
    populate_residency_sample(&chain,1001U);
    chain.sample.records[12].selection_distance_squared=1;
    chain.sample.records[13].selection_distance_squared=1;
    assert(apply_vehicle_residency_policy(&chain));
    assert(residency_contains_stable_vehicle(d,0xA006U));
    assert(!residency_contains_stable_vehicle(d,0xA003U));
    assert(d->residency_slots[4].vehicle==0xA003U);
    assert(chain.residency_replaced && chain.residency_residents==5);
    /* Repeated observation must not restart an outgoing grace period. */
    populate_residency_sample(&chain,2000U);
    chain.sample.records[12].selection_distance_squared=1;
    chain.sample.records[13].selection_distance_squared=1;
    assert(apply_vehicle_residency_policy(&chain));
    assert(residency_contains_vehicle(d,0xA003U));
    populate_residency_sample(&chain,2001U);
    chain.sample.records[12].selection_distance_squared=1;
    chain.sample.records[13].selection_distance_squared=1;
    assert(apply_vehicle_residency_policy(&chain));
    assert(!residency_contains_vehicle(d,0xA003U));

    /* Small distance jitter keeps the incumbent primary; clear movement wins. */
    d->residency_reset_requested=1;
    populate_residency_sample(&chain,3000U);
    assert(apply_vehicle_residency_policy(&chain));
    populate_residency_sample(&chain,3001U);
    chain.sample.records[8].selection_distance_squared=95;
    chain.sample.records[9].selection_distance_squared=95;
    assert(apply_vehicle_residency_policy(&chain));
    assert(!residency_contains_vehicle(d,0xA004U));
    populate_residency_sample(&chain,3002U);
    chain.sample.records[8].selection_distance_squared=80;
    chain.sample.records[9].selection_distance_squared=80;
    assert(apply_vehicle_residency_policy(&chain));
    assert(residency_contains_stable_vehicle(d,0xA004U));
    assert(d->residency_slots[4].vehicle==0xA003U);
    /* Return to primary does not duplicate the tail. */
    populate_residency_sample(&chain,3003U);
    assert(apply_vehicle_residency_policy(&chain));
    assert(residency_contains_stable_vehicle(d,0xA003U));
    assert(d->residency_slots[4].vehicle==0xA004U);
    assert(chain.residency_residents==5);

    /* Three departures: keep two newest/nearest, never displace a primary. */
    populate_residency_sample(&chain,3004U);
    for(i=8;i<14;++i) chain.sample.records[i].selection_distance_squared=1+(float)i;
    assert(apply_vehicle_residency_policy(&chain));
    assert(chain.residency_residents==6);
    assert(residency_contains_stable_vehicle(d,0xA004U));
    assert(residency_contains_stable_vehicle(d,0xA005U));
    assert(residency_contains_stable_vehicle(d,0xA006U));
    assert(!residency_contains_vehicle(d,0xA003U));
    for(i=0;i<6;++i)
        for(uint32_t j=i+1;j<6;++j)
            assert(d->residency_slots[i].vehicle!=d->residency_slots[j].vehicle);

    /* Degraded snapshots freeze owners; no stale group gains authority. */
    populate_residency_sample(&chain,5000U); chain.degraded=1;
    chain.sample.records[12].selection_group_complete=0;
    chain.sample.records[13].selection_group_complete=0;
    assert(apply_vehicle_residency_policy(&chain));
    assert(residency_contains_stable_vehicle(d,0xA006U));
    assert(!chain.sample.records[12].selection_would_keep);
    chain.sample.tick=5005U;
    assert(apply_vehicle_residency_policy(&chain));
    assert(chain.sample.freeze_ms==5U && chain.sample.freeze_calls==2U);
    /* Complete evidence releases missing owners and expired tails. */
    chain.degraded=0;
    assert(apply_vehicle_residency_policy(&chain));
    assert(!residency_contains_vehicle(d,0xA006U));
    assert(chain.residency_residents<=6);

    /* Unsigned tick wrap keeps the same one-second retention duration. */
    d->residency_reset_requested=1;
    populate_residency_sample(&chain,0xFFFFFF00U);
    assert(apply_vehicle_residency_policy(&chain));
    populate_residency_sample(&chain,0xFFFFFF01U);
    chain.sample.records[12].selection_distance_squared=1;
    chain.sample.records[13].selection_distance_squared=1;
    assert(apply_vehicle_residency_policy(&chain));
    populate_residency_sample(&chain,0x2E8U);
    chain.sample.records[12].selection_distance_squared=1;
    chain.sample.records[13].selection_distance_squared=1;
    assert(apply_vehicle_residency_policy(&chain));
    assert(residency_contains_vehicle(d,0xA003U));
    chain.sample.tick=0x2E9U;
    assert(apply_vehicle_residency_policy(&chain));
    assert(!residency_contains_vehicle(d,0xA003U));
    d->residency_lock=1;
    populate_residency_sample(&chain,6000U);
    assert(!apply_vehicle_residency_policy(&chain));
    d->residency_lock=0;
    puts("PASS nearest primary + outgoing tails: immediate admission, expiry, hysteresis, promotion, overflow, degraded freeze, wrap and nonwaiting lock");
}

static void test_vehicle_shadow_quality_policy(void)
{
    VehicleLightDiagnosticState *d=&g_shadow_engine.vehicle_diagnostics;
    unsigned char queue[5U*RENDER_QUEUE_ENTRY_BYTES];
    unsigned char candidates[5][0x20],descriptors[5][0x20],spatials[5][0x60];
    uint64_t handles[5]={0x100000001ULL,0x100000002ULL,0x100000003ULL,
                         0x100000004ULL,0x100000005ULL};
    uint32_t i;
    memset(queue,0,sizeof(queue)); memset(candidates,0,sizeof(candidates));
    memset(descriptors,0,sizeof(descriptors)); memset(spatials,0,sizeof(spatials));
    memset(d->quality_records,0,sizeof(d->quality_records));
    d->selection_enabled=1; d->quality_overflow=0; d->quality_mutated_calls=0;
    d->quality_mutated_type1=0; d->quality_mutated_type3=0;
    d->quality_mutated_entries=0; d->quality_unknown_entries=0;
    d->quality_ambiguous_entries=0; g_shadow_engine.renderer.renderer_calls=12;
    g_shadow_engine.policy_control.disabled=0;
    g_shadow_engine.policy_control.quality_ready=1;
    for(i=0;i<5;++i) {
        unsigned char *entry=queue+(size_t)i*RENDER_QUEUE_ENTRY_BYTES;
        *(unsigned char **)(entry+VEHICLE_QUEUE_CANDIDATE_OFFSET)=candidates[i];
        *(unsigned char **)(candidates[i]+8U)=descriptors[i];
        *(unsigned char **)(candidates[i]+0x10U)=spatials[i];
        *(uint32_t *)(entry+VEHICLE_QUEUE_WIDTH_OFFSET)=4096U;
        *(uint32_t *)(entry+VEHICLE_QUEUE_HEIGHT_OFFSET)=4096U;
        memcpy(spatials[i]+VEHICLE_SPATIAL_DESCRIPTOR_HANDLE_OFFSET,
               &handles[i],sizeof(handles[i]));
        *(int32_t *)(descriptors[i]+8U)=i==1U ? 1 : 3;
    }
    publish_vehicle_quality_identity(0U,12,handles[0],0xA000U,25.0f);
    publish_vehicle_quality_identity(0U,12,handles[1],0xA001U,225.0f);
    publish_vehicle_quality_identity(0U,12,handles[2],0xA002U,625.0f);
    publish_vehicle_quality_identity(0U,12,handles[2],0xB002U,625.0f);
    publish_vehicle_quality_identity(0U,12,handles[4],0xA004U,625.0f);
    *(uint32_t *)(queue+4U*RENDER_QUEUE_ENTRY_BYTES+VEHICLE_QUEUE_WIDTH_OFFSET)=1024U;
    *(uint32_t *)(queue+4U*RENDER_QUEUE_ENTRY_BYTES+VEHICLE_QUEUE_HEIGHT_OFFSET)=512U;
    g_shadow_engine.policy_control.quality_ready=0;
    apply_vehicle_shadow_quality(queue,5U);
    assert(*(uint32_t *)(queue+VEHICLE_QUEUE_WIDTH_OFFSET)==4096U);
    g_shadow_engine.policy_control.quality_ready=1;
    d->selection_writer_active=1;
    apply_vehicle_shadow_quality(queue,5U);
    assert(*(uint32_t *)(queue+VEHICLE_QUEUE_WIDTH_OFFSET)==4096U);
    d->selection_writer_active=0;
    shadow_policy_set_disabled(SHADOW_POLICY_DISABLE_QUALITY,SHADOW_POLICY_DISABLE_QUALITY);
    apply_vehicle_shadow_quality(queue,5U);
    assert(*(uint32_t *)(queue+VEHICLE_QUEUE_WIDTH_OFFSET)==4096U);
    assert(ShadowEngine_GetVehicleHeadlightLimiterEnabled());
    shadow_policy_set_disabled(SHADOW_POLICY_DISABLE_QUALITY,0);
    apply_vehicle_shadow_quality(queue,5U);
    assert(*(uint32_t *)(queue+VEHICLE_QUEUE_WIDTH_OFFSET)==2048U);
    assert(*(uint32_t *)(queue+RENDER_QUEUE_ENTRY_BYTES+VEHICLE_QUEUE_HEIGHT_OFFSET)==2048U);
    assert(*(uint32_t *)(queue+2U*RENDER_QUEUE_ENTRY_BYTES+VEHICLE_QUEUE_WIDTH_OFFSET)==4096U);
    assert(*(uint32_t *)(queue+3U*RENDER_QUEUE_ENTRY_BYTES+VEHICLE_QUEUE_WIDTH_OFFSET)==4096U);
    assert(*(uint32_t *)(queue+4U*RENDER_QUEUE_ENTRY_BYTES+VEHICLE_QUEUE_WIDTH_OFFSET)==1024U);
    assert(d->quality_mutated_calls==1 && d->quality_mutated_entries==2 &&
           d->quality_mutated_type3==1 && d->quality_mutated_type1==1 &&
           d->quality_ambiguous_entries==1 && d->quality_unknown_entries==1);
    memset(d->quality_records,0,sizeof(d->quality_records));
    *(int32_t *)(descriptors[1]+8U)=1;
    publish_vehicle_quality_identity(0U,12,handles[0],0xA000U,25.0f);
    publish_vehicle_quality_identity(0U,12,handles[1],0xA001U,225.0f);
    publish_vehicle_quality_identity(0U,12,handles[2],0xA002U,625.0f);
    publish_vehicle_quality_identity(0U,12,handles[2],0xB002U,625.0f);
    publish_vehicle_quality_identity(0U,12,handles[4],0xA004U,625.0f);
    shadow_policy_set_disabled(SHADOW_POLICY_DISABLE_ALL,SHADOW_POLICY_DISABLE_ALL);
    assert(!ShadowEngine_GetVehicleHeadlightLimiterEnabled());
    ShadowEngine_SetVehicleHeadlightLimiterEnabled(1);
    assert(!shadow_policy_enabled(SHADOW_POLICY_DISABLE_QUALITY));
    /* Native record reuse supplies its next original request. Bypass must
     * preserve it; it does not rewrite a live resource after acquisition. */
    *(uint32_t *)(queue+VEHICLE_QUEUE_WIDTH_OFFSET)=4096U;
    apply_vehicle_shadow_quality(queue,5U);
    assert(*(uint32_t *)(queue+VEHICLE_QUEUE_WIDTH_OFFSET)==4096U);
    shadow_policy_set_disabled(SHADOW_POLICY_DISABLE_ALL,0);
    for(unsigned level=1;level<=4;++level) {
        unsigned cap=256U<<level;
        assert(shadow_policy_set_comparison(4,level,0));
        /* Simulate native rebuilding original requests, including upward transitions. */
        for(i=0;i<5;++i) {
            unsigned char *entry=queue+(size_t)i*RENDER_QUEUE_ENTRY_BYTES;
            *(uint32_t *)(entry+VEHICLE_QUEUE_WIDTH_OFFSET)=i==4?256U:4096U;
            *(uint32_t *)(entry+VEHICLE_QUEUE_HEIGHT_OFFSET)=i==4?128U:4096U;
        }
        apply_vehicle_shadow_quality(queue,5U);
        for(i=0;i<5;++i) {
            unsigned char *entry=queue+(size_t)i*RENDER_QUEUE_ENTRY_BYTES;
            assert(*(uint32_t *)(entry+VEHICLE_QUEUE_WIDTH_OFFSET)==(i<2?cap:i==4?256U:4096U));
            assert(*(uint32_t *)(entry+VEHICLE_QUEUE_HEIGHT_OFFSET)==(i<2?cap:i==4?128U:4096U));
        }
    }
    memset(d->quality_records,0,sizeof(d->quality_records));
    publish_vehicle_quality_identity(0U,12,handles[0],0xA000U,25.0f);
    publish_vehicle_quality_identity(0U,12,handles[2],0xA002U,625.0f);
    publish_vehicle_quality_identity(0U,12,handles[2],0xB002U,625.0f);
    g_shadow_engine.world_quality.mutated_calls=0;
    g_shadow_engine.world_quality.mutated_entries=0;
    for(i=0;i<5;++i) {
        unsigned char *entry=queue+(size_t)i*RENDER_QUEUE_ENTRY_BYTES;
        *(uint32_t *)(entry+VEHICLE_QUEUE_WIDTH_OFFSET)=4096U;
        *(uint32_t *)(entry+VEHICLE_QUEUE_HEIGHT_OFFSET)=4096U;
        *(int32_t *)(descriptors[i]+8U)=i==4U?0:3;
    }
    assert(shadow_policy_set_world_comparison(1,1));
    apply_world_shadow_quality(queue,5U);
    assert(*(uint32_t *)(queue+0U*RENDER_QUEUE_ENTRY_BYTES+VEHICLE_QUEUE_WIDTH_OFFSET)==4096U);
    assert(*(uint32_t *)(queue+1U*RENDER_QUEUE_ENTRY_BYTES+VEHICLE_QUEUE_WIDTH_OFFSET)==512U);
    assert(*(uint32_t *)(queue+2U*RENDER_QUEUE_ENTRY_BYTES+VEHICLE_QUEUE_WIDTH_OFFSET)==4096U);
    assert(*(uint32_t *)(queue+3U*RENDER_QUEUE_ENTRY_BYTES+VEHICLE_QUEUE_WIDTH_OFFSET)==512U);
    assert(*(uint32_t *)(queue+4U*RENDER_QUEUE_ENTRY_BYTES+VEHICLE_QUEUE_WIDTH_OFFSET)==4096U);
    assert(g_shadow_engine.world_quality.mutated_entries==2);
    assert(shadow_policy_set_world_comparison(3,0));
    assert(shadow_policy_set_comparison(4,3,0));
    puts("PASS vehicle/world quality: independent fixed caps, sun/exact/ambiguous separation and lower-request preservation");
}

static void fill_retire_proof(unsigned char *target)
{
    memcpy(target,g_vehicle_retire_prefix,sizeof(g_vehicle_retire_prefix)-1);
    memcpy(target+0x2BF,g_vehicle_retire_array,sizeof(g_vehicle_retire_array)-1);
    memcpy(target+0x2E1,g_vehicle_retire_stride,sizeof(g_vehicle_retire_stride)-1);
    memcpy(target+0x355,g_vehicle_retire_tail,sizeof(g_vehicle_retire_tail)-1);
}

static void test_retire_preflight(void)
{
    unsigned char *image=VirtualAlloc(NULL,0x2100000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    unsigned p,i;
    const unsigned offsets[]={0,0x2BF,0x2E1,0x355};
    assert(image); g_shadow_engine.bootstrap.disrupt_base=image;
    for(p=0;p<ARRAY_COUNT(g_runtime_profiles);++p) {
        unsigned char *target;
        select_runtime_profile_state(&g_runtime_profiles[p]);
        if(!g_runtime_profiles[p].vehicle_retire_rva) { assert(!validate_vehicle_retire_site()); continue; }
        target=image+g_runtime_profiles[p].vehicle_retire_rva;
        fill_retire_proof(target); assert(validate_vehicle_retire_site());
        for(i=0;i<ARRAY_COUNT(offsets);++i) {
            target[offsets[i]]^=1; assert(!validate_vehicle_retire_site());
            assert(!g_shadow_engine.vehicle_lifetime.retire_target); target[offsets[i]]^=1;
        }
        g_runtime_state.image_size=1; assert(!validate_vehicle_retire_site());
    }
    clear_runtime_profile(); assert(!validate_vehicle_retire_site());
    g_shadow_engine.bootstrap.disrupt_base=NULL; VirtualFree(image,0,MEM_RELEASE);
    puts("PASS retirement preflight: four synthetic profiles, disabled A4EE, proof corruption, short/unknown image");
}

static unsigned retire_calls;
static int retire_nested,retire_observe;
static VehicleOwnerRecord retirement_sample(uintptr_t owner)
{
    VehicleOwnerRecord sample;
    memset(&sample,0,sizeof(sample));
    sample.vehicle=(void *)owner; sample.vehicle_array=(void *)(owner+0x1000);
    sample.vehicle_vtable=(void *)(uintptr_t)0x123; sample.skeleton=(void *)(owner+0x2000);
    sample.vehicle_count=13; sample.vehicle_flags=63;
    return sample;
}
static void __fastcall fake_vehicle_retire(void *owner)
{
    ++retire_calls;
    assert(owner!=NULL);
    if(retire_nested) { retire_nested=0; hooked_vehicle_retire((void *)(uintptr_t)2); }
    if(retire_observe) {
        VehicleOwnerRecord sample=retirement_sample((uintptr_t)owner);
        observe_vehicle_lifetime(&sample,2);
    }
}

static void test_retirement_records(void)
{
    VehicleLifetimeDiagnosticState *d=&g_shadow_engine.vehicle_lifetime;
    VehicleOwnerRecord a=retirement_sample(1),b=retirement_sample(2),unknown=retirement_sample(3);
    VehicleRetireTicket ticket,duplicate;
    unsigned i;
    memset(d,0,sizeof(*d)); d->enabled=1; d->original_retire=fake_vehicle_retire;
    retire_calls=0;
    observe_vehicle_lifetime(&a,1); observe_vehicle_lifetime(&b,1);
    assert(d->count==2 && a.lifetime_sequence && b.lifetime_sequence>a.lifetime_sequence);
    assert(a.lifetime_slot==0 && b.lifetime_slot==1);
    unknown.vehicle_flags=31; observe_vehicle_lifetime(&unknown,1); assert(d->count==2);
    hooked_vehicle_retire((void *)(uintptr_t)3); assert(d->count==2 && retire_calls==1);
    retire_nested=1; hooked_vehicle_retire((void *)(uintptr_t)1);
    assert(retire_calls==3 && d->records[0].state==2 && d->records[1].state==2);
    assert(d->records[0].enter_sequence<d->records[1].enter_sequence);
    assert(d->records[1].return_sequence<d->records[0].return_sequence);
    assert(d->records[0].retire_entries==1 && d->records[0].retire_returns==1);
    a=retirement_sample(1); observe_vehicle_lifetime(&a,2);
    assert(d->records[0].reappearances==1 && d->records[0].state==0);
    a.vehicle_array=(void *)(uintptr_t)0x9000; observe_vehicle_lifetime(&a,2);
    assert(d->records[0].tuple_changes==1 && d->records[0].reappearances==1);
    ticket=begin_vehicle_retirement((void *)(uintptr_t)1);
    duplicate=begin_vehicle_retirement((void *)(uintptr_t)1);
    assert(ticket.sequence && !duplicate.sequence && d->records[0].overlaps==1);
    observe_vehicle_lifetime(&a,2); assert(d->records[0].samples_during_retire==1);
    finish_vehicle_retirement(ticket); assert(d->records[0].state==2);
    finish_vehicle_retirement(ticket); assert(d->gaps==1); /* Stale completion cannot replay. */
    d->writer_active=1; a.lifetime_sequence=0; observe_vehicle_lifetime(&a,3);
    assert(!a.lifetime_sequence && d->gaps==2);
    hooked_vehicle_retire((void *)(uintptr_t)1); assert(d->gaps==3);
    flush_vehicle_lifetime_snapshot(3); assert(d->gaps==4); d->writer_active=0;
    ticket=begin_vehicle_retirement((void *)(uintptr_t)2);
    d->writer_active=1; finish_vehicle_retirement(ticket); d->writer_active=0;
    assert(d->gaps==5 && d->records[1].state==1); /* Gap never becomes fake completed cleanup. */
    finish_vehicle_retirement(ticket);
    d->sequence=UINT64_MAX; a.lifetime_sequence=0; observe_vehicle_lifetime(&a,3);
    assert(!a.lifetime_sequence && d->gaps==6); d->sequence=100;
    flush_vehicle_lifetime_snapshot(3);
    assert(d->snapshot.count==2 && d->snapshot.gaps==6);
    d->records[0].owner=0xBAD;
    assert(d->snapshot.records[0].owner==1); /* Snapshot immutable after token release. */
    memset(d,0,sizeof(*d)); d->enabled=1; d->original_retire=fake_vehicle_retire;
    for(i=0;i<VEHICLE_LIFETIME_MAX_RECORDS;++i) {
        a=retirement_sample(0x1000+i); observe_vehicle_lifetime(&a,1);
        assert(a.lifetime_sequence && a.lifetime_slot==i);
    }
    unknown=retirement_sample(0x9999); observe_vehicle_lifetime(&unknown,2);
    assert(!unknown.lifetime_sequence && d->overflow==1 && d->count==VEHICLE_LIFETIME_MAX_RECORDS);
    hooked_vehicle_retire((void *)(uintptr_t)0x1000);
    assert(d->records[0].retire_returns==1 && d->records[0].owner==0x1000);
    rollback_vehicle_lifetime_observer(); a=retirement_sample(0x1000); observe_vehicle_lifetime(&a,3);
    assert(!a.lifetime_sequence && d->records[0].state==2);
    memset(d,0,sizeof(*d));
    puts("PASS lifetime: copied enrollment, unmatched exclusion, native passthrough, nested retirements, reappearance/tuple leads, stale completion, overlap, contention/overflow/sequence gaps and immutable snapshot");
}

static void test_owner_preflight(void)
{
    unsigned char *image=VirtualAlloc(NULL,0x2100000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    unsigned char *wrapper=image+0x2000000,*transform=image+0x2001000,*update;
    unsigned p;
    int32_t rel;
    PatchTransaction transaction;
    PatchRollbackResult rollback;
    VehicleLightDiagnosticState *d=&g_shadow_engine.vehicle_diagnostics;
    assert(image); g_shadow_engine.bootstrap.disrupt_base=image;
    for(p=0;p<ARRAY_COUNT(g_runtime_profiles);++p) {
        select_runtime_profile_state(&g_runtime_profiles[p]);
        if(!g_runtime_profiles[p].vehicle_light_update_rva) {
            assert(!validate_vehicle_owner_sites()); continue;
        }
        update=image+g_runtime_profiles[p].vehicle_light_update_rva;
        memcpy(update,g_vehicle_owner_update_prefix,sizeof(g_vehicle_owner_update_prefix)-1);
        memcpy(update+0x12A,g_vehicle_owner_call_prefix,sizeof(g_vehicle_owner_call_prefix)-1);
        rel=(int32_t)(wrapper-(update+0x138)); memcpy(update+0x134,&rel,4);
        memcpy(wrapper,g_vehicle_owner_wrapper_prefix,sizeof(g_vehicle_owner_wrapper_prefix)-1);
        memcpy(wrapper+0x19,g_vehicle_owner_wrapper_tail,sizeof(g_vehicle_owner_wrapper_tail)-1);
        rel=(int32_t)(transform-(wrapper+0x31)); memcpy(wrapper+0x2D,&rel,4);
        memcpy(transform,g_vehicle_owner_transform_prefix,sizeof(g_vehicle_owner_transform_prefix)-1);
        memcpy(transform+0x73,g_vehicle_owner_transform_fields,sizeof(g_vehicle_owner_transform_fields)-1);
        assert(validate_vehicle_owner_sites());
        assert(g_shadow_engine.vehicle_diagnostics.light_transform_target==transform);
        wrapper[0x10]^=1; assert(!validate_vehicle_owner_sites()); wrapper[0x10]^=1;
        update[0]^=1; assert(!validate_vehicle_owner_sites()); update[0]^=1;
        transform[0x73]^=1; assert(!validate_vehicle_owner_sites()); transform[0x73]^=1;
        rel=0x7FFFFFFF; memcpy(update+0x134,&rel,4); assert(!validate_vehicle_owner_sites());
    }
    /* Two real detours on private synthetic code bytes only: fail each write,
     * then exercise both installed targets and rollback all published state. */
    rel=(int32_t)(wrapper-(update+0x138)); memcpy(update+0x134,&rel,4);
    assert(validate_vehicle_owner_sites());
    for(p=0;p<3;++p) {
        assert(patch_transaction_begin(&transaction,"owner-install-test",p<2?(LONG)p:-1));
        assert(install_vehicle_owner_observer()==(p==2));
        assert(d->owner_tls_valid && d->original_light_transform);
        assert(d->owner_enabled==(p==2));
        rollback=patch_transaction_rollback(&transaction);
        assert(!rollback.failed_restores && rollback.restored_writes==(p<2?p:2));
        assert(rollback.released_allocations==(p==0?1:2));
        rollback_vehicle_owner_observer();
        assert(!d->owner_enabled && !d->owner_tls_valid);
        assert(!d->original_light_update && !d->original_light_transform);
        assert(!memcmp(update,g_vehicle_owner_update_prefix,18));
        assert(!memcmp(transform,g_vehicle_owner_transform_prefix,15));
    }
    /* New third detour is independently optional; legacy two-hook test above
     * remains intact. Failure at every journaled write must undo all three. */
    {
        unsigned char *batch=image+g_runtime_state.selected->vehicle_light_batch_rva;
        fill_batch_proof(batch); assert(validate_vehicle_owner_sites());
        for(p=0;p<4;++p) {
            assert(patch_transaction_begin(&transaction,"batch-install-test",p<3?(LONG)p:-1));
            assert(install_vehicle_owner_observer()==(p==3));
            rollback=patch_transaction_rollback(&transaction);
            assert(!rollback.failed_restores && rollback.restored_writes==(p<3?p:3));
            assert(rollback.released_allocations==(p<3?p+1:3));
            rollback_vehicle_owner_observer();
            assert(!d->batch_enabled && !d->batch_tls_valid && !d->owner_enabled && !d->owner_tls_valid);
            assert(!d->original_light_batch && !d->original_light_update && !d->original_light_transform);
            assert(!memcmp(batch,g_vehicle_batch_prefix,18));
            assert(!memcmp(update,g_vehicle_owner_update_prefix,18));
            assert(!memcmp(transform,g_vehicle_owner_transform_prefix,15));
        }
    }
    {
        unsigned char *retire=image+g_runtime_state.selected->vehicle_retire_rva;
        fill_retire_proof(retire); assert(validate_vehicle_owner_sites());
        for(p=0;p<5;++p) {
            assert(patch_transaction_begin(&transaction,"lifetime-install-test",p<4?(LONG)p:-1));
            assert(install_vehicle_owner_observer()==(p==4));
            rollback=patch_transaction_rollback(&transaction);
            assert(!rollback.failed_restores && rollback.restored_writes==(p<4?p:4));
            assert(rollback.released_allocations==(p<4?p+1:4));
            rollback_vehicle_owner_observer();
            assert(!g_shadow_engine.vehicle_lifetime.enabled && !g_shadow_engine.vehicle_lifetime.original_retire);
            assert(!d->batch_enabled && !d->batch_tls_valid && !d->owner_enabled && !d->owner_tls_valid);
            assert(!d->original_light_batch && !d->original_light_update && !d->original_light_transform);
            assert(!memcmp(retire,g_vehicle_retire_prefix,18));
        }
    }
    clear_runtime_profile(); assert(!validate_vehicle_owner_sites());
    g_shadow_engine.bootstrap.disrupt_base=NULL; VirtualFree(image,0,MEM_RELEASE);
    puts("PASS owner preflight rejects unknown/A4EE, corrupt bytes and out-of-image call target (synthetic only)");
    puts("PASS owner two-detour transaction: each write failure and successful install roll back bytes, allocations, published bindings and TLS");
    puts("PASS batch three-detour transaction: every failed write and full installation restore bytes/bindings/allocations/TLS");
    puts("PASS lifetime four-detour transaction: every failed write/full install restore bytes/bindings/allocations/TLS");
}

static void test_internal_tool_requests(void)
{
    char request[96];
    InternalToolState *t=&g_shadow_engine.internal_tools;
    unsigned action;
    memset(t,0,sizeof(*t)); t->session=123;
    assert(!internal_tool_accept("garbage"));
    assert(!internal_tool_accept("SE1 0 123 1 6\n"));
    for(action=1;action<=10;++action) {
        LONG before=g_shadow_engine.policy_control.disabled;
        _snprintf(request,sizeof(request),"SE1 %lu 123 %u %u\n",
            (unsigned long)GetCurrentProcessId(),action,action);
        assert(internal_tool_accept(request));
        if(action==1U || action==2U || action==5U || action==6U)
            assert(t->settings_result==-1 && g_shadow_engine.policy_control.disabled==before);
        if(action>=7U && action<=9U) assert(!population_state());
        assert(!internal_tool_accept(request)); /* no replay */
    }
    assert(t->acknowledged==10 && t->capture_requests==15);
    assert(!internal_tool_dispatch(0) && !internal_tool_dispatch(17));
    {
        const RuntimeProfile *selected=g_runtime_state.selected;
        LONG capture_requests=t->capture_requests;
        g_runtime_state.selected=NULL;
        for(action=14;action<=16;++action) {
            assert(!internal_tool_dispatch(action));
            assert(!population_state() && t->capture_requests==capture_requests);
        }
        g_runtime_state.selected=selected;
    }
    _snprintf(request,sizeof(request),"SE1 %lu 123 11 5 trailing",
        (unsigned long)GetCurrentProcessId());
    assert(internal_tool_accept(request));
    assert(t->acknowledged==11U && t->settings_result==-1);
    _snprintf(request,sizeof(request),"SE1 %lu 124 12 5\n",
        (unsigned long)GetCurrentProcessId());
    assert(!internal_tool_accept(request));
    assert(t->acknowledged==11U);
    for(action=0;action<=10;++action) {
        LONG before=g_shadow_engine.policy_control.disabled;
        assert(!internal_tool_dispatch(40U+action*2U+1U));
        assert(g_shadow_engine.policy_control.disabled==before);
        assert(shadow_policy_set_suite_settings(action,1U));
        assert(shadow_policy_enabled(SHADOW_POLICY_DISABLE_LIMITER)==(action!=0 && action!=10));
        assert(shadow_policy_selected_limit()==action);
        assert(!shadow_policy_enabled(SHADOW_POLICY_DISABLE_QUALITY));
        if(action) assert(shadow_policy_stable_limit()==action);
    }
    assert(!internal_tool_dispatch(19) && !internal_tool_dispatch(31));
    assert(!internal_tool_dispatch(39) && !internal_tool_dispatch(62));
    assert(!shadow_policy_set_vehicle_limit(11));
    assert(!shadow_policy_set_suite_settings(4,2));
    for(action=100;action<=187;++action) {
        unsigned offset=action-100;
        LONG before=g_shadow_engine.policy_control.disabled;
        assert(!internal_tool_dispatch(action));
        assert(g_shadow_engine.policy_control.disabled==before);
        assert(shadow_policy_set_comparison(offset/8U,((offset%8U)/2U)+1U,offset%2U));
        assert(shadow_policy_resolution()==(512U<<((offset%8U)/2U)));
        assert(shadow_policy_enabled(SHADOW_POLICY_DISABLE_QUALITY)==!(offset%2U));
        assert(shadow_policy_enabled(SHADOW_POLICY_DISABLE_LIMITER)==(offset/8U!=0 && offset/8U!=10));
        assert(shadow_policy_selected_limit()==offset/8U);
        if(offset/8U) assert(shadow_policy_stable_limit()==offset/8U);
    }
    assert(!internal_tool_dispatch(99) && !internal_tool_dispatch(188));
    assert(!shadow_policy_set_comparison(11,3,0));
    assert(!shadow_policy_set_comparison(4,0,0));
    assert(!shadow_policy_set_comparison(4,5,0));
    assert(!shadow_policy_set_comparison(4,3,2));
    /* A legacy Boolean must not reinterpret remembered10 as finite. */
    assert(!ShadowEngine_SetVehicleHeadlightLimiterEnabled(1));
    assert(shadow_policy_selected_limit()==10U);
    assert(shadow_policy_resolution()==4096U);
    assert(shadow_policy_set_vehicle_limit(0));
    assert(shadow_policy_selected_limit()==0U);
    assert(shadow_policy_resolution()==4096U); /* Count-only ABI preserves cap. */
    assert(shadow_policy_set_comparison(4,3,0));
    assert(shadow_policy_stable_limit()==4);
    assert(shadow_policy_resolution()==2048U);
    assert(shadow_policy_set_comparison(4,1,0));
    g_shadow_engine.vehicle_diagnostics.residency_reset_requested=0;
    assert(shadow_policy_set_comparison(4,4,1));
    assert(!g_shadow_engine.vehicle_diagnostics.residency_reset_requested);
    assert(shadow_policy_set_comparison(4,3,0));
    shadow_policy_set_disabled(SHADOW_POLICY_DISABLE_ALL,0);
    _snprintf(request,sizeof(request),"SEV1 %lu 123 12 4 1 1\n",
        (unsigned long)GetCurrentProcessId());
    assert(internal_tool_accept(request));
    assert(t->acknowledged==12U && t->settings_result==-1);
    assert(shadow_policy_stable_limit()==4U);
    assert(shadow_policy_resolution()==2048U);
    assert(shadow_policy_enabled(SHADOW_POLICY_DISABLE_QUALITY));
    _snprintf(request,sizeof(request),"SEV1 %lu 123 13 4 1 5\n",
        (unsigned long)GetCurrentProcessId());
    assert(internal_tool_accept(request));
    assert(t->acknowledged==13U && t->settings_result==-1);
    _snprintf(request,sizeof(request),"SEW2 %lu 123 14 1 1\n",
        (unsigned long)GetCurrentProcessId());
    assert(internal_tool_accept(request));
    assert(t->acknowledged==14U);
    assert(shadow_policy_world_enabled());
    assert(shadow_policy_world_resolution()==512U);
    _snprintf(request,sizeof(request),"SEW2 %lu 123 15 1 5\n",
        (unsigned long)GetCurrentProcessId());
    assert(internal_tool_accept(request));
    assert(t->acknowledged==15U && t->settings_result==-1);
    _snprintf(request,sizeof(request),"SEW2 %lu 123 16 0 3\n",
        (unsigned long)GetCurrentProcessId());
    assert(internal_tool_accept(request));
    assert(!shadow_policy_world_enabled());
    assert(shadow_policy_world_resolution()==2048U);
    _snprintf(request,sizeof(request),"SES1 %lu 123 17 1 10 0 0\n",
        (unsigned long)GetCurrentProcessId());
    assert(internal_tool_accept(request));
    assert(t->acknowledged==17U && t->settings_result==-1 && shadow_policy_selected_limit()==4U);
    _snprintf(request,sizeof(request),"SES2 %lu 123 18 3 10 1 0\n",
        (unsigned long)GetCurrentProcessId());
    assert(internal_tool_accept(request));
    assert(t->acknowledged==18U && shadow_policy_selected_limit()==10U);
    assert(!shadow_policy_enabled(SHADOW_POLICY_DISABLE_LIMITER) && shadow_policy_resolution()==512U);
    _snprintf(request,sizeof(request),"SES2 %lu 123 19 1 0 0 0\n",
        (unsigned long)GetCurrentProcessId());
    assert(internal_tool_accept(request));
    assert(t->acknowledged==19U && shadow_policy_selected_limit()==0U && shadow_policy_stable_limit()==10U);
    {
        char status[384];
        unsigned selected,driver_ready,extras,population_serial,population_notice,partial,marked;
        unsigned window_ms,sample_ms,elapsed_ms,host_compatibility;
        int consumed=0;
        assert(internal_tool_status_text(status,sizeof(status)));
        assert(!strncmp(status,"SE10 ",5U));
        assert(sscanf(status,"SE10 %*u %*u %*u %*d %*d %*d %*d %*d %*d %*u %*u %*u %*u %*u %*u %*u %*d %*u %*u %*u %*u %*u %u %u %u %u %u %u %u %u %u %u %u%n",
            &selected,&driver_ready,&extras,&population_serial,&population_notice,&partial,&marked,
            &window_ms,&sample_ms,&elapsed_ms,&host_compatibility,&consumed)==11);
        assert(selected==0U && driver_ready<=1U);
        assert(extras==vehicle_selection_extra_slots());
        assert(!population_serial && !population_notice && !partial && !marked);
        assert(window_ms==60000U && sample_ms==50U && !elapsed_ms);
        assert(host_compatibility==(unsigned)g_shadow_engine.bootstrap.host_compatibility);
        assert(status[consumed]=='\n' && !status[consumed+1]);
    }
    assert(shadow_policy_set_comparison(4,3,0));
    t->capture_requests=0;
    puts("PASS internal requests: SE10/SES2, explicit0/10 bypass, legacy limiter requests reject, modern independent vehicle/world controls, session/replay/input rejection, independent population controls, bounded captures");
}

static void test_configurable_main_pool(void)
{
    VehicleLightDiagnosticState *d=&g_shadow_engine.vehicle_diagnostics;
    VehicleLimiterChain chain;
    unsigned limit,i,pass;
    for(pass=0;pass<2;++pass) for(i=1;i<=9;++i) {
        limit=pass?10U-i:i;
        assert(shadow_policy_set_vehicle_limit(limit));
        /* The ascending/descending boundary repeats9, which correctly does
         * not reset policy; isolate that repeated-count fixture explicitly. */
        if(pass && i==1U) d->residency_reset_requested=1;
        populate_residency_sample(&chain,1000U+i*100U);
        chain.sample.copied_candidates=26;
        for(unsigned k=0;k<26;++k) {
            VehicleCandidateRecord *record=&chain.sample.records[k];
            record->renderer_type=3;
            record->selection_vehicle=0xA000U+k/2U;
            record->selection_distance_squared=(float)(100U+k);
            record->selection_rank=k/2U;
            record->selection_group_complete=1;
            record->selection_renderer_recent=1;
        }
        assert(apply_vehicle_residency_policy(&chain));
        assert(d->residency_stable_count==limit);
        assert(chain.residency_residents==limit);
        assert(chain.sample.selection_would_keep_lights==limit*2U);
        for(unsigned k=0;k<limit;++k)
            assert(d->residency_slots[k].vehicle==0xA000U+k);
        for(unsigned k=limit;k<12U;++k) assert(!d->residency_slots[k].vehicle);
        /* All slider sizes can admit a new nearest owner immediately. */
        chain.sample.tick+=1U;
        chain.sample.records[24].selection_distance_squared=1;
        chain.sample.records[25].selection_distance_squared=1;
        assert(apply_vehicle_residency_policy(&chain));
        assert(residency_contains_stable_vehicle(d,0xA00CU));
        assert(chain.residency_residents==limit+1U);
        assert(d->residency_slots[limit].vehicle==0xA000U+limit-1U);
    }
    for(i=0;i<=10;i+=10) {
        assert(shadow_policy_set_vehicle_limit(i));
        assert(!shadow_policy_enabled(SHADOW_POLICY_DISABLE_LIMITER));
        prepare_vehicle_limiter_chain(&chain,(void *)(uintptr_t)1,256U,1,0);
        assert(!chain.enabled && !chain.evaluated && !chain.captured && !chain.fail_open);
        assert(chain.head==(void *)(uintptr_t)1 && chain.count==256U);
    }
    assert(shadow_policy_set_comparison(4,3,0));
    puts("PASS configurable main pool: 1-9 ascending/descending, initially empty transitions, retired slots cleared,0/10 bypass, unchanged owner grouping");
}

static void test_resource_transition(void)
{
    unsigned char backing[0x48],other_resource[0x48];
    VehicleLightDiagnosticState *d=&g_shadow_engine.vehicle_diagnostics;
    VehicleQueueTicket ticket;
    VehicleQueueRecord *r;
    unsigned mode;
    for(mode=0;mode<7;++mode) {
        prepare_capture(1);
        memset(backing,0,sizeof(backing));
        memset(other_resource,0,sizeof(other_resource));
        *(void **)(candidate+0x58)=backing;
        *(void **)(backing+0x40)=resource;
        /* Literal fixture offsets independently model the native pointer chain. */
        if(mode==1) *(void **)(candidate+0x58)=NULL;
        if(mode==2) *(void **)(backing+0x40)=NULL;
        if(mode==3) *(void **)(candidate+0x58)=(void *)(uintptr_t)1;
        if(mode==4) *(void **)(backing+0x40)=(void *)(uintptr_t)1;
        ticket=begin_vehicle_queue_sample(queue_data,1,1);
        assert(ticket.capture.active);
        r=&d->queue_samples[0].records[0];
        sample_vehicle_queue_policy_request(ticket);
        if(mode==5) *(void **)(queue_data+0x20B0)=other_resource;
        if(mode==6) *(void **)(queue_data+0x20B0)=NULL;
        finish_vehicle_queue_sample(ticket);
        if(mode==0) {
            assert(r->resource_before.valid==7U);
            assert(r->resource_before.width==2048 && r->resource_before.height==1024);
            assert(!strcmp(shadow_resource_transition(r),"sameAddress"));
        }
        if(mode==1) assert(!strcmp(shadow_resource_transition(r),"noBackingBefore"));
        if(mode==2) assert(!strcmp(shadow_resource_transition(r),"noResourceBefore"));
        if(mode==3) assert(!strcmp(shadow_resource_transition(r),"beforeUnavailable"));
        if(mode==4) {
            assert(r->resource_before.valid==3U && !r->resource_before.width);
            assert(!strcmp(shadow_resource_transition(r),"differentAddress"));
        }
        if(mode==5) assert(!strcmp(shadow_resource_transition(r),"differentAddress"));
        if(mode==6) assert(!strcmp(shadow_resource_transition(r),"afterUnavailable"));
        /* Poison all borrowed pointers: worker formatting must use only copies. */
        r->resource_before.backing=(void *)(uintptr_t)1;
        r->resource_before.resource=(void *)(uintptr_t)1;
        r->resource=(void *)(uintptr_t)1;
        log_shadow_resource_transition(1,0,r);
    }
    prepare_capture(1);
    ticket=begin_vehicle_queue_sample(queue_data,1,0);
    finish_vehicle_queue_sample(ticket);
    r=&d->queue_samples[0].records[0];
    assert(!r->resource_before.valid && !strcmp(shadow_resource_transition(r),"notSubmitted"));
    copy_shadow_resource_before(&r->resource_before,(void *)(uintptr_t)1);
    assert(!r->resource_before.valid);
    puts("PASS bounded resource provenance: null/unreadable backing, null/unreadable resources, same/different addresses, missing output, dropped records and copied-only formatting");
}

static ULONG WINAPI workload_test_property(SEEventRecord *event,ULONG count,
    void *context,ULONG properties,SEProperty *property,ULONG bytes,BYTE *out)
{
    const wchar_t *name=(const wchar_t *)(uintptr_t)property->name;
    (void)event; (void)count; (void)context;
    assert(properties==1 && property->index==0xFFFFFFFFU);
    if(!lstrcmpW(name,L"pIDXGISwapChain")) {
        uint64_t chain=0x1234; assert(bytes==8); memcpy(out,&chain,8); return 0;
    }
    assert(bytes==4); memset(out,0,4); return 0;
}

/* Frozen62 reference for policy equivalence; excluded from the ASI. */
static void reference_classify_vehicle_selection_v62(VehicleCandidateSample *sample,
    const VehicleSelectionSnapshot *snapshot)
{
    VehicleSelectionIndex selection_index;
    uint64_t vehicles[VEHICLE_SELECTION_MAX_VEHICLES];
    uint64_t exact_vehicles[VEHICLE_SELECTION_MAX_VEHICLES];
    uint64_t registry_vehicles[VEHICLE_SELECTION_MAX_VEHICLES];
    float distances[VEHICLE_SELECTION_MAX_VEHICLES];
    uint32_t vehicle_lights[VEHICLE_SELECTION_MAX_VEHICLES];
    uint32_t exact_vehicle_lights[VEHICLE_SELECTION_MAX_VEHICLES];
    uint32_t order[VEHICLE_SELECTION_MAX_VEHICLES];
    DWORD now=sample->tick;
    uint32_t i,j,k,vehicle_count=0,complete_count=0,exact_vehicle_count=0;
    uint32_t registry_vehicle_count=0;
    index_vehicle_selection(snapshot,&selection_index);
    sample->selection_main_limit=shadow_policy_stable_limit();
    sample->selection_snapshot_records=snapshot->count;
    sample->selection_snapshot_gaps=snapshot->gaps;
    sample->selection_snapshot_overflow=snapshot->overflow;
    sample->selection_snapshot_rotations=snapshot->rotations;
    sample->selection_snapshot_same_epoch=snapshot->same_epoch;
    sample->selection_snapshot_previous_epoch=snapshot->previous_epoch;
    sample->selection_snapshot_older_epoch=snapshot->older_epoch;
    for(i=0;i<snapshot->count;++i) {
        const VehicleSelectionRecord *record=&snapshot->records[i];
        DWORD age=now-record->tick;
        for(j=0;j<registry_vehicle_count;++j)
            if(registry_vehicles[j]==record->vehicle) break;
        if(j==registry_vehicle_count &&
           registry_vehicle_count<VEHICLE_SELECTION_MAX_VEHICLES)
            registry_vehicles[registry_vehicle_count++]=record->vehicle;
        if(age<=16U) ++sample->selection_snapshot_age_le_16;
        else if(age<=50U) ++sample->selection_snapshot_age_17_50;
        else if(age<=250U) ++sample->selection_snapshot_age_51_250;
        else ++sample->selection_snapshot_age_over_250;
    }
    sample->selection_snapshot_vehicles=registry_vehicle_count;
    for(i=0;i<sample->copied_candidates;++i) {
        VehicleCandidateRecord *candidate=&sample->records[i];
        const VehicleSelectionRecord *match=NULL;
        uint32_t matches=0;
        LONG delta,renderer_delta;
        if(candidate->renderer_type!=3) continue;
        ++sample->selection_spotlight3;
        if(!candidate->descriptor_handle_valid) {
            ++sample->selection_unknown; continue;
        }
        for(j=0;j<sample->copied_candidates;++j)
            if(sample->records[j].renderer_type==3 &&
               sample->records[j].descriptor_handle_valid &&
               sample->records[j].descriptor_handle==candidate->descriptor_handle)
                ++candidate->selection_candidate_instances;
        match=match_vehicle_selection(snapshot,&selection_index,
            candidate->descriptor_handle,&matches);
        candidate->selection_matches=matches;
        if(matches!=1U || candidate->selection_candidate_instances!=1U) {
            if(matches>1U || candidate->selection_candidate_instances>1U)
                ++sample->selection_ambiguous;
            else ++sample->selection_unknown;
            continue;
        }
        candidate->selection_vehicle=match->vehicle;
        candidate->selection_publish_manager_call=match->publish_manager_call;
        candidate->selection_publish_renderer_epoch=match->publish_renderer_epoch;
        candidate->selection_age_ms=now-match->tick;
        candidate->selection_distance_squared=match->distance_squared;
        ++sample->selection_exact_matches;
        for(j=0;j<exact_vehicle_count;++j)
            if(exact_vehicles[j]==match->vehicle) break;
        if(j==exact_vehicle_count) {
            if(exact_vehicle_count<VEHICLE_SELECTION_MAX_VEHICLES) {
                exact_vehicles[j]=match->vehicle;
                exact_vehicle_lights[j]=1U;
                ++exact_vehicle_count;
            }
        } else ++exact_vehicle_lights[j];
        if(match->publish_manager_call>sample->manager_call) {
            candidate->selection_manager_delta=-1;
            ++sample->selection_delta_future;
        } else {
            delta=sample->manager_call-match->publish_manager_call;
            candidate->selection_manager_delta=delta;
            if(delta==0) ++sample->selection_delta_zero;
            else if(delta==1) ++sample->selection_delta_one;
            else if(delta<=4) ++sample->selection_delta_two_to_four;
            else ++sample->selection_delta_five_plus;
            candidate->selection_current=delta==1;
            if(candidate->selection_current) ++sample->selection_current_matches;
            else ++sample->selection_older_matches;
        }
        if(match->publish_renderer_epoch>sample->renderer_epoch) {
            candidate->selection_renderer_delta=-1;
            ++sample->selection_renderer_future;
            ++sample->selection_renderer_nonrecent_matches;
            continue;
        }
        renderer_delta=sample->renderer_epoch-match->publish_renderer_epoch;
        candidate->selection_renderer_delta=renderer_delta;
        if(renderer_delta==0) ++sample->selection_renderer_same;
        else if(renderer_delta==1) ++sample->selection_renderer_previous;
        else if(renderer_delta<=3) ++sample->selection_renderer_two_to_three;
        else ++sample->selection_renderer_older;
        candidate->selection_renderer_recent=renderer_delta<=3;
        if(!candidate->selection_renderer_recent) {
            ++sample->selection_renderer_nonrecent_matches;
            continue;
        }
        ++sample->selection_renderer_recent_matches;
        for(j=0;j<vehicle_count;++j)
            if(vehicles[j]==match->vehicle) break;
        if(j==vehicle_count) {
            if(vehicle_count==VEHICLE_SELECTION_MAX_VEHICLES) continue;
            vehicles[j]=match->vehicle;
            distances[j]=match->distance_squared;
            vehicle_lights[j]=1U;
            ++vehicle_count;
        } else {
            ++vehicle_lights[j];
            if(match->distance_squared<distances[j]) distances[j]=match->distance_squared;
        }
    }
    sample->selection_exact_vehicles=exact_vehicle_count;
    for(i=0;i<exact_vehicle_count;++i)
        if(exact_vehicle_lights[i]>=2U) ++sample->selection_exact_complete_vehicles;
    for(i=0;i<vehicle_count;++i) {
        if(vehicle_lights[i]>=2U) order[complete_count++]=i;
        else ++sample->selection_partial_vehicles;
    }
    for(i=0;i<complete_count;++i)
        for(j=i+1U;j<complete_count;++j)
            if(distances[order[j]]<distances[order[i]]) {
                k=order[i]; order[i]=order[j]; order[j]=k;
            }
    for(i=0;i<sample->copied_candidates;++i) {
        VehicleCandidateRecord *candidate=&sample->records[i];
        if(!candidate->selection_renderer_recent) continue;
        for(j=0;j<vehicle_count;++j)
            if(vehicles[j]==candidate->selection_vehicle) {
                candidate->selection_group_lights=vehicle_lights[j];
                candidate->selection_group_complete=vehicle_lights[j]>=2U;
                break;
            }
        if(!candidate->selection_group_complete) continue;
        for(j=0;j<complete_count;++j)
            if(vehicles[order[j]]==candidate->selection_vehicle) {
                candidate->selection_rank=j+1U;
                candidate->selection_would_keep=j<sample->selection_main_limit+VEHICLE_SELECTION_OVERRIDE_VEHICLES;
                if(candidate->selection_would_keep) ++sample->selection_would_keep_lights;
                break;
            }
    }
    /* Type-1 alone is not a siren classifier: world controls also use it.
     * This extra pass is diagnostic-only and therefore runs only while an F10
     * capture is active; normal calls pay only this capture-state read.
     * Report only unique, recent type-1 handles published by the proven native
     * vehicle batch, and retain the siren-like role as an observational label. */
    if(InterlockedCompareExchange(
        &g_shadow_engine.vehicle_diagnostics.state,0,0)!=2) {
        sample->selection_vehicles=complete_count;
        return;
    }
    for(i=0;i<sample->copied_candidates;++i) {
        VehicleCandidateRecord *candidate=&sample->records[i];
        const VehicleSelectionRecord *match=NULL;
        uint32_t matches=0;
        LONG renderer_delta;
        if(candidate->renderer_type!=1) continue;
        ++sample->type1_candidates;
        if(!candidate->descriptor_handle_valid) {
            ++sample->type1_unknown;
            continue;
        }
        for(j=0;j<sample->copied_candidates;++j)
            if(sample->records[j].renderer_type==1 &&
               sample->records[j].descriptor_handle_valid &&
               sample->records[j].descriptor_handle==candidate->descriptor_handle)
                ++candidate->selection_candidate_instances;
        match=match_vehicle_selection(snapshot,&selection_index,
            candidate->descriptor_handle,&matches);
        candidate->selection_matches=matches;
        if(matches!=1U || candidate->selection_candidate_instances!=1U) {
            if(matches>1U || candidate->selection_candidate_instances>1U)
                ++sample->type1_ambiguous;
            else ++sample->type1_unknown;
            continue;
        }
        candidate->selection_vehicle=match->vehicle;
        candidate->selection_publish_manager_call=match->publish_manager_call;
        candidate->selection_publish_renderer_epoch=match->publish_renderer_epoch;
        candidate->selection_age_ms=now-match->tick;
        candidate->selection_distance_squared=match->distance_squared;
        if(match->publish_renderer_epoch>sample->renderer_epoch) {
            candidate->selection_renderer_delta=-1;
            ++sample->type1_unknown;
            continue;
        }
        renderer_delta=sample->renderer_epoch-match->publish_renderer_epoch;
        candidate->selection_renderer_delta=renderer_delta;
        candidate->selection_renderer_recent=renderer_delta<=3;
        if(!candidate->selection_renderer_recent) {
            ++sample->type1_unknown;
            continue;
        }
        candidate->selection_vehicle_type1=1U;
    }
    sample->selection_vehicles=complete_count;
}

static void test_preparation_equivalence(void)
{
    VehicleCandidateSample *old=calloc(1,sizeof(*old)),*fast=calloc(1,sizeof(*fast));
    VehicleSelectionSnapshot *snapshot=calloc(1,sizeof(*snapshot));
    uint32_t round,i;
    LARGE_INTEGER start,end,frequency;
    LONGLONG old_ticks=0,fast_ticks=0;
    assert(old && fast && snapshot);
    assert(QueryPerformanceFrequency(&frequency));
    g_shadow_engine.vehicle_diagnostics.state=0;
    snapshot->count=VEHICLE_SELECTION_SNAPSHOT_RECORDS;
    for(i=0;i<snapshot->count;++i) {
        VehicleSelectionRecord *r=&snapshot->records[i];
        r->descriptor_handle=0x100000000ULL+i%128U;
        r->vehicle=1U+(i%128U)/2U;
        r->distance_squared=(float)(1U+(i%128U)/2U);
        r->publish_renderer_epoch=100-(LONG)(i%4U); r->tick=1000U;
    }
    for(round=0;round<100;++round) {
        memset(old,0,sizeof(*old));
        old->copied_candidates=128; old->renderer_epoch=100; old->tick=1016;
        for(i=0;i<128;++i) {
            old->records[i].renderer_type=3;
            old->records[i].descriptor_handle_valid=1;
            old->records[i].descriptor_handle=0x100000000ULL+i;
        }
        if(round%3U==0) old->records[round%128U].descriptor_handle=old->records[1].descriptor_handle;
        if(round%5U==0) old->records[round%128U].descriptor_handle_valid=0;
        if(round%7U==0) snapshot->records[round].vehicle=9999U;
        assert(shadow_policy_set_vehicle_limit(round%10U+1U));
        *fast=*old;
        QueryPerformanceCounter(&start);
        reference_classify_vehicle_selection_v62(old,snapshot);
        QueryPerformanceCounter(&end); old_ticks+=end.QuadPart-start.QuadPart;
        QueryPerformanceCounter(&start);
        classify_vehicle_selection_fields(fast,snapshot,0);
        QueryPerformanceCounter(&end); fast_ticks+=end.QuadPart-start.QuadPart;
        assert(old->selection_vehicles==fast->selection_vehicles);
        assert(old->selection_would_keep_lights==fast->selection_would_keep_lights);
        for(i=0;i<128;++i) assert(!memcmp(&old->records[i],&fast->records[i],sizeof(VehicleCandidateRecord)));
    }
    free(old); free(fast); free(snapshot);
    puts("PASS preparation equivalence:100 dense reference comparisons across1..10, duplicate/full-generation handles, missing identity and owner ambiguity");
    printf("SYNTHETIC classification only: v62=%.3fus v63=%.3fus; not game FPS\n",
        old_ticks*10000.0/frequency.QuadPart,fast_ticks*10000.0/frequency.QuadPart);
}

static void test_scoped_candidate_copy(void)
{
    unsigned char *memory=VirtualAlloc(NULL,0x10000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    VehicleCopyRegionCache cache={0};
    VehicleCandidateRecord old,fast;
    LARGE_INTEGER start,end,frequency;
    LONGLONG old_ticks=0,new_ticks=0;
    DWORD protection;
    unsigned i,round;
    assert(memory && QueryPerformanceFrequency(&frequency));
    {
        unsigned char *payload=VirtualAlloc(NULL,0x20000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
        VehicleCandidateRecord interleaved[16],staged[16];
        VehicleCopyRegionCache prior={0},ordered={0};
        assert(payload);
        for(i=0;i<16;++i) {
            unsigned char *node=memory+i*0x100;
            unsigned char *descriptor=payload+(15-i)*0x1000;
            unsigned char *spatial=payload+0x10000+(15-i)*0x1000;
            *(void **)node=i==15?NULL:node+0x100;
            *(void **)(node+8)=descriptor; *(void **)(node+16)=spatial;
            *(int32_t *)(descriptor+8)=i==3?1:3;
            *(uint64_t *)(spatial+VEHICLE_SPATIAL_DESCRIPTOR_HANDLE_OFFSET)=i==7?UINT64_MAX:i;
            if(i==4) *(void **)(node+8)=NULL;
            if(i==5) *(void **)(node+16)=NULL;
        }
        memset(interleaved,0,sizeof(interleaved)); memset(staged,0,sizeof(staged));
        for(i=0;i<16;++i) {
            assert(copy_vehicle_candidate_fields_scoped(&interleaved[i],memory+i*0x100,0,&prior));
            assert(copy_vehicle_candidate_node(&staged[i],memory+i*0x100,&ordered));
        }
        copy_vehicle_candidate_payloads(staged,16,&ordered);
        assert(!memcmp(interleaved,staged,sizeof(staged)));
        assert(ordered.queries<prior.queries);
        printf("PASS staged descending payloads: exact records/order; queries %u -> %u; not game FPS\n",
            prior.queries,ordered.queries);
        assert(VirtualProtect(payload,0x1000,PAGE_NOACCESS,&protection));
        assert(VirtualProtect(payload+0x10000,0x1000,PAGE_READWRITE|PAGE_GUARD,&protection));
        memset(&prior,0,sizeof(prior)); memset(&ordered,0,sizeof(ordered));
        memset(interleaved,0,sizeof(interleaved)); memset(staged,0,sizeof(staged));
        for(i=0;i<16;++i) {
            assert(copy_vehicle_candidate_fields_scoped(&interleaved[i],memory+i*0x100,0,&prior));
            assert(copy_vehicle_candidate_node(&staged[i],memory+i*0x100,&ordered));
        }
        copy_vehicle_candidate_payloads(staged,16,&ordered);
        assert(!memcmp(interleaved,staged,sizeof(staged)));
        VirtualFree(payload,0,MEM_RELEASE);
    }
    for(i=0;i<137;++i) {
        unsigned char *node=memory+i*0x100;
        *(void **)(node+8)=node+0x20; *(void **)(node+16)=node+0x80;
        *(int32_t *)(node+0x28)=3;
        *(uint64_t *)(node+0x80+VEHICLE_SPATIAL_DESCRIPTOR_HANDLE_OFFSET)=i;
        memset(&old,0,sizeof(old)); memset(&fast,0,sizeof(fast));
        assert(copy_vehicle_candidate_fields(&old,node,0));
        assert(copy_vehicle_candidate_fields_scoped(&fast,node,0,&cache));
        assert(!memcmp(&old,&fast,sizeof(old)));
    }
    assert(cache.queries==1 && cache.hits==410);
    {
        unsigned char *fragmented=VirtualAlloc(NULL,0x40000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
        assert(fragmented);
        for(i=0;i<64;++i) assert(VirtualProtect(fragmented+i*4096,4096,
            i%2?PAGE_READONLY:PAGE_READWRITE,&protection));
        memset(&cache,0,sizeof(cache));
        for(i=0;i<64;++i) assert(vehicle_copy_readable(&cache,fragmented+((i*17)%64)*4096+16,8));
        assert(cache.count==64 && cache.queries==64);
        for(i=0;i<64;++i) assert(vehicle_copy_readable(&cache,fragmented+(63-i)*4096+16,8));
        assert(cache.queries==64 && cache.hits==64);
        for(i=1;i<cache.count;++i) assert(cache.regions[i-1].end<=cache.regions[i].begin);
        VirtualFree(fragmented,0,MEM_RELEASE);
    }
    for(round=0;round<20;++round) {
        QueryPerformanceCounter(&start);
        for(i=0;i<137;++i) assert(copy_vehicle_candidate_fields(&old,memory+i*0x100,0));
        QueryPerformanceCounter(&end); old_ticks+=end.QuadPart-start.QuadPart;
        memset(&cache,0,sizeof(cache)); QueryPerformanceCounter(&start);
        for(i=0;i<137;++i) assert(copy_vehicle_candidate_fields_scoped(&fast,memory+i*0x100,0,&cache));
        QueryPerformanceCounter(&end); new_ticks+=end.QuadPart-start.QuadPart;
    }
    printf("SYNTHETIC137 contiguous candidate copies: old=%.3fus scoped=%.3fus; not live layout or FPS\n",
        old_ticks*50000.0/frequency.QuadPart,new_ticks*50000.0/frequency.QuadPart);
    assert(VirtualProtect(memory+0xF000,0x1000,PAGE_NOACCESS,&protection));
    memset(&cache,0,sizeof(cache)); /* New copy pass must not reuse old protection. */
    assert(!vehicle_copy_readable(&cache,memory+0xF000,8));
    assert(!vehicle_copy_readable(&cache,memory+0xEFFC,8));
    assert(!vehicle_copy_readable(&cache,NULL,8));
    assert(!vehicle_copy_readable(&cache,(void *)(UINTPTR_MAX-2),8));
    assert(VirtualProtect(memory+0xF000,0x1000,PAGE_READWRITE|PAGE_GUARD,&protection));
    memset(&cache,0,sizeof(cache)); assert(!vehicle_copy_readable(&cache,memory+0xF000,8));
    memset(&cache,0,sizeof(cache)); cache.count=VEHICLE_COPY_REGIONS;
    assert(vehicle_copy_readable(&cache,memory,8) && cache.uncached==1);
    *(int32_t *)(memory+0x28)=1; *(void **)(memory+16)=memory+0xF000;
    memset(&fast,0,sizeof(fast)); memset(&cache,0,sizeof(cache));
    assert(copy_vehicle_candidate_fields_scoped(&fast,memory,0,&cache));
    assert(fast.renderer_type==1 && !fast.descriptor_handle_valid && cache.queries==1);
    VirtualFree(memory,0,MEM_RELEASE);
    puts("PASS scoped copy: full fields,411 to1 shared-region queries,64 fragmented regions, fresh-pass permissions, guard/bounds/null/overflow, bounded saturation and nonfiltered-type exclusion");
}

static void test_player_and_incoming(void)
{
    VehicleLightDiagnosticState *d=&g_shadow_engine.vehicle_diagnostics;
    unsigned char root[16]={0},controller[16]={0},data[24]={0},reference[16]={0},entity[96]={0};
    unsigned char *root_pointer=root,*entry=controller;
    float xyz[3]={1,2,3},out[3];
    VehicleLimiterChain chain;
    VehicleResidencyCandidate candidate;
    VehicleCandidateSample *sample=calloc(1,sizeof(*sample));
    VehicleSelectionSnapshot *snapshot=calloc(1,sizeof(*snapshot));
    uint32_t i,limit;
    {
        unsigned char *image=VirtualAlloc(NULL,4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
        unsigned char *saved_base=g_shadow_engine.bootstrap.disrupt_base;
        const RuntimeProfile *saved_profile=g_runtime_state.selected;
        uint32_t saved_size=g_runtime_state.image_size;
        RuntimeProfile proof={0}; int32_t displacement=256-39;
        const unsigned char binding[]={0x48,0x8B,5,0,0,0,0,0x83,0x78,8,0,0x74,0x18,
            0x48,0x8B,0,0x48,0x8B,0,0x48,0x85,0xC0,0x74,0x0D,0x48,0x8B,0x40,8,
            0x4C,0x8B,0x48,0x10,0x4D,0x8B,9};
        const unsigned char position[]={0x8B,0x41,0x50,0x89,2,0x8B,0x41,0x54,
            0x89,0x42,4,0x8B,0x41,0x58,0x89,0x42,8};
        assert(image); memcpy(image+32,binding,sizeof(binding));
        memcpy(image+35,&displacement,4); memcpy(image+128,position,sizeof(position));
        proof.local_player_binding_rva=32; proof.entity_position_reader_rva=128;
        g_runtime_state.selected=&proof; g_runtime_state.image_size=4096;
        g_shadow_engine.bootstrap.disrupt_base=image;
        initialize_vehicle_player_anchor(); assert(d->player_root_address==image+256);
        for(i=7;i<sizeof(binding);++i) {
            image[32+i]^=1; initialize_vehicle_player_anchor(); assert(!d->player_root_address);
            image[32+i]^=1;
        }
        for(i=0;i<sizeof(position);++i) {
            image[128+i]^=1; initialize_vehicle_player_anchor(); assert(!d->player_root_address);
            image[128+i]^=1;
        }
        proof.local_player_binding_rva=0;
        initialize_vehicle_player_anchor(); assert(!d->player_root_address);
        g_shadow_engine.bootstrap.disrupt_base=saved_base;
        g_runtime_state.selected=saved_profile; g_runtime_state.image_size=saved_size;
        VirtualFree(image,0,MEM_RELEASE);
    }
    *(void **)root=&entry; *(uint32_t *)(root+8)=1;
    *(void **)(controller+8)=data; *(void **)(data+16)=reference;
    *(uint64_t *)reference=123; *(void **)(reference+8)=entity;
    memcpy(entity+0x50,xyz,12);
    d->player_root_address=(unsigned char *)&root_pointer;
    assert(copy_local_player_position(out) && !memcmp(out,xyz,12));
    assert(sample && snapshot);
    sample->tick=100; sample->renderer_epoch=1; sample->copied_candidates=2;
    snapshot->count=2;
    for(i=0;i<2;++i) {
        sample->records[i].renderer_type=3;
        sample->records[i].descriptor_handle_valid=1;
        sample->records[i].descriptor_handle=100+i;
        snapshot->records[i].descriptor_handle=100+i;
        snapshot->records[i].vehicle=123;
        snapshot->records[i].tick=100; snapshot->records[i].publish_renderer_epoch=1;
        snapshot->records[i].distance_squared=9999;
        snapshot->records[i].position_valid=1;
        memcpy(snapshot->records[i].position,xyz,12);
        snapshot->records[i].position[0]+=4;
    }
    classify_vehicle_selection_fields(sample,snapshot,0);
    assert(sample->player_anchor_valid && sample->records[0].selection_distance_squared==16);
    assert(sample->records[0].selection_group_complete);
    snapshot->records[0].position_valid=0;
    memset(sample->records,0,2*sizeof(*sample->records));
    for(i=0;i<2;++i) {
        sample->records[i].renderer_type=3; sample->records[i].descriptor_handle_valid=1;
        sample->records[i].descriptor_handle=100+i;
    }
    classify_vehicle_selection_fields(sample,snapshot,0);
    assert(!sample->records[0].selection_group_complete && !sample->records[1].selection_group_complete);
    free(sample); free(snapshot);
    *(uint64_t *)reference=UINT64_MAX; assert(!copy_local_player_position(out));
    *(uint64_t *)reference=123; *(uint32_t *)(root+8)=0;
    assert(!copy_local_player_position(out));
    d->player_root_address=NULL;
    for(limit=1;limit<=9;++limit) {
        assert(shadow_policy_set_vehicle_limit(limit)); d->residency_reset_requested=1;
        populate_residency_sample(&chain,1000); chain.sample.player_anchor_valid=1;
        for(i=0;i<14;++i) chain.sample.records[i].selection_distance_squared=(float)(i/2+1);
        assert(apply_vehicle_residency_policy(&chain));
        assert(chain.residency_residents==(limit+2<7 ? limit+2:7));
    }
    /* A distant departing primary loses the extra slot to a nearby arrival. */
    assert(shadow_policy_set_vehicle_limit(1)); d->residency_reset_requested=1;
    populate_residency_sample(&chain,2000); chain.sample.player_anchor_valid=1;
    assert(apply_vehicle_residency_policy(&chain));
    populate_residency_sample(&chain,2001); chain.sample.player_anchor_valid=1;
    chain.sample.records[0].selection_distance_squared=400;
    chain.sample.records[1].selection_distance_squared=400;
    for(i=2;i<8;++i) chain.sample.records[i].selection_distance_squared=(float)(i/2);
    assert(apply_vehicle_residency_policy(&chain));
    assert(chain.residency_residents==3 && !residency_contains_vehicle(d,0xA000));
    assert(residency_contains_vehicle(d,0xA003));
    memset(&candidate,0,sizeof(candidate)); candidate.vehicle=123; candidate.handle=99;
    candidate.distance_squared=400; memset(d->approach,0,sizeof(d->approach));
    assert(vehicle_approach_distance(d,&candidate,1000)==20);
    candidate.distance_squared=324;
    assert(vehicle_approach_distance(d,&candidate,1100)<18);
    candidate.handle=100; assert(vehicle_approach_distance(d,&candidate,1101)==18);
    assert(vehicle_approach_distance(d,&candidate,1500)==18);
    /* A farther fast arrival must not take either actually nearer extra. */
    assert(shadow_policy_set_vehicle_limit(1)); d->residency_reset_requested=1;
    populate_residency_sample(&chain,3000); chain.sample.player_anchor_valid=1;
    for(i=0;i<14;++i) chain.sample.records[i].selection_distance_squared=
        i<2?1.0f:i<4?25.0f:i<6?36.0f:400.0f;
    assert(apply_vehicle_residency_policy(&chain));
    populate_residency_sample(&chain,3100); chain.sample.player_anchor_valid=1;
    for(i=0;i<14;++i) chain.sample.records[i].selection_distance_squared=
        i<2?1.0f:i<4?25.0f:i<6?36.0f:i<8?49.0f:400.0f;
    assert(apply_vehicle_residency_policy(&chain));
    assert(residency_contains_vehicle(d,0xA001) && residency_contains_vehicle(d,0xA002));
    assert(!residency_contains_vehicle(d,0xA003));
    assert(chain.sample.records[0].selection_role==1 && chain.sample.records[2].selection_role==2);
    /* Prediction cannot take the retained outgoing slot either. */
    d->residency_slots[1].vehicle=0xA002; d->residency_slots[1].admitted_tick=3100;
    d->residency_slots[2].vehicle=0xA003; d->residency_slots[2].admitted_tick=3100;
    populate_residency_sample(&chain,3200); chain.sample.player_anchor_valid=1;
    for(i=0;i<14;++i) chain.sample.records[i].selection_distance_squared=
        i<2?100.0f:i<4?144.0f:i<8?400.0f:i<10?144.0f:900.0f;
    assert(apply_vehicle_residency_policy(&chain));
    assert(residency_contains_vehicle(d,0xA002) && residency_contains_vehicle(d,0xA003));
    assert(chain.sample.records[4].selection_role==3);
    assert(!residency_contains_vehicle(d,0xA004));
    d->residency_reset_requested=1;
    populate_residency_sample(&chain,4000); chain.sample.player_anchor_valid=1;
    for(i=0;i<14;++i) chain.sample.records[i].selection_distance_squared=
        i<2?100.0f:i<4?400.0f:900.0f;
    assert(apply_vehicle_residency_policy(&chain));
    populate_residency_sample(&chain,4100); chain.sample.player_anchor_valid=1;
    for(i=0;i<14;++i) chain.sample.records[i].selection_distance_squared=
        i<2?100.0f:i<4?144.0f:900.0f;
    assert(apply_vehicle_residency_policy(&chain));
    assert(chain.sample.records[2].selection_role==4);
    puts("PASS player reference validity and incoming preparation: all limits N+2, nearby before outgoing, full-handle history and gap reset");
}

static void test_workload_capture(void)
{
    ShadowWorkloadState *w=(ShadowWorkloadState *)calloc(1,sizeof(*w));
    VehicleLimiterChain chain;
    LARGE_INTEGER frequency;
    SEEventRecord event;
    WorkloadPresent *p;
    WorkloadTimingTicket manager_timing,queue_timing;
    unsigned i;
    assert(w && QueryPerformanceFrequency(&frequency));
    g_shadow_engine.workload=w;
    assert(!workload_manager_begin((void *)1,1,0)); /* Idle never reads pointers. */
    w->phase=1; assert(!workload_request());
    assert(internal_tool_dispatch(11)); /* Busy acknowledged, no OS trace launched. */
    prepare_capture(0);
    g_shadow_engine.renderer.renderer_calls=0;
    g_shadow_engine.vehicle_diagnostics.selection_enabled=1;
    g_shadow_engine.policy_control.disabled=SHADOW_POLICY_DISABLE_LIMITER;
    w->frequency=frequency.QuadPart; w->start=workload_qpc();
    assert(WORKLOAD_SECONDS==5U && WORKLOAD_SAMPLES==50U);
    w->end=w->start+WORKLOAD_SECONDS*w->frequency; w->active=1;
    workload_poll(); assert(w->notice==2 && w->phase==1);
    assert(workload_manager_begin(candidate,1,1));
    prepare_vehicle_limiter_chain(&chain,candidate,1,1,1);
    assert(!chain.prepare_stage_ticks[0] && !chain.validation_queries);
    chain.prepare_stage_ticks[0]=7; chain.validation_queries=3; chain.validation_hits=9;
    manager_timing=workload_reserve_manager(1);
    assert(w->inflight==1);
    workload_manager_end(1,&chain);
    assert(w->managers[0].stages[0]==7 && w->managers[0].validation_queries==3 &&
        w->managers[0].validation_hits==9);
    assert(!w->lock && w->managers[0].valid && w->managers[0].candidates==1);
    assert(w->managers[0].suppressed==0 && !w->managers[0].enabled);
    assert(!workload_manager_begin((void *)1,1,1)); /* Occupied slot no dereference. */
    workload_timing_start(&manager_timing);
    workload_timing_stop(&manager_timing,0);
    workload_timing_start(&manager_timing);
    workload_timing_stop(&manager_timing,1);
    queue_timing=workload_queue(queue_data,1,1,1,1);
    assert(w->inflight==2 && queue_timing.state==w && !w->lock);
    workload_timing_start(&queue_timing); workload_timing_stop(&queue_timing,0);
    w->active=0; w->phase=2;
    workload_poll(); assert(w->phase==2 && w->inflight==2 && !w->lock);
    assert(!workload_request()); /* A pending ticket forbids reset/reuse. */
    workload_timing_release(&queue_timing); assert(w->inflight==1);
    workload_timing_release(&manager_timing); assert(!w->inflight);
    workload_timing_release(&manager_timing); assert(!w->inflight);
    assert(w->managers[0].timing_complete && w->queues[0].timing_complete);
    workload_poll(); assert(!w->phase && w->notice==5); /* No log => never claim saved. */
    w->phase=1; w->active=1;
    assert(w->queues[0].valid && w->queues[0].unknown==1 && w->queues[0].faces==1);
    assert(!memcmp(queue_data,queue_copy,sizeof(queue_data)));
    w->lock=1; assert(!workload_manager_begin((void *)1,1,1)); w->lock=0;
    w->start=workload_qpc()+w->frequency;
    assert(!workload_manager_begin((void *)1,1,1)); /* Three-second lead-in exclusion. */
    w->start=0; w->end=1;
    assert(!workload_manager_begin((void *)1,1,1)); /* End cutoff. */
    p=&w->present; memset(p,0,sizeof(*p));
    p->pid=GetCurrentProcessId(); p->frequency=1000000; p->start=100; p->end=1000000;
    p->property=workload_test_property;
    memset(&event,0,sizeof(event)); event.context=p;
    event.header.provider=g_workload_dxgi; event.header.pid=p->pid;
    event.header.thread=77; event.header.flags=0x40;
    for(i=0;i<3;++i) {
        event.header.timestamp=1000+i*10000; event.header.event.id=42;
        workload_present_event(&event);
        event.header.timestamp+=100; event.header.event.id=43;
        workload_present_event(&event);
    }
    assert(p->chains[0].presents==3 && p->chains[0].intervals==2);
    assert(p->chains[0].sum==20000 && p->chains[0].histogram[10]==2);
    event.header.pid++; workload_present_event(&event); assert(p->callbacks==6);
    event.header.pid--; event.header.timestamp=p->end;
    workload_present_event(&event); assert(p->callbacks==6);
    event.header.timestamp=40000; event.header.event.version=1;
    workload_present_event(&event); assert(p->property_errors==1);
    p->consumer=(uint64_t)-1; assert(workload_present_cleanup(p));
    {
        uint64_t old_ids[]={1,2,0x100000003ULL},new_ids[]={2,1,0x200000003ULL};
        WorkloadQueueSample a={0},b={0};
        assert(workload_set_added(new_ids,3,old_ids,3)==1);
        assert(workload_set_added(old_ids,3,new_ids,3)==1);
        assert(workload_set_added(old_ids,3,old_ids,3)==0);
        a.valid=b.valid=1; assert(workload_turnover_valid(&a,&b));
        a.unreadable=1; assert(!workload_turnover_valid(&a,&b));
        a.unreadable=0; b.missing_handles=1; assert(!workload_turnover_valid(&a,&b));
        b.missing_handles=0; b.ambiguous=1; assert(!workload_turnover_valid(&a,&b));
    }
    g_shadow_engine.workload=NULL; free(w);
    puts("PASS bounded workload: idle/busy/time/slot/lock exclusion, bypass sampling, unknown != world, immutable queue, named DXGI properties and successful-present intervals");
}

static int log_contains(const wchar_t *path,const char *needle)
{
    char text[8192]; DWORD bytes=0;
    HANDLE file=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_EXISTING,0,NULL);
    assert(file!=INVALID_HANDLE_VALUE);
    assert(ReadFile(file,text,sizeof(text)-1,&bytes,NULL)); CloseHandle(file);
    text[bytes]=0; return strstr(text,needle)!=NULL;
}

static void test_session_preservation(void)
{
    wchar_t directory[MAX_PATH],current[MAX_PATH],previous[MAX_PATH];
    char source[MAX_PATH],*tail;
    HANDLE locked;
    ShadowWorkloadState *w;
    assert(GetFullPathNameA(__FILE__,MAX_PATH,source,NULL));
    tail=strrchr(source,'\\'); assert(tail); *tail=0;
    _snwprintf(directory,MAX_PATH,L"%hs\\log-fixture-%lu",source,(unsigned long)GetCurrentProcessId());
    assert(CreateDirectoryW(directory,NULL)); /* Never reuse an existing test target. */
    lstrcpyW(g_shadow_engine.bootstrap.snapshot.bin_directory,directory);
    lstrcatW(g_shadow_engine.bootstrap.snapshot.bin_directory,L"\\");
    lstrcpyW(current,g_shadow_engine.bootstrap.snapshot.bin_directory);
    lstrcatW(current,L"ShadowEnginePatch.log");
    lstrcpyW(previous,g_shadow_engine.bootstrap.snapshot.bin_directory);
    lstrcatW(previous,L"ShadowEnginePatch.previous.log");
    assert(begin_session_log()); append_log("first-session-fixture");
    assert(flush_session_log());
    g_shadow_engine.bootstrap.session_log_state=0;
    assert(begin_session_log()); append_log("second-session-fixture");
    assert(log_contains(previous,"first-session-fixture"));
    assert(!log_contains(current,"first-session-fixture"));
    g_shadow_engine.bootstrap.session_log_state=0;
    assert(begin_session_log()); append_log("third-session-fixture");
    assert(log_contains(previous,"second-session-fixture"));
    assert(!log_contains(previous,"first-session-fixture"));
    locked=CreateFileW(previous,GENERIC_READ,0,NULL,OPEN_EXISTING,0,NULL);
    assert(locked!=INVALID_HANDLE_VALUE);
    g_shadow_engine.bootstrap.session_log_state=0;
    assert(begin_session_log());
    assert(log_contains(current,"third-session-fixture") && log_contains(current,"cleanLog=0"));
    CloseHandle(locked);
    w=(ShadowWorkloadState *)calloc(1,sizeof(*w)); assert(w);
    g_shadow_engine.workload=w; w->frequency=1000000; w->phase=2;
    workload_poll(); assert(!w->phase && w->notice==4);
    locked=CreateFileW(current,GENERIC_READ,0,NULL,OPEN_EXISTING,0,NULL);
    assert(locked!=INVALID_HANDLE_VALUE);
    w->phase=2; workload_poll(); assert(!w->phase && w->notice==5);
    CloseHandle(locked); free(w); g_shadow_engine.workload=NULL;
    assert(DeleteFileW(current)); assert(DeleteFileW(previous)); assert(RemoveDirectoryW(directory));
    memset(&g_shadow_engine.bootstrap,0,sizeof(g_shadow_engine.bootstrap));
    puts("PASS session preservation: bounded rotation, locked-backup append fallback, report flush, save failure notification, no fixture files retained");
}

int main(void)
{
    VehicleLightDiagnosticState *d=&g_shadow_engine.vehicle_diagnostics;
    VehicleCaptureTicket manager;
    VehicleQueueTicket renderer;
    VehicleLimiterChain idle_limiter;
    uint32_t i;
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);
    memset(&g_shadow_engine,0,sizeof(g_shadow_engine));
    test_session_preservation();
    memset(&idle_limiter,0,sizeof(idle_limiter));
    test_preflight();
    test_resource_transition();
    test_batch_preflight();
    test_retire_preflight();
    test_owner_preflight();
    test_retirement_records();
    test_batch_scope();
    test_vehicle_batch_publication();
    test_selection_index_equivalence();
    test_selection_projection_equivalence();
    test_nearest_vehicle_selection();
    test_capture_analysis();
    test_vehicle_residency_policy();
    test_configurable_main_pool();
    test_vehicle_shadow_quality_policy();
    test_internal_tool_requests();
    test_workload_capture();
    test_preparation_equivalence();
    test_player_and_incoming();
    test_scoped_candidate_copy();
    test_protected_admission_summary();
    test_owner_capture();
    test_owner_retry();
    test_focused_owner_windows();
    test_identity_lineage();
    test_handle_link();
    g_shadow_engine.hooks.get_async_key_state=no_key;
    prepare_capture(1);
    for(i=0;i<VEHICLE_CAPTURE_SAMPLES;++i) {
        manager=begin_vehicle_candidate_sample(candidate,1,77);
        assert(manager.active);
        assert(!begin_vehicle_queue_sample(queue_data,1,1).capture.active);
        finish_vehicle_candidate_sample(manager,NULL,&idle_limiter);
        assert(!queued_flush); /* Fourth manager sample cannot flush early. */
        renderer=begin_vehicle_queue_sample(queue_data,1,1);
        assert(renderer.capture.active && d->writer_active);
        assert(d->queue_samples[i].records[0].native_width==4096U);
        /* Simulate policy mutation between the native-input and submitted snapshots. */
        *(uint32_t *)(queue_data+VEHICLE_QUEUE_WIDTH_OFFSET)=2048U;
        sample_vehicle_queue_policy_request(renderer);
        assert(d->queue_samples[i].records[0].policy_sampled);
        assert(!begin_vehicle_candidate_sample(candidate,1,78).active);
        assert(!begin_vehicle_queue_sample(queue_data,1,1).capture.active);
        /* Timeout cannot retire an in-flight renderer ticket. */
        d->started_tick=GetTickCount()-3000U;
        poll_vehicle_candidate_capture_key(); assert(d->state==2 && !queued_flush);
        d->started_tick=GetTickCount()-1600U;
        finish_vehicle_queue_sample(renderer);
        assert(!d->writer_active);
        assert(d->queue_samples[i].records[0].light.renderer_type==3);
        assert(d->queue_samples[i].records[0].spatial_root==(void *)(uintptr_t)0x1234);
        assert(d->queue_samples[i].records[0].width==2048);
        assert(d->queue_samples[i].records[0].native_width==4096);
        assert(d->queue_samples[i].records[0].resource_width==2048);
        assert(d->queue_samples[i].records[0].resource_height==1024);
        assert(d->queue_samples[i].complete);
        *(uint32_t *)(queue_data+VEHICLE_QUEUE_WIDTH_OFFSET)=4096U;
        assert(memcmp(queue_copy,queue_data,sizeof(queue_data))==0);
    }
    assert(d->state==4 && queued_flush);
    /* Frozen worker uses copies even after original pointer fields go invalid. */
    memset(candidate,0xFF,sizeof(candidate)); memset(queue_data,0xFF,sizeof(queue_data));
    queued_flush(NULL); assert(d->state==0 && d->sample_count==0 && d->queue_sample_count==0);
    puts("PASS paired manager/renderer windows, exact copied dimensions, contention/timeout exclusion, no native queue writes, copied-only flush");

    prepare_capture(0);
    for(i=0;i<4;++i) {
        manager=begin_vehicle_candidate_sample(candidate,1,1); assert(manager.active);
        finish_vehicle_candidate_sample(manager,NULL,&idle_limiter);
    }
    assert(queued_flush && d->queue_sample_count==0); queued_flush(NULL);
    puts("PASS legacy F10 still completes when extended preflight is unavailable");

    prepare_capture(1);
    *(void **)(queue_data+RENDER_QUEUE_ENTRY_BYTES+0x10)=(void *)(uintptr_t)1;
    *(uint32_t *)(queue_data+RENDER_QUEUE_ENTRY_BYTES+0x20A0)=1;
    renderer=begin_vehicle_queue_sample(queue_data,2,1); assert(renderer.capture.active);
    sample_vehicle_queue_policy_request(renderer);
    finish_vehicle_queue_sample(renderer);
    assert(d->queue_samples[0].records[1].light.candidate==NULL);
    assert(!d->queue_samples[0].records[1].submitted);
    assert(!d->queue_samples[0].records[1].policy_sampled);
    d->state=0;
    assert(!begin_vehicle_queue_sample((void *)(uintptr_t)1,0xFFFFFFFFU,0xFFFFFFFFU).capture.active);
    prepare_capture(1);
    renderer=begin_vehicle_queue_sample(queue_data,0xFFFFFFFFU,0); finish_vehicle_queue_sample(renderer);
    assert(d->queue_samples[0].copied_entries==PHYSICAL_QUEUE_ENTRIES && !d->queue_samples[0].complete);
    prepare_capture(1);
    *(uint32_t *)(queue_data+0x20A0)=7;
    renderer=begin_vehicle_queue_sample(queue_data,1,0); finish_vehicle_queue_sample(renderer);
    assert(!d->queue_samples[0].complete);
    prepare_capture(1);
    renderer=begin_vehicle_queue_sample(NULL,1,1); finish_vehicle_queue_sample(renderer);
    assert(!d->queue_samples[0].complete);
    prepare_capture(1); d->started_tick=GetTickCount()-3000U;
    poll_vehicle_candidate_capture_key(); assert(d->state==4 && queued_flush); queued_flush(NULL);
    prepare_capture(0); reject_thread=1;
    for(i=0;i<4;++i) {
        manager=begin_vehicle_candidate_sample(candidate,1,1);
        finish_vehicle_candidate_sample(manager,NULL,&idle_limiter);
    }
    assert(d->state==0 && d->flush_failures==1 && !d->writer_active);
    puts("PASS dropped-pointer exclusion, idle fast path, bounded/malformed/null queues, partial timeout and worker-creation failure");
    return 0;
}
