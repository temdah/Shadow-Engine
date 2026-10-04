/* Module 18: bounded vehicle-light classification, owner selection and quality
 * policy. F10
 * records four synchronous manager snapshots over 1.5 seconds, copies every
 * retained field before the candidate lifetime ends, then writes the copy on
 * a worker thread. Renderer samples copy candidate identity before the native
 * call and resource metadata after it. Only copied values reach the worker;
 * no engine pointer is dereferenced there and no rendering policy is changed. */
#include <math.h>
#include "../internal/overhead_probe.h"
typedef struct VehicleCaptureTicket {
    LONG active;
    uint32_t sample_index;
} VehicleCaptureTicket;

typedef struct VehicleQueueTicket {
    VehicleCaptureTicket capture;
    void *queue; /* Borrowed only across this synchronous original renderer call. */
} VehicleQueueTicket;

/* Complete native instruction spans, independently checked on four images.
 * Update argument 4 is XMM3 (float), not R9; argument 5 is a stack byte. */
static const unsigned char g_vehicle_owner_update_prefix[] =
    "\x48\x89\x5c\x24\x08\x48\x89\x74\x24\x10\x57\x48\x81\xec\x20\x01\x00\x00"
    "\x48\x83\x79\x10\x00\x49\x8b\xf0\x48\x8b\xda\x48\x8b\xf9\x0f\x84\x12\x01\x00\x00"
    "\x48\x83\x79\x08\x00\x0f\x84\x07\x01\x00\x00\x80\x79\x31\x00\x74\x06"
    "\x80\x79\x32\x00\x74\x0a\x80\x79\x30\x00\x0f\x84\xf1\x00\x00\x00"
    "\x48\x8b\x01\x0f\x28\xcb\xff\x50\x40";
static const unsigned char g_vehicle_owner_call_prefix[] =
    "\x48\x8b\x4f\x10\x48\x8d\x54\x24\x60\xe8";
static const unsigned char g_vehicle_owner_wrapper_prefix[] =
    "\x48\x89\x5c\x24\x08\x57\x48\x83\xec\x20\x48\x8b\xf9\x48\x83\xc1\x20\x48\x8b\xda\xe8";
static const unsigned char g_vehicle_owner_wrapper_tail[] =
    "\x4c\x8b\xc3\x48\x8b\xcf\x48\x8b\xd0\x48\x8b\x5c\x24\x30\x48\x83\xc4\x20\x5f\xe9";
static const unsigned char g_vehicle_owner_transform_prefix[] =
    "\x48\x8b\xc4\x48\x89\x58\x08\x48\x89\x68\x10\x48\x89\x70\x18\x57"
    "\x48\x81\xec\xd0\x00\x00\x00\x0f\x29\x70\xe8\x0f\x29\x78\xd8"
    "\x44\x0f\x29\x40\xc8\x48\x8b\xf1\x49\x8b\xd8\x48\x8b\xfa";
static const unsigned char g_vehicle_owner_transform_fields[] =
    "\xf3\x0f\x11\x46\x4c\xf3\x0f\x11\x4e\x50\xf3\x0f\x11\x56\x54";

static int vehicle_owner_image_range(const unsigned char *address,size_t size)
{
    uintptr_t base=(uintptr_t)g_shadow_engine.bootstrap.disrupt_base;
    uintptr_t value=(uintptr_t)address;
    return base && value>=base && size<=g_runtime_state.image_size &&
        value-base<=g_runtime_state.image_size-size && readable_memory(address,size);
}

/* Never invoke the native camera getter: it refreshes a camera transform cache.
 * These read-only Global proofs instead follow GetLocalPlayerEntityId's entity
 * reference and the same +50/+54/+58 position fields used by GetEntityPosition. */
static void initialize_vehicle_player_anchor(void)
{
    const RuntimeProfile *p=g_runtime_state.selected;
    unsigned char *binding,*reader,*root;
    int32_t displacement;
    static const unsigned char tail[]={0x83,0x78,8,0,0x74,0x18,0x48,0x8B,0,
        0x48,0x8B,0,0x48,0x85,0xC0,0x74,0x0D,0x48,0x8B,0x40,8,0x4C,0x8B,0x48,0x10,0x4D,0x8B,9};
    static const unsigned char position[]={0x8B,0x41,0x50,0x89,2,0x8B,0x41,0x54,
        0x89,0x42,4,0x8B,0x41,0x58,0x89,0x42,8};
    g_shadow_engine.vehicle_diagnostics.player_root_address=NULL;
    if(!p || !p->local_player_binding_rva || !p->entity_position_reader_rva) return;
    binding=g_shadow_engine.bootstrap.disrupt_base+p->local_player_binding_rva;
    reader=g_shadow_engine.bootstrap.disrupt_base+p->entity_position_reader_rva;
    if(!vehicle_owner_image_range(binding,7U+sizeof(tail)) ||
       !vehicle_owner_image_range(reader,sizeof(position)) ||
       memcmp(binding,"\x48\x8B\x05",3) || memcmp(binding+7,tail,sizeof(tail)) ||
       memcmp(reader,position,sizeof(position))) return;
    memcpy(&displacement,binding+3,4); root=binding+7+displacement;
    if(vehicle_owner_image_range(root,sizeof(void *)))
        g_shadow_engine.vehicle_diagnostics.player_root_address=root;
}

static int copy_entity_reference_position(unsigned char *reference,float *out)
{
    unsigned char *entity;
    uint64_t identity;
    unsigned i;
    if(!reference || !readable_memory(reference,16)) return 0;
    memcpy(&identity,reference,8); memcpy(&entity,reference+8,8);
    if(identity==UINT64_MAX || !entity || !readable_memory(entity+0x50,12)) return 0;
    memcpy(out,entity+0x50,12);
    for(i=0;i<3;++i) if(out[i]!=out[i] || out[i]>1000000.0f || out[i]<-1000000.0f) return 0;
    return *(uint64_t *)reference==identity && *(unsigned char **)(reference+8)==entity;
}

static int copy_local_player_position(float *out)
{
    unsigned char *root_address=g_shadow_engine.vehicle_diagnostics.player_root_address;
    unsigned char *root,*array,*controller,*data,*reference;
    if(!root_address) return 0;
    root=*(unsigned char **)root_address;
    if(!root || !readable_memory(root,12) || !*(uint32_t *)(root+8)) return 0;
    array=*(unsigned char **)root;
    if(!array || !readable_memory(array,8)) return 0;
    controller=*(unsigned char **)array;
    if(!controller || !readable_memory(controller,16)) return 0;
    data=*(unsigned char **)(controller+8);
    if(!data || !readable_memory(data,24)) return 0;
    reference=*(unsigned char **)(data+16);
    if(!copy_entity_reference_position(reference,out)) return 0;
    return *(unsigned char **)root_address==root && *(unsigned char **)root==array &&
        *(unsigned char **)array==controller && *(unsigned char **)(controller+8)==data &&
        *(unsigned char **)(data+16)==reference;
}

/* Batch prologue's first 18 bytes are whole non-relative instructions. The
 * remaining spans prove argument preservation, array bounds and virtual call
 * independently of relocatable helper calls. No guessed cluster offsets. */
static const unsigned char g_vehicle_batch_prefix[] =
    "\x4c\x89\x4c\x24\x20\x4c\x89\x44\x24\x18\x53\x55\x56\x57\x41\x56\x41\x57"
    "\x48\x83\xec\x68\x0f\x29\x74\x24\x50\x0f\x57\xf6\x45\x32\xff\x0f\x29\x7c\x24\x40"
    "\x49\x8b\xd9\x49\x8b\xf8\x48\x8b\xe9\x0f\x28\xf9";
static const unsigned char g_vehicle_batch_array[] =
    "\x48\x8b\xb5\x48\x01\x00\x00\xc6\x85\x2d\x01\x00\x00\x00\x44\x8b\xb5\x50\x01\x00\x00"
    "\x4d\x69\xf6\x30\x01\x00\x00\x4c\x03\xf6\x49\x3b\xf6\x0f\x84\xd6\x00\x00\x00";
static const unsigned char g_vehicle_batch_restore[] =
    "\x48\x8b\x9c\x24\xb8\x00\x00\x00\x48\x8b\xbc\x24\xb0\x00\x00\x00"
    "\x0f\x28\xce\x45\x0f\xb6\xc7\x48\x8b\xce";
static const unsigned char g_vehicle_batch_dispatch[] =
    "\x80\xbd\xdc\x01\x00\x00\x00\x75\x0d\x80\xbd\x2c\x01\x00\x00\x00\x75\x04\x33\xc9"
    "\xeb\x02\xb1\x01\x48\x8b\x06\x88\x4c\x24\x20\x0f\x28\xdf\x4c\x8b\xc3\x48\x8b\xd7"
    "\x48\x8b\xce\xff\x50\x30\x48\x81\xc6\x30\x01\x00\x00\x49\x3b\xf6\x0f\x85\x46\xff\xff\xff"
    "\x4c\x8b\x6c\x24\x60\x4c\x8b\xa4\x24\xa8\x00\x00\x00\x0f\x28\x74\x24\x50\x0f\x28\x7c\x24\x40"
    "\x48\x83\xc4\x68\x41\x5f\x41\x5e\x5f\x5e\x5d\x5b\xc3";

#define VEHICLE_BATCH_ARRAY_OFFSET 0x148U
#define VEHICLE_BATCH_COUNT_OFFSET 0x150U
#define VEHICLE_BATCH_ELEMENT_BYTES 0x130U
#define VEHICLE_BATCH_RETURN_OFFSET 0x1BAU

static int validate_vehicle_batch_site(void)
{
    VehicleLightDiagnosticState *d=&g_shadow_engine.vehicle_diagnostics;
    const RuntimeProfile *p=g_runtime_state.selected;
    unsigned char *batch;
    d->light_batch_target=NULL;
    if(!p || !p->vehicle_light_batch_rva || !g_shadow_engine.bootstrap.disrupt_base) return 0;
    batch=g_shadow_engine.bootstrap.disrupt_base+p->vehicle_light_batch_rva;
    if(!vehicle_owner_image_range(batch,0x1EEU) ||
       memcmp(batch,g_vehicle_batch_prefix,sizeof(g_vehicle_batch_prefix)-1U) ||
       memcmp(batch+0xD9U,g_vehicle_batch_array,sizeof(g_vehicle_batch_array)-1U) ||
       memcmp(batch+0x16DU,g_vehicle_batch_restore,sizeof(g_vehicle_batch_restore)-1U) ||
       memcmp(batch+0x18CU,g_vehicle_batch_dispatch,sizeof(g_vehicle_batch_dispatch)-1U)) return 0;
    d->light_batch_target=batch;
    return 1;
}

static int validate_vehicle_owner_sites(void)
{
    VehicleLightDiagnosticState *d=&g_shadow_engine.vehicle_diagnostics;
    const RuntimeProfile *p=g_runtime_state.selected;
    unsigned char *update,*wrapper,*transform;
    int32_t rel;
    validate_vehicle_batch_site();
    validate_vehicle_retire_site();
    d->light_update_target=NULL; d->light_transform_target=NULL;
    if(!p || !p->vehicle_light_update_rva || !g_shadow_engine.bootstrap.disrupt_base)
        return 0;
    update=g_shadow_engine.bootstrap.disrupt_base+p->vehicle_light_update_rva;
    if(!vehicle_owner_image_range(update,0x138U) ||
       memcmp(update,g_vehicle_owner_update_prefix,sizeof(g_vehicle_owner_update_prefix)-1U) ||
       memcmp(update+0x12AU,g_vehicle_owner_call_prefix,sizeof(g_vehicle_owner_call_prefix)-1U)) return 0;
    memcpy(&rel,update+0x134U,4); wrapper=update+0x138U+rel;
    if(!vehicle_owner_image_range(wrapper,0x31U) ||
       memcmp(wrapper,g_vehicle_owner_wrapper_prefix,sizeof(g_vehicle_owner_wrapper_prefix)-1U) ||
       memcmp(wrapper+0x19U,g_vehicle_owner_wrapper_tail,sizeof(g_vehicle_owner_wrapper_tail)-1U)) return 0;
    memcpy(&rel,wrapper+0x2DU,4); transform=wrapper+0x31U+rel;
    if(!vehicle_owner_image_range(transform,0x82U) ||
       memcmp(transform,g_vehicle_owner_transform_prefix,sizeof(g_vehicle_owner_transform_prefix)-1U) ||
       memcmp(transform+0x73U,g_vehicle_owner_transform_fields,sizeof(g_vehicle_owner_transform_fields)-1U)) return 0;
    d->light_update_target=update; d->light_transform_target=transform;
    return 1;
}

typedef struct VehicleOwnerScope {
    LONG serial;
    uint32_t index;
    void *light;
} VehicleOwnerScope;

/* Borrowed pointers live only during the native batch. Publication stages
 * scalar copies and commits the complete group after native return. */
#define VEHICLE_BATCH_PUBLICATION_MAX 64U
#define VEHICLE_SELECTION_DRIVER_ROLE 5U
typedef struct VehicleBatchScope {
    LONG serial;
    void *vehicle,*skeleton,*world_transform;
    uint64_t vehicle_entity_id;
    float distance_squared;
    float position[3];
    uint32_t position_valid;
    uint32_t pending_count,pending_overflow;
    VehicleSelectionRecord pending[VEHICLE_BATCH_PUBLICATION_MAX];
} VehicleBatchScope;

static void copy_vehicle_batch_evidence(VehicleOwnerRecord *r)
{
    VehicleLightDiagnosticState *d=&g_shadow_engine.vehicle_diagnostics;
    const VehicleBatchScope *scope;
    unsigned char *vehicle;
    uintptr_t component,base,distance;
    if(!d->batch_enabled || !d->batch_tls_valid) return;
    scope=(const VehicleBatchScope *)TlsGetValue(d->batch_tls);
    if(!scope || scope->serial!=d->serial) return;
    r->vehicle=scope->vehicle; r->vehicle_flags=1U;
    if(r->update_caller==(uintptr_t)d->light_batch_target+VEHICLE_BATCH_RETURN_OFFSET)
        r->vehicle_flags|=4U;
    if(r->skeleton==scope->skeleton) r->vehicle_flags|=8U;
    if(r->world_transform==scope->world_transform) r->vehicle_flags|=16U;
    /* A nested unrelated component update must never borrow this owner. */
    if((r->vehicle_flags&29U)!=29U) return;
    vehicle=(unsigned char *)scope->vehicle;
    if(!vehicle || !readable_memory(vehicle,VEHICLE_BATCH_COUNT_OFFSET+sizeof(uint32_t))) return;
    r->vehicle_vtable=*(void **)vehicle;
    memcpy(&r->vehicle_array,vehicle+VEHICLE_BATCH_ARRAY_OFFSET,sizeof(void *));
    memcpy(&r->vehicle_count,vehicle+VEHICLE_BATCH_COUNT_OFFSET,sizeof(uint32_t));
    r->vehicle_flags|=2U;
    component=(uintptr_t)r->component; base=(uintptr_t)r->vehicle_array;
    if(!base || component<base) return;
    distance=component-base;
    /* Avoid overflow-prone end-pointer arithmetic; never scan the array. */
    if(distance%VEHICLE_BATCH_ELEMENT_BYTES ||
       distance/VEHICLE_BATCH_ELEMENT_BYTES>=r->vehicle_count) return;
    r->vehicle_index=(uint32_t)(distance/VEHICLE_BATCH_ELEMENT_BYTES);
    r->vehicle_flags|=32U;
}

static int vehicle_selection_lock(void)
{
    VehicleLightDiagnosticState *d=&g_shadow_engine.vehicle_diagnostics;
    if(InterlockedCompareExchange(&d->selection_writer_active,1,0)==0) return 1;
    return 0;
}

static void vehicle_selection_unlock(void)
{
    InterlockedExchange(&g_shadow_engine.vehicle_diagnostics.selection_writer_active,0);
}

static uint32_t vehicle_selection_hash(uint64_t handle,uint64_t vehicle)
{
    uint64_t value=handle^(vehicle+(handle<<6U)+(handle>>2U));
    value^=value>>33U;
    value*=0xff51afd7ed558ccdULL;
    value^=value>>33U;
    return (uint32_t)value;
}

/* Caller owns the selection token, as do all quality-registry readers. Tags
 * describe bank epochs; they are not an independent synchronization mechanism. */
static void publish_vehicle_quality_identity(uint32_t bank,LONG renderer_epoch,
    uint64_t handle,uint64_t vehicle,float distance_squared)
{
    VehicleLightDiagnosticState *d=&g_shadow_engine.vehicle_diagnostics;
    uint32_t index=vehicle_selection_hash(handle,0U)&
        (VEHICLE_SELECTION_SLOTS_PER_EPOCH-1U);
    uint32_t probe;
    LONG tag=renderer_epoch+1;
    for(probe=0;probe<VEHICLE_SELECTION_SLOTS_PER_EPOCH;++probe) {
        VehicleQualityRecord *record=&d->quality_records[bank]
            [(index+probe)&(VEHICLE_SELECTION_SLOTS_PER_EPOCH-1U)];
        LONG seen=record->epoch_tag;
        if(seen!=tag) {
            record->epoch_tag=0;
            record->descriptor_handle=handle;
            record->vehicle=vehicle;
            record->distance_squared=distance_squared;
            record->ambiguous=0U;
            record->epoch_tag=tag;
            return;
        }
        if(record->descriptor_handle==handle) {
            /* Publication and quality reads hold the registry token. The
             * epoch tag alone is not a seqlock across same-epoch updates. */
            record->epoch_tag=0;
            if(record->vehicle!=vehicle) record->ambiguous=1U;
            else record->distance_squared=distance_squared;
            record->epoch_tag=tag;
            return;
        }
    }
    InterlockedIncrement(&d->quality_overflow);
}

/* Publish copied provenance only while the component is proven to belong to
 * the active native vehicle batch. Four renderer-queue epochs bound storage.
 * The complete native descriptor handle is {slot,generation}; teardown at
 * Global RVA 0x25B84D0 increments the slot generation before reuse. */
static void observe_vehicle_selection(void *component,void *skeleton,
    void *world_transform,uintptr_t update_caller)
{
    VehicleLightDiagnosticState *d=&g_shadow_engine.vehicle_diagnostics;
    VehicleBatchScope *scope;
    VehicleSelectionRecord *r;
    unsigned char *vehicle,*light;
    void *array;
    uint64_t handle;
    uintptr_t value,base,distance;
    uint32_t count;
    if(!d->selection_enabled || !d->batch_enabled || !d->batch_tls_valid) return;
    scope=(VehicleBatchScope *)TlsGetValue(d->batch_tls);
    if(!scope || !scope->vehicle || skeleton!=scope->skeleton ||
       world_transform!=scope->world_transform ||
       update_caller!=(uintptr_t)d->light_batch_target+VEHICLE_BATCH_RETURN_OFFSET ||
       scope->distance_squared<0.0f || scope->distance_squared!=scope->distance_squared)
        return;
    vehicle=(unsigned char *)scope->vehicle;
    if(!readable_memory(vehicle,VEHICLE_BATCH_COUNT_OFFSET+sizeof(uint32_t)) ||
       !component || !readable_memory(component,VEHICLE_OWNER_LIGHT_OFFSET+sizeof(void *))) return;
    memcpy(&array,vehicle+VEHICLE_BATCH_ARRAY_OFFSET,sizeof(array));
    memcpy(&count,vehicle+VEHICLE_BATCH_COUNT_OFFSET,sizeof(count));
    value=(uintptr_t)component; base=(uintptr_t)array;
    if(!base || value<base) return;
    distance=value-base;
    if(distance%VEHICLE_BATCH_ELEMENT_BYTES ||
       distance/VEHICLE_BATCH_ELEMENT_BYTES>=count) return;
    light=*(unsigned char **)((unsigned char *)component+VEHICLE_OWNER_LIGHT_OFFSET);
    if(!light || !readable_memory(light,VEHICLE_OWNER_DESCRIPTOR_HANDLE_OFFSET+sizeof(uint64_t))) return;
    memcpy(&handle,light+VEHICLE_OWNER_DESCRIPTOR_HANDLE_OFFSET,sizeof(handle));
    if(handle==UINT64_MAX) return;
    if(scope->pending_count>=VEHICLE_BATCH_PUBLICATION_MAX) {
        scope->pending_overflow=1U;
        return;
    }
    r=&scope->pending[scope->pending_count++];
    memset(r,0,sizeof(*r));
    r->descriptor_handle=handle;
    r->vehicle=(uint64_t)(uintptr_t)scope->vehicle;
    r->vehicle_entity_id=scope->vehicle_entity_id;
    r->distance_squared=scope->distance_squared;
    memcpy(r->position,scope->position,sizeof(r->position));
    r->position_valid=scope->position_valid;
    r->tick=GetTickCount();
    r->observations=1U;
}

