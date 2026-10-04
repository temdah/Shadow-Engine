/* Fixed world/local-light quality. Vehicle ownership remains in module18;
 * this module consumes only its exact classification and never owns it. */
static void apply_world_shadow_quality(void *queue,uint32_t admitted_entries)
{
    WorldLightQualityState *world=&g_shadow_engine.world_quality;
    unsigned char *base=(unsigned char *)queue;
    LONG renderer_epoch=InterlockedCompareExchange(
        &g_shadow_engine.renderer.renderer_calls,0,0);
    uint32_t cap=shadow_policy_world_resolution();
    uint32_t entry_index,mutated=0,under_cap=0,inspected=0;
    int dimensions_readable;
    VehicleCopyRegionCache readable;
    if(!base || !shadow_policy_world_enabled() ||
       !g_shadow_engine.policy_control.quality_ready) return;
    if(!vehicle_selection_lock()) {
#if SHADOW_ENGINE_INTERNAL_DIAGNOSTICS
        InterlockedIncrement(&g_shadow_engine.vehicle_diagnostics.world_quality_lock_skips);
#endif
        return;
    }
    /* Borrow only successful protection facts inside this synchronous pass.
     * Never carry them across the separate vehicle pass or a native call. */
    memset(&readable,0,sizeof(readable));
    if(admitted_entries>TOTAL_LOCAL_MAPS) admitted_entries=TOTAL_LOCAL_MAPS;
    dimensions_readable=quality_dimensions_readable(queue,admitted_entries);
    for(entry_index=0;entry_index<admitted_entries;++entry_index) {
        unsigned char *entry=base+(size_t)entry_index*RENDER_QUEUE_ENTRY_BYTES;
        unsigned char *candidate;
        unsigned char *descriptor,*spatial;
        uint64_t handle,vehicle=0;
        float vehicle_distance=0.0f;
        uint32_t width,height;
        int32_t type;
        int identity;
        if(dimensions_readable &&
           *(uint32_t *)(entry+VEHICLE_QUEUE_WIDTH_OFFSET)<=cap &&
           *(uint32_t *)(entry+VEHICLE_QUEUE_HEIGHT_OFFSET)<=cap) {
            ++under_cap;
            continue;
        }
        ++inspected;
        candidate=*(unsigned char **)(entry+VEHICLE_QUEUE_CANDIDATE_OFFSET);
        if(!candidate) continue;
        descriptor=*(unsigned char **)(candidate+8U);
        spatial=*(unsigned char **)(candidate+0x10U);
        if(!descriptor || !spatial || !vehicle_copy_readable(&readable,descriptor,0x44U) ||
           !vehicle_copy_readable(&readable,spatial+VEHICLE_SPATIAL_DESCRIPTOR_HANDLE_OFFSET,
               sizeof(handle))) {
            InterlockedIncrement(&world->skipped_unreadable);
            continue;
        }
        type=*(int32_t *)(descriptor+8U);
        if(type<=0 || type>4) continue;
        memcpy(&handle,spatial+VEHICLE_SPATIAL_DESCRIPTOR_HANDLE_OFFSET,sizeof(handle));
        if(handle!=UINT64_MAX) {
            identity=lookup_vehicle_quality_identity(handle,renderer_epoch,&vehicle,
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
    if(under_cap) atomic_add_long(&world->under_cap_entries,(LONG)under_cap);
    if(inspected) atomic_add_long(&world->inspected_entries,(LONG)inspected);
    if(admitted_entries && !dimensions_readable)
        InterlockedIncrement(&world->fast_check_unavailable_calls);
    if(mutated) {
        InterlockedIncrement(&world->mutated_calls);
        atomic_add_long(&world->mutated_entries,(LONG)mutated);
    }
}
