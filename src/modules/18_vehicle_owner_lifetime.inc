/* Copied tracked-owner retirement evidence. Never owns an engine object or
 * classifies/suppresses a light. Only matched F10 samples enroll owners;
 * native teardown observes numeric tokens without reading engine memory. */
#include "../internal/overhead_probe.h"
static const unsigned char g_vehicle_retire_prefix[] =
    "\x48\x89\x5c\x24\x10\x48\x89\x74\x24\x18\x55\x57\x41\x54\x41\x55\x41\x56"
    "\x48\x8d\x6c\x24\xc0\x48\x81\xec\x40\x01\x00\x00\x48\x8b\xb9\x18\x01\x00\x00";
static const unsigned char g_vehicle_retire_array[] =
    "\x48\x8d\x83\x48\x01\x00\x00\x48\x8d\x4d\x10\x48\x89\x45\x18\x48\x83\xc0\x08"
    "\x4c\x89\x75\x20\x48\x89\x45\x28";
static const unsigned char g_vehicle_retire_stride[] =
    "\x48\xc7\x45\x30\x30\x01\x00\x00\x48\x89\x45\x10\xe8";
static const unsigned char g_vehicle_retire_tail[] =
    "\x48\x8b\xcb\x48\x89\x43\x30\x4c\x8d\x9c\x24\x40\x01\x00\x00\x49\x8b\x5b\x38"
    "\x49\x8b\x73\x40\x49\x8b\xe3\x41\x5e\x41\x5d\x41\x5c\x5f\x5d\xe9";

static int validate_vehicle_retire_site(void)
{
    VehicleLifetimeDiagnosticState *d=&g_shadow_engine.vehicle_lifetime;
    const RuntimeProfile *p=g_runtime_state.selected;
    unsigned char *base=g_shadow_engine.bootstrap.disrupt_base,*target;
    d->retire_target=NULL;
    if(!p || !base || !p->vehicle_retire_rva || g_runtime_state.image_size<0x37CU ||
       p->vehicle_retire_rva>g_runtime_state.image_size-0x37CU) return 0;
    target=base+p->vehicle_retire_rva;
    if(!readable_memory(target,0x37CU) ||
       memcmp(target,g_vehicle_retire_prefix,sizeof(g_vehicle_retire_prefix)-1U) ||
       memcmp(target+0x2BFU,g_vehicle_retire_array,sizeof(g_vehicle_retire_array)-1U) ||
       memcmp(target+0x2E1U,g_vehicle_retire_stride,sizeof(g_vehicle_retire_stride)-1U) ||
       memcmp(target+0x355U,g_vehicle_retire_tail,sizeof(g_vehicle_retire_tail)-1U)) return 0;
    d->retire_target=target;
    return 1;
}

static int vehicle_lifetime_lock(void)
{
    VehicleLifetimeDiagnosticState *d=&g_shadow_engine.vehicle_lifetime;
    if(InterlockedCompareExchange(&d->writer_active,1,0)==0) return 1;
    InterlockedIncrement(&d->gaps);
    return 0;
}

static void vehicle_lifetime_unlock(void)
{
    InterlockedExchange(&g_shadow_engine.vehicle_lifetime.writer_active,0);
}

/* Caller holds the token. Zero is never a valid observation sequence. */
static uint64_t vehicle_lifetime_next_sequence(void)
{
    VehicleLifetimeDiagnosticState *d=&g_shadow_engine.vehicle_lifetime;
    if(d->sequence==UINT64_MAX) { InterlockedIncrement(&d->gaps); return 0; }
    ++d->sequence;
    return d->sequence;
}

