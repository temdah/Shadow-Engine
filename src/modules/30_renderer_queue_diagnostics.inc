/* Module 30: whole-record face admission, queue/tail validation, residency
 * sampling, F8/F9 queue snapshots, and face-cost safety. */
#include "../internal/overhead_probe.h"
#include "../internal/detail_window.h"
typedef struct RendererQueueReadScope {
    uintptr_t begin,end;
} RendererQueueReadScope;

/* Only queue-owned fields may share this stack-local validation. The caller
 * makes no native calls while scanning and discards the scope on return.
 * Resource objects keep their own checks: sharing a queue's lifetime never
 * proves the lifetime or readability of an object reached through a slot. */
static int renderer_queue_readable(RendererQueueReadScope *scope,
    const void *address,size_t size)
{
    MEMORY_BASIC_INFORMATION info;
    uintptr_t begin=(uintptr_t)address,end=begin+size,region_begin,region_end;
    if(!address || !size || end<begin) return 0;
    if(begin>=scope->begin && end<=scope->end) return 1;
    if(!VirtualQuery(address,&info,sizeof(info))) return 0;
    if(info.State!=MEM_COMMIT || (info.Protect&(PAGE_NOACCESS|PAGE_GUARD))) return 0;
    region_begin=(uintptr_t)info.BaseAddress;
    region_end=region_begin+info.RegionSize;
    if(region_end<region_begin || begin<region_begin || end>region_end) return 0;
    /* VirtualQuery's exact forward region, never the wider AllocationBase. */
    scope->begin=region_begin;
    scope->end=region_end;
    return 1;
}

