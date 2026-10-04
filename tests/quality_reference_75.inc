/* Frozen .75 oracle for Q1/Q2. Bodies copied from .76 source-before.zip;
 * only reference symbol names change. Never synchronize with the candidate.
 * module18 SHA256 4f5c5cabfcf0c7e3354791fd7a41fa6eafa2e996668b895a29a0808a14b1684f
 * module19 SHA256 7eb771db66ba8287875a58ce58a028ceefa12fd35684a001b15e807b539e0104 */

static uint32_t frozen75_vehicle_selection_hash(uint64_t handle,uint64_t vehicle)
{
    uint64_t value=handle^(vehicle+(handle<<6U)+(handle>>2U));
    value^=value>>33U;
    value*=0xff51afd7ed558ccdULL;
    value^=value>>33U;
    return (uint32_t)value;
}

static int frozen75_lookup_vehicle_quality_identity(uint64_t handle,LONG renderer_epoch,
    uint64_t *vehicle,float *distance_squared)
{
    VehicleLightDiagnosticState *d=&g_shadow_engine.vehicle_diagnostics;
    uint64_t matched_vehicle=0;
    float matched_distance=0.0f;
    uint32_t delta;
    uint32_t index=frozen75_vehicle_selection_hash(handle,0U)&
        (VEHICLE_SELECTION_SLOTS_PER_EPOCH-1U);
    int matched=0;
    for(delta=0;delta<VEHICLE_SELECTION_EPOCH_BANKS;++delta) {
        LONG epoch=renderer_epoch-(LONG)delta;
        LONG tag;
        uint32_t bank,probe;
        if(epoch<0) break;
        tag=epoch+1;
        bank=(uint32_t)epoch&(VEHICLE_SELECTION_EPOCH_BANKS-1U);
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

static void frozen75_apply_vehicle_shadow_quality(void *queue,uint32_t admitted_entries)
{
    VehicleLightDiagnosticState *d=&g_shadow_engine.vehicle_diagnostics;
    unsigned char *base=(unsigned char *)queue;
    LONG renderer_epoch=InterlockedCompareExchange(
        &g_shadow_engine.renderer.renderer_calls,0,0);
    uint32_t entry_index,mutated=0;
    LONG policy=InterlockedCompareExchange(&g_shadow_engine.policy_control.disabled,0,0);
    uint32_t selected_cap=shadow_policy_resolution_from(policy);
    if(!base || !d->selection_enabled || !g_shadow_engine.policy_control.quality_ready ||
       (policy&SHADOW_POLICY_DISABLE_QUALITY)) return;
    /* Never wait on a publisher, nor accept a torn same-epoch owner. This
     * bounded lock covers only CPU request classification, never native work. */
    if(!vehicle_selection_lock()) {
#if SHADOW_ENGINE_INTERNAL_DIAGNOSTICS
        InterlockedIncrement(&d->quality_lock_skips);
#endif
        return;
    }
    if(admitted_entries>TOTAL_LOCAL_MAPS) admitted_entries=TOTAL_LOCAL_MAPS;
    for(entry_index=0;entry_index<admitted_entries;++entry_index) {
        unsigned char *entry=base+(size_t)entry_index*RENDER_QUEUE_ENTRY_BYTES;
        unsigned char *candidate=*(unsigned char **)(entry+VEHICLE_QUEUE_CANDIDATE_OFFSET);
        unsigned char *descriptor,*spatial;
        uint64_t handle,vehicle=0;
        uint32_t width,height;
        int32_t renderer_type;
        int identity;
        if(!candidate) continue;
        descriptor=*(unsigned char **)(candidate+8U);
        spatial=*(unsigned char **)(candidate+0x10U);
        if(!descriptor || !spatial) continue;
        renderer_type=*(int32_t *)(descriptor+8U);
        if(renderer_type!=1 && renderer_type!=3) continue;
        memcpy(&handle,spatial+VEHICLE_SPATIAL_DESCRIPTOR_HANDLE_OFFSET,sizeof(handle));
        if(handle==UINT64_MAX) {
            InterlockedIncrement(&d->quality_unknown_entries);
            continue;
        }
        identity=frozen75_lookup_vehicle_quality_identity(handle,renderer_epoch,&vehicle,NULL);
        if(identity<0) {
            InterlockedIncrement(&d->quality_ambiguous_entries);
            continue;
        }
        if(!identity) {
            InterlockedIncrement(&d->quality_unknown_entries);
            continue;
        }
        width=*(uint32_t *)(entry+VEHICLE_QUEUE_WIDTH_OFFSET);
        height=*(uint32_t *)(entry+VEHICLE_QUEUE_HEIGHT_OFFSET);
        if(width>selected_cap)
            *(uint32_t *)(entry+VEHICLE_QUEUE_WIDTH_OFFSET)=selected_cap;
        if(height>selected_cap)
            *(uint32_t *)(entry+VEHICLE_QUEUE_HEIGHT_OFFSET)=selected_cap;
        if(width>selected_cap || height>selected_cap) {
            ++mutated;
            if(renderer_type==1) InterlockedIncrement(&d->quality_mutated_type1);
            else InterlockedIncrement(&d->quality_mutated_type3);
        }
    }
    vehicle_selection_unlock();
    if(mutated) {
        InterlockedIncrement(&d->quality_mutated_calls);
        atomic_add_long(&d->quality_mutated_entries,(LONG)mutated);
    }
}

typedef struct frozen75_VehicleCopyRegionCache {
    struct { uintptr_t begin,end; } regions[VEHICLE_COPY_REGIONS];
    uint32_t count,uncached,queries,hits;
} frozen75_VehicleCopyRegionCache;

static int frozen75_vehicle_copy_readable(frozen75_VehicleCopyRegionCache *cache,const void *address,size_t size)
{
    uintptr_t begin=(uintptr_t)address,end=begin+size;
    MEMORY_BASIC_INFORMATION info;
    uint32_t low=0,high,slot;
    if(!cache) return readable_memory(address,size);
    if(!address || end<begin) return 0;
    /* Sorted disjoint regions: logarithmic lookup, no process-wide cache. */
    high=cache->count;
    while(low<high) {
        uint32_t middle=low+(high-low)/2U;
        if(cache->regions[middle].begin<=begin) low=middle+1U;
        else high=middle;
    }
    if(low && end<=cache->regions[low-1U].end) {
            ++cache->hits; return 1;
    }
    ++cache->queries;
    if(!VirtualQuery(address,&info,sizeof(info)) || info.State!=MEM_COMMIT ||
       (info.Protect&(PAGE_NOACCESS|PAGE_GUARD))) return 0;
    if((uintptr_t)info.BaseAddress+info.RegionSize<(uintptr_t)info.BaseAddress ||
       end>(uintptr_t)info.BaseAddress+info.RegionSize) return 0;
    if(cache->count==VEHICLE_COPY_REGIONS) { ++cache->uncached; return 1; }
    slot=low;
    memmove(&cache->regions[slot+1U],&cache->regions[slot],
        (cache->count-slot)*sizeof(cache->regions[0]));
    ++cache->count;
    cache->regions[slot].begin=(uintptr_t)info.BaseAddress;
    cache->regions[slot].end=(uintptr_t)info.BaseAddress+info.RegionSize;
    return 1;
}

static void frozen75_apply_world_shadow_quality(void *queue,uint32_t admitted_entries)
{
    WorldLightQualityState *world=&g_shadow_engine.world_quality;
    unsigned char *base=(unsigned char *)queue;
    LONG renderer_epoch=InterlockedCompareExchange(
        &g_shadow_engine.renderer.renderer_calls,0,0);
    uint32_t cap=shadow_policy_world_resolution();
    uint32_t entry_index,mutated=0;
    frozen75_VehicleCopyRegionCache readable;
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
        if(!descriptor || !spatial || !frozen75_vehicle_copy_readable(&readable,descriptor,0x44U) ||
           !frozen75_vehicle_copy_readable(&readable,spatial+VEHICLE_SPATIAL_DESCRIPTOR_HANDLE_OFFSET,
               sizeof(handle))) {
            InterlockedIncrement(&world->skipped_unreadable);
            continue;
        }
        type=*(int32_t *)(descriptor+8U);
        if(type<=0 || type>4) continue;
        memcpy(&handle,spatial+VEHICLE_SPATIAL_DESCRIPTOR_HANDLE_OFFSET,sizeof(handle));
        if(handle!=UINT64_MAX) {
            identity=frozen75_lookup_vehicle_quality_identity(handle,renderer_epoch,&vehicle,
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
