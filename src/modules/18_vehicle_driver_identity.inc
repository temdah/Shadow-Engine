/* Exact, read-only driven-vehicle identity. No native resolver is invoked:
 * those helpers create references or mutate component caches. Live reads use
 * fault-contained self-process copies because these containers are not owned
 * by the shadow hook. Rechecks detect changes; they do not pin native objects.
 * All live addresses are local to one synchronous lookup. Only scalar full IDs
 * and the CVehicle comparison token leave this module. */
#include "../internal/driver_identity_trace.h"
typedef struct DriverIdentity {
    uint64_t owner,entity_id,player_id;
} DriverIdentity;

static void driver_trace_stage(DriverIdentityTrace *trace,uint32_t stage)
{
    if(trace) { trace->stage=stage; trace->detail=DRIVER_DETAIL_NONE; }
}
static int driver_trace_failure(DriverIdentityTrace *trace,uint32_t detail)
{
    if(trace) trace->detail=detail;
    return 0;
}
static uint64_t driver_trace_invalid_id(DriverIdentityTrace *trace,uint32_t detail)
{
    driver_trace_failure(trace,detail);
    return UINT64_MAX;
}

enum {
    DRIVER_COMPONENTS_OFFSET=0x68, DRIVER_CACHE_PAIRS=8,
    DRIVER_CACHE_BACKING_OFFSET=0x40,
    DRIVER_REFERENCE_OFFSET=0x10, DRIVER_PAWN_VEHICLE_OFFSET=0x90,
    DRIVER_ENTITY_FLAGS_OFFSET=0x60, DRIVER_ENTITY_FLAGS_MASK=0x10008,
    DRIVER_ENTITY_FLAGS_REQUIRED=0x10000,
    DRIVER_SEATS_OFFSET=0x1F8,
    DRIVER_SEAT_STRIDE=0x90, DRIVER_SEAT_ROLE_OFFSET=0x48,
    DRIVER_SEAT_ROLE=1,
    /* Defensive work budgets, not declarations of native capacities. */
    DRIVER_MAX_COMPONENTS=256, DRIVER_MAX_SEATS=64
};

static int driver_copy(uintptr_t address,void *out,size_t size)
{
    SIZE_T copied=0;
    if(address<0x10000U || !size || address>UINTPTR_MAX-size) return 0;
    return ReadProcessMemory(GetCurrentProcess(),(const void *)address,out,size,&copied) &&
        copied==size;
}

static int driver_same(uintptr_t address,const void *expected,size_t size)
{
    unsigned char current[80];
    return size<=sizeof(current) && driver_copy(address,current,size) &&
        !memcmp(current,expected,size);
}

typedef struct DriverReference { uint64_t id; uintptr_t entity; } DriverReference;
typedef struct DriverComponentHeader { uintptr_t array; uint32_t count,padding; uintptr_t cache; } DriverComponentHeader;
typedef struct DriverPair { uint32_t key,index; } DriverPair;
typedef struct DriverCache { DriverPair pairs[8]; uintptr_t backing; uint32_t count,padding; } DriverCache;
typedef struct DriverSeatHeader { uintptr_t seats; uint32_t count; } DriverSeatHeader;
typedef struct DriverSeatValue { uint32_t role,padding[3]; uint64_t occupant; } DriverSeatValue;
typedef struct DriverPlayerRoot { uintptr_t array; uint32_t count; } DriverPlayerRoot;

static int driver_reference(uintptr_t address,DriverReference *out,DriverIdentityTrace *trace)
{
    if(!driver_copy(address,out,sizeof(*out))) return driver_trace_failure(trace,DRIVER_DETAIL_READ);
    if(out->id==UINT64_MAX) return driver_trace_failure(trace,DRIVER_DETAIL_FULL_ID_INVALID);
    if(!out->entity) return driver_trace_failure(trace,DRIVER_DETAIL_MISSING);
    return 1;
}