static void trace_renderer_capacity(void *queue)
{
    unsigned char *base=(unsigned char *)queue;
    uint32_t entries,i,faces=0,active=0;
    DWORD now=GetTickCount();
    if(now-g_shadow_engine.renderer.last_residency_probe_tick<250U) return;
    g_shadow_engine.renderer.last_residency_probe_tick=now;
    if(!base || !readable_memory(base+RENDER_QUEUE_COUNT_OFFSET,sizeof(uint32_t)))
        return;
    entries=*(uint32_t *)(base+RENDER_QUEUE_COUNT_OFFSET);
    if(entries>TOTAL_LOCAL_MAPS+1U) entries=TOTAL_LOCAL_MAPS+1U;
    for(i=0;i<entries;++i) {
        unsigned char *entry=base+(uint64_t)i*RENDER_QUEUE_ENTRY_BYTES;
        uint32_t entry_faces;
        if(!readable_memory(entry+0x20A0,0x10)) break;
        entry_faces=*(uint32_t *)(entry+0x20A0);
        if(entry_faces>6U) entry_faces=0;
        if(entry_faces) ++active;
        faces+=entry_faces;
    }
    if(faces>ORIGINAL_LOCAL_MAPS &&
       InterlockedCompareExchange(&g_shadow_engine.renderer.extra_face_use_seen,1,0)==0) {
        append_log("STAGE_F_EXTRA_CAPACITY_FIRST_USE rendererCall=%ld queue=%p entries=%u activeEntries=%u faces=%u firstExtraMap=%u highestUsedMap=%u physicalMaps=%u overflow=%u",
            (long)g_shadow_engine.renderer.renderer_calls,queue,entries,active,faces,
            ORIGINAL_LOCAL_MAPS,faces-1U,TOTAL_LOCAL_MAPS,
            faces>TOTAL_LOCAL_MAPS?faces-TOTAL_LOCAL_MAPS:0U);
    }
    /* Public builds keep a low-frequency capacity heartbeat for support logs.
     * Immediate first-use, overflow, clamp, and tail-integrity diagnostics are
     * reported separately, so successful play does not need a line per second. */
    if(now-g_shadow_engine.renderer.last_residency_log_tick>=30000U) {
#if SHADOW_ENGINE_INTERNAL_DIAGNOSTICS
        PeriodicResidencySummary snapshot={0};
        DetailWindowStatus detail=detail_window_status();
        snapshot.detail_epoch=detail.epoch; snapshot.detail_phase=detail.phase;
        snapshot.queue_identity=(uint64_t)(uintptr_t)queue;
        snapshot.captured_tick=now;
        snapshot.entries=entries; snapshot.active_entries=active; snapshot.faces=faces;
        snapshot.renderer_call=g_shadow_engine.renderer.renderer_calls;
        snapshot.b4=g_shadow_engine.renderer.observed_scheduler_b4;
        snapshot.a8=g_shadow_engine.renderer.observed_cache_a8;
        snapshot.latest_candidates=g_shadow_engine.renderer.latest_candidate_count;
        snapshot.peak_candidates=g_shadow_engine.renderer.peak_candidate_count;
        snapshot.latest_admitted=g_shadow_engine.renderer.latest_admitted_count;
        snapshot.peak_admitted=g_shadow_engine.renderer.peak_admitted_count;
        snapshot.latest_dynamic_or_special=g_shadow_engine.renderer.latest_dynamic_or_special_count;
        snapshot.latest_cached_bindings=g_shadow_engine.renderer.latest_cached_binding_count;
        snapshot.peak_cached_bindings=g_shadow_engine.renderer.peak_cached_binding_count;
        snapshot.latest_owner_records=g_shadow_engine.renderer.latest_owner_record_count;
        snapshot.peak_owner_records=g_shadow_engine.renderer.peak_owner_record_count;
        snapshot.admission_scan_state=g_shadow_engine.renderer.admission_scan_state;
        snapshot.owner_reserve_state=g_shadow_engine.renderer.owner_reserve_state;
        snapshot.owner_capacity=g_shadow_engine.renderer.owner_capacity_after;
        snapshot.owner_base_changes=g_shadow_engine.renderer.owner_base_changed_during_manager;
        periodic_summary_publish(&snapshot);
#else
        append_log("STAGE_F_RESIDENCY rendererCall=%ld queue=%p entries=%u activeEntries=%u faces=%u extraMapsInUse=%u physicalMaps=%u overflow=%u B4=%ld A8=%ld latestCandidates=%ld peakCandidates=%ld latestAdmitted=%ld peakAdmitted=%ld latestDynamicOrSpecial=%ld latestCachedBindings=%ld peakCachedBindings=%ld latestOwnerRecords=%ld peakOwnerRecords=%ld admissionScanState=%ld ownerReserveState=%ld ownerCapacity=%ld ownerBaseChangesDuringManager=%ld",
            (long)g_shadow_engine.renderer.renderer_calls,queue,entries,active,faces,
            faces>ORIGINAL_LOCAL_MAPS?faces-ORIGINAL_LOCAL_MAPS:0U,
            TOTAL_LOCAL_MAPS,
            faces>TOTAL_LOCAL_MAPS?faces-TOTAL_LOCAL_MAPS:0U,
            (long)g_shadow_engine.renderer.observed_scheduler_b4,(long)g_shadow_engine.renderer.observed_cache_a8,
            (long)g_shadow_engine.renderer.latest_candidate_count,(long)g_shadow_engine.renderer.peak_candidate_count,
            (long)g_shadow_engine.renderer.latest_admitted_count,(long)g_shadow_engine.renderer.peak_admitted_count,
            (long)g_shadow_engine.renderer.latest_dynamic_or_special_count,
            (long)g_shadow_engine.renderer.latest_cached_binding_count,
            (long)g_shadow_engine.renderer.peak_cached_binding_count,
            (long)g_shadow_engine.renderer.latest_owner_record_count,
            (long)g_shadow_engine.renderer.peak_owner_record_count,
            (long)g_shadow_engine.renderer.admission_scan_state,
            (long)g_shadow_engine.renderer.owner_reserve_state,(long)g_shadow_engine.renderer.owner_capacity_after,
            (long)g_shadow_engine.renderer.owner_base_changed_during_manager);
#endif
        g_shadow_engine.renderer.last_residency_log_tick=now;
    }
}
/* The scheduler's B4 limit counts admitted dynamic-light records, but the
 * renderer indexes local shadow maps by the sum of every record's face count.
 * Point/cascade records can therefore make admitted positions request more
 * than the real local-map count. Keep complete records in priority order and
 * temporarily expose only the largest prefix whose whole faces fit.  Restoring
 * the queue count after rendering leaves the native cleanup/ownership path in
 * control of every record that the manager built. */