/* Preserve bank/slot snapshot order, including equal-time ties and conflicts.
 * Called only for a newly occupied slot under the publication lock. */
static void index_vehicle_selection_slot(uint32_t bank,uint32_t slot,uint64_t handle)
{
    VehicleLightDiagnosticState *d=&g_shadow_engine.vehicle_diagnostics;
    uint32_t bucket=vehicle_selection_hash(handle,0U)&
        (VEHICLE_SELECTION_SLOTS_PER_EPOCH-1U);
    uint32_t *link=&d->selection_heads[bank][bucket];
    while(*link && *link<slot+1U) link=&d->selection_next[bank][*link-1U];
    d->selection_next[bank][slot]=*link;
    *link=slot+1U;
}

/* All records are owned scalar copies. Never hold this token over native
 * execution. Reserve enough free capacity before either registry is changed,
 * so a rejected group cannot leave one bulb published and another missing. */
static void publish_vehicle_selection_batch(const VehicleBatchScope *scope)
{
    VehicleLightDiagnosticState *d=&g_shadow_engine.vehicle_diagnostics;
    uint32_t bank,index,probe,slot,n;
    LONG renderer_epoch,manager_call;
    if(!scope->pending_count || !d->selection_enabled) return;
    InterlockedIncrement(&d->selection_publish_inflight);
    renderer_epoch=InterlockedCompareExchange(&g_shadow_engine.renderer.renderer_calls,0,0);
    bank=(uint32_t)renderer_epoch&(VEHICLE_SELECTION_EPOCH_BANKS-1U);
    if(!vehicle_selection_lock()) {
        InterlockedIncrement(&d->selection_gaps);
        InterlockedExchange(&d->selection_gap_epoch_tag[bank],renderer_epoch+1);
        InterlockedDecrement(&d->selection_publish_inflight);
        return;
    }
    /* Choose the epoch after acquiring ownership: a delayed publisher must
     * not roll an already newer bank backwards. */
    renderer_epoch=InterlockedCompareExchange(&g_shadow_engine.renderer.renderer_calls,0,0);
    bank=(uint32_t)renderer_epoch&(VEHICLE_SELECTION_EPOCH_BANKS-1U);
    if(scope->pending_overflow ||
       (d->selection_bank_epoch[bank]==renderer_epoch &&
        d->selection_bank_count[bank]>VEHICLE_SELECTION_SLOTS_PER_EPOCH-scope->pending_count)) {
        InterlockedIncrement(&d->selection_overflow);
        InterlockedExchange(&d->selection_overflow_epoch_tag[bank],renderer_epoch+1);
        vehicle_selection_unlock();
        InterlockedDecrement(&d->selection_publish_inflight);
        return;
    }
    if(d->selection_bank_epoch[bank]!=renderer_epoch) {
        d->selection_bank_epoch[bank]=renderer_epoch;
        d->selection_bank_count[bank]=0;
        memset(d->selection_heads[bank],0,sizeof(d->selection_heads[bank]));
        InterlockedIncrement(&d->selection_rotations);
    }
    manager_call=InterlockedCompareExchange(&g_shadow_engine.renderer.manager_calls,0,0);
    for(n=0;n<scope->pending_count;++n) {
        const VehicleSelectionRecord *pending=&scope->pending[n];
        VehicleSelectionRecord *r;
        slot=UINT32_MAX;
        index=vehicle_selection_hash(pending->descriptor_handle,pending->vehicle)&
            (VEHICLE_SELECTION_SLOTS_PER_EPOCH-1U);
        for(probe=0;probe<VEHICLE_SELECTION_SLOTS_PER_EPOCH;++probe) {
            VehicleSelectionRecord *candidate=&d->selection_records[bank]
                [(index+probe)&(VEHICLE_SELECTION_SLOTS_PER_EPOCH-1U)];
            if(!candidate->observations ||
               candidate->publish_renderer_epoch!=renderer_epoch) {
                slot=(index+probe)&(VEHICLE_SELECTION_SLOTS_PER_EPOCH-1U);
                break;
            }
            if(candidate->descriptor_handle==pending->descriptor_handle &&
               candidate->vehicle==pending->vehicle) {
                slot=(index+probe)&(VEHICLE_SELECTION_SLOTS_PER_EPOCH-1U);
                break;
            }
        }
        if(slot==UINT32_MAX) {
            InterlockedIncrement(&d->selection_overflow);
            InterlockedExchange(&d->selection_overflow_epoch_tag[bank],renderer_epoch+1);
            vehicle_selection_unlock();
            InterlockedDecrement(&d->selection_publish_inflight);
            return;
        }
        r=&d->selection_records[bank][slot];
        if(!r->observations || r->publish_renderer_epoch!=renderer_epoch) {
            memset(r,0,sizeof(*r));
            r->vehicle_entity_id=pending->vehicle_entity_id;
            ++d->selection_bank_count[bank];
            index_vehicle_selection_slot(bank,slot,pending->descriptor_handle);
        }
        r->descriptor_handle=pending->descriptor_handle;
        r->vehicle=pending->vehicle;
        if(r->vehicle_entity_id!=pending->vehicle_entity_id)
            r->vehicle_entity_id=UINT64_MAX;
        r->distance_squared=pending->distance_squared;
        memcpy(r->position,pending->position,sizeof(r->position));
        r->position_valid=pending->position_valid;
        r->publish_manager_call=manager_call;
        r->publish_renderer_epoch=renderer_epoch;
        r->tick=pending->tick;
        ++r->observations;
        publish_vehicle_quality_identity(bank,renderer_epoch,pending->descriptor_handle,
            pending->vehicle,pending->distance_squared);
    }
    vehicle_selection_unlock();
    InterlockedDecrement(&d->selection_publish_inflight);
}

/* Return 1 only for one stable vehicle owner across the accepted renderer
 * epochs. Zero is missing/still-publishing and -1 is explicit ambiguity. */