static uintptr_t driver_component(uintptr_t entity,uint32_t class_id,DriverIdentityTrace *trace)
{
    DriverComponentHeader header;
    DriverCache cache;
    DriverPair pair;
    uintptr_t component=0,pair_address=0;
    uint32_t i,index=UINT32_MAX;
    if(!driver_copy(entity+DRIVER_COMPONENTS_OFFSET,&header,sizeof(header)))
        return driver_trace_failure(trace,DRIVER_DETAIL_READ);
    if(!header.array) return driver_trace_failure(trace,DRIVER_DETAIL_MISSING_ARRAY);
    if(!header.cache) return driver_trace_failure(trace,DRIVER_DETAIL_MISSING_CACHE);
    if(!header.count || header.count>DRIVER_MAX_COMPONENTS)
        return driver_trace_failure(trace,DRIVER_DETAIL_COUNT_BOUND);
    if(!driver_copy(header.cache,&cache,sizeof(cache))) return driver_trace_failure(trace,DRIVER_DETAIL_READ);
    if(cache.count>DRIVER_MAX_COMPONENTS) return driver_trace_failure(trace,DRIVER_DETAIL_COUNT_BOUND);
    for(i=0;i<DRIVER_CACHE_PAIRS;++i) if(cache.pairs[i].key==class_id) {
        pair=cache.pairs[i]; pair_address=header.cache+(uintptr_t)i*sizeof(pair);
        index=pair.index; break;
    }
    if(pair_address && index>=header.count) return driver_trace_failure(trace,DRIVER_DETAIL_CACHE_INDEX);
    if(!pair_address && cache.backing) {
        /* Read-only search of the already initialized cache. Never populate it. */
        for(i=0;i<cache.count;++i) {
            uintptr_t address=cache.backing+(uintptr_t)i*sizeof(pair);
            if(!driver_copy(address,&pair,sizeof(pair))) return driver_trace_failure(trace,DRIVER_DETAIL_READ);
            if(pair.key==class_id) { pair_address=address; index=pair.index; break; }
        }
    }
    if(!pair_address) return driver_trace_failure(trace,DRIVER_DETAIL_CACHE_CLASS_MISSING);
    if(index>=header.count) return driver_trace_failure(trace,DRIVER_DETAIL_CACHE_INDEX);
    if(!driver_copy(header.array+(uintptr_t)index*8U,&component,8U))
        return driver_trace_failure(trace,DRIVER_DETAIL_READ);
    if(!component) return driver_trace_failure(trace,DRIVER_DETAIL_COMPONENT_NULL);
    if(!driver_same(header.array+(uintptr_t)index*8U,&component,8U) ||
       !driver_same(pair_address,&pair,sizeof(pair)) ||
       !driver_same(entity+DRIVER_COMPONENTS_OFFSET,&header,sizeof(header)) ||
       !driver_same(header.cache+DRIVER_CACHE_BACKING_OFFSET,&cache.backing,12U))
        return driver_trace_failure(trace,DRIVER_DETAIL_READ_OR_CHANGED);
    return component;
}

static uint64_t driver_seat_occupant(uintptr_t vehicle,DriverIdentityTrace *trace)
{
    DriverSeatHeader header;
    DriverSeatValue value;
    uintptr_t slot;
    uint32_t i;
    /* Read the related role/occupant bytes together inside each bounded seat.
     * The first driver-role seat is authoritative, including an empty seat. */
    if(!driver_copy(vehicle+DRIVER_SEATS_OFFSET,&header,12U)) return driver_trace_invalid_id(trace,DRIVER_DETAIL_READ);
    if(!header.seats) return driver_trace_invalid_id(trace,DRIVER_DETAIL_MISSING_ARRAY);
    if(!header.count || header.count>DRIVER_MAX_SEATS) return driver_trace_invalid_id(trace,DRIVER_DETAIL_COUNT_BOUND);
    for(i=0;i<header.count;++i) {
        slot=header.seats+(uintptr_t)i*DRIVER_SEAT_STRIDE+DRIVER_SEAT_ROLE_OFFSET;
        if(!driver_copy(slot,&value,sizeof(value))) return driver_trace_invalid_id(trace,DRIVER_DETAIL_READ);
        if(value.role!=DRIVER_SEAT_ROLE) continue;
        if(trace) trace->seat_player_id=value.occupant;
        if(!driver_same(slot,&value,sizeof(value)) ||
           !driver_same(vehicle+DRIVER_SEATS_OFFSET,&header,12U))
            return driver_trace_invalid_id(trace,DRIVER_DETAIL_READ_OR_CHANGED);
        return value.occupant;
    }
    return driver_trace_invalid_id(trace,DRIVER_DETAIL_SEAT_NOT_FOUND);
}