static uint32_t clamp_renderer_queue_to_face_budget(void *queue,
    uint32_t *original_entries,uint32_t *requested_faces,
    uint32_t *admitted_faces)
{
    unsigned char *base=(unsigned char *)queue;
    RendererQueueReadScope read_scope={0};
    uint32_t entries,index,faces=0,safe_entries=0;
    if(original_entries) *original_entries=0;
    if(requested_faces) *requested_faces=0;
    if(admitted_faces) *admitted_faces=0;
    if(!base || !renderer_queue_readable(&read_scope,base+RENDER_QUEUE_COUNT_OFFSET,
                                 sizeof(uint32_t))) return 0;
    entries=*(uint32_t *)(base+RENDER_QUEUE_COUNT_OFFSET);
    if(original_entries) *original_entries=entries;
    if(entries>TOTAL_LOCAL_MAPS+1U) entries=TOTAL_LOCAL_MAPS+1U;
    for(index=0;index<entries;++index) {
        unsigned char *entry=base+(uint64_t)index*RENDER_QUEUE_ENTRY_BYTES;
        uint32_t count;
        if(!renderer_queue_readable(&read_scope,entry+0x20A0,sizeof(uint32_t))) break;
        count=*(uint32_t *)(entry+0x20A0);
        if(count>6U) break;
        if(faces+count>TOTAL_LOCAL_MAPS) break;
        faces+=count;
        safe_entries=index+1U;
    }
    if(requested_faces) {
        uint32_t total=faces;
        for(;index<entries;++index) {
            unsigned char *entry=base+(uint64_t)index*RENDER_QUEUE_ENTRY_BYTES;
            uint32_t count;
            if(!renderer_queue_readable(&read_scope,entry+0x20A0,sizeof(uint32_t))) break;
            count=*(uint32_t *)(entry+0x20A0);
            if(count>6U) break;
            total+=count;
        }
        *requested_faces=total;
    }
    if(admitted_faces) *admitted_faces=faces;
    if(safe_entries<entries) {
        *(uint32_t *)(base+RENDER_QUEUE_COUNT_OFFSET)=safe_entries;
        return safe_entries;
    }
    return entries;
}
#if SHADOW_ENGINE_INTERNAL_DIAGNOSTICS
static void trace_queue_tail_integrity(void *queue,const char *phase,
    volatile LONG *reported,uint32_t entries,uint32_t faces,LONG epoch,unsigned kind)
{
    unsigned char *base=(unsigned char *)queue;
    void *tail=NULL;
    int invalid=0;
    if(!detail_window_enter(epoch,kind)) return;
    if(!base || !readable_memory(base+RENDER_QUEUE_TAIL_OFFSET,
                                 sizeof(void *))) goto leave;
    tail=*(void **)(base+RENDER_QUEUE_TAIL_OFFSET);
    invalid=tail && !readable_memory(tail,0x38U) &&
        InterlockedCompareExchange(reported,1,0)==0;
leave:
    detail_window_leave();
    /* Urgent evidence stays synchronous, after optional metadata ownership.
     * tail is an identity token for formatting only; never read after leave. */
    if(invalid) {
        append_log("STAGE_G_QUEUE_TAIL_INVALID phase=%s queue=%p tail=%p entries=%u faces=%u tailOffset=0x%X builderReadRva=0x30D1BD directWriterRva=0x3CA76F mutation=0",
            phase,queue,tail,entries,faces,RENDER_QUEUE_TAIL_OFFSET);
    }
}