static int lookup_vehicle_quality_identity(uint64_t handle,LONG renderer_epoch,
    uint64_t *vehicle,float *distance_squared)
{
    VehicleLightDiagnosticState *d=&g_shadow_engine.vehicle_diagnostics;
    uint64_t matched_vehicle=0;
    float matched_distance=0.0f;
    uint32_t delta;
    uint32_t index=vehicle_selection_hash(handle,0U)&
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

/* The face-count guard does not cover these later dimension fields. Borrow
 * one fresh, whole-span protection fact only for this synchronous pass. A
 * failed optional proof must leave the original classification path intact. */
static int quality_dimensions_readable(const void *queue,uint32_t entries)
{
    uintptr_t start=(uintptr_t)queue;
    size_t length;
    if(!queue || !entries || entries>TOTAL_LOCAL_MAPS ||
       start>UINTPTR_MAX-VEHICLE_QUEUE_WIDTH_OFFSET) return 0;
    start+=VEHICLE_QUEUE_WIDTH_OFFSET;
    length=(size_t)(entries-1U)*RENDER_QUEUE_ENTRY_BYTES+
        VEHICLE_QUEUE_HEIGHT_OFFSET+sizeof(uint32_t)-VEHICLE_QUEUE_WIDTH_OFFSET;
    if(start>UINTPTR_MAX-length) return 0;
    return readable_memory((const void *)start,length);
}

/* Queue records and their candidate pointers are synchronously owned by the
 * native renderer call about to consume them. Cap only exact recent vehicle
 * type-1/type-3 requests; unknown/shared/world handles retain native values.
 * The capped fields remain coherent with the resource created by the original
 * renderer and are overwritten by the native builder on record reuse. */
/* These child intervals use the existing opt-in workload window. Parent
 * limiter/batch intervals already include them; never add both totals. */
static int copy_policy_driver_identity(DriverIdentity *out,DriverIdentityTrace *trace)
{
    int valid;
    OVERHEAD_BEGIN(driver_timing,OH_DRIVER_IDENTITY);
    valid=trace?copy_current_driver_identity_traced(out,trace):copy_current_driver_identity(out);
    OVERHEAD_END(driver_timing);
    return valid;
}

enum {
    QUALITY_DECISION_NONE, QUALITY_DECISION_UNDER_CAP, QUALITY_DECISION_NOT_VEHICLE,
    QUALITY_DECISION_NO_HANDLE, QUALITY_DECISION_UNKNOWN, QUALITY_DECISION_AMBIGUOUS,
    QUALITY_DECISION_NATIVE_UNDER_CAP, QUALITY_DECISION_CAPPED
};

static const char *vehicle_quality_decision_name(uint32_t value)
{
    static const char *const names[]={"notInspected","underCap","notVehicleCandidate",
        "noHandle","unknownOwner","ambiguousOwner","nativeUnderCap",
        "capped"};
    return value<sizeof(names)/sizeof(names[0])?names[value]:"invalid";
}

static void apply_vehicle_shadow_quality_with_sample(void *queue,uint32_t admitted_entries,
    VehicleQueueSample *sample)
{
    /* One selected cap applies to every exact vehicle owner, including the
     * driven car. World and unproven identities retain native requests. */
    VehicleLightDiagnosticState *d=&g_shadow_engine.vehicle_diagnostics;
    unsigned char *base=(unsigned char *)queue;
    LONG renderer_epoch=InterlockedCompareExchange(
        &g_shadow_engine.renderer.renderer_calls,0,0);
    uint32_t entry_index,mutated=0,under_cap=0,inspected=0;
    int dimensions_readable;
    LONG policy=InterlockedCompareExchange(&g_shadow_engine.policy_control.disabled,0,0);
    uint32_t selected_cap=shadow_policy_resolution_from(policy);
    if(sample) sample->quality_policy=policy;
    if(!base || !d->selection_enabled || !g_shadow_engine.policy_control.quality_ready ||
       (policy&SHADOW_POLICY_DISABLE_QUALITY)) return;
    if(sample) sample->quality_evaluated=1U;
    /* Never wait on a publisher, nor accept a torn same-epoch owner. This
     * bounded lock covers request classification/capping, never native work. */
    if(!vehicle_selection_lock()) {
        if(sample) sample->quality_lock_skipped=1U;
#if SHADOW_ENGINE_INTERNAL_DIAGNOSTICS
        InterlockedIncrement(&d->quality_lock_skips);
#endif
        return;
    }
    if(admitted_entries>TOTAL_LOCAL_MAPS) admitted_entries=TOTAL_LOCAL_MAPS;
    dimensions_readable=quality_dimensions_readable(queue,admitted_entries);
    for(entry_index=0;entry_index<admitted_entries;++entry_index) {
        unsigned char *entry=base+(size_t)entry_index*RENDER_QUEUE_ENTRY_BYTES;
        unsigned char *candidate;
        unsigned char *descriptor,*spatial;
        uint64_t handle;
        uint64_t owner=0;
        uint32_t width,height;
        int32_t renderer_type;
        int identity;
        VehicleQueueRecord *record=sample && entry_index<sample->copied_entries ?
            &sample->records[entry_index]:NULL;
        if(record) {
            record->quality_decision=QUALITY_DECISION_NOT_VEHICLE;
            record->quality_owner=0;
        }
        if(dimensions_readable &&
           *(uint32_t *)(entry+VEHICLE_QUEUE_WIDTH_OFFSET)<=selected_cap &&
           *(uint32_t *)(entry+VEHICLE_QUEUE_HEIGHT_OFFSET)<=selected_cap) {
            if(record) record->quality_decision=QUALITY_DECISION_UNDER_CAP;
            ++under_cap;
            continue;
        }
        ++inspected;
        candidate=*(unsigned char **)(entry+VEHICLE_QUEUE_CANDIDATE_OFFSET);
        if(!candidate) continue;
        descriptor=*(unsigned char **)(candidate+8U);
        spatial=*(unsigned char **)(candidate+0x10U);
        if(!descriptor || !spatial) continue;
        renderer_type=*(int32_t *)(descriptor+8U);
        if(renderer_type!=1 && renderer_type!=3) continue;
        memcpy(&handle,spatial+VEHICLE_SPATIAL_DESCRIPTOR_HANDLE_OFFSET,sizeof(handle));
        if(handle==UINT64_MAX) {
            if(record) record->quality_decision=QUALITY_DECISION_NO_HANDLE;
            InterlockedIncrement(&d->quality_unknown_entries);
            continue;
        }
        identity=lookup_vehicle_quality_identity(handle,renderer_epoch,
            &owner,NULL);
        if(record) record->quality_owner=owner;
        if(identity<0) {
            if(record) record->quality_decision=QUALITY_DECISION_AMBIGUOUS;
            InterlockedIncrement(&d->quality_ambiguous_entries);
            continue;
        }
        if(!identity) {
            if(record) record->quality_decision=QUALITY_DECISION_UNKNOWN;
            InterlockedIncrement(&d->quality_unknown_entries);
            continue;
        }
        width=*(uint32_t *)(entry+VEHICLE_QUEUE_WIDTH_OFFSET);
        height=*(uint32_t *)(entry+VEHICLE_QUEUE_HEIGHT_OFFSET);
        if(record) record->quality_decision=QUALITY_DECISION_NATIVE_UNDER_CAP;
        if(width>selected_cap || height>selected_cap) {
            if(record) record->quality_decision=QUALITY_DECISION_CAPPED;
            if(width>selected_cap) *(uint32_t *)(entry+VEHICLE_QUEUE_WIDTH_OFFSET)=selected_cap;
            if(height>selected_cap) *(uint32_t *)(entry+VEHICLE_QUEUE_HEIGHT_OFFSET)=selected_cap;
            ++mutated;
            if(renderer_type==1) InterlockedIncrement(&d->quality_mutated_type1);
            else InterlockedIncrement(&d->quality_mutated_type3);
        }
    }
    vehicle_selection_unlock();
    if(under_cap) atomic_add_long(&d->quality_under_cap_entries,(LONG)under_cap);
    if(inspected) atomic_add_long(&d->quality_inspected_entries,(LONG)inspected);
    if(admitted_entries && !dimensions_readable)
        InterlockedIncrement(&d->quality_fast_check_unavailable_calls);
    if(mutated) {
        InterlockedIncrement(&d->quality_mutated_calls);
        atomic_add_long(&d->quality_mutated_entries,(LONG)mutated);
    }
}

static void apply_vehicle_shadow_quality(void *queue,uint32_t admitted_entries)
{
    apply_vehicle_shadow_quality_with_sample(queue,admitted_entries,NULL);
}

static int vehicle_capture_lock(void);
static void vehicle_capture_unlock(void);

static void vehicle_batch_tls_failed(void)
{
    VehicleLightDiagnosticState *d=&g_shadow_engine.vehicle_diagnostics;
    InterlockedExchange(&d->batch_enabled,0);
    InterlockedExchange(&d->selection_enabled,0);
    if(vehicle_capture_lock()) {
        if(d->state==2) ++d->batch_tls_failures;
        vehicle_capture_unlock();
    }
}

static void __fastcall hooked_vehicle_light_batch(void *vehicle,float delta,
    void *skeleton,void *world_transform,float distance_squared)
{
    VehicleLightDiagnosticState *d=&g_shadow_engine.vehicle_diagnostics;
    VehicleBatchScope scope;
    void *previous=NULL;
    int scoped=0;
    OVERHEAD_BEGIN(batch_probe,OH_VEHICLE_BATCH_PATCH);
    if(d->batch_enabled && d->batch_tls_valid) {
        scope.pending_count=0U; scope.pending_overflow=0U;
        scope.serial=InterlockedCompareExchange(&d->serial,0,0);
        scope.vehicle=vehicle; scope.skeleton=skeleton; scope.world_transform=world_transform;
        {
            OVERHEAD_BEGIN(identity_timing,OH_DRIVER_PUBLICATION);
            scope.vehicle_entity_id=copy_vehicle_entity_id(vehicle);
            OVERHEAD_END(identity_timing);
        }
        scope.distance_squared=distance_squared;
        scope.position_valid=0;
        memset(scope.position,0,sizeof(scope.position));
        if(d->player_root_address && vehicle && readable_memory(vehicle,24))
            scope.position_valid=copy_entity_reference_position(
                *(unsigned char **)((unsigned char *)vehicle+16),scope.position);
        previous=TlsGetValue(d->batch_tls);
        scoped=TlsSetValue(d->batch_tls,&scope)!=0;
        if(!scoped) vehicle_batch_tls_failed();
    }
    OVERHEAD_PAUSE(batch_probe);
    d->original_light_batch(vehicle,delta,skeleton,world_transform,distance_squared);
    OVERHEAD_RESUME(batch_probe);
    if(scoped) {
        OVERHEAD_PAUSE(batch_probe);
        {
            OVERHEAD_BEGIN(publication_probe,OH_VEHICLE_PUBLISH);
            publish_vehicle_selection_batch(&scope);
            OVERHEAD_END(publication_probe);
        }
        OVERHEAD_RESUME(batch_probe);
    }
    if(scoped && !TlsSetValue(d->batch_tls,previous)) vehicle_batch_tls_failed();
    OVERHEAD_END(batch_probe);
}

/* Native spatial copy preserves the descriptor handle at +0x50 across copies.
 * Validate register provenance and the complete no-call field-copy tail. */
static const unsigned char g_vehicle_link_copy_prefix[] =
    "\x48\x89\x5c\x24\x08\x48\x89\x74\x24\x10\x57\x48\x83\xec\x30"
    "\x8b\x42\x08\x48\x8b\xfa\x48\x8b\xf1";
static const unsigned char g_vehicle_link_copy_tail[] =
    "\x44\x0f\xb6\x5f\x48\x48\x8b\x5c\x24\x40\x44\x88\x5e\x48"
    "\x0f\xb6\x47\x49\x88\x46\x49\x0f\xb6\x47\x4a\x88\x46\x4a"
    "\x8b\x47\x4c\x89\x46\x4c\x48\x8b\x47\x50\x48\x89\x46\x50"
    "\x48\x8b\xc6\x48\x8b\x74\x24\x48\x48\x83\xc4\x30\x5f\xc3";

static int validate_vehicle_handle_link_sites(void)
{
    const RuntimeProfile *p=g_runtime_state.selected;
    unsigned char *copy;
    if(!p || !p->vehicle_spatial_copy_rva || !g_shadow_engine.bootstrap.disrupt_base) return 0;
    copy=g_shadow_engine.bootstrap.disrupt_base+p->vehicle_spatial_copy_rva;
    return vehicle_owner_image_range(copy,0xD6U) &&
        !memcmp(copy,g_vehicle_link_copy_prefix,sizeof(g_vehicle_link_copy_prefix)-1U) &&
        !memcmp(copy+0x9EU,g_vehicle_link_copy_tail,sizeof(g_vehicle_link_copy_tail)-1U);
}

static void copy_vehicle_descriptor_handle(VehicleQueueRecord *record)
{
    unsigned char *spatial=(unsigned char *)record->light.spatial;
    if(!g_shadow_engine.vehicle_diagnostics.handle_link_enabled || !spatial ||
       !readable_memory(spatial+VEHICLE_SPATIAL_DESCRIPTOR_HANDLE_OFFSET,sizeof(uint64_t))) return;
    memcpy(&record->descriptor_handle,spatial+VEHICLE_SPATIAL_DESCRIPTOR_HANDLE_OFFSET,sizeof(uint64_t));
    record->descriptor_handle_valid=record->descriptor_handle!=UINT64_MAX;
}

static int validate_vehicle_resolution_sites(void)
{
    unsigned char *renderer,*call,*lookup;
    uint32_t i;
    int32_t relative;
    if(!g_runtime_state.selected || !g_shadow_engine.bootstrap.disrupt_base) return 0;
    renderer=ENGINE_ADDRESS(RENDER_QUEUE_PROCESS_RVA);
    if(!renderer || !readable_memory(renderer,0x380U)) return 0;
    for(i=0;i<ARRAY_COUNT(g_vehicle_queue_proofs);++i) {
        const VehicleDiagnosticProof *proof=&g_vehicle_queue_proofs[i];
        if(memcmp(renderer+proof->offset,proof->bytes,proof->length)!=0) return 0;
    }
    call=renderer+0x339U;
    if(*call!=0xE8) return 0;
    memcpy(&relative,call+1,sizeof(relative));
    lookup=call+5+relative;
    /* A separate, already-byte-validated observer resolves this same callee.
     * Never follow a redirected call into unrelated readable memory. */
#if SHADOW_ENGINE_INTERNAL_DIAGNOSTICS
    if(!intersection_matches_lookup_target(lookup)) return 0;
#endif
    if(!readable_memory(lookup,0x100U)) return 0;
    for(i=0;i<ARRAY_COUNT(g_vehicle_resource_proofs);++i) {
        const VehicleResourceProof *proof=&g_vehicle_resource_proofs[i];
        if(memcmp(lookup+proof->load_offset,proof->loads,proof->load_length)==0 &&
           memcmp(lookup+proof->store_offset,proof->stores,sizeof(proof->stores))==0)
            return 1;
    }
    return 0;
}

static int vehicle_capture_lock(void)
{
    return InterlockedCompareExchange(&g_shadow_engine.vehicle_diagnostics.writer_active,1,0)==0;
}
static void vehicle_capture_unlock(void)
{
    InterlockedExchange(&g_shadow_engine.vehicle_diagnostics.writer_active,0);
}

/* Copied-only identity comparison. Several spatial instances may share a
 * descriptor: return the multiplicity, never treat it as unique ownership. */
static uint32_t vehicle_owner_handle_matches(const VehicleQueueSample *sample,uint64_t handle)
{
    uint32_t i,matches=0;
    if(handle==UINT64_MAX) return 0;
    for(i=0;i<sample->copied_entries && i<PHYSICAL_QUEUE_ENTRIES;++i) {
        const VehicleQueueRecord *r=&sample->records[i];
        if(r->submitted && (r->valid&3U)==3U && r->descriptor_handle_valid && r->descriptor_handle==handle) ++matches;
    }
    return matches;
}

/* Quotas reserve later windows without evicting live TLS-indexed records.
 * All fields are observations; skeleton and handle upper bits are not owners
 * or generations until their respective lifetimes are independently proven. */
static int begin_vehicle_owner_record(VehicleOwnerScope *scope,void *component,
    void *skeleton,void *world_transform,float delta,unsigned char force_update,
    uintptr_t update_caller)
{
    VehicleLightDiagnosticState *d=&g_shadow_engine.vehicle_diagnostics;
    VehicleOwnerRecord *r;
    DWORD now;
    uint32_t epoch,i;
    unsigned char *light;
    if(!d->owner_enabled || InterlockedCompareExchange(&d->state,0,0)!=2) return 0;
    now=GetTickCount();
    if(!vehicle_capture_lock()) return 0;
    if(d->state!=2) { vehicle_capture_unlock(); return 0; }
    if(d->owner_focused) {
        if(d->queue_sample_count<=0 || d->queue_sample_count>(LONG)VEHICLE_CAPTURE_SAMPLES) {
            vehicle_capture_unlock(); return 0;
        }
        epoch=(uint32_t)d->queue_sample_count-1U;
        if(now-d->owner_window_tick[epoch]>=VEHICLE_OWNER_WINDOW_MS ||
           d->owner_window_probes[epoch]>=VEHICLE_OWNER_PROBES_PER_WINDOW) {
            vehicle_capture_unlock(); return 0;
        }
        ++d->owner_window_probes[epoch];
    } else epoch=(now-d->started_tick)/VEHICLE_CAPTURE_INTERVAL_MS;
    if(epoch>=VEHICLE_CAPTURE_SAMPLES) { vehicle_capture_unlock(); return 0; }
    for(i=0;i<d->owner_count;++i)
        if(d->owner_records[i].component==component && d->owner_records[i].epoch==epoch) {
            uint64_t spatial_handle,descriptor_handle;
            r=&d->owner_records[i];
            /* Retry missing transforms at most once per 16 ms, never every
             * update. Revalidate the borrowed light/handles before joining
             * another synchronous call to the existing copied observation. */
            if(r->transform_seen || (r->valid&3U)!=3U ||
               now-r->last_probe_tick<VEHICLE_OWNER_RETRY_MS) {
                vehicle_capture_unlock(); return 0;
            }
            r->last_probe_tick=now; ++r->update_attempts;
            if(!readable_memory(component,VEHICLE_OWNER_COMPONENT_BYTES) ||
               *(void **)((unsigned char *)component+VEHICLE_OWNER_LIGHT_OFFSET)!=r->light ||
               !r->light || !readable_memory(r->light,VEHICLE_OWNER_LIGHT_BYTES)) {
                vehicle_capture_unlock(); return 0;
            }
            memcpy(&spatial_handle,(unsigned char *)r->light+VEHICLE_OWNER_SPATIAL_HANDLE_OFFSET,8);
            memcpy(&descriptor_handle,(unsigned char *)r->light+VEHICLE_OWNER_DESCRIPTOR_HANDLE_OFFSET,8);
            if(spatial_handle!=r->spatial_handle || descriptor_handle!=r->descriptor_handle) {
                vehicle_capture_unlock(); return 0;
            }
            scope->serial=d->serial; scope->index=i; scope->light=r->light;
            vehicle_capture_unlock(); return 1;
        }
    if(d->owner_count==VEHICLE_OWNER_MAX_RECORDS ||
       d->owner_window_records[epoch]>=VEHICLE_OWNER_RECORDS_PER_WINDOW) {
        ++d->owner_overflow; ++d->owner_window_overflow[epoch];
        vehicle_capture_unlock(); return 0;
    }
    if(!component || !readable_memory(component,VEHICLE_OWNER_COMPONENT_BYTES)) {
        ++d->owner_window_unreadable[epoch];
        vehicle_capture_unlock(); return 0;
    }
    light=*(unsigned char **)((unsigned char *)component+VEHICLE_OWNER_LIGHT_OFFSET);
    if(d->owner_focused) {
        uint64_t handle;
        if(!light || !readable_memory(light,VEHICLE_OWNER_LIGHT_BYTES)) {
            ++d->owner_window_unreadable[epoch]; vehicle_capture_unlock(); return 0;
        }
        memcpy(&handle,light+VEHICLE_OWNER_DESCRIPTOR_HANDLE_OFFSET,sizeof(handle));
        if(!vehicle_owner_handle_matches(&d->queue_samples[epoch],handle)) {
            ++d->owner_window_unmatched[epoch]; vehicle_capture_unlock(); return 0;
        }
    }
    scope->serial=d->serial; scope->index=d->owner_count++;
    ++d->owner_window_records[epoch];
    r=&d->owner_records[scope->index]; memset(r,0,sizeof(*r));
    r->component=component; r->skeleton=skeleton; r->world_transform=world_transform;
    r->update_caller=update_caller;
    copy_vehicle_batch_evidence(r);
    observe_vehicle_lifetime(r,d->serial);
    r->delta=delta; r->force_update=force_update; r->tick=now;
    r->thread=GetCurrentThreadId(); r->epoch=epoch; r->valid=1U;
    r->last_probe_tick=now; r->update_attempts=1U;
    r->component_vtable=*(void **)component;
    if(skeleton && readable_memory(skeleton,sizeof(void *))) {
        r->skeleton_header=*(void **)skeleton; r->skeleton_header_valid=1U;
    }
    r->settings=*(void **)((unsigned char *)component+VEHICLE_OWNER_SETTINGS_OFFSET);
    r->light=*(void **)((unsigned char *)component+VEHICLE_OWNER_LIGHT_OFFSET);
    r->bone_index=*(int32_t *)((unsigned char *)component+VEHICLE_OWNER_BONE_OFFSET);
    scope->light=r->light; light=(unsigned char *)r->light;
    if(light && readable_memory(light,VEHICLE_OWNER_LIGHT_BYTES)) {
        memcpy(&r->spatial_handle,light+VEHICLE_OWNER_SPATIAL_HANDLE_OFFSET,8);
        memcpy(&r->descriptor_handle,light+VEHICLE_OWNER_DESCRIPTOR_HANDLE_OFFSET,8);
        r->light_enabled=light[VEHICLE_OWNER_ENABLED_OFFSET];
        r->light_secondary=light[VEHICLE_OWNER_SECONDARY_OFFSET]; r->valid|=2U;
    }
    vehicle_capture_unlock(); return 1;
}

static void __fastcall hooked_vehicle_light_transform(void *light,void *spatial,void *matrix)
{
    VehicleLightDiagnosticState *d=&g_shadow_engine.vehicle_diagnostics;
    OVERHEAD_BEGIN(transform_probe,OH_VEHICLE_TRANSFORM_PATCH);
    if(d->owner_enabled && InterlockedCompareExchange(&d->state,0,0)==2 && d->owner_tls_valid) {
        VehicleOwnerScope *scope=(VehicleOwnerScope *)TlsGetValue(d->owner_tls);
        if(scope && vehicle_capture_lock()) {
            if(d->state==2 && scope->serial==d->serial && scope->index<d->owner_count) {
                VehicleOwnerRecord *r=&d->owner_records[scope->index];
                if(scope->light!=light) ++r->transform_mismatch;
                else {
                    ++r->transform_seen;
                    if(spatial && readable_memory(spatial,sizeof(void *))) {
                        r->spatial=spatial; r->spatial_root=*(void **)spatial; r->valid|=4U;
                    }
                }
            }
            vehicle_capture_unlock();
        }
    }
    OVERHEAD_END(transform_probe);
    d->original_light_transform(light,spatial,matrix);
}

static void __fastcall hooked_vehicle_light_update(void *component,void *skeleton,
    void *world_transform,float delta,unsigned char force_update)
{
    VehicleLightDiagnosticState *d=&g_shadow_engine.vehicle_diagnostics;
#if SHADOW_ENGINE_INTERNAL_DIAGNOSTICS
    VehicleOwnerScope scope;
    void *previous=NULL;
    int scoped=0,recorded=0;
#endif
    uintptr_t update_caller=(uintptr_t)__builtin_return_address(0);
    OVERHEAD_BEGIN(update_probe,OH_VEHICLE_UPDATE_PATCH);
#if SHADOW_ENGINE_INTERNAL_DIAGNOSTICS
    if(d->owner_enabled && d->owner_tls_valid && InterlockedCompareExchange(&d->state,0,0)==2) {
        recorded=begin_vehicle_owner_record(&scope,component,skeleton,world_transform,delta,
            force_update,update_caller);
        previous=TlsGetValue(d->owner_tls);
        /* Mask an outer scope even when this nested update has no new record. */
        scoped=TlsSetValue(d->owner_tls,recorded?&scope:NULL)!=0;
        if(!scoped) InterlockedExchange(&d->owner_enabled,0);
        if(!scoped && vehicle_capture_lock()) {
            if(d->state==2) ++d->owner_tls_failures;
            vehicle_capture_unlock();
        }
    }
#endif
    OVERHEAD_PAUSE(update_probe);
    d->original_light_update(component,skeleton,world_transform,delta,force_update);
    OVERHEAD_RESUME(update_probe);
    observe_vehicle_selection(component,skeleton,world_transform,update_caller);
#if SHADOW_ENGINE_INTERNAL_DIAGNOSTICS
    if(scoped && !TlsSetValue(d->owner_tls,previous)) {
        /* Never leave a borrowed stack address eligible for future reads. */
        InterlockedExchange(&d->owner_enabled,0);
    }
#endif
    OVERHEAD_END(update_probe);
}

static int install_vehicle_owner_observer(void)
{
    VehicleLightDiagnosticState *d=&g_shadow_engine.vehicle_diagnostics;
    if(!d->light_update_target
#if SHADOW_ENGINE_INTERNAL_DIAGNOSTICS
       || !d->light_transform_target
#else
       || !d->light_batch_target
#endif
       ) {
        append_log("STAGE_VEHICLE_OWNER_OBSERVER enabled=0 reason=unprovenBoundary");
        return 1;
    }
#if SHADOW_ENGINE_INTERNAL_DIAGNOSTICS
    d->owner_tls=TlsAlloc();
    if(d->owner_tls==TLS_OUT_OF_INDEXES) return 0;
    d->owner_tls_valid=1;
#endif
    if(d->light_batch_target) {
        d->batch_tls=TlsAlloc();
        if(d->batch_tls==TLS_OUT_OF_INDEXES) return 0;
        d->batch_tls_valid=1;
        if(!install_detour(d->light_batch_target,g_vehicle_batch_prefix,18U,
            hooked_vehicle_light_batch,(void **)&d->original_light_batch)) return 0;
        InterlockedExchange(&d->batch_enabled,1);
    }
#if SHADOW_ENGINE_INTERNAL_DIAGNOSTICS
    if(!install_detour(d->light_transform_target,g_vehicle_owner_transform_prefix,15U,
        hooked_vehicle_light_transform,(void **)&d->original_light_transform)) return 0;
    if(d->batch_enabled && !install_vehicle_lifetime_observer()) return 0;
#endif
    if(!install_detour(d->light_update_target,g_vehicle_owner_update_prefix,18U,
        hooked_vehicle_light_update,(void **)&d->original_light_update)) return 0;
    InterlockedExchange(&d->selection_enabled,d->batch_enabled?1:0);
    initialize_vehicle_player_anchor();
    initialize_vehicle_driver_identity();
    append_log("STAGE_VEHICLE_DISTANCE_ANCHOR localPlayerProof=%u fallback=nativeReference profileCoverage=explicit",
        d->player_root_address?1U:0U);
#if SHADOW_ENGINE_INTERNAL_DIAGNOSTICS
    InterlockedExchange(&d->owner_enabled,1);
#endif
    append_log("STAGE_VEHICLE_BATCH_OBSERVER enabled=%ld batchRva=0x%llX boundedCapture=%u nativePassthrough=1 generationProven=0 publication=wholeBatchAfterNative componentRead=afterUpdate",
        (long)d->batch_enabled,d->light_batch_target?
        (unsigned long long)(d->light_batch_target-g_shadow_engine.bootstrap.disrupt_base):0ULL,
        SHADOW_ENGINE_INTERNAL_DIAGNOSTICS);
    append_log("STAGE_VEHICLE_OWNER_OBSERVER enabled=1 updateRva=0x%llX transformRva=0x%llX boundedCapture=%u mutation=0 ownerProven=0",
        (unsigned long long)(d->light_update_target-g_shadow_engine.bootstrap.disrupt_base),
        d->light_transform_target?
        (unsigned long long)(d->light_transform_target-g_shadow_engine.bootstrap.disrupt_base):0ULL,
        SHADOW_ENGINE_INTERNAL_DIAGNOSTICS);
    append_log("STAGE_VEHICLE_SELECTION_OBSERVER enabled=%ld epochBanks=%u hashSlotsPerEpoch=%u snapshotRecords=%u stableVehicles=%u overrideVehicles=%u keepVehicles=%u rendererEpochObserved=1 processLifetimeEviction=0 candidateMutation=1 fullHandleGenerationProven=1 uncertaintyPassthrough=perCandidate residency=nearestPrimaryIncomingThenOutgoing outgoingHoldMs=%u admissionDelayMs=%u distanceSquaredRatio=%u/%u qualityMaxDimension=%u qualityTypes=1,3 policePriority=0 policeRuntimeTagMapped=0",
        (long)d->selection_enabled,VEHICLE_SELECTION_EPOCH_BANKS,
        VEHICLE_SELECTION_SLOTS_PER_EPOCH,VEHICLE_SELECTION_SNAPSHOT_RECORDS,
        VEHICLE_SELECTION_STABLE_VEHICLES,VEHICLE_SELECTION_OVERRIDE_VEHICLES,
        VEHICLE_SELECTION_KEEP_VEHICLES,VEHICLE_TRANSITION_HOLD_MS,
        0U,VEHICLE_RESIDENCY_DISTANCE_NUMERATOR,
        VEHICLE_RESIDENCY_DISTANCE_DENOMINATOR,shadow_policy_resolution());
    return 1;
}

static void rollback_vehicle_owner_observer(void)
{
    VehicleLightDiagnosticState *d=&g_shadow_engine.vehicle_diagnostics;
    InterlockedExchange(&d->owner_enabled,0);
    InterlockedExchange(&d->batch_enabled,0);
    InterlockedExchange(&d->selection_enabled,0);
#if SHADOW_ENGINE_INTERNAL_DIAGNOSTICS
    rollback_vehicle_lifetime_observer();
#endif
    if(d->batch_tls_valid) { TlsFree(d->batch_tls); d->batch_tls_valid=0; }
    if(d->owner_tls_valid) { TlsFree(d->owner_tls); d->owner_tls_valid=0; }
}
static void vehicle_capture_close_if_complete(void)
{
    VehicleLightDiagnosticState *d=&g_shadow_engine.vehicle_diagnostics;
    /* Called only by the diagnostic writer owner. Timeout takes this same
     * nonwaiting ownership token, so it cannot freeze an in-progress copy. */
    if(d->state==2 && d->sample_count==(LONG)VEHICLE_CAPTURE_SAMPLES &&
       (!d->resolution_enabled || d->queue_sample_count==(LONG)VEHICLE_CAPTURE_SAMPLES) &&
       (!d->owner_focused || GetTickCount()-d->owner_window_tick[VEHICLE_CAPTURE_SAMPLES-1U]>=VEHICLE_OWNER_WINDOW_MS))
        InterlockedExchange(&d->state,3);
}

/* Stack-owned protection facts for one synchronous candidate or quality pass.
 * Never a lifetime pin, owner lookup, or cross-call pointer-validity cache. */
#define VEHICLE_COPY_REGIONS 128U
typedef struct VehicleCopyRegionCache {
    struct { uintptr_t begin,end; } regions[VEHICLE_COPY_REGIONS];
    uint32_t count,uncached,queries,hits;
} VehicleCopyRegionCache;

static int vehicle_copy_readable(VehicleCopyRegionCache *cache,const void *address,size_t size)
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

static int copy_vehicle_candidate_node(VehicleCandidateRecord *record,void *candidate,
    VehicleCopyRegionCache *cache)
{
    unsigned char *node=(unsigned char *)candidate;
    record->candidate=candidate;
    record->renderer_type=-1;
    if(!node || !vehicle_copy_readable(cache,node,0x18U)) return 0;
    record->readable=1;
    record->next=*(void **)node;
    record->descriptor=*(void **)(node+8U);
    record->spatial=*(void **)(node+0x10U);
    return 1;
}

static void copy_vehicle_candidate_descriptor(VehicleCandidateRecord *record,int detailed,
    VehicleCopyRegionCache *cache)
{
    if(record->descriptor && vehicle_copy_readable(cache,record->descriptor,0x44U)) {
        int32_t type=*(int32_t *)((unsigned char *)record->descriptor+8U);
        record->renderer_type=(type>=0 && type<=4)?type:-1;
        if(detailed)
            record->descriptor_flags=*(uint32_t *)((unsigned char *)record->descriptor+0x40U);
    }
}

static void copy_vehicle_candidate_handle(VehicleCandidateRecord *record,
    VehicleCopyRegionCache *cache)
{
    if(record->spatial && vehicle_copy_readable(cache,(unsigned char *)record->spatial+
       VEHICLE_SPATIAL_DESCRIPTOR_HANDLE_OFFSET,sizeof(uint64_t))) {
        memcpy(&record->descriptor_handle,(unsigned char *)record->spatial+
            VEHICLE_SPATIAL_DESCRIPTOR_HANDLE_OFFSET,sizeof(uint64_t));
        record->descriptor_handle_valid=record->descriptor_handle!=UINT64_MAX;
    }
}

static int copy_vehicle_candidate_fields_scoped(VehicleCandidateRecord *record,void *candidate,
    int detailed,VehicleCopyRegionCache *cache)
{
    if(!copy_vehicle_candidate_node(record,candidate,cache)) return 0;
    copy_vehicle_candidate_descriptor(record,detailed,cache);
    /* Policy never filters other types; requested diagnostics still copy them. */
    if(cache && record->renderer_type!=3) return 1;
    if(detailed && record->spatial && readable_memory(
       (unsigned char *)record->spatial+0x14U,3U*sizeof(float))) {
        record->observed_x=*(float *)((unsigned char *)record->spatial+0x14U);
        record->observed_y=*(float *)((unsigned char *)record->spatial+0x18U);
        record->observed_z=*(float *)((unsigned char *)record->spatial+0x1CU);
    }
    copy_vehicle_candidate_handle(record,cache);
    return 1;
}

typedef struct VehicleCopyAddress {
    uintptr_t address;
    uint32_t index;
} VehicleCopyAddress;

static int compare_vehicle_copy_address(const void *left,const void *right)
{
    uintptr_t a=((const VehicleCopyAddress *)left)->address;
    uintptr_t b=((const VehicleCopyAddress *)right)->address;
    return a<b?-1:(a>b?1:0);
}

/* Only the read order changes. Native list/record order stays untouched.
 * VirtualQuery describes a forward span from the queried page: ascending reads
 * reuse that validated span without assuming AllocationBase has one protection.
 * All addresses and protection facts expire at the end of this synchronous call. */
static void copy_vehicle_candidate_payloads(VehicleCandidateRecord *records,uint32_t count,
    VehicleCopyRegionCache *cache)
{
    VehicleCopyAddress order[VEHICLE_CAPTURE_MAX_CANDIDATES];
    uint32_t i,n=0;
    for(i=0;i<count;++i) if(records[i].descriptor) {
        order[n].address=(uintptr_t)records[i].descriptor;
        order[n++].index=i;
    }
    qsort(order,n,sizeof(order[0]),compare_vehicle_copy_address);
    for(i=0;i<n;++i) copy_vehicle_candidate_descriptor(&records[order[i].index],0,cache);
    n=0;
    for(i=0;i<count;++i) if(records[i].renderer_type==3 && records[i].spatial) {
        order[n].address=(uintptr_t)records[i].spatial;
        order[n++].index=i;
    }
    qsort(order,n,sizeof(order[0]),compare_vehicle_copy_address);
    for(i=0;i<n;++i) copy_vehicle_candidate_handle(&records[order[i].index],cache);
}

static int copy_vehicle_candidate_fields(VehicleCandidateRecord *record,void *candidate,
    int detailed)
{
    return copy_vehicle_candidate_fields_scoped(record,candidate,detailed,NULL);
}

static int copy_vehicle_candidate(VehicleCandidateRecord *record,void *candidate)
{
    return copy_vehicle_candidate_fields(record,candidate,1);
}

static int vehicle_capture_candidate_type(const VehicleCandidateRecord *record)
{
    return record ? record->renderer_type : -1;
}

static uint32_t vehicle_identity_sample_count(uint32_t mask)
{
    uint32_t count=0;
    while(mask) { count+=mask&1U; mask>>=1U; }
    return count;
}

static int vehicle_identity_matches_owner(const VehicleIdentityRecord *identity,
    const VehicleOwnerRecord *owner)
{
    return identity->component_token==(uint64_t)(uintptr_t)owner->component &&
        identity->light_token==(uint64_t)(uintptr_t)owner->light &&
        identity->skeleton_token==(uint64_t)(uintptr_t)owner->skeleton &&
        identity->descriptor_handle==owner->descriptor_handle;
}

/* The frozen F10 worker is the sole writer. The registry retains numeric
 * identity tokens only and never follows them. Absence means "not observed in
 * this capture", not despawn; changed/reused values remain diagnostic leads. */
static void update_vehicle_identity_registry(VehicleLightDiagnosticState *d,LONG serial)
{
    uint32_t i,j,current=0,groups=0,component_leads=0,handle_leads=0;
    for(i=0;i<d->identity_count;++i) d->identity_records[i].last_sample_mask=0;
    for(i=0;i<d->owner_count;++i) {
        const VehicleOwnerRecord *owner=&d->owner_records[i];
        VehicleIdentityRecord *identity=NULL;
        if((owner->valid&3U)!=3U || owner->epoch>=VEHICLE_CAPTURE_SAMPLES ||
           owner->descriptor_handle==UINT64_MAX) continue;
        for(j=0;j<d->identity_count;++j) {
            if(vehicle_identity_matches_owner(&d->identity_records[j],owner)) {
                identity=&d->identity_records[j]; break;
            }
        }
        if(!identity) {
            if(d->identity_count>=VEHICLE_IDENTITY_MAX_RECORDS) {
                ++d->identity_overflow; continue;
            }
            identity=&d->identity_records[d->identity_count++];
            memset(identity,0,sizeof(*identity));
            identity->component_token=(uint64_t)(uintptr_t)owner->component;
            identity->light_token=(uint64_t)(uintptr_t)owner->light;
            identity->skeleton_token=(uint64_t)(uintptr_t)owner->skeleton;
            identity->descriptor_handle=owner->descriptor_handle;
            identity->lineage_id=++d->identity_next_lineage;
            identity->first_serial=serial;
        }
        identity->last_sample_mask|=1U<<owner->epoch;
    }
    for(i=0;i<d->identity_count;++i) {
        VehicleIdentityRecord *identity=&d->identity_records[i];
        if(identity->last_sample_mask) {
            ++current;
            ++identity->captures_seen;
            identity->total_sample_observations+=vehicle_identity_sample_count(identity->last_sample_mask);
            identity->last_serial=serial;
            identity->consecutive_absent=0;
        } else if(identity->last_serial>0 && identity->last_serial<serial) {
            ++identity->absent_captures;
            ++identity->consecutive_absent;
        }
    }
    for(i=0;i<d->identity_count;++i) {
        const VehicleIdentityRecord *identity=&d->identity_records[i];
        uint32_t current_component_variants=0,prior_component_variants=0;
        uint32_t current_handle_identities=0,prior_handle_identities=0;
        if(identity->last_sample_mask) {
            for(j=0;j<d->identity_count;++j) {
                const VehicleIdentityRecord *other=&d->identity_records[j];
                if(i==j) continue;
                if(other->component_token==identity->component_token) {
                    if(other->last_sample_mask) ++current_component_variants;
                    else if(other->last_serial>0 && other->last_serial<serial) ++prior_component_variants;
                }
                if(other->descriptor_handle==identity->descriptor_handle) {
                    if(other->last_sample_mask) ++current_handle_identities;
                    else if(other->last_serial>0 && other->last_serial<serial) ++prior_handle_identities;
                }
            }
            if(current_component_variants || prior_component_variants) ++component_leads;
            if(current_handle_identities || prior_handle_identities) ++handle_leads;
        }
        append_log("STAGE_VEHICLE_LINEAGE_RECORD serial=%ld lineage=%ld firstSerial=%ld lastSerial=%ld capturesSeen=%u samplesSeen=%u sampleMask=0x%X absentCaptures=%u absentStreak=%u componentToken=0x%llX lightToken=0x%llX skeletonToken=0x%llX descriptorHandle=0x%llX currentComponentVariants=%u priorComponentVariants=%u currentHandleIdentities=%u priorHandleIdentities=%u opaqueTokensOnly=1 nativeLifetimeProven=0",
            (long)serial,(long)identity->lineage_id,(long)identity->first_serial,
            (long)identity->last_serial,identity->captures_seen,
            identity->total_sample_observations,identity->last_sample_mask,
            identity->absent_captures,identity->consecutive_absent,
            (unsigned long long)identity->component_token,
            (unsigned long long)identity->light_token,
            (unsigned long long)identity->skeleton_token,
            (unsigned long long)identity->descriptor_handle,
            current_component_variants,prior_component_variants,
            current_handle_identities,prior_handle_identities);
    }
    for(i=0;i<d->identity_count;++i) {
        const VehicleIdentityRecord *identity=&d->identity_records[i];
        uint32_t members=0,handles=0,mask=0;
        if(!identity->last_sample_mask || !identity->skeleton_token) continue;
        for(j=0;j<i;++j)
            if(d->identity_records[j].last_sample_mask &&
               d->identity_records[j].skeleton_token==identity->skeleton_token) break;
        if(j<i) continue;
        for(j=0;j<d->identity_count;++j) {
            const VehicleIdentityRecord *other=&d->identity_records[j];
            uint32_t k,unique=1;
            if(!other->last_sample_mask || other->skeleton_token!=identity->skeleton_token) continue;
            ++members; mask|=other->last_sample_mask;
            for(k=0;k<j;++k)
                if(d->identity_records[k].last_sample_mask &&
                   d->identity_records[k].skeleton_token==identity->skeleton_token &&
                   d->identity_records[k].descriptor_handle==other->descriptor_handle) {
                    unique=0; break;
                }
            if(unique) ++handles;
        }
        ++groups;
        append_log("STAGE_VEHICLE_LINEAGE_GROUP serial=%ld skeletonToken=0x%llX identities=%u distinctHandles=%u sampleMask=0x%X groupingLeadOnly=1 ownerProven=0",
            (long)serial,(unsigned long long)identity->skeleton_token,members,handles,mask);
    }
    append_log("STAGE_VEHICLE_LINEAGE_SUMMARY serial=%ld registryRecords=%u currentIdentities=%u groups=%u componentReuseLeads=%u handleReuseOrSharingLeads=%u overflow=%u absenceMeansNotObserved=1 destructionProven=0 ownerProven=0 mutation=0",
        (long)serial,d->identity_count,current,groups,component_leads,handle_leads,
        d->identity_overflow);
}

static void correlate_vehicle_queue_window(VehicleCandidateSample *sample,
    const VehicleQueueSample *queue)
{
    uint32_t i,j;
    sample->queue_correlation_tick_delta_ms=sample->tick>queue->tick ?
        sample->tick-queue->tick : queue->tick-sample->tick;
    for(i=0;i<queue->copied_entries;++i) {
        const VehicleQueueRecord *record=&queue->records[i];
        uint32_t matches=0,protected_match=0,type1_match=0;
        if(!record->submitted || !record->descriptor_handle_valid) continue;
        for(j=0;j<sample->copied_candidates;++j) {
            const VehicleCandidateRecord *candidate=&sample->records[j];
            if(!candidate->descriptor_handle_valid ||
               candidate->descriptor_handle!=record->descriptor_handle ||
               (!candidate->policy_protected && !candidate->selection_vehicle_type1))
                continue;
            ++matches;
            protected_match|=candidate->policy_protected;
            type1_match|=candidate->selection_vehicle_type1;
        }
        if(matches!=1U) {
            if(matches>1U) ++sample->queue_handle_ambiguous;
            continue;
        }
        if(protected_match) {
            ++sample->queue_protected_handle_matches;
            sample->queue_protected_faces+=record->face_count;
        }
        if(type1_match) {
            ++sample->queue_vehicle_type1_handle_matches;
            sample->queue_vehicle_type1_faces+=record->face_count;
        }
    }
}

#include "18_vehicle_capture_analysis.inc"

static DWORD WINAPI flush_vehicle_candidate_capture(void *unused)
{
    VehicleLightDiagnosticState *diagnostic=&g_shadow_engine.vehicle_diagnostics;
    LONG serial=InterlockedCompareExchange(&diagnostic->serial,0,0);
    LONG sample_count=InterlockedCompareExchange(&diagnostic->sample_count,0,0);
    LONG sample_index,queue_count=diagnostic->queue_sample_count;
    uint32_t total_records=0;
    (void)unused;
    if(sample_count<0) sample_count=0;
    if(sample_count>(LONG)VEHICLE_CAPTURE_SAMPLES)
        sample_count=(LONG)VEHICLE_CAPTURE_SAMPLES;
    if(queue_count<0) queue_count=0;
    if(queue_count>(LONG)VEHICLE_CAPTURE_SAMPLES)
        queue_count=(LONG)VEHICLE_CAPTURE_SAMPLES;
    append_log("STAGE_VEHICLE_CAPTURE_BEGIN serial=%ld samples=%ld intervalMs=%u durationMs=%lu maxCandidatesPerSample=%u mutation=transactionalCandidateChain enginePointerDereferenceAfterHook=0 opaqueIdentityRetention=1 selectionPolicy=configurableStablePlusOverride2CompleteVehicleGroups qualityMaxDimension=%u",
        (long)serial,(long)sample_count,VEHICLE_CAPTURE_INTERVAL_MS,
        sample_count>0 ? (unsigned long)(diagnostic->samples[sample_count-1].tick-
            diagnostic->samples[0].tick) : 0UL,
        VEHICLE_CAPTURE_MAX_CANDIDATES,shadow_policy_resolution());
    for(sample_index=0;sample_index<sample_count;++sample_index) {
        VehicleCandidateSample *sample=&diagnostic->samples[sample_index];
        append_log("STAGE_DRIVER_SELECTION serial=%ld sample=%ld actualLimiterLookup=%u stage=%s detail=%s playerId=0x%llX vehicleId=0x%llX seatPlayerId=0x%llX owner=0x%llX trafficLimit=%u extraSlots=%u",
            (long)serial,(long)sample_index,sample->limiter_evaluated,
            driver_identity_stage_name(sample->driver_trace.stage),
            driver_identity_detail_name(sample->driver_trace.detail),
            (unsigned long long)sample->driver_trace.player_id,
            (unsigned long long)sample->driver_trace.vehicle_id,
            (unsigned long long)sample->driver_trace.seat_player_id,
            (unsigned long long)sample->driver_trace.owner,
            sample->selection_main_limit,sample->selection_extra_slots);
        log_vehicle_capture_analysis(serial,sample_index,sample,
            sample_index ? &diagnostic->samples[sample_index-1] : NULL);
        uint32_t record_index;
        if(sample_index<queue_count)
            correlate_vehicle_queue_window(sample,
                &diagnostic->queue_samples[sample_index]);
        append_log("STAGE_VEHICLE_CAPTURE_SAMPLE serial=%ld sample=%ld tickOffsetMs=%lu managerCall=%ld rendererEpoch=%ld reportedCandidates=%u copiedCandidates=%u admittedCandidates=%u cachedCandidates=%u chainEnded=%u truncated=%u playerAnchor=%u driverValid=%u driverHeadlightLights=%u driverOwner=0x%llX driverEntityId=0x%llX driverPlayerId=0x%llX",
            (long)serial,(long)sample_index,
            (unsigned long)(sample->tick-diagnostic->samples[0].tick),
            (long)sample->manager_call,(long)sample->renderer_epoch,
            sample->reported_candidates,
            sample->copied_candidates,sample->admitted_candidates,
            sample->cached_candidates,sample->chain_ended,
            sample->reported_candidates>sample->copied_candidates?1U:0U,sample->player_anchor_valid,
            sample->driver_valid,sample->driver_headlight_lights,
            (unsigned long long)sample->driver_owner,(unsigned long long)sample->driver_entity_id,
            (unsigned long long)sample->driver_player_id);
        append_log("STAGE_VEHICLE_SELECTION_SUMMARY serial=%ld sample=%ld beforeNativeManager=1 managerCall=%ld rendererEpoch=%ld registryRecords=%u registryVehicles=%u registryAgeLe16=%u registryAge17To50=%u registryAge51To250=%u registryAgeOver250=%u registryGaps=%ld registryOverflow=%ld registryRotations=%ld registrySameEpoch=%u registryPreviousEpoch=%u registryOlderEpoch=%u spotlight3Candidates=%u exactMatches=%u exactVehicles=%u exactCompleteVehicles=%u managerDelta0=%u managerDelta1=%u managerDelta2To4=%u managerDelta5Plus=%u managerDeltaFuture=%u managerStrictMatches=%u managerNonStrictMatches=%u rendererSame=%u rendererPrevious=%u renderer2To3=%u rendererOlder=%u rendererFuture=%u rendererRecentMatches=%u rendererNonRecentMatches=%u ambiguous=%u unknown=%u completeVehicleGroups=%u partialVehicleGroups=%u keepVehicles=%u wouldKeepLights=%u selectionBasis=rendererDelta0To3 candidateMutation=1 worldUnknownPassthrough=1 fullHandleGenerationProven=1",
            (long)serial,(long)sample_index,(long)sample->manager_call,
            (long)sample->renderer_epoch,
            sample->selection_snapshot_records,sample->selection_snapshot_vehicles,
            sample->selection_snapshot_age_le_16,sample->selection_snapshot_age_17_50,
            sample->selection_snapshot_age_51_250,sample->selection_snapshot_age_over_250,
            (long)sample->selection_snapshot_gaps,(long)sample->selection_snapshot_overflow,
            (long)sample->selection_snapshot_rotations,
            sample->selection_snapshot_same_epoch,
            sample->selection_snapshot_previous_epoch,
            sample->selection_snapshot_older_epoch,
            sample->selection_spotlight3,
            sample->selection_exact_matches,sample->selection_exact_vehicles,
            sample->selection_exact_complete_vehicles,
            sample->selection_delta_zero,sample->selection_delta_one,
            sample->selection_delta_two_to_four,sample->selection_delta_five_plus,
            sample->selection_delta_future,
            sample->selection_current_matches,sample->selection_older_matches,
            sample->selection_renderer_same,sample->selection_renderer_previous,
            sample->selection_renderer_two_to_three,sample->selection_renderer_older,
            sample->selection_renderer_future,
            sample->selection_renderer_recent_matches,
            sample->selection_renderer_nonrecent_matches,
            sample->selection_ambiguous,sample->selection_unknown,
            sample->selection_vehicles,sample->selection_partial_vehicles,
            sample->selection_main_limit+sample->selection_extra_slots,
            sample->selection_would_keep_lights);
        append_log("STAGE_VEHICLE_ADMISSION_SUMMARY serial=%ld sample=%ld limiterEnabled=%u limiterEvaluated=%u limiterActive=%u limiterFailOpen=%u limiterSuppressed=%u residencyApplied=%u residencyDegraded=%u residencyInitialized=%u residencyReplaced=%u residencyPending=%u residencyResidents=%u protectedHeadlightLights=%u protectedHeadlightAdmittedLights=%u protectedHeadlightVehicles=%u protectedFullyAdmittedVehicles=%u protectedPartiallyAdmittedVehicles=%u protectedNotAdmittedVehicles=%u type1Candidates=%u type1Ambiguous=%u type1Unknown=%u vehicleType1Candidates=%u vehicleType1AdmittedCandidates=%u vehicleType1Vehicles=%u vehicleType1AdmittedVehicles=%u protectedLossWithVehicleType1Admitted=%u coincidenceNotCausation=1 vehicleType1SirenLikeRoleNotDirectlyProven=1",
            (long)serial,(long)sample_index,sample->limiter_enabled,
            sample->limiter_evaluated,sample->limiter_active,
            sample->limiter_fail_open,sample->limiter_suppressed,
            sample->residency_applied,sample->residency_degraded,
            sample->residency_initialized,sample->residency_replaced,
            sample->residency_pending,sample->residency_residents,
            sample->protected_headlight_lights,
            sample->protected_headlight_admitted_lights,
            sample->protected_headlight_vehicles,
            sample->protected_headlight_fully_admitted_vehicles,
            sample->protected_headlight_partially_admitted_vehicles,
            sample->protected_headlight_not_admitted_vehicles,
            sample->type1_candidates,sample->type1_ambiguous,
            sample->type1_unknown,sample->vehicle_type1_candidates,
            sample->vehicle_type1_admitted_candidates,
            sample->vehicle_type1_vehicles,
            sample->vehicle_type1_admitted_vehicles,
            sample->protected_loss_with_vehicle_type1_admitted);
        append_log("STAGE_VEHICLE_QUEUE_CORRELATION serial=%ld sample=%ld queueSampleAvailable=%u tickDeltaMs=%lu protectedHandleMatches=%u protectedFaces=%u vehicleType1HandleMatches=%u vehicleType1Faces=%u ambiguousRows=%u sameWindowObservationOnly=1 admissionCausationProven=0",
            (long)serial,(long)sample_index,sample_index<queue_count,
            (unsigned long)sample->queue_correlation_tick_delta_ms,
            sample->queue_protected_handle_matches,
            sample->queue_protected_faces,
            sample->queue_vehicle_type1_handle_matches,
            sample->queue_vehicle_type1_faces,
            sample->queue_handle_ambiguous);
        for(record_index=0;record_index<sample->copied_candidates;++record_index) {
            const VehicleCandidateRecord *record=&sample->records[record_index];
            append_log("STAGE_VEHICLE_CAPTURE_RECORD serial=%ld sample=%ld index=%u candidate=%p next=%p descriptor=%p spatial=%p readable=%u type=%d descriptorFlags=0x%08X observedTuple=%.6g,%.6g,%.6g admitted=%u lane=%s cacheOwner=%p bindingFlags=0x%02X descriptorHandle=0x%llX handleValid=%u candidateHandleInstances=%u selectionMatches=%u vehicle=0x%llX publishManagerCall=%ld managerDelta=%ld publishRendererEpoch=%ld rendererDelta=%ld ageMs=%lu selectionDistanceSquared=%.6g managerStrict=%u rendererRecent=%u groupLights=%u groupComplete=%u vehicleRank=%u wouldKeepShadow=%u policyProtected=%u vehicleType1=%u policyRole=%u activePolicy=1",
                (long)serial,(long)sample_index,record_index,
                record->candidate,record->next,record->descriptor,record->spatial,
                record->readable,vehicle_capture_candidate_type(record),
                record->descriptor_flags,record->observed_x,record->observed_y,
                record->observed_z,record->admitted,
                record->admitted ? (record->cache_owner?"cached":"dynamic") : "none",
                record->cache_owner,record->binding_flags&0xFFU,
                (unsigned long long)record->descriptor_handle,
                record->descriptor_handle_valid,
                record->selection_candidate_instances,record->selection_matches,
                (unsigned long long)record->selection_vehicle,
                (long)record->selection_publish_manager_call,
                (long)record->selection_manager_delta,
                (long)record->selection_publish_renderer_epoch,
                (long)record->selection_renderer_delta,
                (unsigned long)record->selection_age_ms,
                record->selection_distance_squared,record->selection_current,
                record->selection_renderer_recent,
                record->selection_group_lights,record->selection_group_complete,
                record->selection_rank,record->selection_would_keep,
                record->policy_protected,record->selection_vehicle_type1,record->selection_role);
        }
        total_records+=sample->copied_candidates;
    }
    append_log("STAGE_VEHICLE_RESOLUTION_BEGIN serial=%ld enabled=%ld queueSamples=%ld metadataOnly=1 gpuDescriptorRead=0 ownerProven=0 handleLinkEnabled=%ld",
        (long)serial,(long)diagnostic->resolution_enabled,(long)queue_count,(long)diagnostic->handle_link_enabled);
    for(sample_index=0;sample_index<queue_count;++sample_index) {
        const VehicleQueueSample *sample=&diagnostic->queue_samples[sample_index];
        uint32_t index;
        append_log("STAGE_VEHICLE_QUALITY serial=%ld sample=%ld evaluated=%u lockSkipped=%u selectedCap=%u qualityEnabled=%u decisionScope=actualQualityCall",
            (long)serial,(long)sample_index,sample->quality_evaluated,
            sample->quality_lock_skipped,
            shadow_policy_resolution_from(sample->quality_policy),
            !(sample->quality_policy&SHADOW_POLICY_DISABLE_QUALITY));
        append_log("STAGE_SHADOW_COST serial=%ld sample=%ld nativeRendererSubmitUs=%.3f worldEnabled=%u worldCap=%u vehicleLockSkipsDelta=%ld worldLockSkipsDelta=%ld cpuWallElapsed=1 gpuTime=0 skipScope=sampleInterval settingsSnapshotOnly=1",
            (long)serial,(long)sample_index,
            vehicle_capture_elapsed_us(sample->native_qpc[0],sample->native_qpc[1],
                g_shadow_engine.bootstrap.snapshot.frequency.QuadPart),
            sample->world_enabled,sample->world_cap,
            (long)(sample->vehicle_lock_skips_after-sample->vehicle_lock_skips_before),
            (long)(sample->world_lock_skips_after-sample->world_lock_skips_before));
        append_log("STAGE_VEHICLE_RESOLUTION_SAMPLE serial=%ld sample=%ld tickOffsetMs=%lu thread=%lu rendererCall=%ld originalEntries=%u admittedEntries=%u copiedEntries=%u complete=%u selectedCap=%u qualityRequested=%u settingsSnapshotOnly=1",
            (long)serial,(long)sample_index,
            (unsigned long)(sample->tick-diagnostic->started_tick),(unsigned long)sample->thread,
            (long)sample->renderer_call,sample->original_entries,sample->admitted_entries,
            sample->copied_entries,sample->complete,
            shadow_policy_resolution_from(sample->requested_policy),
            !(sample->requested_policy&SHADOW_POLICY_DISABLE_QUALITY));
        for(index=0;index<sample->copied_entries;++index) {
            const VehicleQueueRecord *record=&sample->records[index];
            const VehicleCandidateRecord *light=&record->light;
            append_log("STAGE_VEHICLE_QUALITY_RECORD serial=%ld sample=%ld entry=%u decision=%s owner=0x%llX",
                (long)serial,(long)sample_index,record->entry_index,
                vehicle_quality_decision_name(record->quality_decision),
                (unsigned long long)record->quality_owner);
            append_log("STAGE_VEHICLE_RESOLUTION_RECORD serial=%ld sample=%ld entry=%u valid=0x%X candidate=%p descriptor=%p spatial=%p spatialRoot=%p type=%d descriptorFlags=0x%08X observedTuple=%.6g,%.6g,%.6g submitted=%u faces=%u mapStart=%u requestedWidth=%u requestedHeight=%u resource=%p resourceWidth=%u resourceHeight=%u descriptorHandle=0x%llX descriptorHandleValid=%u",
                (long)serial,(long)sample_index,record->entry_index,record->valid,
                light->candidate,light->descriptor,light->spatial,record->spatial_root,
                light->renderer_type,light->descriptor_flags,light->observed_x,
                light->observed_y,light->observed_z,record->submitted,record->face_count,
                record->map_start,record->width,record->height,record->resource,
                record->resource_width,record->resource_height,
                (unsigned long long)record->descriptor_handle,record->descriptor_handle_valid);
            append_log("STAGE_SHADOW_LOD_RECORD serial=%ld sample=%ld entry=%u nativeWidth=%u nativeHeight=%u requestedWidth=%u requestedHeight=%u policySampled=%u resourceWidth=%u resourceHeight=%u resourceValid=%u requestResourceMismatch=%u distanceKnown=0 metadataOnly=1",
                (long)serial,(long)sample_index,record->entry_index,
                record->native_width,record->native_height,record->width,record->height,
                record->policy_sampled,record->resource_width,record->resource_height,
                !!(record->valid&8U),
                record->policy_sampled && (record->valid&8U) &&
                    (record->width!=record->resource_width || record->height!=record->resource_height));
            log_shadow_resource_transition(serial,sample_index,record);
        }
    }
    append_log("STAGE_VEHICLE_RESOLUTION_END serial=%ld queueSamples=%ld metadataOnly=1",
        (long)serial,(long)queue_count);
    append_log("STAGE_VEHICLE_OWNER_BEGIN serial=%ld enabled=%ld records=%u overflow=%u tlsFailures=%u maxRecords=%u disruptBase=%p callerAndHeaderCopied=1 ownerProven=0 persistentCopiedTokenRegistry=1 focused=%ld perWindow=%u windowMs=%u probeBudget=%u",
        (long)serial,(long)diagnostic->owner_enabled,diagnostic->owner_count,
        diagnostic->owner_overflow,diagnostic->owner_tls_failures,VEHICLE_OWNER_MAX_RECORDS,
        g_shadow_engine.bootstrap.disrupt_base,
        (long)diagnostic->owner_focused,VEHICLE_OWNER_RECORDS_PER_WINDOW,
        VEHICLE_OWNER_WINDOW_MS,VEHICLE_OWNER_PROBES_PER_WINDOW);
    {
        uint32_t index;
        for(index=0;index<diagnostic->owner_count;++index) {
            const VehicleOwnerRecord *r=&diagnostic->owner_records[index];
            append_log("STAGE_VEHICLE_OWNER_RECORD serial=%ld index=%u epoch=%u tickOffsetMs=%lu thread=%lu valid=0x%X component=%p componentVtable=%p settings=%p light=%p skeleton=%p skeletonHeader=%p skeletonHeaderValid=%u updateCaller=0x%llX worldTransform=%p spatialHandle=0x%llX descriptorHandle=0x%llX boneIndex=%d delta=%.6g forceUpdate=%u lightEnabled=%u lightSecondary=%u transformSeen=%u transformMismatch=%u spatial=%p spatialRoot=%p updateAttempts=%u",
                (long)serial,index,r->epoch,(unsigned long)(r->tick-diagnostic->started_tick),
                (unsigned long)r->thread,r->valid,r->component,r->component_vtable,r->settings,
                r->light,r->skeleton,r->skeleton_header,r->skeleton_header_valid,
                (unsigned long long)r->update_caller,r->world_transform,
                (unsigned long long)r->spatial_handle,
                (unsigned long long)r->descriptor_handle,(int)r->bone_index,r->delta,
                (unsigned)r->force_update,(unsigned)r->light_enabled,(unsigned)r->light_secondary,
                r->transform_seen,r->transform_mismatch,r->spatial,r->spatial_root,r->update_attempts);
            append_log("STAGE_VEHICLE_BATCH_LINK serial=%ld index=%u epoch=%u component=%p vehicle=%p vehicleVtable=%p array=%p count=%u memberIndex=%u flags=0x%X matched=%u generationProven=0",
                (long)serial,index,r->epoch,r->component,r->vehicle,r->vehicle_vtable,
                r->vehicle_array,r->vehicle_count,r->vehicle_index,r->vehicle_flags,
                r->vehicle_flags==VEHICLE_BATCH_MATCH_FLAGS?1U:0U);
            append_log("STAGE_VEHICLE_LIFETIME_LINK serial=%ld index=%u vehicle=%p tracked=%u slot=%u observationSeq=%llu generationProven=0",
                (long)serial,index,r->vehicle,r->lifetime_sequence?1U:0U,r->lifetime_slot,
                (unsigned long long)r->lifetime_sequence);
        }
    }
    for(sample_index=0;sample_index<(LONG)VEHICLE_CAPTURE_SAMPLES;++sample_index) {
        uint32_t entry,index;
        const VehicleQueueSample *sample=&diagnostic->queue_samples[sample_index];
        append_log("STAGE_VEHICLE_OWNER_WINDOW serial=%ld sample=%ld records=%u overflow=%u probes=%u budgetReached=%u unmatchedUpdates=%u unreadableUpdates=%u rendererPublished=%u startOffsetMs=%lu",
            (long)serial,(long)sample_index,diagnostic->owner_window_records[sample_index],
            diagnostic->owner_window_overflow[sample_index],diagnostic->owner_window_probes[sample_index],
            diagnostic->owner_window_probes[sample_index]>=VEHICLE_OWNER_PROBES_PER_WINDOW,
            diagnostic->owner_window_unmatched[sample_index],diagnostic->owner_window_unreadable[sample_index],
            sample_index<queue_count,
            sample_index<queue_count?(unsigned long)(diagnostic->owner_window_tick[sample_index]-diagnostic->started_tick):0UL);
        if(!diagnostic->owner_focused || sample_index>=queue_count) continue;
        for(entry=0;entry<sample->copied_entries && entry<PHYSICAL_QUEUE_ENTRIES;++entry) {
            const VehicleQueueRecord *q=&sample->records[entry];
            uint32_t matches=0;
            if(!q->submitted || (q->valid&3U)!=3U || !q->descriptor_handle_valid) continue;
            for(index=0;index<diagnostic->owner_count;++index) {
                const VehicleOwnerRecord *r=&diagnostic->owner_records[index];
                if(r->epoch==(uint32_t)sample_index && (r->valid&3U)==3U &&
                   r->descriptor_handle==q->descriptor_handle) ++matches;
            }
            append_log("STAGE_VEHICLE_OWNER_LINK serial=%ld sample=%ld entry=%u descriptorHandle=0x%llX componentMatches=%u rendererInstances=%u identityOnly=1 ownerProven=0",
                (long)serial,(long)sample_index,entry,(unsigned long long)q->descriptor_handle,matches,
                vehicle_owner_handle_matches(sample,q->descriptor_handle));
        }
    }
    append_log("STAGE_VEHICLE_OWNER_END serial=%ld records=%u mutation=0",
        (long)serial,diagnostic->owner_count);
    {
        uint32_t i,j,matched=0,unmatched=0,groups=0;
        for(i=0;i<diagnostic->owner_count;++i) {
            const VehicleOwnerRecord *r=&diagnostic->owner_records[i];
            if(r->vehicle_flags!=VEHICLE_BATCH_MATCH_FLAGS) { ++unmatched; continue; }
            ++matched;
            for(j=0;j<i;++j)
                if(diagnostic->owner_records[j].vehicle_flags==VEHICLE_BATCH_MATCH_FLAGS &&
                   diagnostic->owner_records[j].vehicle==r->vehicle) break;
            if(j==i) ++groups;
        }
        append_log("STAGE_VEHICLE_BATCH_SUMMARY serial=%ld enabled=%ld matched=%u unmatched=%u vehicles=%u tlsFailures=%u copiedOnly=1 generationProven=0",
            (long)serial,(long)diagnostic->batch_enabled,matched,unmatched,groups,diagnostic->batch_tls_failures);
    }
    flush_vehicle_lifetime_snapshot(serial);
    update_vehicle_identity_registry(diagnostic,serial);
    append_log("STAGE_VEHICLE_CAPTURE_END serial=%ld samples=%ld copiedRecords=%u mutation=0 classifyAutomatically=0 nextAction=reviewRendererEpochCoverageAndHashSaturation",
        (long)serial,(long)sample_count,total_records);
    InterlockedExchange(&diagnostic->sample_count,0);
    InterlockedExchange(&diagnostic->queue_sample_count,0);
    InterlockedExchange(&diagnostic->state,0);
    return 0;
}

static void schedule_vehicle_candidate_capture_flush(void)
{
    VehicleLightDiagnosticState *diagnostic=&g_shadow_engine.vehicle_diagnostics;
    HANDLE thread;
    if(InterlockedCompareExchange(&diagnostic->state,4,3)!=3) return;
    thread=CreateThread(NULL,0,flush_vehicle_candidate_capture,NULL,0,NULL);
    if(thread) {
        CloseHandle(thread);
        return;
    }
    InterlockedIncrement(&diagnostic->flush_failures);
    append_log("STAGE_VEHICLE_CAPTURE_FLUSH_FAILED serial=%ld error=%lu captureDiscarded=1 mutation=0",
        (long)diagnostic->serial,(unsigned long)GetLastError());
    InterlockedExchange(&diagnostic->sample_count,0);
    InterlockedExchange(&diagnostic->state,0);
}

static void arm_vehicle_candidate_capture(void)
{
    VehicleLightDiagnosticState *diagnostic=&g_shadow_engine.vehicle_diagnostics;
    LONG state;
    if(!vehicle_capture_lock()) return;
    state=InterlockedCompareExchange(&diagnostic->state,1,0);
    if(state!=0) {
        vehicle_capture_unlock();
        append_log("STAGE_VEHICLE_CAPTURE_IGNORED activeState=%ld serial=%ld instruction=waitForCaptureEnd",
            (long)state,(long)diagnostic->serial);
        return;
    }
    memset(diagnostic->samples,0,sizeof(diagnostic->samples));
    memset(diagnostic->queue_samples,0,sizeof(diagnostic->queue_samples));
    memset(diagnostic->owner_records,0,sizeof(diagnostic->owner_records));
    diagnostic->owner_count=0; diagnostic->owner_overflow=0; diagnostic->owner_tls_failures=0;
    diagnostic->batch_tls_failures=0;
    memset(diagnostic->owner_window_records,0,sizeof(diagnostic->owner_window_records));
    memset(diagnostic->owner_window_overflow,0,sizeof(diagnostic->owner_window_overflow));
    memset(diagnostic->owner_window_probes,0,sizeof(diagnostic->owner_window_probes));
    memset(diagnostic->owner_window_unmatched,0,sizeof(diagnostic->owner_window_unmatched));
    memset(diagnostic->owner_window_unreadable,0,sizeof(diagnostic->owner_window_unreadable));
    memset(diagnostic->owner_window_tick,0,sizeof(diagnostic->owner_window_tick));
    diagnostic->resolution_enabled=validate_vehicle_resolution_sites();
    diagnostic->handle_link_enabled=validate_vehicle_handle_link_sites();
    diagnostic->owner_focused=diagnostic->owner_enabled && diagnostic->resolution_enabled && diagnostic->handle_link_enabled;
    diagnostic->started_tick=GetTickCount();
    InterlockedExchange(&diagnostic->sample_count,0);
    InterlockedExchange(&diagnostic->queue_sample_count,0);
    InterlockedIncrement(&diagnostic->serial);
    InterlockedExchange(&diagnostic->state,2);
    vehicle_capture_unlock();
    append_log("STAGE_VEHICLE_CAPTURE_ARMED serial=%ld samples=%u intervalMs=%u windowMs=%u key=F10 mutation=0",
        (long)diagnostic->serial,VEHICLE_CAPTURE_SAMPLES,
        VEHICLE_CAPTURE_INTERVAL_MS,
        (VEHICLE_CAPTURE_SAMPLES-1U)*VEHICLE_CAPTURE_INTERVAL_MS);
    append_log("STAGE_VEHICLE_RESOLUTION_ARMED serial=%ld enabled=%ld preflight=readOnly metadataOnly=1 mutation=0",
        (long)diagnostic->serial,(long)diagnostic->resolution_enabled);
}

static void poll_vehicle_candidate_capture_key(void)
{
    VehicleLightDiagnosticState *diagnostic=&g_shadow_engine.vehicle_diagnostics;
    LONG state;
    DWORD now;
    state=InterlockedCompareExchange(&diagnostic->state,0,0);
    if(state==2) {
        now=GetTickCount();
        if(vehicle_capture_lock()) {
          vehicle_capture_close_if_complete();
          if(diagnostic->state==2 && now-diagnostic->started_tick>=VEHICLE_CAPTURE_TIMEOUT_MS) {
            append_log("STAGE_VEHICLE_CAPTURE_TIMEOUT serial=%ld capturedSamples=%ld timeoutMs=%u action=flushPartial",
                (long)diagnostic->serial,(long)diagnostic->sample_count,
                VEHICLE_CAPTURE_TIMEOUT_MS);
            InterlockedCompareExchange(&diagnostic->state,3,2);
          }
          vehicle_capture_unlock();
        }
    }
    schedule_vehicle_candidate_capture_flush();
}

static int lock_vehicle_selection_snapshot(VehicleSelectionSnapshot *snapshot)
{
    VehicleLightDiagnosticState *d=&g_shadow_engine.vehicle_diagnostics;
    /* Records beyond count are never read; do not clear the entire bank copy. */
    memset(snapshot,0,offsetof(VehicleSelectionSnapshot,records));
    if(!d->selection_enabled) return 0;
    if(!vehicle_selection_lock()) {
        InterlockedIncrement(&d->selection_gaps);
        snapshot->gaps=InterlockedCompareExchange(&d->selection_gaps,0,0);
        return 0;
    }
    snapshot->lock_acquired=1U;
    snapshot->gaps=InterlockedCompareExchange(&d->selection_gaps,0,0);
    snapshot->overflow=InterlockedCompareExchange(&d->selection_overflow,0,0);
    snapshot->rotations=InterlockedCompareExchange(&d->selection_rotations,0,0);
    return 1;
}

static void vehicle_selection_bank_health(VehicleSelectionSnapshot *snapshot,
    uint32_t bank,LONG renderer_epoch)
{
    VehicleLightDiagnosticState *d=&g_shadow_engine.vehicle_diagnostics;
    LONG active_epoch=d->selection_bank_epoch[bank];
    if(active_epoch>renderer_epoch || renderer_epoch-active_epoch<=3) {
        if(InterlockedCompareExchange(&d->selection_gap_epoch_tag[bank],0,0)==active_epoch+1)
            ++snapshot->gap_epochs;
        if(InterlockedCompareExchange(&d->selection_overflow_epoch_tag[bank],0,0)==active_epoch+1)
            ++snapshot->overflow_epochs;
    }
}

static void unlock_vehicle_selection_snapshot(VehicleSelectionSnapshot *snapshot)
{
    vehicle_selection_unlock();
    snapshot->publisher_overlap=InterlockedCompareExchange(
        &g_shadow_engine.vehicle_diagnostics.selection_publish_inflight,0,0)!=0;
}

static void copy_vehicle_selection_records_locked(VehicleSelectionSnapshot *snapshot,
    LONG renderer_epoch)
{
    VehicleLightDiagnosticState *d=&g_shadow_engine.vehicle_diagnostics;
    uint32_t bank,slot;
    snapshot->count=snapshot->same_epoch=snapshot->previous_epoch=snapshot->older_epoch=0;
    for(bank=0;bank<VEHICLE_SELECTION_EPOCH_BANKS;++bank) {
        LONG active_epoch=d->selection_bank_epoch[bank];
        for(slot=0;slot<VEHICLE_SELECTION_SLOTS_PER_EPOCH;++slot) {
            const VehicleSelectionRecord *record=&d->selection_records[bank][slot];
            if(!record->observations || record->publish_renderer_epoch!=active_epoch)
                continue;
            if(snapshot->count<VEHICLE_SELECTION_SNAPSHOT_RECORDS)
                snapshot->records[snapshot->count++]=*record;
            if(active_epoch==renderer_epoch) ++snapshot->same_epoch;
            else if(active_epoch==renderer_epoch-1) ++snapshot->previous_epoch;
            else ++snapshot->older_epoch;
        }
    }
}

static void snapshot_vehicle_selection(VehicleSelectionSnapshot *snapshot,
    LONG renderer_epoch)
{
    uint32_t bank;
    if(!lock_vehicle_selection_snapshot(snapshot)) return;
    for(bank=0;bank<VEHICLE_SELECTION_EPOCH_BANKS;++bank)
        vehicle_selection_bank_health(snapshot,bank,renderer_epoch);
    copy_vehicle_selection_records_locked(snapshot,renderer_epoch);
    unlock_vehicle_selection_snapshot(snapshot);
}

/* Candidate native fields have already been copied outside the lock. Capture
 * only patch-owned scalar matches, never borrow a registry pointer on return.
 * Old active banks still participate: their conflicting owners must fail open
 * before the classifier checks freshness. Detailed captures keep the full path. */
static void snapshot_vehicle_selection_projection(VehicleSelectionSnapshot *summary,
    VehicleSelectionProjection *projection,const VehicleCandidateSample *sample)
{
    VehicleLightDiagnosticState *d=&g_shadow_engine.vehicle_diagnostics;
    uint32_t bank,i,visits=0;
    projection->fallback_full_snapshot=0;
    memset(projection->matches,0,sample->copied_candidates*sizeof(projection->matches[0]));
    if(!lock_vehicle_selection_snapshot(summary)) return;
    for(bank=0;bank<VEHICLE_SELECTION_EPOCH_BANKS;++bank) {
        LONG epoch=d->selection_bank_epoch[bank];
        uint32_t count=d->selection_bank_count[bank];
        vehicle_selection_bank_health(summary,bank,sample->renderer_epoch);
        summary->count+=count;
        if(epoch==sample->renderer_epoch) summary->same_epoch+=count;
        else if(epoch==sample->renderer_epoch-1) summary->previous_epoch+=count;
        else summary->older_epoch+=count;
    }
    for(i=0;i<sample->copied_candidates;++i) {
        const VehicleCandidateRecord *candidate=&sample->records[i];
        VehicleSelectionRecord *match=&projection->records[i];
        uint32_t bucket;
        if(candidate->renderer_type!=3 || !candidate->descriptor_handle_valid) continue;
        bucket=vehicle_selection_hash(candidate->descriptor_handle,0U)&
            (VEHICLE_SELECTION_SLOTS_PER_EPOCH-1U);
        for(bank=0;bank<VEHICLE_SELECTION_EPOCH_BANKS;++bank) {
            uint32_t link;
            for(link=d->selection_heads[bank][bucket];link;
                link=d->selection_next[bank][link-1U]) {
                const VehicleSelectionRecord *record=&d->selection_records[bank][link-1U];
                /* Hostile collisions/duplicates must not multiply lock hold by
                 * candidate count. Finish through the original outside-lock
                 * matcher after at most one bank set's worth of lookup visits. */
                if(visits++==VEHICLE_SELECTION_SNAPSHOT_RECORDS) {
                    projection->fallback_full_snapshot=1U;
                    copy_vehicle_selection_records_locked(summary,sample->renderer_epoch);
                    unlock_vehicle_selection_snapshot(summary);
                    return;
                }
                if(record->descriptor_handle!=candidate->descriptor_handle) continue;
                if(!projection->matches[i]) {
                    *match=*record; projection->matches[i]=1U; continue;
                }
                if(record->vehicle!=match->vehicle) {
                    projection->matches[i]=2U; continue;
                }
                {
                    uint64_t entity_id=match->vehicle_entity_id;
                    if(record->publish_renderer_epoch>match->publish_renderer_epoch ||
                       (record->publish_renderer_epoch==match->publish_renderer_epoch &&
                        record->tick>match->tick)) *match=*record;
                    if(entity_id!=record->vehicle_entity_id) match->vehicle_entity_id=UINT64_MAX;
                }
            }
        }
    }
    unlock_vehicle_selection_snapshot(summary);
}

/* Snapshot-owned index; no engine pointer, persistent identity or extra lock.
 * Reverse insertion preserves the linear scanner's order, including ties and
 * conflicting owners. Hash collisions always retain full-handle comparison. */
typedef struct VehicleSelectionIndex {
    uint32_t heads[VEHICLE_SELECTION_SLOTS_PER_EPOCH];
    uint32_t next[VEHICLE_SELECTION_SNAPSHOT_RECORDS];
} VehicleSelectionIndex;

static void index_vehicle_selection(const VehicleSelectionSnapshot *snapshot,
    VehicleSelectionIndex *index)
{
    uint32_t i=snapshot->count;
    memset(index->heads,0xFF,sizeof(index->heads));
    while(i) {
        uint32_t bucket;
        --i;
        bucket=vehicle_selection_hash(snapshot->records[i].descriptor_handle,0U)&
            (VEHICLE_SELECTION_SLOTS_PER_EPOCH-1U);
        index->next[i]=index->heads[bucket];
        index->heads[bucket]=i;
    }
}

static const VehicleSelectionRecord *match_vehicle_selection(
    const VehicleSelectionSnapshot *snapshot,const VehicleSelectionIndex *index,
    uint64_t handle,uint32_t *matches)
{
    const VehicleSelectionRecord *match=NULL;
    uint32_t bucket=vehicle_selection_hash(handle,0U)&
        (VEHICLE_SELECTION_SLOTS_PER_EPOCH-1U);
    uint32_t j;
    *matches=0;
    for(j=index->heads[bucket];j!=UINT32_MAX;j=index->next[j]) {
        const VehicleSelectionRecord *record=&snapshot->records[j];
        if(record->descriptor_handle!=handle) continue;
        if(!match) { match=record; *matches=1U; continue; }
        if(record->vehicle!=match->vehicle) { *matches=2U; continue; }
        if(record->publish_renderer_epoch>match->publish_renderer_epoch ||
           (record->publish_renderer_epoch==match->publish_renderer_epoch &&
            record->tick>match->tick)) match=record;
    }
    return match;
}

/* Manager-call deltas remain comparison telemetry. The full generation-aware
 * handle authorizes matches within the four proven renderer epochs. All bulbs
 * from one complete vehicle group share one rank. */
/* Per-call full-owner index. No owner lifetime is inferred from a bucket. */
#define VEHICLE_GROUP_BUCKETS (VEHICLE_SELECTION_MAX_VEHICLES*2U)
static uint32_t vehicle_group_index(uint32_t *buckets,const uint64_t *owners,
    uint32_t count,uint64_t owner)
{
    uint32_t slot=vehicle_selection_hash(owner,0U)&(VEHICLE_GROUP_BUCKETS-1U),probe;
    for(probe=0;probe<VEHICLE_GROUP_BUCKETS;++probe) {
        uint32_t index=buckets[slot];
        if(index==UINT32_MAX) {
            if(count<VEHICLE_SELECTION_MAX_VEHICLES) buckets[slot]=count;
            return count;
        }
        if(owners[index]==owner) return index;
        slot=(slot+1U)&(VEHICLE_GROUP_BUCKETS-1U);
    }
    return count;
}

/* Only the driver's potential match needs this extra full-snapshot proof.
 * Ordinary preparation has already aggregated it in the projection. */
static uint64_t vehicle_selection_full_entity_id(const VehicleSelectionSnapshot *snapshot,
    const VehicleSelectionIndex *index,uint64_t handle)
{
    uint32_t bucket=vehicle_selection_hash(handle,0U)&(VEHICLE_SELECTION_SLOTS_PER_EPOCH-1U);
    uint32_t j;
    uint64_t entity_id=UINT64_MAX;
    int found=0;
    for(j=index->heads[bucket];j!=UINT32_MAX;j=index->next[j]) {
        const VehicleSelectionRecord *record=&snapshot->records[j];
        if(record->descriptor_handle!=handle) continue;
        if(!found) { entity_id=record->vehicle_entity_id; found=1; }
        else if(entity_id!=record->vehicle_entity_id) return UINT64_MAX;
    }
    return entity_id;
}

static int vehicle_selection_is_driver(const VehicleCandidateSample *sample,
    const VehicleCandidateRecord *candidate)
{
    return sample->driver_valid && candidate->selection_entity_id!=UINT64_MAX &&
        candidate->selection_vehicle==sample->driver_owner &&
        candidate->selection_entity_id==sample->driver_entity_id;
}

static void classify_vehicle_selection_projected_fields(VehicleCandidateSample *sample,
    const VehicleSelectionSnapshot *snapshot,const VehicleSelectionProjection *projection,
    int detailed)
{
    VehicleSelectionIndex selection_index;
    uint32_t candidate_heads[VEHICLE_SELECTION_SLOTS_PER_EPOCH];
    uint32_t candidate_next[VEHICLE_CAPTURE_MAX_CANDIDATES];
    uint64_t vehicles[VEHICLE_SELECTION_MAX_VEHICLES];
    uint32_t group_buckets[VEHICLE_GROUP_BUCKETS];
    uint64_t exact_vehicles[VEHICLE_SELECTION_MAX_VEHICLES];
    uint64_t registry_vehicles[VEHICLE_SELECTION_MAX_VEHICLES];
    float distances[VEHICLE_SELECTION_MAX_VEHICLES];
    uint32_t vehicle_lights[VEHICLE_SELECTION_MAX_VEHICLES];
    uint32_t exact_vehicle_lights[VEHICLE_SELECTION_MAX_VEHICLES];
    uint32_t order[VEHICLE_SELECTION_MAX_VEHICLES];
    uint32_t rank_by_group[VEHICLE_SELECTION_MAX_VEHICLES];
    DWORD now=sample->tick;
    uint32_t i,j,k,vehicle_count=0,complete_count=0,exact_vehicle_count=0;
    uint32_t registry_vehicle_count=0;
    float player_position[3];
    DriverIdentity driver;
    int player_position_valid=copy_local_player_position(player_position);
#if SHADOW_ENGINE_INTERNAL_DIAGNOSTICS
    if(InterlockedCompareExchange(&g_shadow_engine.vehicle_diagnostics.state,0,0)==2)
        sample->driver_valid=copy_policy_driver_identity(&driver,&sample->driver_trace)!=0;
    else
#endif
    sample->driver_valid=copy_policy_driver_identity(&driver,NULL)!=0;
    sample->driver_headlight_lights=0;
    sample->driver_owner=sample->driver_entity_id=sample->driver_player_id=0;
    if(sample->driver_valid) {
        sample->driver_owner=driver.owner;
        sample->driver_entity_id=driver.entity_id;
        sample->driver_player_id=driver.player_id;
    }
    sample->player_anchor_valid=player_position_valid;
    memset(group_buckets,0xFF,sizeof(group_buckets));
    /* A projection supplies matches by candidate index, not snapshot records.
     * Only the full snapshot supports detailed registry/type1 diagnostics. */
    if(projection) {
        detailed=0;
        if(projection->fallback_full_snapshot) projection=NULL;
    }
    if(!projection) index_vehicle_selection(snapshot,&selection_index);
    /* Per-call numeric index, never retained across native pointer lifetimes. */
    memset(candidate_heads,0xFF,sizeof(candidate_heads));
    for(i=0;i<sample->copied_candidates;++i) {
        uint32_t bucket=vehicle_selection_hash(sample->records[i].descriptor_handle,0U)&
            (VEHICLE_SELECTION_SLOTS_PER_EPOCH-1U);
        candidate_next[i]=candidate_heads[bucket]; candidate_heads[bucket]=i;
    }
    sample->selection_main_limit=shadow_policy_stable_limit();
    sample->selection_extra_slots=vehicle_selection_extra_slots();
    sample->selection_snapshot_records=snapshot->count;
    sample->selection_snapshot_gaps=snapshot->gaps;
    sample->selection_snapshot_overflow=snapshot->overflow;
    sample->selection_snapshot_rotations=snapshot->rotations;
    sample->selection_snapshot_same_epoch=snapshot->same_epoch;
    sample->selection_snapshot_previous_epoch=snapshot->previous_epoch;
    sample->selection_snapshot_older_epoch=snapshot->older_epoch;
    for(i=0;detailed && i<snapshot->count;++i) {
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
        for(j=candidate_heads[vehicle_selection_hash(candidate->descriptor_handle,0U)&
                (VEHICLE_SELECTION_SLOTS_PER_EPOCH-1U)];j!=UINT32_MAX;j=candidate_next[j])
            if(sample->records[j].renderer_type==3 &&
               sample->records[j].descriptor_handle_valid &&
               sample->records[j].descriptor_handle==candidate->descriptor_handle)
                ++candidate->selection_candidate_instances;
        if(projection) {
            matches=projection->matches[i];
            if(matches) match=&projection->records[i];
        } else match=match_vehicle_selection(snapshot,&selection_index,
            candidate->descriptor_handle,&matches);
        candidate->selection_matches=matches;
        if(matches!=1U || candidate->selection_candidate_instances!=1U) {
            if(matches>1U || candidate->selection_candidate_instances>1U)
                ++sample->selection_ambiguous;
            else ++sample->selection_unknown;
            continue;
        }
        candidate->selection_vehicle=match->vehicle;
        candidate->selection_entity_id=match->vehicle_entity_id;
        if(!projection && sample->driver_valid && match->vehicle==sample->driver_owner)
            candidate->selection_entity_id=vehicle_selection_full_entity_id(snapshot,&selection_index,
                candidate->descriptor_handle);
        candidate->selection_publish_manager_call=match->publish_manager_call;
        candidate->selection_publish_renderer_epoch=match->publish_renderer_epoch;
        candidate->selection_age_ms=now-match->tick;
        candidate->selection_distance_squared=match->distance_squared;
        if(g_shadow_engine.vehicle_diagnostics.player_root_address &&
           !vehicle_selection_is_driver(sample,candidate)) {
            float x,y,z;
            if(!player_position_valid || !match->position_valid) {
                /* Do not suppress using a camera-distance fallback in a player-ranked call. */
                ++sample->selection_unknown; continue;
            }
            x=match->position[0]-player_position[0]; y=match->position[1]-player_position[1];
            z=match->position[2]-player_position[2];
            candidate->selection_distance_squared=x*x+y*y+z*z;
        }
        ++sample->selection_exact_matches;
        if(detailed) {
        for(j=0;j<exact_vehicle_count;++j)
            if(exact_vehicles[j]==match->vehicle) break;
        if(j==exact_vehicle_count) {
            if(exact_vehicle_count<VEHICLE_SELECTION_MAX_VEHICLES) {
                exact_vehicles[j]=match->vehicle;
                exact_vehicle_lights[j]=1U;
                ++exact_vehicle_count;
            }
        } else ++exact_vehicle_lights[j];
        }
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
        if(vehicle_selection_is_driver(sample,candidate)) {
            candidate->selection_driver=1U;
            candidate->selection_would_keep=1U;
            candidate->selection_role=VEHICLE_SELECTION_DRIVER_ROLE;
            candidate->policy_protected=1U;
            ++sample->driver_headlight_lights;
            ++sample->selection_would_keep_lights;
            continue;
        }
        j=vehicle_group_index(group_buckets,vehicles,vehicle_count,match->vehicle);
        if(j==vehicle_count) {
            if(vehicle_count==VEHICLE_SELECTION_MAX_VEHICLES) continue;
            vehicles[j]=match->vehicle;
            distances[j]=candidate->selection_distance_squared;
            vehicle_lights[j]=1U;
            ++vehicle_count;
        } else {
            ++vehicle_lights[j];
            if(candidate->selection_distance_squared<distances[j]) distances[j]=candidate->selection_distance_squared;
        }
    }
    sample->selection_exact_vehicles=exact_vehicle_count;
    for(i=0;i<exact_vehicle_count;++i)
        if(exact_vehicle_lights[i]>=2U) ++sample->selection_exact_complete_vehicles;
    for(i=0;i<vehicle_count;++i) {
        rank_by_group[i]=0U;
        if(vehicle_lights[i]>=2U) order[complete_count++]=i;
        else ++sample->selection_partial_vehicles;
    }
    for(i=0;i<complete_count;++i)
        for(j=i+1U;j<complete_count;++j)
            if(distances[order[j]]<distances[order[i]]) {
                k=order[i]; order[i]=order[j]; order[j]=k;
            }
    /* Reuse the original group index and exact sorted order. Partial groups
     * have rank zero; saturated owners remain absent. All indices die here. */
    for(i=0;i<complete_count;++i) rank_by_group[order[i]]=i+1U;
    for(i=0;i<sample->copied_candidates;++i) {
        VehicleCandidateRecord *candidate=&sample->records[i];
        if(!candidate->selection_renderer_recent || candidate->selection_driver) continue;
        j=vehicle_group_index(group_buckets,vehicles,vehicle_count,candidate->selection_vehicle);
        if(j==vehicle_count) continue;
        candidate->selection_group_lights=vehicle_lights[j];
        candidate->selection_group_complete=vehicle_lights[j]>=2U;
        if(!candidate->selection_group_complete) continue;
        candidate->selection_rank=rank_by_group[j];
        candidate->selection_would_keep=rank_by_group[j]<=
            sample->selection_main_limit+sample->selection_extra_slots;
        if(candidate->selection_would_keep) ++sample->selection_would_keep_lights;
    }
    /* Type-1 alone is not a siren classifier: world controls also use it.
     * This extra pass is diagnostic-only and therefore runs only while an F10
     * capture is active; normal calls pay only this capture-state read.
     * Report only unique, recent type-1 handles published by the proven native
     * vehicle batch, and retain the siren-like role as an observational label. */
    if(!detailed || InterlockedCompareExchange(
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
        candidate->selection_entity_id=match->vehicle_entity_id;
        if(sample->driver_valid && match->vehicle==sample->driver_owner)
            candidate->selection_entity_id=vehicle_selection_full_entity_id(snapshot,&selection_index,
                candidate->descriptor_handle);
        if(vehicle_selection_is_driver(sample,candidate)) {
            candidate->selection_driver=1U;
            candidate->selection_role=VEHICLE_SELECTION_DRIVER_ROLE;
        }
    }
    sample->selection_vehicles=complete_count;
}

static void classify_vehicle_selection_fields(VehicleCandidateSample *sample,
    const VehicleSelectionSnapshot *snapshot,int detailed)
{
    classify_vehicle_selection_projected_fields(sample,snapshot,NULL,detailed);
}

static void classify_vehicle_selection_sample(VehicleCandidateSample *sample,
    const VehicleSelectionSnapshot *snapshot)
{
    classify_vehicle_selection_fields(sample,snapshot,1);
}

static void update_vehicle_limiter_peak(volatile LONG *peak,uint32_t value)
{
    LONG seen=InterlockedCompareExchange(peak,0,0);
    while((uint32_t)seen<value) {
        LONG prior=InterlockedCompareExchange(peak,(LONG)value,seen);
        if(prior==seen) break;
        seen=prior;
    }
}

typedef struct VehicleResidencyCandidate {
    uint64_t vehicle,handle;
    uint64_t vehicle_entity_id;
    float distance_squared;
    float priority_distance;
    float projected_distance;
} VehicleResidencyCandidate;

/* Patch-owned scalar prediction; collisions discard history, never identity
 * checks. No resource or native object is retained by this table. */
static float vehicle_approach_distance(VehicleLightDiagnosticState *d,
    const VehicleResidencyCandidate *candidate,DWORD now)
{
    uint32_t index=(uint32_t)((candidate->vehicle>>4)^candidate->handle)&255U;
    VehicleApproachRecord *history=&d->approach[index];
    float distance=(float)sqrt(candidate->distance_squared),speed=0.0f;
    DWORD elapsed=now-history->tick;
    if(history->vehicle==candidate->vehicle && history->handle==candidate->handle &&
       elapsed<=250U) {
        speed=history->speed;
        if(elapsed>=32U) {
            speed=(distance-history->distance)*1000.0f/(float)elapsed;
            history->tick=now; history->distance=distance;
            history->speed=speed;
        }
    } else {
        history->vehicle=candidate->vehicle; history->handle=candidate->handle;
        history->tick=now; history->distance=distance; history->speed=0.0f;
    }
    /* A 200ms look-ahead, capped at eight world units, cannot hold an owner. */
    if(speed<0.0f) {
        float closing=-speed*0.2f;
        if(closing>8.0f) closing=8.0f;
        distance-=closing;
    }
    return distance>0.0f ? distance:0.0f;
}

static int vehicle_residency_lock(void)
{
    return InterlockedCompareExchange(
        &g_shadow_engine.vehicle_diagnostics.residency_lock,1,0)==0;
}

static void vehicle_residency_unlock(void)
{
    InterlockedExchange(&g_shadow_engine.vehicle_diagnostics.residency_lock,0);
}

static int residency_contains_vehicle(const VehicleLightDiagnosticState *d,
    uint64_t vehicle)
{
    uint32_t slot;
    for(slot=0;slot<(d->residency_stable_count+d->residency_extra_slots);++slot)
        if(d->residency_slots[slot].vehicle==vehicle) return 1;
    return 0;
}

static int residency_contains_stable_vehicle(const VehicleLightDiagnosticState *d,
    uint64_t vehicle)
{
    uint32_t slot;
    if(!vehicle) return 0;
    for(slot=0;slot<d->residency_stable_count;++slot)
        if(d->residency_slots[slot].vehicle==vehicle) return 1;
    return 0;
}

/* Owner selection only: these tokens never own or cache shadow resources.
 * Primary slots follow distance with a small incumbent margin. The final two
 * slots prefer actual-near owners, then outgoing retention, then forecasts. */
static int apply_vehicle_residency_policy(VehicleLimiterChain *chain)
{
    VehicleLightDiagnosticState *d=&g_shadow_engine.vehicle_diagnostics;
    VehicleCandidateSample *sample=&chain->sample;
    VehicleResidencyCandidate candidates[VEHICLE_SELECTION_MAX_VEHICLES];
    uint64_t group_owners[VEHICLE_SELECTION_MAX_VEHICLES];
    uint32_t group_buckets[VEHICLE_GROUP_BUCKETS];
    VehicleResidentSlot next[VEHICLE_SELECTION_MAX_RESIDENTS];
    VehicleResidentSlot outgoing[VEHICLE_SELECTION_MAX_RESIDENTS];
    uint32_t candidate_count=0,outgoing_count=0,i,j,slot;
    uint32_t resident_count=0,prior_count=0,stable_limit,extra_slots;
    LONG reset_request;
    DWORD now=sample->tick;
    int initialized=0,replaced=0,pending=0;
    if(!vehicle_residency_lock()) {
        InterlockedIncrement(&d->residency_lock_bypass);
        return 0;
    }
    stable_limit=shadow_policy_stable_limit();
    extra_slots=vehicle_selection_extra_slots();
    sample->selection_main_limit=stable_limit;
    sample->selection_extra_slots=extra_slots;
    reset_request=InterlockedExchange(&d->residency_reset_requested,0);
    if((reset_request&~VEHICLE_RESIDENCY_RESET_EXTRAS) ||
       d->residency_stable_count!=stable_limit) {
        memset(d->residency_slots,0,sizeof(d->residency_slots));
        memset(d->approach,0,sizeof(d->approach));
        d->residency_stable_count=stable_limit;
        d->residency_extra_slots=extra_slots;
        d->freeze_active=0; d->freeze_calls=0;
    } else if(d->residency_extra_slots!=extra_slots) {
        /* A degraded publication cannot rebuild primary identities. Disabling
         * extras must only remove the tail, preserving those primary owners. */
        for(slot=stable_limit;slot<VEHICLE_SELECTION_MAX_RESIDENTS;++slot)
            memset(&d->residency_slots[slot],0,sizeof(d->residency_slots[slot]));
        d->residency_extra_slots=extra_slots;
    }
    /* Driving is independent proof that this exact former traffic owner must
     * leave our slots, even while unrelated publication evidence is degraded. */
    if(sample->driver_valid && sample->driver_entity_id!=UINT64_MAX)
        for(slot=0;slot<VEHICLE_SELECTION_MAX_RESIDENTS;++slot)
            if(d->residency_slots[slot].vehicle==sample->driver_owner &&
               d->residency_slots[slot].vehicle_entity_id==sample->driver_entity_id)
                memset(&d->residency_slots[slot],0,sizeof(d->residency_slots[slot]));
#if SHADOW_ENGINE_INTERNAL_DIAGNOSTICS
    if(chain->degraded) {
        if(!d->freeze_active) { d->freeze_started=now; d->freeze_active=1; }
        ++d->freeze_calls;
        if(now-d->freeze_started>d->freeze_peak_ms) d->freeze_peak_ms=now-d->freeze_started;
        if(d->freeze_calls>d->freeze_peak_calls) d->freeze_peak_calls=d->freeze_calls;
        sample->freeze_ms=now-d->freeze_started;
        sample->freeze_calls=d->freeze_calls;
    } else { d->freeze_active=0; d->freeze_calls=0; }
    sample->freeze_peak_ms=d->freeze_peak_ms;
    sample->freeze_peak_calls=d->freeze_peak_calls;
#endif
    for(slot=0;slot<stable_limit+extra_slots;++slot)
        if(d->residency_slots[slot].vehicle) ++prior_count;
    /* Incomplete evidence cannot authorize changing owner identities. */
    if(!chain->degraded) {
        memset(group_buckets,0xFF,sizeof(group_buckets));
        for(i=0;i<sample->copied_candidates;++i) {
            const VehicleCandidateRecord *record=&sample->records[i];
            if(record->renderer_type!=3 || record->selection_driver || !record->selection_group_complete ||
               !record->selection_renderer_recent) continue;
            j=vehicle_group_index(group_buckets,group_owners,candidate_count,record->selection_vehicle);
            if(j==candidate_count) {
                if(candidate_count==VEHICLE_SELECTION_MAX_VEHICLES) continue;
                candidates[j].vehicle=record->selection_vehicle;
                candidates[j].vehicle_entity_id=record->selection_entity_id;
                group_owners[j]=record->selection_vehicle;
                candidates[j].handle=record->descriptor_handle;
                candidates[j].distance_squared=record->selection_distance_squared;
                ++candidate_count;
            } else if(record->selection_distance_squared<candidates[j].distance_squared) {
                candidates[j].distance_squared=record->selection_distance_squared;
            }
            if(j<candidate_count && record->descriptor_handle<candidates[j].handle)
                candidates[j].handle=record->descriptor_handle;
            if(j<candidate_count && candidates[j].vehicle_entity_id!=record->selection_entity_id)
                candidates[j].vehicle_entity_id=UINT64_MAX;
        }
        for(i=0;i<candidate_count;++i) {
            candidates[i].priority_distance=candidates[i].distance_squared;
            candidates[i].projected_distance=sample->player_anchor_valid ?
                vehicle_approach_distance(d,&candidates[i],now):0.0f;
            if(residency_contains_stable_vehicle(d,candidates[i].vehicle))
                candidates[i].priority_distance*=
                    (float)VEHICLE_RESIDENCY_DISTANCE_NUMERATOR/
                    (float)VEHICLE_RESIDENCY_DISTANCE_DENOMINATOR;
        }
        for(i=0;i<candidate_count;++i)
            for(j=i+1U;j<candidate_count;++j)
                if(candidates[j].priority_distance<candidates[i].priority_distance ||
                   (candidates[j].priority_distance==candidates[i].priority_distance &&
                    candidates[j].vehicle<candidates[i].vehicle)) {
                    VehicleResidencyCandidate swap=candidates[i];
                    candidates[i]=candidates[j]; candidates[j]=swap;
                }
        memset(next,0,sizeof(next));
        /* Admission has no residency timer. Old tails may return to primary. */
        for(i=0;i<candidate_count && i<stable_limit;++i) {
            next[i].vehicle=candidates[i].vehicle;
            next[i].vehicle_entity_id=candidates[i].vehicle_entity_id;
            next[i].role=1U;
            next[i].distance_squared=candidates[i].distance_squared;
            next[i].admitted_tick=next[i].last_seen_tick=now;
            if(!residency_contains_stable_vehicle(d,next[i].vehicle) && prior_count)
                replaced=1;
        }
        for(slot=0;slot<stable_limit+extra_slots;++slot) {
            VehicleResidentSlot old=d->residency_slots[slot];
            if(!old.vehicle) continue;
            for(i=0;i<stable_limit;++i)
                if(next[i].vehicle==old.vehicle) break;
            if(i<stable_limit) continue; /* No primary/tail duplicates. */
            for(i=0;i<candidate_count;++i)
                if(candidates[i].vehicle==old.vehicle) break;
            if(i==candidate_count) continue; /* Missing group cannot be retained. */
            if(slot<stable_limit) old.admitted_tick=now; /* Start outgoing grace. */
            else if(now-old.admitted_tick>=VEHICLE_TRANSITION_HOLD_MS) continue;
            old.distance_squared=candidates[i].distance_squared;
            old.last_seen_tick=now;
            outgoing[outgoing_count++]=old;
            outgoing[outgoing_count-1U].role=3U;
        }
        /* Capacity pressure keeps the newest departures. Ties prefer the
         * nearest outgoing owner, then identity for deterministic selection. */
        for(i=0;i<outgoing_count;++i)
            for(j=i+1U;j<outgoing_count;++j) {
                DWORD age_i=now-outgoing[i].admitted_tick;
                DWORD age_j=now-outgoing[j].admitted_tick;
                if(age_j<age_i || (age_j==age_i &&
                   (outgoing[j].distance_squared<outgoing[i].distance_squared ||
                    (outgoing[j].distance_squared==outgoing[i].distance_squared &&
                     outgoing[j].vehicle<outgoing[i].vehicle)))) {
                    VehicleResidentSlot swap=outgoing[i];
                    outgoing[i]=outgoing[j]; outgoing[j]=swap;
                }
            }
        slot=stable_limit;
        if(sample->player_anchor_valid && candidate_count>stable_limit) {
            /* Actual nearby distance wins; forecasts cannot evict a closer
             * near-field extra. Primary ranking is unchanged. */
            for(i=stable_limit;i<candidate_count;++i)
                for(j=i+1U;j<candidate_count;++j)
                    if(candidates[j].distance_squared<candidates[i].distance_squared ||
                       (candidates[j].distance_squared==candidates[i].distance_squared &&
                        candidates[j].vehicle<candidates[i].vehicle)) {
                        VehicleResidencyCandidate swap=candidates[i];
                        candidates[i]=candidates[j]; candidates[j]=swap;
                    }
            for(i=stable_limit;i<candidate_count &&
                slot<stable_limit+extra_slots;++i) {
                if(candidates[i].distance_squared>64.0f) continue;
                next[slot].vehicle=candidates[i].vehicle;
                next[slot].vehicle_entity_id=candidates[i].vehicle_entity_id;
                next[slot].role=2U;
                next[slot].distance_squared=candidates[i].distance_squared;
                next[slot].admitted_tick=next[slot].last_seen_tick=now;
                ++slot;
            }
        }
        for(i=0;i<outgoing_count &&
            slot<stable_limit+extra_slots;++i) {
            for(j=stable_limit;j<slot;++j)
                if(next[j].vehicle==outgoing[i].vehicle) break;
            if(j<slot) continue;
            next[slot++]=outgoing[i];
        }
        /* Speculation uses only spare capacity after near-field and outgoing
         * protection. No native result/resource is prepared ahead of admission. */
        if(sample->player_anchor_valid && candidate_count>stable_limit &&
           slot<stable_limit+extra_slots) {
            float frontier=(float)sqrt(candidates[stable_limit-1U].distance_squared);
            for(i=stable_limit;i<candidate_count;++i)
                for(j=i+1U;j<candidate_count;++j)
                    if(candidates[j].projected_distance<candidates[i].projected_distance) {
                        VehicleResidencyCandidate swap=candidates[i];
                        candidates[i]=candidates[j]; candidates[j]=swap;
                    }
            for(i=stable_limit;i<candidate_count &&
                slot<stable_limit+extra_slots;++i) {
                if(candidates[i].projected_distance>=frontier ||
                   candidates[i].projected_distance*candidates[i].projected_distance>=
                   candidates[i].distance_squared) continue;
                for(j=stable_limit;j<slot;++j)
                    if(next[j].vehicle==candidates[i].vehicle) break;
                if(j<slot) continue;
                next[slot].vehicle=candidates[i].vehicle;
                next[slot].vehicle_entity_id=candidates[i].vehicle_entity_id;
                next[slot].role=4U;
                next[slot].distance_squared=candidates[i].distance_squared;
                next[slot].admitted_tick=next[slot].last_seen_tick=now;
                ++slot;
            }
        }
        for(slot=0;slot<stable_limit+extra_slots;++slot) {
            if(!d->residency_slots[slot].vehicle) continue;
            for(i=0;i<stable_limit+extra_slots;++i)
                if(next[i].vehicle==d->residency_slots[slot].vehicle) break;
            if(i==stable_limit+extra_slots)
                InterlockedIncrement(&d->residency_dropped);
        }
        memcpy(d->residency_slots,next,sizeof(next));
    }
    for(slot=0;slot<stable_limit+extra_slots;++slot)
        if(d->residency_slots[slot].vehicle) {
            ++resident_count;
            if(slot>=stable_limit) pending=1;
        }
    if(!prior_count && resident_count) {
        initialized=1;
        InterlockedIncrement(&d->residency_initializations);
    }
    if(replaced) InterlockedIncrement(&d->residency_replacements);
    sample->selection_would_keep_lights=0;
    for(i=0;i<sample->copied_candidates;++i) {
        VehicleCandidateRecord *record=&sample->records[i];
        record->selection_would_keep=0;
        record->selection_role=0;
        if(record->selection_driver) {
            record->selection_would_keep=1U;
            record->selection_role=VEHICLE_SELECTION_DRIVER_ROLE;
        } else if(record->renderer_type==3 && record->selection_group_complete &&
           record->selection_renderer_recent)
            for(slot=0;slot<stable_limit+extra_slots;++slot)
                if(d->residency_slots[slot].vehicle==record->selection_vehicle) {
                    record->selection_would_keep=1;
                    record->selection_role=d->residency_slots[slot].role; break;
                }
        if(record->selection_would_keep) ++sample->selection_would_keep_lights;
    }
    chain->residency_applied=1U;
    chain->residency_initialized=initialized;
    chain->residency_replaced=replaced;
    chain->residency_pending=pending;
    chain->residency_residents=resident_count;
    InterlockedIncrement(&d->residency_applied_calls);
    if(pending) InterlockedIncrement(&d->residency_pending_holds);
    vehicle_residency_unlock();
    return 1;
}

/* Rebuild the native singly linked candidate list in place and restore it
 * after the synchronous manager call. Only exact, unique, complete nonresident
 * vehicle groups are removed. Every uncertain candidate remains fail-open. */
static void prepare_enabled_vehicle_limiter_chain(VehicleLimiterChain *chain,
    void *candidates,uint32_t candidate_count,LONG manager_call,int measure)
{
    VehicleLightDiagnosticState *d=&g_shadow_engine.vehicle_diagnostics;
    VehicleSelectionSnapshot snapshot;
    VehicleSelectionProjection projection;
    VehicleCopyRegionCache copy_cache;
    LARGE_INTEGER stage_start,stage_end;
    unsigned char *node=(unsigned char *)candidates,*head=NULL,*tail=NULL;
    uint32_t i,kept=0;
    if(!d->selection_enabled) {
        InterlockedIncrement(&d->limiter_fail_unavailable);
        goto fail_open;
    }
    if(!candidate_count || candidate_count>VEHICLE_CAPTURE_MAX_CANDIDATES) {
        InterlockedIncrement(&d->limiter_fail_candidate_chain);
        goto fail_open;
    }
    chain->sample.tick=GetTickCount();
    chain->sample.manager_call=manager_call;
    chain->sample.renderer_epoch=InterlockedCompareExchange(
        &g_shadow_engine.renderer.renderer_calls,0,0);
    chain->sample.reported_candidates=candidate_count;
    if(measure) QueryPerformanceCounter(&stage_start);
    copy_cache.count=copy_cache.uncached=copy_cache.queries=copy_cache.hits=0;
    for(i=0;i<candidate_count && node;++i) {
        VehicleCandidateRecord *record=&chain->sample.records[i];
        memset(record,0,sizeof(*record));
        chain->nodes[i]=node;
        if(!copy_vehicle_candidate_node(record,node,&copy_cache)) {
            InterlockedIncrement(&d->limiter_fail_candidate_chain);
            goto fail_open;
        }
        chain->original_next[i]=(void *)record->next;
        node=(unsigned char *)record->next;
        chain->sample.copied_candidates=i+1U;
    }
    if(i!=candidate_count || node!=NULL) {
        InterlockedIncrement(&d->limiter_fail_candidate_chain);
        goto fail_open;
    }
    chain->captured=candidate_count;
    copy_vehicle_candidate_payloads(chain->sample.records,candidate_count,&copy_cache);
    chain->validation_queries=copy_cache.queries;
    chain->validation_hits=copy_cache.hits;
    if(measure) { QueryPerformanceCounter(&stage_end);
        chain->prepare_stage_ticks[1]=stage_end.QuadPart-stage_start.QuadPart;
        stage_start=stage_end; }
    /* The coherent registry observation now follows native-field copying.
     * No native memory reads or writes occur while its token is held. */
    snapshot_vehicle_selection_projection(&snapshot,&projection,&chain->sample);
    if(measure) { QueryPerformanceCounter(&stage_end);
        chain->prepare_stage_ticks[0]=stage_end.QuadPart-stage_start.QuadPart;
        stage_start=stage_end; }
    if(!snapshot.lock_acquired) {
        InterlockedIncrement(&d->limiter_fail_snapshot_lock);
        goto fail_open;
    }
    chain->degraded=snapshot.publisher_overlap || snapshot.gap_epochs ||
        snapshot.overflow_epochs;
    if(chain->degraded) {
        InterlockedIncrement(&d->limiter_degraded_calls);
        if(snapshot.publisher_overlap)
            InterlockedIncrement(&d->limiter_degraded_publisher_overlap);
        if(snapshot.gap_epochs)
            InterlockedIncrement(&d->limiter_degraded_gap_epoch);
        if(snapshot.overflow_epochs)
            InterlockedIncrement(&d->limiter_degraded_overflow_epoch);
    }
    classify_vehicle_selection_projected_fields(&chain->sample,&snapshot,&projection,0);
    if(measure) { QueryPerformanceCounter(&stage_end);
        chain->prepare_stage_ticks[2]=stage_end.QuadPart-stage_start.QuadPart;
        stage_start=stage_end; }
    chain->evaluated=1U;
    chain->complete_vehicles=chain->sample.selection_vehicles;
    apply_vehicle_residency_policy(chain);
    if(measure) { QueryPerformanceCounter(&stage_end);
        chain->prepare_stage_ticks[3]=stage_end.QuadPart-stage_start.QuadPart; }
    for(i=0;i<candidate_count;++i) {
        VehicleCandidateRecord *record=&chain->sample.records[i];
        int suppress=record->renderer_type==3 && !record->selection_driver &&
            record->selection_group_complete &&
            record->selection_renderer_recent &&
            !record->selection_would_keep;
        if(record->selection_group_complete && record->selection_renderer_recent)
            ++chain->classified;
        if(suppress) { ++chain->suppressed; continue; }
        if(!head) head=chain->nodes[i];
        else if(*(void **)tail!=chain->nodes[i]) {
            *(void **)tail=chain->nodes[i]; ++chain->links_changed;
        }
        tail=chain->nodes[i];
        ++kept;
    }
    if(!chain->suppressed || !head || !tail) return;
    if(*(void **)tail) { *(void **)tail=NULL; ++chain->links_changed; }
    chain->head=head;
    chain->count=kept;
    chain->active=1U;
    InterlockedIncrement(&d->limiter_active_calls);
    atomic_add_long(&d->limiter_suppressed,(LONG)chain->suppressed);
    update_vehicle_limiter_peak(&d->limiter_peak_suppressed,chain->suppressed);
    return;
fail_open:
    chain->fail_open=1U;
    InterlockedIncrement(&d->limiter_fail_open_calls);
}

/* Keep the large selection frame behind the bypass. TinyCC probes stack pages
 * on function entry even when the source returns before using local arrays. */
static void prepare_vehicle_limiter_chain(VehicleLimiterChain *chain,
    void *candidates,uint32_t candidate_count,LONG manager_call,int measure)
{
    VehicleLightDiagnosticState *d=&g_shadow_engine.vehicle_diagnostics;
    memset(chain,0,offsetof(VehicleLimiterChain,nodes));
    memset(&chain->sample,0,offsetof(VehicleCandidateSample,records));
    chain->head=candidates;
    chain->count=candidate_count;
    InterlockedIncrement(&d->limiter_calls);
    chain->enabled=shadow_policy_enabled(SHADOW_POLICY_DISABLE_LIMITER);
    if(!chain->enabled) {
        InterlockedIncrement(&d->limiter_bypass_disabled);
        return;
    }
    prepare_enabled_vehicle_limiter_chain(chain,candidates,candidate_count,manager_call,measure);
}

static void restore_vehicle_limiter_chain(VehicleLimiterChain *chain)
{
    uint32_t i;
    if(!chain->active) return;
    for(i=0;i<chain->captured;++i)
        if(*(void **)chain->nodes[i]!=chain->original_next[i])
            *(void **)chain->nodes[i]=chain->original_next[i];
}

typedef struct VehicleAdmissionOwner {
    uint64_t vehicle;
    uint32_t lights,admitted;
} VehicleAdmissionOwner;

static void summarize_vehicle_policy_admission(VehicleCandidateSample *sample,
    const VehicleLimiterChain *chain)
{
    VehicleAdmissionOwner headlights[VEHICLE_SELECTION_MAX_VEHICLES];
    VehicleAdmissionOwner type1[VEHICLE_SELECTION_MAX_VEHICLES];
    uint32_t headlight_count=0,type1_count=0,i,j;
    memset(headlights,0,sizeof(headlights));
    memset(type1,0,sizeof(type1));
    sample->limiter_enabled=chain->enabled;
    sample->limiter_evaluated=chain->evaluated;
    sample->limiter_active=chain->active;
    sample->limiter_fail_open=chain->fail_open;
    sample->limiter_suppressed=chain->suppressed;
    sample->residency_applied=chain->residency_applied;
    sample->residency_degraded=chain->degraded;
    sample->residency_initialized=chain->residency_initialized;
    sample->residency_replaced=chain->residency_replaced;
    sample->residency_pending=chain->residency_pending;
    sample->residency_residents=chain->residency_residents;
    if(!chain->evaluated) return;
    sample->selection_main_limit=chain->sample.selection_main_limit;
    sample->selection_extra_slots=chain->sample.selection_extra_slots;
    sample->driver_trace=chain->sample.driver_trace;
    sample->driver_valid=chain->sample.driver_valid;
    sample->driver_headlight_lights=chain->sample.driver_headlight_lights;
    sample->driver_owner=chain->sample.driver_owner;
    sample->driver_entity_id=chain->sample.driver_entity_id;
    sample->driver_player_id=chain->sample.driver_player_id;
    for(i=0;i<chain->sample.copied_candidates;++i) {
        const VehicleCandidateRecord *source=&chain->sample.records[i];
        VehicleCandidateRecord *captured=NULL;
        for(j=0;j<sample->copied_candidates;++j)
            if(sample->records[j].candidate==source->candidate) {
                captured=&sample->records[j];
                break;
            }
        if(!captured) continue;
        captured->selection_role=source->selection_role;
        captured->selection_driver=source->selection_driver;
        captured->selection_entity_id=source->selection_entity_id;
        if(source->renderer_type==3 && source->selection_would_keep) {
            captured->policy_protected=1U;
            ++sample->protected_headlight_lights;
            if(captured->admitted) ++sample->protected_headlight_admitted_lights;
            for(j=0;j<headlight_count;++j)
                if(headlights[j].vehicle==source->selection_vehicle) break;
            if(j==headlight_count && headlight_count<VEHICLE_SELECTION_MAX_VEHICLES) {
                headlights[j].vehicle=source->selection_vehicle;
                ++headlight_count;
            }
            if(j<headlight_count) {
                ++headlights[j].lights;
                if(captured->admitted) ++headlights[j].admitted;
            }
        }
        if(source->selection_vehicle_type1) {
            captured->selection_vehicle_type1=1U;
            captured->selection_vehicle=source->selection_vehicle;
            captured->selection_matches=source->selection_matches;
            captured->selection_candidate_instances=source->selection_candidate_instances;
            captured->selection_publish_manager_call=source->selection_publish_manager_call;
            captured->selection_publish_renderer_epoch=source->selection_publish_renderer_epoch;
            captured->selection_renderer_delta=source->selection_renderer_delta;
            captured->selection_renderer_recent=source->selection_renderer_recent;
            captured->selection_age_ms=source->selection_age_ms;
            captured->selection_distance_squared=source->selection_distance_squared;
            ++sample->vehicle_type1_candidates;
            if(captured->admitted) ++sample->vehicle_type1_admitted_candidates;
            for(j=0;j<type1_count;++j)
                if(type1[j].vehicle==source->selection_vehicle) break;
            if(j==type1_count && type1_count<VEHICLE_SELECTION_MAX_VEHICLES) {
                type1[j].vehicle=source->selection_vehicle;
                ++type1_count;
            }
            if(j<type1_count) {
                ++type1[j].lights;
                if(captured->admitted) ++type1[j].admitted;
            }
        }
    }
    sample->protected_headlight_vehicles=headlight_count;
    for(i=0;i<headlight_count;++i) {
        if(headlights[i].admitted==headlights[i].lights)
            ++sample->protected_headlight_fully_admitted_vehicles;
        else if(headlights[i].admitted)
            ++sample->protected_headlight_partially_admitted_vehicles;
        else ++sample->protected_headlight_not_admitted_vehicles;
    }
    sample->vehicle_type1_vehicles=type1_count;
    for(i=0;i<type1_count;++i)
        if(type1[i].admitted) ++sample->vehicle_type1_admitted_vehicles;
    sample->protected_loss_with_vehicle_type1_admitted=
        (sample->protected_headlight_partially_admitted_vehicles ||
         sample->protected_headlight_not_admitted_vehicles) &&
        sample->vehicle_type1_admitted_vehicles;
}

static VehicleCaptureTicket begin_vehicle_candidate_sample(void *candidates,
    uint32_t candidate_count,LONG manager_call)
{
    VehicleLightDiagnosticState *diagnostic=&g_shadow_engine.vehicle_diagnostics;
    VehicleCaptureTicket ticket={0,0};
    VehicleCandidateSample *sample;
    VehicleSelectionSnapshot selection_snapshot;
    unsigned char *node=(unsigned char *)candidates;
    LONG sample_index;
    DWORD now;
    uint32_t index;
    if(InterlockedCompareExchange(&diagnostic->state,0,0)!=2) return ticket;
    now=GetTickCount();
    sample_index=InterlockedCompareExchange(&diagnostic->sample_count,0,0);
    if(sample_index<0 || sample_index>=(LONG)VEHICLE_CAPTURE_SAMPLES) return ticket;
    if(now-diagnostic->started_tick<(DWORD)sample_index*VEHICLE_CAPTURE_INTERVAL_MS)
        return ticket;
    if(!vehicle_capture_lock()) return ticket;
    if(diagnostic->state!=2 || diagnostic->sample_count!=sample_index) {
        vehicle_capture_unlock(); return ticket;
    }
    sample=&diagnostic->samples[sample_index];
    memset(sample,0,sizeof(*sample));
    sample->tick=now;
    sample->manager_call=manager_call;
    sample->renderer_epoch=InterlockedCompareExchange(
        &g_shadow_engine.renderer.renderer_calls,0,0);
    sample->reported_candidates=candidate_count;
    snapshot_vehicle_selection(&selection_snapshot,sample->renderer_epoch);
    for(index=0;index<candidate_count &&
        index<VEHICLE_CAPTURE_MAX_CANDIDATES && node;++index) {
        VehicleCandidateRecord *record=&sample->records[index];
        if(!copy_vehicle_candidate(record,node)) break;
        node=(unsigned char *)record->next;
        sample->copied_candidates=index+1U;
    }
    sample->chain_ended=node==NULL ? 1U:0U;
    classify_vehicle_selection_sample(sample,&selection_snapshot);
    ticket.active=1;
    ticket.sample_index=(uint32_t)sample_index;
    return ticket;
}

static void finish_vehicle_candidate_sample(VehicleCaptureTicket ticket,
    void *bindings,const VehicleLimiterChain *chain)
{
    VehicleLightDiagnosticState *diagnostic=&g_shadow_engine.vehicle_diagnostics;
    VehicleCandidateSample *sample;
    unsigned char *records=NULL;
    uint32_t admitted=0,binding_index,record_index;
    if(!ticket.active || ticket.sample_index>=VEHICLE_CAPTURE_SAMPLES) return;
    sample=&diagnostic->samples[ticket.sample_index];
    sample->freeze_ms=chain->sample.freeze_ms;
    sample->freeze_calls=chain->sample.freeze_calls;
    sample->freeze_peak_ms=chain->sample.freeze_peak_ms;
    sample->freeze_peak_calls=chain->sample.freeze_peak_calls;
    if(bindings && readable_memory(bindings,12U)) {
        records=*(unsigned char **)bindings;
        admitted=*(uint32_t *)((unsigned char *)bindings+8U);
        if(admitted>VEHICLE_CAPTURE_MAX_CANDIDATES ||
           (admitted && (!records || !readable_memory(records,(size_t)admitted*0x18U)))) {
            records=NULL;
            admitted=0;
        } else sample->admission_observed=1U;
    }
    sample->admitted_candidates=admitted;
    if(records) {
        for(binding_index=0;binding_index<admitted;++binding_index) {
            unsigned char *binding=records+(size_t)binding_index*0x18U;
            void *candidate=*(void **)binding;
            void *cache=*(void **)(binding+8U);
            if(cache) ++sample->cached_candidates;
            for(record_index=0;record_index<sample->copied_candidates;++record_index) {
                VehicleCandidateRecord *record=&sample->records[record_index];
                if(record->candidate==candidate) {
                    record->admitted=1;
                    record->cache_owner=cache;
                    record->binding_flags=(uint32_t)*(unsigned char *)(binding+0x10U);
                    break;
                }
            }
        }
    }
    summarize_vehicle_policy_admission(sample,chain);
    InterlockedExchange(&diagnostic->sample_count,(LONG)ticket.sample_index+1);
    vehicle_capture_close_if_complete();
    vehicle_capture_unlock();
    schedule_vehicle_candidate_capture_flush();
}

static VehicleQueueTicket begin_vehicle_queue_sample(void *queue,
    uint32_t original_entries,uint32_t admitted_entries)
{
    VehicleLightDiagnosticState *d=&g_shadow_engine.vehicle_diagnostics;
    VehicleQueueTicket ticket={{0,0},NULL};
    VehicleQueueSample *sample;
    LONG sample_index;
    DWORD now;
    uint32_t index,map_start=0,limit;
    if(InterlockedCompareExchange(&d->state,0,0)!=2) return ticket;
    if(!d->resolution_enabled) return ticket;
    sample_index=InterlockedCompareExchange(&d->queue_sample_count,0,0);
    if(sample_index<0 || sample_index>=(LONG)VEHICLE_CAPTURE_SAMPLES) return ticket;
    now=GetTickCount();
    if(now-d->started_tick<(DWORD)sample_index*VEHICLE_CAPTURE_INTERVAL_MS ||
       !vehicle_capture_lock()) return ticket;
    if(d->state!=2 || d->queue_sample_count!=sample_index) {
        vehicle_capture_unlock(); return ticket;
    }
    sample=&d->queue_samples[sample_index];
    memset(sample,0,sizeof(*sample));
    sample->tick=now; sample->thread=GetCurrentThreadId();
    sample->requested_policy=InterlockedCompareExchange(&g_shadow_engine.policy_control.disabled,0,0);
    sample->world_enabled=shadow_policy_world_enabled();
    sample->world_cap=shadow_policy_world_resolution();
    sample->vehicle_lock_skips_before=InterlockedCompareExchange(&d->quality_lock_skips,0,0);
    sample->world_lock_skips_before=InterlockedCompareExchange(&d->world_quality_lock_skips,0,0);
    sample->renderer_call=g_shadow_engine.renderer.renderer_calls;
    sample->original_entries=original_entries; sample->admitted_entries=admitted_entries;
    limit=original_entries<PHYSICAL_QUEUE_ENTRIES ? original_entries:PHYSICAL_QUEUE_ENTRIES;
    for(index=0;queue && index<limit;++index) {
        unsigned char *entry=(unsigned char *)queue+(size_t)index*RENDER_QUEUE_ENTRY_BYTES;
        VehicleQueueRecord *record=&sample->records[index];
        void *candidate;
        if(!readable_memory(entry,VEHICLE_QUEUE_HEIGHT_OFFSET+sizeof(uint32_t))) break;
        record->valid=1U; record->entry_index=index; record->map_start=map_start;
        record->submitted=index<admitted_entries;
        record->face_count=*(uint32_t *)(entry+VEHICLE_QUEUE_FACE_COUNT_OFFSET);
        record->width=*(uint32_t *)(entry+VEHICLE_QUEUE_WIDTH_OFFSET);
        record->height=*(uint32_t *)(entry+VEHICLE_QUEUE_HEIGHT_OFFSET);
        record->native_width=record->width;
        record->native_height=record->height;
        candidate=*(void **)(entry+VEHICLE_QUEUE_CANDIDATE_OFFSET);
        /* Dropped trailing records are not passed to the native renderer.
         * Keep their dimensions only; do not extend their pointer lifetime. */
        if(record->submitted && copy_vehicle_candidate(&record->light,candidate)) {
            record->valid|=2U;
            copy_shadow_resource_before(&record->resource_before,candidate);
            copy_vehicle_descriptor_handle(record);
            if(record->light.spatial && readable_memory(record->light.spatial,sizeof(void *))) {
                record->spatial_root=*(void **)record->light.spatial;
                record->valid|=4U;
            }
        } else record->light.renderer_type=-1;
        ++sample->copied_entries;
        if(record->face_count>6U) { record->valid=0; break; }
        map_start+=record->face_count;
    }
    ticket.capture.active=1; ticket.capture.sample_index=(uint32_t)sample_index;
    ticket.queue=queue;
    return ticket;
}

/* Begin owns the nonwaiting capture token until finish. Only already-validated
 * submitted entries are reread before native rendering. No queue writes. */
static void sample_vehicle_queue_policy_request(VehicleQueueTicket ticket)
{
    VehicleQueueSample *sample;
    uint32_t index;
    if(!ticket.capture.active) return;
    sample=&g_shadow_engine.vehicle_diagnostics.queue_samples[ticket.capture.sample_index];
    for(index=0;index<sample->copied_entries;++index) {
        VehicleQueueRecord *record=&sample->records[index];
        unsigned char *entry=(unsigned char *)ticket.queue+(size_t)index*RENDER_QUEUE_ENTRY_BYTES;
        if(!(record->valid&1U) || !record->submitted) continue;
        record->width=*(uint32_t *)(entry+VEHICLE_QUEUE_WIDTH_OFFSET);
        record->height=*(uint32_t *)(entry+VEHICLE_QUEUE_HEIGHT_OFFSET);
        record->policy_sampled=1U;
    }
}

static void finish_vehicle_queue_sample(VehicleQueueTicket ticket)
{
    VehicleLightDiagnosticState *d=&g_shadow_engine.vehicle_diagnostics;
    VehicleQueueSample *sample;
    uint32_t index;
    if(!ticket.capture.active) return;
    sample=&d->queue_samples[ticket.capture.sample_index];
    for(index=0;index<sample->copied_entries;++index) {
        VehicleQueueRecord *record=&sample->records[index];
        unsigned char *entry=(unsigned char *)ticket.queue+(size_t)index*RENDER_QUEUE_ENTRY_BYTES;
        if(!record->submitted || !readable_memory(entry+VEHICLE_QUEUE_RESOURCE_OFFSET,sizeof(void *)))
            continue;
        record->resource=*(void **)(entry+VEHICLE_QUEUE_RESOURCE_OFFSET);
        if(record->resource && readable_memory(record->resource,
           VEHICLE_RESOURCE_HEIGHT_OFFSET+sizeof(uint32_t))) {
            record->resource_width=*(uint32_t *)((unsigned char *)record->resource+VEHICLE_RESOURCE_WIDTH_OFFSET);
            record->resource_height=*(uint32_t *)((unsigned char *)record->resource+VEHICLE_RESOURCE_HEIGHT_OFFSET);
            record->valid|=8U;
        }
    }
    sample->vehicle_lock_skips_after=InterlockedCompareExchange(&d->quality_lock_skips,0,0);
    sample->world_lock_skips_after=InterlockedCompareExchange(&d->world_quality_lock_skips,0,0);
    sample->complete=sample->copied_entries==sample->original_entries &&
        (!sample->copied_entries || sample->records[sample->copied_entries-1U].valid!=0);
    d->owner_window_tick[ticket.capture.sample_index]=GetTickCount();
    InterlockedExchange(&d->queue_sample_count,(LONG)ticket.capture.sample_index+1);
    vehicle_capture_close_if_complete();
    vehicle_capture_unlock();
    schedule_vehicle_candidate_capture_flush();
}
