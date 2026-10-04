/* Independent H5 oracle: exact frozen .74 bodies. Do not synchronize these
 * implementations with the candidate. Old world uses the old four-bank lookup. */

/* Frozen .74 18_vehicle_light_diagnostics.inc SHA256 c8264c51398d649d7741407ab7dac32c94f93ea93783e5c05398d72e51ce87fe; only the two reference symbol names change. */
static int reference_lookup_vehicle_quality_identity(uint64_t handle,LONG renderer_epoch,
    uint64_t *vehicle,float *distance_squared)
{
    VehicleLightDiagnosticState *d=&g_shadow_engine.vehicle_diagnostics;
    uint64_t matched_vehicle=0;
    float matched_distance=0.0f;
    uint32_t delta;
    int matched=0;
    for(delta=0;delta<VEHICLE_SELECTION_EPOCH_BANKS;++delta) {
        LONG epoch=renderer_epoch-(LONG)delta;
        LONG tag;
        uint32_t bank,index,probe;
        if(epoch<0) break;
        tag=epoch+1;
        bank=(uint32_t)epoch&(VEHICLE_SELECTION_EPOCH_BANKS-1U);
        index=vehicle_selection_hash(handle,0U)&
            (VEHICLE_SELECTION_SLOTS_PER_EPOCH-1U);
        for(probe=0;probe<VEHICLE_SELECTION_SLOTS_PER_EPOCH;++probe) {
            VehicleQualityRecord *record=&d->quality_records[bank]
                [(index+probe)&(VEHICLE_SELECTION_SLOTS_PER_EPOCH-1U)];
            LONG before=InterlockedCompareExchange(&record->epoch_tag,0,0);
            uint64_t seen_handle,seen_vehicle;
            float seen_distance;
            uint32_t ambiguous;
            LONG after;
            if(before!=tag) break;
            seen_handle=record->descriptor_handle;
            seen_vehicle=record->vehicle;
            seen_distance=record->distance_squared;
            ambiguous=record->ambiguous;
            after=InterlockedCompareExchange(&record->epoch_tag,0,0);
            if(before!=after || after!=tag) continue;
            if(seen_handle!=handle) continue;
            if(ambiguous || !seen_vehicle) return -1;
            if(matched && matched_vehicle!=seen_vehicle) return -1;
            if(!matched) {
                matched_vehicle=seen_vehicle;
                matched_distance=seen_distance;
            }
            matched=1;
            break;
        }
    }
    if(matched && vehicle) *vehicle=matched_vehicle;
    if(matched && distance_squared) *distance_squared=matched_distance;
    return matched;
}

/* Frozen .74 19_world_light_quality.inc SHA256 ad36cf086c2d23f73ce18fd17effcfb1a731067daf4d77e6d5fd57a95410d9f9; only the two reference symbol names change. */
static void reference_apply_world_shadow_quality(void *queue,uint32_t admitted_entries)
{
    WorldLightQualityState *world=&g_shadow_engine.world_quality;
    unsigned char *base=(unsigned char *)queue;
    LONG renderer_epoch=InterlockedCompareExchange(
        &g_shadow_engine.renderer.renderer_calls,0,0);
    uint32_t cap=shadow_policy_world_resolution();
    uint32_t entry_index,mutated=0;
    if(!base || !shadow_policy_world_enabled() ||
       !g_shadow_engine.policy_control.quality_ready) return;
    if(!vehicle_selection_lock()) {
#if SHADOW_ENGINE_INTERNAL_DIAGNOSTICS
        InterlockedIncrement(&g_shadow_engine.vehicle_diagnostics.world_quality_lock_skips);
#endif
        return;
    }
    if(admitted_entries>TOTAL_LOCAL_MAPS) admitted_entries=TOTAL_LOCAL_MAPS;
    for(entry_index=0;entry_index<admitted_entries;++entry_index) {
        unsigned char *entry=base+(size_t)entry_index*RENDER_QUEUE_ENTRY_BYTES;
        unsigned char *candidate=*(unsigned char **)(entry+VEHICLE_QUEUE_CANDIDATE_OFFSET);
        unsigned char *descriptor,*spatial;
        uint64_t handle,vehicle=0;
        float vehicle_distance=0.0f;
        uint32_t width,height;
        int32_t type;
        int identity;
        if(!candidate) continue;
        descriptor=*(unsigned char **)(candidate+8U);
        spatial=*(unsigned char **)(candidate+0x10U);
        if(!descriptor || !spatial || !readable_memory(descriptor,0x44U) ||
           !readable_memory(spatial+VEHICLE_SPATIAL_DESCRIPTOR_HANDLE_OFFSET,
               sizeof(handle))) {
            InterlockedIncrement(&world->skipped_unreadable);
            continue;
        }
        type=*(int32_t *)(descriptor+8U);
        if(type<=0 || type>4) continue;
        memcpy(&handle,spatial+VEHICLE_SPATIAL_DESCRIPTOR_HANDLE_OFFSET,sizeof(handle));
        if(handle!=UINT64_MAX) {
            identity=reference_lookup_vehicle_quality_identity(handle,renderer_epoch,&vehicle,
                &vehicle_distance);
            if(identity>0) { InterlockedIncrement(&world->skipped_vehicle); continue; }
            if(identity<0) { InterlockedIncrement(&world->skipped_ambiguous); continue; }
        }
        width=*(uint32_t *)(entry+VEHICLE_QUEUE_WIDTH_OFFSET);
        height=*(uint32_t *)(entry+VEHICLE_QUEUE_HEIGHT_OFFSET);
        if(width>cap) *(uint32_t *)(entry+VEHICLE_QUEUE_WIDTH_OFFSET)=cap;
        if(height>cap) *(uint32_t *)(entry+VEHICLE_QUEUE_HEIGHT_OFFSET)=cap;
        if(width>cap || height>cap) ++mutated;
    }
    vehicle_selection_unlock();
    if(mutated) {
        InterlockedIncrement(&world->mutated_calls);
        atomic_add_long(&world->mutated_entries,(LONG)mutated);
    }
}