static void log_manual_queue_capture(const char *state,void *manager,void *queue,
    uint32_t original_entries,uint32_t requested_faces,
    uint32_t admitted_entries,uint32_t admitted_faces)
{
    unsigned char *base=(unsigned char *)queue;
    uint32_t scan_entries=original_entries;
    uint32_t entry_index,map_cursor=0;
    uint32_t stable_limit=shadow_policy_stable_limit();
    uint32_t extra_slots=vehicle_selection_extra_slots();
    void *tail=NULL;
    LONG serial=InterlockedCompareExchange(&g_shadow_engine.renderer.renderer_calls,0,0);
    DetailWindowStatus detail=detail_window_status();
    append_log("STAGE_DETAIL_QUEUE_COVERAGE state=%s captureSerial=%ld detailEpoch=%ld detailPhase=%ld admissionAndTailCoverage=%s latestMayBeStale=1 peaksScope=recordedWindowsOnly faceSafetyCountersScope=continuous",
        state,(long)serial,(long)detail.epoch,(long)detail.phase,
        detail.phase<0 ? "unknown":(detail.epoch ? "recordedWindowsOnly":"notRecorded"));
    if(scan_entries>TOTAL_LOCAL_MAPS+1U) scan_entries=TOTAL_LOCAL_MAPS+1U;
    if(base && readable_memory(base+RENDER_QUEUE_TAIL_OFFSET,sizeof(void *)))
        tail=*(void **)(base+RENDER_QUEUE_TAIL_OFFSET);
    append_log("STAGE_J_MANUAL_CAPTURE_BEGIN serial=%ld admissionAndTailScope=recordedWindowsOnly latestMayBeStale=1 state=%s rendererCall=%ld manager=%p queue=%p tail=%p originalEntries=%u requestedFaces=%u admittedEntries=%u admittedFaces=%u faceBudgetClamps=%ld faceBudgetDroppedEntries=%ld faceBudgetPeakRequested=%ld B4=%ld A8=%ld latestCandidates=%ld peakCandidates=%ld latestManagerAdmitted=%ld peakManagerAdmitted=%ld latestDynamicOrSpecial=%ld latestCachedBindings=%ld peakCachedBindings=%ld latestOwnerRecords=%ld peakOwnerRecords=%ld admissionScanState=%ld ownerReserveState=%ld ownerCapacity=%ld ownerBaseChangesDuringManager=%ld limiterEnabled=%ld limiterCalls=%ld limiterActiveCalls=%ld limiterBypassDisabled=%ld limiterFailOpenCalls=%ld limiterFailUnavailable=%ld limiterFailSnapshotLock=%ld limiterFailPublisherOverlap=%ld limiterFailGapEpoch=%ld limiterFailOverflowEpoch=%ld limiterFailCandidateChain=%ld limiterDegradedCalls=%ld limiterDegradedPublisherOverlap=%ld limiterDegradedGapEpoch=%ld limiterDegradedOverflowEpoch=%ld",
        (long)serial,state,(long)g_shadow_engine.renderer.renderer_calls,manager,queue,tail,
        original_entries,requested_faces,admitted_entries,admitted_faces,
        (long)g_shadow_engine.renderer.face_budget_clamps,
        (long)g_shadow_engine.renderer.face_budget_dropped_entries,
        (long)g_shadow_engine.renderer.face_budget_peak_requested,
        (long)g_shadow_engine.renderer.observed_scheduler_b4,(long)g_shadow_engine.renderer.observed_cache_a8,
        (long)g_shadow_engine.renderer.latest_candidate_count,(long)g_shadow_engine.renderer.peak_candidate_count,
        (long)g_shadow_engine.renderer.latest_admitted_count,(long)g_shadow_engine.renderer.peak_admitted_count,
        (long)g_shadow_engine.renderer.latest_dynamic_or_special_count,
        (long)g_shadow_engine.renderer.latest_cached_binding_count,
        (long)g_shadow_engine.renderer.peak_cached_binding_count,
        (long)g_shadow_engine.renderer.latest_owner_record_count,
        (long)g_shadow_engine.renderer.peak_owner_record_count,
        (long)g_shadow_engine.renderer.admission_scan_state,
        (long)g_shadow_engine.renderer.owner_reserve_state,(long)g_shadow_engine.renderer.owner_capacity_after,
        (long)g_shadow_engine.renderer.owner_base_changed_during_manager,
        (long)shadow_policy_enabled(SHADOW_POLICY_DISABLE_LIMITER),
        (long)g_shadow_engine.vehicle_diagnostics.limiter_calls,
        (long)g_shadow_engine.vehicle_diagnostics.limiter_active_calls,
        (long)g_shadow_engine.vehicle_diagnostics.limiter_bypass_disabled,
        (long)g_shadow_engine.vehicle_diagnostics.limiter_fail_open_calls,
        (long)g_shadow_engine.vehicle_diagnostics.limiter_fail_unavailable,
        (long)g_shadow_engine.vehicle_diagnostics.limiter_fail_snapshot_lock,
        (long)g_shadow_engine.vehicle_diagnostics.limiter_fail_publisher_overlap,
        (long)g_shadow_engine.vehicle_diagnostics.limiter_fail_gap_epoch,
        (long)g_shadow_engine.vehicle_diagnostics.limiter_fail_overflow_epoch,
        (long)g_shadow_engine.vehicle_diagnostics.limiter_fail_candidate_chain,
        (long)g_shadow_engine.vehicle_diagnostics.limiter_degraded_calls,
        (long)g_shadow_engine.vehicle_diagnostics.limiter_degraded_publisher_overlap,
        (long)g_shadow_engine.vehicle_diagnostics.limiter_degraded_gap_epoch,
        (long)g_shadow_engine.vehicle_diagnostics.limiter_degraded_overflow_epoch);
    append_log("STAGE_J_VEHICLE_POLICY serial=%ld stableVehicles=%u overrideVehicles=%u keepVehicles=%u residencyAppliedCalls=%ld residencyLockBypass=%ld residencyInitializations=%ld primaryHandoffs=%ld residencyPendingHolds=%ld residencyDropped=%ld limiterSuppressed=%ld limiterPeakSuppressed=%ld outgoingHoldMs=%u admissionDelayMs=%u residencyDistanceSquaredRatio=%u/%u qualityMaxDimension=%u qualityOverflow=%ld qualityMutatedCalls=%ld qualityMutatedType1=%ld qualityMutatedType3=%ld qualityMutatedEntries=%ld qualityUnknownEntries=%ld qualityAmbiguousEntries=%ld classificationScope=inspectedEntries policePriority=0 policeRuntimeTagMapped=0",
        (long)serial,stable_limit,
        extra_slots,stable_limit+extra_slots,
        (long)g_shadow_engine.vehicle_diagnostics.residency_applied_calls,
        (long)g_shadow_engine.vehicle_diagnostics.residency_lock_bypass,
        (long)g_shadow_engine.vehicle_diagnostics.residency_initializations,
        (long)g_shadow_engine.vehicle_diagnostics.residency_replacements,
        (long)g_shadow_engine.vehicle_diagnostics.residency_pending_holds,
        (long)g_shadow_engine.vehicle_diagnostics.residency_dropped,
        (long)g_shadow_engine.vehicle_diagnostics.limiter_suppressed,
        (long)g_shadow_engine.vehicle_diagnostics.limiter_peak_suppressed,
        VEHICLE_TRANSITION_HOLD_MS,
        0U,VEHICLE_RESIDENCY_DISTANCE_NUMERATOR,
        VEHICLE_RESIDENCY_DISTANCE_DENOMINATOR,shadow_policy_resolution(),
        (long)g_shadow_engine.vehicle_diagnostics.quality_overflow,
        (long)g_shadow_engine.vehicle_diagnostics.quality_mutated_calls,
        (long)g_shadow_engine.vehicle_diagnostics.quality_mutated_type1,
        (long)g_shadow_engine.vehicle_diagnostics.quality_mutated_type3,
        (long)g_shadow_engine.vehicle_diagnostics.quality_mutated_entries,
        (long)g_shadow_engine.vehicle_diagnostics.quality_unknown_entries,
        (long)g_shadow_engine.vehicle_diagnostics.quality_ambiguous_entries);
    {
        append_log("STAGE_J_WORLD_QUALITY serial=%ld enabled=%u selectedCap=%u mode=fixed mutatedCalls=%ld mutatedEntries=%ld skippedExactVehicle=%ld skippedAmbiguous=%ld skippedUnreadable=%ld classificationScope=inspectedEntries sunType0Untouched=1",
            (long)serial,(unsigned)shadow_policy_world_enabled(),
            shadow_policy_world_resolution(),
            (long)g_shadow_engine.world_quality.mutated_calls,
            (long)g_shadow_engine.world_quality.mutated_entries,
            (long)g_shadow_engine.world_quality.skipped_vehicle,
            (long)g_shadow_engine.world_quality.skipped_ambiguous,
            (long)g_shadow_engine.world_quality.skipped_unreadable);
    }
    append_log("STAGE_J_QUALITY_INSPECTION serial=%ld scope=vehicle classificationScope=inspectedEntries underCapEntries=%ld inspectedEntries=%ld fastCheckUnavailableCalls=%ld workRegister=Q1,Q2",
        (long)serial,
        (long)InterlockedCompareExchange(&g_shadow_engine.vehicle_diagnostics.quality_under_cap_entries,0,0),
        (long)InterlockedCompareExchange(&g_shadow_engine.vehicle_diagnostics.quality_inspected_entries,0,0),
        (long)InterlockedCompareExchange(&g_shadow_engine.vehicle_diagnostics.quality_fast_check_unavailable_calls,0,0));
    append_log("STAGE_J_QUALITY_INSPECTION serial=%ld scope=world classificationScope=inspectedEntries underCapEntries=%ld inspectedEntries=%ld fastCheckUnavailableCalls=%ld workRegister=Q1,Q2",
        (long)serial,
        (long)InterlockedCompareExchange(&g_shadow_engine.world_quality.under_cap_entries,0,0),
        (long)InterlockedCompareExchange(&g_shadow_engine.world_quality.inspected_entries,0,0),
        (long)InterlockedCompareExchange(&g_shadow_engine.world_quality.fast_check_unavailable_calls,0,0));
    append_log("STAGE_J_MANUAL_CAPTURE_HANDLES serial=%ld mapCount=%u handle0=%08X handleNativeLast=%08X handleExternalFirst=%08X handleLast=%08X",
        (long)serial,TOTAL_LOCAL_MAPS,
        g_shadow_engine.resources.shadow_map_ids[0],
        g_shadow_engine.resources.shadow_map_ids[ORIGINAL_LOCAL_MAPS-1U],
        g_shadow_engine.resources.shadow_map_ids[ORIGINAL_LOCAL_MAPS],
        g_shadow_engine.resources.shadow_map_ids[TOTAL_LOCAL_MAPS-1U]);
    if(!base) {
        append_log("STAGE_J_MANUAL_CAPTURE_END serial=%ld state=%s scannedEntries=0 scannedFaces=0 queueReadable=0",
            (long)serial,state);
        log_manual_external_slice_snapshot(state,serial);
        return;
    }
    for(entry_index=0;entry_index<scan_entries;++entry_index) {
        unsigned char *entry=base+(uint64_t)entry_index*RENDER_QUEUE_ENTRY_BYTES;
        void *objects[6]={0};
        uint32_t costs[6]={0};
        uint32_t face_index,face_count=0,raw_faces=0,entry_readable=0;
        if(readable_memory(entry+0x20A0,sizeof(uint32_t))) {
            entry_readable=1;
            raw_faces=*(uint32_t *)(entry+0x20A0);
            face_count=raw_faces<=6U ? raw_faces : 0U;
        }
        for(face_index=0;face_index<face_count;++face_index) {
            unsigned char *slot=entry+0x7D8+(uint64_t)face_index*0x820U;
            if(!readable_memory(slot,sizeof(void *))) break;
            objects[face_index]=*(void **)slot;
            if(objects[face_index] && readable_memory(
               (unsigned char *)objects[face_index]+0x20,sizeof(uint32_t)))
                costs[face_index]=*(uint32_t *)((unsigned char *)objects[face_index]+0x20);
        }
        append_log("STAGE_J_MANUAL_CAPTURE_ENTRY serial=%ld entry=%u entryReadable=%u rawFaces=%u mappedFaces=%u mapStart=%u mapEndExclusive=%u f0=%p/%u f1=%p/%u f2=%p/%u f3=%p/%u f4=%p/%u f5=%p/%u",
            (long)serial,entry_index,entry_readable,raw_faces,face_count,
            map_cursor,map_cursor+face_count,
            objects[0],costs[0],objects[1],costs[1],
            objects[2],costs[2],objects[3],costs[3],
            objects[4],costs[4],objects[5],costs[5]);
        map_cursor+=face_count;
    }
    append_log("STAGE_J_MANUAL_CAPTURE_END serial=%ld state=%s scannedEntries=%u scannedFaces=%u queueReadable=1",
        (long)serial,state,scan_entries,map_cursor);
    log_manual_external_slice_snapshot(state,serial);
}