static void observe_vehicle_lifetime(VehicleOwnerRecord *sample,LONG serial)
{
    VehicleLifetimeDiagnosticState *d=&g_shadow_engine.vehicle_lifetime;
    VehicleLifetimeRecord *r;
    uint32_t index;
    uint64_t sequence;
    sample->lifetime_sequence=0; sample->lifetime_slot=0;
    if(!d->enabled || sample->vehicle_flags!=VEHICLE_BATCH_MATCH_FLAGS || !sample->vehicle ||
       !vehicle_lifetime_lock()) return;
    for(index=0;index<d->count;++index)
        if(d->records[index].owner==(uint64_t)(uintptr_t)sample->vehicle) break;
    if(index==d->count && d->count==VEHICLE_LIFETIME_MAX_RECORDS) {
        InterlockedIncrement(&d->overflow); vehicle_lifetime_unlock(); return;
    }
    sequence=vehicle_lifetime_next_sequence();
    if(!sequence) { vehicle_lifetime_unlock(); return; }
    r=&d->records[index];
    if(index==d->count) {
        memset(r,0,sizeof(*r)); ++d->count;
        r->owner=(uint64_t)(uintptr_t)sample->vehicle;
        r->first_serial=serial; r->first_sequence=sequence;
    } else {
        if(r->state==2U) { ++r->reappearances; r->state=0; }
        if(r->state==1U) ++r->samples_during_retire;
        if(r->array!=(uint64_t)(uintptr_t)sample->vehicle_array || r->array_count!=sample->vehicle_count ||
           r->skeleton!=(uint64_t)(uintptr_t)sample->skeleton ||
           r->vtable!=(uint64_t)(uintptr_t)sample->vehicle_vtable) ++r->tuple_changes;
    }
    r->array=(uint64_t)(uintptr_t)sample->vehicle_array; r->array_count=sample->vehicle_count;
    r->skeleton=(uint64_t)(uintptr_t)sample->skeleton; r->vtable=(uint64_t)(uintptr_t)sample->vehicle_vtable;
    r->last_serial=serial; r->last_sample_sequence=sequence; ++r->observations;
    sample->lifetime_slot=index; sample->lifetime_sequence=sequence;
    vehicle_lifetime_unlock();
}

typedef struct VehicleRetireTicket {
    uint64_t sequence,owner;
    uint32_t index;
} VehicleRetireTicket;

static VehicleRetireTicket begin_vehicle_retirement(void *vehicle)
{
    VehicleLifetimeDiagnosticState *d=&g_shadow_engine.vehicle_lifetime;
    VehicleRetireTicket ticket={0,0,0};
    uint32_t i;
    if(!d->enabled || !vehicle_lifetime_lock()) return ticket;
    for(i=0;i<d->count;++i) {
        VehicleLifetimeRecord *r=&d->records[i];
        if(r->owner!=(uint64_t)(uintptr_t)vehicle) continue;
        if(r->state==1U) { ++r->overlaps; break; }
        ticket.sequence=vehicle_lifetime_next_sequence();
        if(!ticket.sequence) break;
        ticket.owner=r->owner; ticket.index=i;
        r->enter_sequence=ticket.sequence; r->state=1U; ++r->retire_entries;
        r->enter_tick=GetTickCount(); r->enter_thread=GetCurrentThreadId();
        break;
    }
    vehicle_lifetime_unlock();
    return ticket;
}

static void finish_vehicle_retirement(VehicleRetireTicket ticket)
{
    VehicleLifetimeDiagnosticState *d=&g_shadow_engine.vehicle_lifetime;
    VehicleLifetimeRecord *r;
    uint64_t sequence;
    if(!ticket.sequence || !vehicle_lifetime_lock()) return;
    if(ticket.index>=d->count) { InterlockedIncrement(&d->gaps); vehicle_lifetime_unlock(); return; }
    r=&d->records[ticket.index];
    if(r->owner!=ticket.owner || r->state!=1U || r->enter_sequence!=ticket.sequence) {
        InterlockedIncrement(&d->gaps); vehicle_lifetime_unlock(); return;
    }
    sequence=vehicle_lifetime_next_sequence();
    if(sequence) {
        r->return_sequence=sequence; r->state=2U; ++r->retire_returns;
        r->return_tick=GetTickCount(); r->return_thread=GetCurrentThreadId();
    }
    vehicle_lifetime_unlock();
}

static void __fastcall hooked_vehicle_retire(void *vehicle)
{
    VehicleRetireTicket ticket;
    OVERHEAD_BEGIN(retirement_probe,OH_VEHICLE_RETIRE_PATCH);
    ticket=begin_vehicle_retirement(vehicle);
    OVERHEAD_PAUSE(retirement_probe);
    g_shadow_engine.vehicle_lifetime.original_retire(vehicle);
    OVERHEAD_RESUME(retirement_probe);
    /* Only the numeric ticket survives the native call. No freed-memory read. */
    finish_vehicle_retirement(ticket);
    OVERHEAD_END(retirement_probe);
}