static uint64_t copy_vehicle_entity_id(void *vehicle)
{
    uintptr_t reference;
    DriverReference value;
    if(!g_shadow_engine.vehicle_diagnostics.driver_proof_ready || !vehicle ||
       !driver_copy((uintptr_t)vehicle+DRIVER_REFERENCE_OFFSET,&reference,8U) ||
       !driver_reference(reference,&value,NULL) ||
       !driver_same((uintptr_t)vehicle+DRIVER_REFERENCE_OFFSET,&reference,8U) ||
       !driver_same(reference,&value,sizeof(value))) return UINT64_MAX;
    return value.id;
}

static int copy_current_driver_identity_traced(DriverIdentity *out,DriverIdentityTrace *trace)
{
    const RuntimeProfile *runtime=g_runtime_state.selected;
    const DriverIdentityProfile *profile;
    uintptr_t root_address=(uintptr_t)g_shadow_engine.vehicle_diagnostics.player_root_address;
    uintptr_t root,controller,data,player_reference,pawn,pawn_reference;
    uintptr_t vehicle_reference,vehicle,component_reference;
    uint32_t class_id,entity_flags;
    uint64_t occupant;
    DriverPlayerRoot players;
    DriverReference player,pawn_owner,target,component;
    if(trace) memset(trace,0,sizeof(*trace));
    if(!out) return driver_trace_failure(trace,DRIVER_DETAIL_INVALID_OUTPUT);
    memset(out,0,sizeof(*out));
    driver_trace_stage(trace,DRIVER_TRACE_PROOF);
    if(!g_shadow_engine.vehicle_diagnostics.driver_proof_ready || !runtime ||
       !(profile=runtime->driver_identity) || !root_address)
        return driver_trace_failure(trace,DRIVER_DETAIL_MISSING);
    /* Class metadata can initialize after startup. A missing class rejects this
     * lookup without invoking a lazy initializer or disabling later recovery. */
    driver_trace_stage(trace,DRIVER_TRACE_METADATA);
    if(!driver_copy((uintptr_t)g_shadow_engine.bootstrap.disrupt_base+
            profile->pawn_class_id_rva,&class_id,4U)) return driver_trace_failure(trace,DRIVER_DETAIL_READ);
    if(class_id!=profile->pawn_class_id) return driver_trace_failure(trace,DRIVER_DETAIL_METADATA_MISMATCH);
    if(!driver_copy((uintptr_t)g_shadow_engine.bootstrap.disrupt_base+
            profile->vehicle_class_id_rva,&class_id,4U)) return driver_trace_failure(trace,DRIVER_DETAIL_READ);
    if(class_id!=profile->vehicle_class_id) return driver_trace_failure(trace,DRIVER_DETAIL_METADATA_MISMATCH);
    driver_trace_stage(trace,DRIVER_TRACE_PLAYER_CHAIN);
    if(!driver_copy(root_address,&root,8U)) return driver_trace_failure(trace,DRIVER_DETAIL_READ);
    if(!root) return driver_trace_failure(trace,DRIVER_DETAIL_MISSING);
    if(!driver_copy(root,&players,12U)) return driver_trace_failure(trace,DRIVER_DETAIL_READ);
    if(!players.count) return driver_trace_failure(trace,DRIVER_DETAIL_MISSING);
    if(!players.array) return driver_trace_failure(trace,DRIVER_DETAIL_MISSING_ARRAY);
    if(!driver_copy(players.array,&controller,8U)) return driver_trace_failure(trace,DRIVER_DETAIL_READ);
    if(!controller) return driver_trace_failure(trace,DRIVER_DETAIL_MISSING);
    if(!driver_copy(controller+8U,&data,8U)) return driver_trace_failure(trace,DRIVER_DETAIL_READ);
    if(!data) return driver_trace_failure(trace,DRIVER_DETAIL_MISSING);
    if(!driver_copy(data+16U,&player_reference,8U)) return driver_trace_failure(trace,DRIVER_DETAIL_READ);
    driver_trace_stage(trace,DRIVER_TRACE_PLAYER_REFERENCE);
    if(!driver_reference(player_reference,&player,trace)) return 0;
    if(trace) trace->player_id=player.id;
    driver_trace_stage(trace,DRIVER_TRACE_PAWN_COMPONENT);
    pawn=driver_component(player.entity,profile->pawn_class_id,trace);
    if(!pawn) return 0;
    driver_trace_stage(trace,DRIVER_TRACE_PAWN_REFERENCE);
    if(!driver_copy(pawn+DRIVER_REFERENCE_OFFSET,&pawn_reference,8U))
        return driver_trace_failure(trace,DRIVER_DETAIL_READ);
    /* Equal reference addresses share one copied full reference. Both referring
     * fields and that reference are rechecked before returning any identity. */
    if(pawn_reference==player_reference) pawn_owner=player;
    else if(!driver_reference(pawn_reference,&pawn_owner,trace)) return 0;
    if(pawn_owner.id!=player.id) return driver_trace_failure(trace,DRIVER_DETAIL_ID_MISMATCH);
    if(pawn_owner.entity!=player.entity) return driver_trace_failure(trace,DRIVER_DETAIL_ENTITY_MISMATCH);
    driver_trace_stage(trace,DRIVER_TRACE_VEHICLE_REFERENCE);
    /* Byte-proven native CPawn driver predicate follows this direct reference.
     * CPawn+40 is separate state and is not the pawn's owning CEntity. */
    if(!driver_copy(pawn+DRIVER_PAWN_VEHICLE_OFFSET,&vehicle_reference,8U))
        return driver_trace_failure(trace,DRIVER_DETAIL_READ);
    if(!vehicle_reference) return driver_trace_failure(trace,DRIVER_DETAIL_MISSING);
    if(!driver_reference(vehicle_reference,&target,trace)) return 0;
    if(trace) trace->vehicle_id=target.id;
    driver_trace_stage(trace,DRIVER_TRACE_VEHICLE_FLAGS);
    if(!driver_copy(target.entity+DRIVER_ENTITY_FLAGS_OFFSET,&entity_flags,4U))
        return driver_trace_failure(trace,DRIVER_DETAIL_READ);
    if((entity_flags&DRIVER_ENTITY_FLAGS_MASK)!=DRIVER_ENTITY_FLAGS_REQUIRED)
        return driver_trace_failure(trace,DRIVER_DETAIL_ENTITY_FLAGS);
    driver_trace_stage(trace,DRIVER_TRACE_VEHICLE_COMPONENT);
    vehicle=driver_component(target.entity,profile->vehicle_class_id,trace);
    if(!vehicle) return 0;
    if(trace) trace->owner=(uint64_t)vehicle;
    driver_trace_stage(trace,DRIVER_TRACE_COMPONENT_REFERENCE);
    if(!driver_copy(vehicle+DRIVER_REFERENCE_OFFSET,&component_reference,8U))
        return driver_trace_failure(trace,DRIVER_DETAIL_READ);
    if(component_reference==vehicle_reference) component=target;
    else if(!driver_reference(component_reference,&component,trace)) return 0;
    if(component.id!=target.id) return driver_trace_failure(trace,DRIVER_DETAIL_ID_MISMATCH);
    if(component.entity!=target.entity) return driver_trace_failure(trace,DRIVER_DETAIL_ENTITY_MISMATCH);
    driver_trace_stage(trace,DRIVER_TRACE_DRIVER_SEAT);
    occupant=driver_seat_occupant(vehicle,trace);
    if(occupant!=player.id) {
        if(!trace || trace->detail==DRIVER_DETAIL_NONE)
            driver_trace_failure(trace,DRIVER_DETAIL_SEAT_OCCUPANT_MISMATCH);
        return 0;
    }
    /* No object is pinned. Recheck the direct association, full generations,
     * owner references, native eligibility flags and player root in this call.
     * Aliased full references need one recheck, not two identical RPM calls. */
    driver_trace_stage(trace,DRIVER_TRACE_RECHECK);
    if(!driver_same(pawn+DRIVER_REFERENCE_OFFSET,&pawn_reference,8U) ||
       !driver_same(pawn+DRIVER_PAWN_VEHICLE_OFFSET,&vehicle_reference,8U) ||
       !driver_same(vehicle+DRIVER_REFERENCE_OFFSET,&component_reference,8U) ||
       (component_reference!=vehicle_reference && !driver_same(component_reference,&component,sizeof(component))) ||
       !driver_same(vehicle_reference,&target,sizeof(target)) ||
       (pawn_reference!=player_reference && !driver_same(pawn_reference,&pawn_owner,sizeof(pawn_owner))) ||
       !driver_same(player_reference,&player,sizeof(player)) ||
       !driver_same(target.entity+DRIVER_ENTITY_FLAGS_OFFSET,&entity_flags,4U) ||
       !driver_same(data+16U,&player_reference,8U) ||
       !driver_same(controller+8U,&data,8U) || !driver_same(players.array,&controller,8U) ||
       !driver_same(root,&players,12U) || !driver_same(root_address,&root,8U))
        return driver_trace_failure(trace,DRIVER_DETAIL_READ_OR_CHANGED);
    occupant=driver_seat_occupant(vehicle,trace);
    if(occupant!=player.id) {
        if(!trace || trace->detail==DRIVER_DETAIL_NONE)
            driver_trace_failure(trace,DRIVER_DETAIL_SEAT_OCCUPANT_MISMATCH);
        return 0;
    }
    out->owner=(uint64_t)vehicle; out->entity_id=target.id; out->player_id=player.id;
    driver_trace_stage(trace,DRIVER_TRACE_SUCCESS);
    return 1;
}