static void poll_manual_capture(void *manager,void *queue,
    uint32_t original_entries,uint32_t requested_faces,
    uint32_t admitted_entries,uint32_t admitted_faces)
{
    LONG requests=InterlockedExchange(&g_shadow_engine.internal_tools.capture_requests,0);
    if(requests&SHADOW_CAPTURE_ARM) atomic_or_long(&g_shadow_engine.intersection.requests,1);
    intersection_poll(requests&SHADOW_CAPTURE_MARK,requests&SHADOW_CAPTURE_FINISH);
    if(requests&SHADOW_CAPTURE_MARK)
        log_manual_queue_capture("BLACK_F8",manager,queue,original_entries,
            requested_faces,admitted_entries,admitted_faces);
    if(requests&SHADOW_CAPTURE_FINISH)
        log_manual_queue_capture("RECOVERED_F9",manager,queue,original_entries,
            requested_faces,admitted_entries,admitted_faces);
    if(requests&SHADOW_CAPTURE_VEHICLE) arm_vehicle_candidate_capture();
    poll_vehicle_candidate_capture_key();
}
#endif

static void __fastcall hooked_renderer_queue(void *manager,void *queue,
    uint64_t argument3,void *argument4,void *argument5,uint64_t argument6,
    uint64_t argument7,uint64_t argument8,uint64_t argument9)
{
    OVERHEAD_BEGIN(renderer_timing,OH_RENDERER_PATCH);
#if SHADOW_ENGINE_INTERNAL_DIAGNOSTICS
    LONG tail_epoch=detail_window_epoch();
#endif
    void *result_record=argument7 ?
        (unsigned char *)(uintptr_t)argument7-0x70U:NULL;
#if SHADOW_ENGINE_INTERNAL_DIAGNOSTICS
    DetailProducerTicket producer_ticket;
    WorkloadTimingTicket workload_timing={0};
    PopulationTicket population_ticket={0};
    VehicleQueueTicket vehicle_capture;
#endif
    uint32_t original_entries=0;
#if SHADOW_ENGINE_INTERNAL_DIAGNOSTICS
    uint32_t requested_faces=0,admitted_faces=0;
#endif
    uint32_t admitted_entries;
    OVERHEAD_BEGIN(clamp_timing,OH_QUEUE_CLAMP);
    admitted_entries=clamp_renderer_queue_to_face_budget(queue,
#if SHADOW_ENGINE_INTERNAL_DIAGNOSTICS
        &original_entries,&requested_faces,&admitted_faces);
#else
        &original_entries,NULL,NULL);
