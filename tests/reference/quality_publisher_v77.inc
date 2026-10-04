/* Frozen .77 publisher and its scalar helpers, extracted from the current
 * pre-edit working-file snapshot for Section 3 / Build A.
 * Source SHA256: FC0D1ADA7AE09A83DF8D4A7496CF3D17BDC19442015C5B7CD1BF8676DEDF9805
 * Only static function names are prefixed; logic is unchanged.
 * Shared lock functions remain current; their ownership contract is tested. */
static uint32_t frozen77_vehicle_selection_hash(uint64_t handle,uint64_t vehicle)
{
    uint64_t value=handle^(vehicle+(handle<<6U)+(handle>>2U));
    value^=value>>33U;
    value*=0xff51afd7ed558ccdULL;
    value^=value>>33U;
    return (uint32_t)value;
}

static void frozen77_publish_vehicle_quality_identity(uint32_t bank,LONG renderer_epoch,
    uint64_t handle,uint64_t vehicle,float distance_squared)
{
    VehicleLightDiagnosticState *d=&g_shadow_engine.vehicle_diagnostics;
    uint32_t index=frozen77_vehicle_selection_hash(handle,0U)&
        (VEHICLE_SELECTION_SLOTS_PER_EPOCH-1U);
    uint32_t probe;
    LONG tag=renderer_epoch+1;
    for(probe=0;probe<VEHICLE_SELECTION_SLOTS_PER_EPOCH;++probe) {
        VehicleQualityRecord *record=&d->quality_records[bank]
            [(index+probe)&(VEHICLE_SELECTION_SLOTS_PER_EPOCH-1U)];
        LONG seen=InterlockedCompareExchange(&record->epoch_tag,0,0);
        if(seen!=tag) {
            InterlockedExchange(&record->epoch_tag,0);
            record->descriptor_handle=handle;
            record->vehicle=vehicle;
            record->distance_squared=distance_squared;
            record->ambiguous=0U;
            InterlockedExchange(&record->epoch_tag,tag);
            return;
        }
        if(record->descriptor_handle==handle) {
            /* Publication and quality reads hold the registry token. The
             * epoch tag alone is not a seqlock across same-epoch updates. */
            InterlockedExchange(&record->epoch_tag,0);
            if(record->vehicle!=vehicle) record->ambiguous=1U;
            else record->distance_squared=distance_squared;
            InterlockedExchange(&record->epoch_tag,tag);
            return;
        }
    }
    InterlockedIncrement(&d->quality_overflow);
}

static void frozen77_index_vehicle_selection_slot(uint32_t bank,uint32_t slot,uint64_t handle)
{
    VehicleLightDiagnosticState *d=&g_shadow_engine.vehicle_diagnostics;
    uint32_t bucket=frozen77_vehicle_selection_hash(handle,0U)&
        (VEHICLE_SELECTION_SLOTS_PER_EPOCH-1U);
    uint32_t *link=&d->selection_heads[bank][bucket];
    while(*link && *link<slot+1U) link=&d->selection_next[bank][*link-1U];
    d->selection_next[bank][slot]=*link;
    *link=slot+1U;
}

static void frozen77_publish_vehicle_selection_batch(const VehicleBatchScope *scope)
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
        index=frozen77_vehicle_selection_hash(pending->descriptor_handle,pending->vehicle)&
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
            ++d->selection_bank_count[bank];
            frozen77_index_vehicle_selection_slot(bank,slot,pending->descriptor_handle);
        }
        r->descriptor_handle=pending->descriptor_handle;
        r->vehicle=pending->vehicle;
        r->distance_squared=pending->distance_squared;
        memcpy(r->position,pending->position,sizeof(r->position));
        r->position_valid=pending->position_valid;
        r->publish_manager_call=manager_call;
        r->publish_renderer_epoch=renderer_epoch;
        r->tick=pending->tick;
        ++r->observations;
        frozen77_publish_vehicle_quality_identity(bank,renderer_epoch,pending->descriptor_handle,
            pending->vehicle,pending->distance_squared);
    }
    vehicle_selection_unlock();
    InterlockedDecrement(&d->selection_publish_inflight);
}

static int frozen77_lookup_vehicle_quality_identity(uint64_t handle,LONG renderer_epoch,
    uint64_t *vehicle,float *distance_squared)
{
    VehicleLightDiagnosticState *d=&g_shadow_engine.vehicle_diagnostics;
    uint64_t matched_vehicle=0;
    float matched_distance=0.0f;
    uint32_t delta;
    uint32_t index=frozen77_vehicle_selection_hash(handle,0U)&
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