static int copy_current_driver_identity(DriverIdentity *out)
{
    return copy_current_driver_identity_traced(out,NULL);
}

static int driver_proof_range(uint64_t rva,size_t size)
{
    return g_shadow_engine.bootstrap.disrupt_base && size<=g_runtime_state.image_size &&
        rva<=g_runtime_state.image_size-size &&
        readable_memory(g_shadow_engine.bootstrap.disrupt_base+rva,size);
}

static void initialize_vehicle_driver_identity(void)
{
    const RuntimeProfile *runtime=g_runtime_state.selected;
    const DriverIdentityProfile *profile;
    unsigned char *base=g_shadow_engine.bootstrap.disrupt_base;
    size_t i;
    InterlockedExchange(&g_shadow_engine.vehicle_diagnostics.driver_proof_ready,0);
    if(!runtime || !(profile=runtime->driver_identity) ||
       !g_shadow_engine.vehicle_diagnostics.player_root_address ||
       !profile->spans || !profile->span_count) return;
    for(i=0;i<profile->span_count;++i) {
        const DriverProofSpan *span=&profile->spans[i];
        if(!span->bytes || !span->size || !driver_proof_range(span->rva,span->size) ||
           memcmp(base+span->rva,span->bytes,span->size)) return;
    }
    if(!driver_proof_range(profile->pawn_class_id_rva,4U) ||
       !driver_proof_range(profile->vehicle_class_id_rva,4U)) return;
    InterlockedExchange(&g_shadow_engine.vehicle_diagnostics.driver_proof_ready,1);
    append_log("DRIVER_IDENTITY_READY section=3 build=B workRegister=C1-D3 retained=C1-D2 proof=readOnlyDirectPawnVehicleReferenceAndDriverSeat");
}