#endif
    OVERHEAD_END(clamp_timing);
#if SHADOW_ENGINE_INTERNAL_DIAGNOSTICS
    OVERHEAD_BEGIN(tail_before_timing,OH_QUEUE_TAIL);
    trace_queue_tail_integrity(queue,"beforeOriginal",
        &g_shadow_engine.renderer.tail_invalid_before_renderer,original_entries,requested_faces,
        tail_epoch,DETAIL_WINDOW_TAIL_BEFORE);
    OVERHEAD_END(tail_before_timing);
    if(requested_faces>(uint32_t)InterlockedCompareExchange(
           &g_shadow_engine.renderer.face_budget_peak_requested,0,0))
        InterlockedExchange(&g_shadow_engine.renderer.face_budget_peak_requested,(LONG)requested_faces);
    if(original_entries>admitted_entries) {
        InterlockedIncrement(&g_shadow_engine.renderer.face_budget_clamps);
        atomic_add_long(&g_shadow_engine.renderer.face_budget_dropped_entries,
            (LONG)(original_entries-admitted_entries));
    }
    OVERHEAD_BEGIN(capture_begin_timing,OH_CAPTURE_OBSERVER);
    poll_manual_capture(manager,queue,original_entries,requested_faces,
        admitted_entries,admitted_faces);
    vehicle_capture=begin_vehicle_queue_sample(queue,original_entries,admitted_entries);
    workload_timing=workload_queue(queue,original_entries,admitted_entries,requested_faces,admitted_faces);
    population_ticket=population_queue_begin(queue,original_entries,admitted_entries,
        requested_faces,admitted_faces,g_shadow_engine.renderer.renderer_calls);
    OVERHEAD_END(capture_begin_timing);