static int install_vehicle_lifetime_observer(void)
{
    VehicleLifetimeDiagnosticState *d=&g_shadow_engine.vehicle_lifetime;
    if(!d->retire_target) {
        append_log("STAGE_VEHICLE_LIFETIME_OBSERVER enabled=0 reason=unprovenBoundary ownerCaptureRetained=1");
        return 1;
    }
    if(!install_detour(d->retire_target,g_vehicle_retire_prefix,18U,
        hooked_vehicle_retire,(void **)&d->original_retire)) return 0;
    InterlockedExchange(&d->enabled,1);
    append_log("STAGE_VEHICLE_LIFETIME_OBSERVER enabled=1 retireRva=0x%llX maxTracked=%u enroll=matchedF10 noEngineReads=1 generationProven=0",
        (unsigned long long)(d->retire_target-g_shadow_engine.bootstrap.disrupt_base),VEHICLE_LIFETIME_MAX_RECORDS);
    return 1;
}

static void rollback_vehicle_lifetime_observer(void)
{
    InterlockedExchange(&g_shadow_engine.vehicle_lifetime.enabled,0);
}

/* Called only by the single F10 worker. One bounded copy under the token;
 * all formatting occurs after releasing it. Live records may advance meanwhile. */
static void flush_vehicle_lifetime_snapshot(LONG serial)
{
    VehicleLifetimeDiagnosticState *d=&g_shadow_engine.vehicle_lifetime;
    VehicleLifetimeSnapshot *s=&d->snapshot;
    uint32_t i;
    if(!vehicle_lifetime_lock()) {
        append_log("STAGE_VEHICLE_LIFETIME_SUMMARY serial=%ld snapshot=0 reason=contention generationProven=0",(long)serial);
        return;
    }
    s->sequence=d->sequence; s->count=d->count; s->enabled=d->enabled;
    s->gaps=InterlockedCompareExchange(&d->gaps,0,0);
    s->overflow=InterlockedCompareExchange(&d->overflow,0,0);
    memcpy(s->records,d->records,s->count*sizeof(s->records[0]));
    vehicle_lifetime_unlock();
    append_log("STAGE_VEHICLE_LIFETIME_SUMMARY serial=%ld snapshot=1 enabled=%ld throughSequence=%llu tracked=%u gaps=%ld overflow=%ld maxTracked=%u generationProven=0",
        (long)serial,(long)s->enabled,(unsigned long long)s->sequence,s->count,(long)s->gaps,(long)s->overflow,VEHICLE_LIFETIME_MAX_RECORDS);
    for(i=0;i<s->count;++i) {
        const VehicleLifetimeRecord *r=&s->records[i];
        append_log("STAGE_VEHICLE_LIFETIME_RECORD serial=%ld slot=%u vehicle=0x%llX vtable=0x%llX array=0x%llX count=%u skeleton=0x%llX state=%u firstSerial=%ld lastSerial=%ld observations=%u firstSeq=%llu sampleSeq=%llu enterSeq=%llu returnSeq=%llu retireEntries=%u retireReturns=%u reappearances=%u tupleChanges=%u duringRetire=%u overlaps=%u enterTick=%lu returnTick=%lu enterThread=%lu returnThread=%lu generationProven=0",
            (long)serial,i,(unsigned long long)r->owner,(unsigned long long)r->vtable,
            (unsigned long long)r->array,r->array_count,(unsigned long long)r->skeleton,
            r->state,(long)r->first_serial,(long)r->last_serial,r->observations,
            (unsigned long long)r->first_sequence,(unsigned long long)r->last_sample_sequence,
            (unsigned long long)r->enter_sequence,(unsigned long long)r->return_sequence,
            r->retire_entries,r->retire_returns,r->reappearances,r->tuple_changes,
            r->samples_during_retire,r->overlaps,(unsigned long)r->enter_tick,
            (unsigned long)r->return_tick,(unsigned long)r->enter_thread,(unsigned long)r->return_thread);
    }
}