#endif
    OVERHEAD_BEGIN(vehicle_quality_timing,OH_VEHICLE_QUALITY);
#if SHADOW_ENGINE_INTERNAL_DIAGNOSTICS
    if(vehicle_capture.capture.active)
        apply_vehicle_shadow_quality_with_sample(queue,admitted_entries,
            &g_shadow_engine.vehicle_diagnostics.queue_samples[vehicle_capture.capture.sample_index]);
    else
#endif
    apply_vehicle_shadow_quality(queue,admitted_entries);
    OVERHEAD_END(vehicle_quality_timing);
    OVERHEAD_BEGIN(world_quality_timing,OH_WORLD_QUALITY);
    apply_world_shadow_quality(queue,admitted_entries);
    OVERHEAD_END(world_quality_timing);
#if SHADOW_ENGINE_INTERNAL_DIAGNOSTICS
    sample_vehicle_queue_policy_request(vehicle_capture);
    population_queue_prepared(population_ticket,queue);
#endif
    /* Establish publication ownership before even synchronous Slice jobs can
     * finish. The later frame builder only observes this already-open cycle. */
    OVERHEAD_BEGIN(producer_timing,OH_PRODUCER_OPEN);
    begin_external_slice_producer_cycle(result_record);
    OVERHEAD_END(producer_timing);
#if SHADOW_ENGINE_INTERNAL_DIAGNOSTICS
    OVERHEAD_BEGIN(capture_producer_timing,OH_CAPTURE_OBSERVER);
    intersection_observe(IX_PRODUCER,result_record,0,NULL,0,0);
    intersection_queue(result_record,queue,original_entries,admitted_entries,
        requested_faces,admitted_faces);
    OVERHEAD_END(capture_producer_timing);
    OVERHEAD_BEGIN(completion_begin_timing,OH_COMPLETION_BEGIN);
    producer_ticket=completion_diagnostic_begin_producer_batch(result_record);
    OVERHEAD_END(completion_begin_timing);
    vehicle_capture_renderer_phase(vehicle_capture,0);
#endif
    OVERHEAD_PAUSE(renderer_timing);
    OVERHEAD_BEGIN(native_renderer_timing,OH_RENDERER_NATIVE);
#if SHADOW_ENGINE_INTERNAL_DIAGNOSTICS
    workload_timing_start(&workload_timing);
#endif
    g_shadow_engine.hooks.original_renderer_queue(manager,queue,argument3,argument4,argument5,
        argument6,argument7,argument8,argument9);
#if SHADOW_ENGINE_INTERNAL_DIAGNOSTICS
    workload_timing_stop(&workload_timing,0);
#endif
    OVERHEAD_END(native_renderer_timing);
    OVERHEAD_RESUME(renderer_timing);
#if SHADOW_ENGINE_INTERNAL_DIAGNOSTICS
    workload_timing_release(&workload_timing);
    vehicle_capture_renderer_phase(vehicle_capture,1);
    OVERHEAD_BEGIN(completion_seal_timing,OH_COMPLETION_SEAL);
    completion_diagnostic_seal_producer_batch(result_record,&producer_ticket);
    OVERHEAD_END(completion_seal_timing);
    OVERHEAD_BEGIN(capture_finish_timing,OH_CAPTURE_OBSERVER);
    finish_vehicle_queue_sample(vehicle_capture);
    population_queue_end(population_ticket,queue);
    intersection_observe(IX_PRODUCER_RETURN,result_record,0,NULL,0,0);
    OVERHEAD_END(capture_finish_timing);
    OVERHEAD_BEGIN(tail_after_timing,OH_QUEUE_TAIL);
    trace_queue_tail_integrity(queue,"afterOriginalBeforeCountRestore",
        &g_shadow_engine.renderer.tail_invalid_after_renderer,original_entries,requested_faces,
        tail_epoch,DETAIL_WINDOW_TAIL_AFTER);
    OVERHEAD_END(tail_after_timing);
#endif
    InterlockedIncrement(&g_shadow_engine.renderer.renderer_calls);
#if SHADOW_ENGINE_INTERNAL_DIAGNOSTICS
    OVERHEAD_BEGIN(capacity_timing,OH_QUEUE_CAPACITY);
    trace_renderer_capacity(queue);
    OVERHEAD_END(capacity_timing);
#endif
    if(queue && original_entries>admitted_entries)
        *(uint32_t *)((unsigned char *)queue+RENDER_QUEUE_COUNT_OFFSET)=
            original_entries;
    OVERHEAD_END(renderer_timing);
}

static uint32_t __fastcall hooked_face_cost_reader(void *queue)
{
    OVERHEAD_BEGIN(face_cost_timing,OH_FACE_COST_READER);
    unsigned char *base=(unsigned char *)queue;
    RendererQueueReadScope read_scope={0};
    uint32_t entries,entry_index,total=0;
    if(!base || !renderer_queue_readable(&read_scope,base+RENDER_QUEUE_COUNT_OFFSET,sizeof(uint32_t))) {
        OVERHEAD_END(face_cost_timing);
        return 0;
    }
    entries=*(uint32_t *)(base+RENDER_QUEUE_COUNT_OFFSET);
    if(entries>TOTAL_LOCAL_MAPS) entries=TOTAL_LOCAL_MAPS;
    for(entry_index=0;entry_index<entries;++entry_index) {
        unsigned char *entry=base+(uint64_t)entry_index*RENDER_QUEUE_ENTRY_BYTES;
        uint32_t face_count,face_index;
        if(!renderer_queue_readable(&read_scope,entry+0x20A0,sizeof(uint32_t))) break;
        face_count=*(uint32_t *)(entry+0x20A0);
        if(face_count>6U) face_count=6U;
        for(face_index=0;face_index<face_count;++face_index) {
            void *object;
            unsigned char *slot=entry+0x7D8+(uint64_t)face_index*0x820U;
            if(!renderer_queue_readable(&read_scope,slot,sizeof(void *))) break;
            object=*(void **)slot;
            if(!object || !readable_memory((unsigned char *)object+0x20,
                                           sizeof(uint32_t))) {
                InterlockedIncrement(&g_shadow_engine.renderer.null_face_cost_skips);
                continue;
            }
            total+=*(uint32_t *)((unsigned char *)object+0x20);
        }
    }
    OVERHEAD_END(face_cost_timing);
    return total;
}

static uint32_t count_nonnull_objects(void **objects,uint32_t count)
{
    uint32_t index,nonzero=0;
    if(!objects || !count) return 0;
    if(!readable_memory(objects,(size_t)count*sizeof(void *))) return count;
    for(index=0;index<count;++index) if(objects[index]) ++nonzero;
    return nonzero;
}

static uint64_t build_null_mask(void **objects,uint32_t count)
{
    uint64_t mask=0;
    uint32_t index,limit=count<64U ? count:64U;
    if(!count) return 0;
    if(!objects) return limit==64U ? ~(uint64_t)0 :
        (((uint64_t)1U<<limit)-1U);
    if(!readable_memory(objects,(size_t)count*sizeof(void *))) return 0;
    for(index=0;index<limit;++index)
        if(!objects[index]) mask|=(uint64_t)1U<<index;
    return mask;
}
