/* Module 70: signature preflight, phased hook installation, startup proof,
 * exported initialization entry point, and DllMain. */
static int install_early_construction_hooks(HMODULE disrupt)
{
    unsigned char *base=(unsigned char *)disrupt;
    g_disrupt_base=base;
    if(memcmp(base+RESOURCE_CONSTRUCTOR_RVA,g_resource_prologue,
              sizeof(g_resource_prologue))!=0) {
        append_log("STAGE_D_ABORT signature=resource_constructor expectedBuildMismatch=1 noEngineMutation=1");
        return 0;
    }
    if(memcmp(base+REGISTER_PASS_RVA,g_register_pass_prologue,
              sizeof(g_register_pass_prologue))!=0) {
        append_log("STAGE_D_ABORT signature=register_pass expectedBuildMismatch=1 noEngineMutation=1");
        return 0;
    }
    if(!validate_map_range_loop_extension()) {
        append_log("STAGE_D_ABORT signature=native_map_loop expectedBuildMismatch=1 noEngineMutation=1");
        return 0;
    }
    if(!validate_downstream_sites(base)) {
        append_log("STAGE_E_ABORT signature=deferred_downstream expectedBuildMismatch=1 noEngineMutation=1");
        return 0;
    }
    append_log("STAGE_M_PREFLIGHT_COMPLETE signatures=resource,pass,mapLoop,earlyQueueLayout,deferredHandleConsumers,deferredPassIndexConsumers,deferredV1216Scheduler,deferredShadowManager,resourceWrapper,renderRecordConstructor,frameBuilder,renderRecordAcquire,renderRecordRelease,sharedTextureLookup requiredPassMaxKey=0x%04X requiredPassSlots=%u passTableVerification=readOnly resourceLifecycleTrace=bounded resourceProducerTrace=ring renderRecordLifecycleTrace=bounded tailRelocations=%u noEngineMutationBeforeThisMarker=1",
        REQUIRED_PASS_MAX_KEY,REQUIRED_PASS_TABLE_SLOTS,
        (unsigned)(sizeof(g_tail_patches)/sizeof(g_tail_patches[0])));
    if(!apply_queue_layout()) {
        append_log("STAGE_E_ABORT mutation=early_queue_layout installFailed=1 beforeQueueConstruction=1");
        return 0;
    }
    append_log("STAGE_E_EARLY_QUEUE_LAYOUT_APPLIED beforeQueueConstruction=1 physicalQueueEntries=25 allocationBytes=0x397C0 relocatedTailReferences=%u tailInitializedByNativeConstructor=1",
        (unsigned)(sizeof(g_tail_patches)/sizeof(g_tail_patches[0])));
    if(!install_map_range_loop_extension()) {
        append_log("STAGE_E_ABORT hook=native_map_loop_extension installFailed=1");
        return 0;
    }
    if(!install_detour(base+RENDER_RECORD_CONSTRUCTOR_RVA,
        g_render_record_constructor_prologue,
        sizeof(g_render_record_constructor_prologue),
        hooked_render_record_constructor,
        (void **)&g_original_render_record_constructor)) {
        append_log("STAGE_N_ABORT hook=renderRecordConstructor installFailed=1 priorEarlyMutationsAlreadyInstalled=1");
        return 0;
    }
    if(!install_detour(base+RESOURCE_CONSTRUCTOR_RVA,g_resource_prologue,
                       sizeof(g_resource_prologue),hooked_resource_constructor,
                       (void **)&g_original_resource_constructor)) {
        append_log("STAGE_D_ABORT hook=resource_constructor installFailed=1");
        return 0;
    }
    if(!install_detour(base+REGISTER_PASS_RVA,g_register_pass_prologue,
                       sizeof(g_register_pass_prologue),hooked_register_pass,
                       (void **)&g_original_register_pass)) {
        append_log("STAGE_D_ABORT hook=register_pass installFailed=1 resourceHookAlreadyInstalled=1");
        return 0;
    }
    InterlockedExchange(&g_hooks_installed,1);
    append_log("STAGE_M_EARLY_HOOKS_INSTALLED nativeLoopMaps=24 nativeManagerMaps=16 externalMaps=8 pairedExtraPasses=16 passTableAllocatorHook=0 readOnlyPassTableVerification=1 requiredPassSlots=50 earlyQueueLayout=25 renderRecordConstructorTrace=1 downstreamHandleRouting=0 downstreamPassRouting=0 schedulerChanged=0 managerHook=0");
    return 1;
}

static int install_deferred_downstream(void)
{
    HMODULE user32;
    if(InterlockedCompareExchange(&g_downstream_state,1,0)!=0) return 0;
    if(!validate_deferred_sites_after_early_queue(g_disrupt_base)) {
        append_log("STAGE_E_DOWNSTREAM_ABORT signaturesChangedDuringDeferral=1");
        InterlockedExchange(&g_downstream_state,-1);
        return 0;
    }
    if(!install_handle_relays() || !install_pass_index_relays() ||
       !apply_v1216_scheduler()) {
        append_log("STAGE_E_DOWNSTREAM_ABORT mutationOrRelayInstallFailed=1");
        InterlockedExchange(&g_downstream_state,-1);
        return 0;
    }
    g_profile_callback=(ProfileCallbackFn)(g_disrupt_base+PROFILE_CALLBACK_RVA);
    g_owner_vector_reserve=(OwnerVectorReserveFn)
        (g_disrupt_base+OWNER_VECTOR_RESERVE_RVA);
    if(!install_manager_detour()) {
        append_log("STAGE_E_DOWNSTREAM_ABORT managerHookFailed=1");
        InterlockedExchange(&g_downstream_state,-1);
        return 0;
    }
    if(!install_detour(g_disrupt_base+RENDER_QUEUE_PROCESS_RVA,
        g_renderer_queue_prologue,sizeof(g_renderer_queue_prologue),
        hooked_renderer_queue,(void **)&g_original_renderer_queue)) {
        append_log("STAGE_F_DOWNSTREAM_ABORT rendererQueueHookFailed=1 managerHookAlreadyInstalled=1");
        InterlockedExchange(&g_downstream_state,-1);
        return 0;
    }
    if(!install_detour(g_disrupt_base+FACE_COST_READER_RVA,
        g_face_cost_reader_prologue,sizeof(g_face_cost_reader_prologue),
        hooked_face_cost_reader,(void **)&g_original_face_cost_reader)) {
        append_log("STAGE_F_DOWNSTREAM_ABORT faceCostGuardHookFailed=1 priorHooksAlreadyInstalled=1");
        InterlockedExchange(&g_downstream_state,-1);
        return 0;
    }
    if(!install_detour(g_disrupt_base+SHARED_TEXTURE_LOOKUP_RVA,
        g_shared_texture_lookup_prologue,
        sizeof(g_shared_texture_lookup_prologue),
        hooked_shared_texture_lookup,
        (void **)&g_original_shared_texture_lookup)) {
        append_log("STAGE_N_DOWNSTREAM_ABORT sharedTextureLookupHookFailed=1 priorHooksAlreadyInstalled=1");
        InterlockedExchange(&g_lifecycle_hooks_state,-1);
        InterlockedExchange(&g_downstream_state,-1);
        return 0;
    }
    if(!install_detour(g_disrupt_base+FRAME_BUILDER_RVA,
        g_frame_builder_prologue,sizeof(g_frame_builder_prologue),
        hooked_frame_builder,(void **)&g_original_frame_builder)) {
        append_log("STAGE_N_DOWNSTREAM_ABORT frameBuilderHookFailed=1 sharedTextureLookupHookAlreadyInstalled=1 priorHooksAlreadyInstalled=1");
        InterlockedExchange(&g_lifecycle_hooks_state,-1);
        InterlockedExchange(&g_downstream_state,-1);
        return 0;
    }
    if(!install_detour(g_disrupt_base+RENDER_RECORD_ACQUIRE_RVA,
        g_render_record_acquire_prologue,
        sizeof(g_render_record_acquire_prologue),
        hooked_render_record_acquire,
        (void **)&g_original_render_record_acquire)) {
        append_log("STAGE_N_DOWNSTREAM_ABORT renderRecordAcquireHookFailed=1 priorLifecycleHooksAlreadyInstalled=1");
        InterlockedExchange(&g_lifecycle_hooks_state,-1);
        InterlockedExchange(&g_downstream_state,-1);
        return 0;
    }
    if(!install_detour(g_disrupt_base+RENDER_RECORD_RELEASE_RVA,
        g_render_record_release_prologue,
        sizeof(g_render_record_release_prologue),
        hooked_render_record_release,
        (void **)&g_original_render_record_release)) {
        append_log("STAGE_N_DOWNSTREAM_ABORT renderRecordReleaseHookFailed=1 priorLifecycleHooksAlreadyInstalled=1");
        InterlockedExchange(&g_lifecycle_hooks_state,-1);
        InterlockedExchange(&g_downstream_state,-1);
        return 0;
    }
    InterlockedExchange(&g_lifecycle_hooks_state,2);
    if(!install_detour(g_disrupt_base+RESOURCE_SUBMIT_RVA,
        g_resource_submit_prologue,sizeof(g_resource_submit_prologue),
        hooked_resource_submit,(void **)&g_original_resource_submit)) {
        append_log("STAGE_F_DOWNSTREAM_ABORT resourceSubmitGuardHookFailed=1 priorHooksAlreadyInstalled=1");
        InterlockedExchange(&g_downstream_state,-1);
        return 0;
    }
    if(!install_detour(g_disrupt_base+RESOURCE_WRAPPER_RVA,
        g_resource_wrapper_prologue,sizeof(g_resource_wrapper_prologue),
        hooked_resource_wrapper,(void **)&g_original_resource_wrapper)) {
        append_log("STAGE_F_DOWNSTREAM_ABORT resourceProducerHookFailed=1 resourceSubmitGuardAlreadyInstalled=1 priorHooksAlreadyInstalled=1");
        InterlockedExchange(&g_downstream_state,-1);
        return 0;
    }
    user32=GetModuleHandleW(L"user32.dll");
    if(!user32) user32=LoadLibraryW(L"user32.dll");
    if(user32) g_get_async_key_state=(GetAsyncKeyStateFn)
        GetProcAddress(user32,"GetAsyncKeyState");
    InterlockedExchange(&g_renderer_hook_state,2);
    InterlockedExchange(&g_routing_state,2);
    InterlockedExchange(&g_downstream_state,2);
    append_log("STAGE_M_DOWNSTREAM_INSTALLED afterConstructors=1 stabilizationMs=%u physicalQueueEntries=25 queueLayoutAppliedBeforeConstruction=1 physicalFaceBudget=24 wholeRecordPriorityPrefix=1 queueCountRestoredAfterRender=1 relocatedTailReferences=%u handleConsumers=3 passIndexConsumers=2 schedulerInit=16 schedulerFloor=16 schedulerUpper=16 schedulerUpperRva=0x2E9C3B cacheOwnerCeiling=4 ownerVectorPhysicalReserve=8 ownerRecordStride=0x700 reserveRva=0x319160 rendererCapacityProbe=1 queueTailIntegrityProbe=1 nullFaceCostGuard=1 nullResourceAcquireReleaseCompaction=1 boundedResourceNullTraceFirst=%u boundedResourceNullTracePeriod=%u resourceProducerTraceSlots=%u resourceProducerCallerRva=1 renderRecordLifecycleHooks=constructor,builder,lookup,acquire,release lifecycleRecordSlots=%u lifecycleThreadSlots=%u manualBlackWorldCapture=%u blackKey=F8 recoveredKey=F9 candidateReject=0 firstManagerProfileCallback=1 fullCandidatePassthrough=1 filtering=0",
        DOWNSTREAM_STABILIZE_MS,
        (unsigned)(sizeof(g_tail_patches)/sizeof(g_tail_patches[0])),
        RESOURCE_TRACE_INITIAL_EVENTS,RESOURCE_TRACE_PERIOD,
        RESOURCE_PRODUCER_SLOTS,LIFECYCLE_RECORD_SLOTS,
        LIFECYCLE_THREAD_SLOTS,
        g_get_async_key_state ? 1U : 0U);
    return 1;
}

static void log_handle_snapshot(void)
{
    append_log("RESOURCE_CONSTRUCTOR_HIT count=%ld manager=%p nativeNonZero=%ld nativeDistinct=%ld handles=%08X,%08X,%08X,%08X,%08X,%08X,%08X,%08X,%08X,%08X,%08X,%08X,%08X,%08X,%08X,%08X extraCreation=8 externalNonZero=%ld externalDistinct=%ld",
        (long)g_resource_hits,g_last_resource_manager,
        (long)g_native_nonzero_handles,(long)g_native_distinct_handles,
        g_native_handles[0],g_native_handles[1],g_native_handles[2],g_native_handles[3],
        g_native_handles[4],g_native_handles[5],g_native_handles[6],g_native_handles[7],
        g_native_handles[8],g_native_handles[9],g_native_handles[10],g_native_handles[11],
        g_native_handles[12],g_native_handles[13],g_native_handles[14],g_native_handles[15],
        (long)g_range_nonzero_handles,(long)g_range_distinct_handles);
}

static DWORD WINAPI stage_e_worker(void *unused)
{
    HMODULE disrupt=NULL;
    volatile unsigned char *intercepted=NULL;
    volatile unsigned char *ready=NULL;
    void **original_run_game=NULL;
    unsigned char last_intercepted=0xFF,last_ready=0xFF;
    void *last_original=(void *)(uintptr_t)~0ULL;
    LONG last_resource=0,last_register=0;
    DWORD elapsed=0;
    int install_attempted=0;
    int session_started=0;
    DWORD constructors_ready_elapsed=0;
    (void)unused;
    if(g_snapshot.troplo) {
        intercepted=(volatile unsigned char *)GetProcAddress(g_snapshot.troplo,"g_RunGameIntercepted");
        ready=(volatile unsigned char *)GetProcAddress(g_snapshot.troplo,"g_NexusReady");
        original_run_game=(void **)GetProcAddress(g_snapshot.troplo,"OriginalRunGame");
    }

    while(elapsed<=POLL_TIMEOUT_MS) {
        LARGE_INTEGER now;
        unsigned char current_intercepted=intercepted ? *intercepted : 0;
        unsigned char current_ready=ready ? *ready : 0;
        void *current_original=original_run_game ? *original_run_game : NULL;
        LONG current_resource=g_resource_hits;
        LONG current_register=g_register_hits;
        if(!disrupt) {
            disrupt=GetModuleHandleW(L"Disrupt_b64.dll");
            if(!disrupt) disrupt=GetModuleHandleW(L"Disrupt_64.dll");
        }
        if(!session_started && disrupt && current_intercepted &&
           current_original && !current_ready) {
            session_started=begin_session_log();
            if(session_started) {
                append_log("BOOTSTRAP_BEGIN product=ShadowEnginePatch version=%s pid=%lu initThread=%lu proxy=%p self=%p",
                    PATCH_VERSION,(unsigned long)GetCurrentProcessId(),
                    (unsigned long)g_snapshot.initialize_thread,g_snapshot.proxy,g_self);
                append_log("SESSION_ROLE_CONFIRMED runGameIntercepted=1 originalRunGame=%p nexusReady=0 helperWritesSuppressed=1 logMode=create-always",
                    current_original);
                append_log("DESIGN role=engine-only stage=P-nonnull-sidecar-repair earlyNativeMaps=24 earlyPasses17to24=1 passTableAllocatorHook=0 passTableVerification=readOnly requiredPassMaxKey=0x310C requiredPassSlots=50 directAddedSlotVerification=1 failClosedBeforeExtraPassRegistration=1 queueAllocationAndTailRelocationsBeforeConstruction=1 remainingDownstreamRoutingDeferredUntilAfterConstructors=1 stabilizationMs=%u schedulerInitThen=16 schedulerFloorThen=16 schedulerUpperThen=16 schedulerUpperRva=0x2E9C3B cacheOwnerCeilingA8=4 ownerVectorPhysicalReserve=8 ownerRecordStride=0x700 reserveRva=0x319160 failClosedOnReserveMismatch=1 physicalFaceBudget=24 wholeRecordPriorityPrefix=1 queueCountRestoredAfterRender=1 targetOrdinaryDynamicPositions=15 rendererProbeActive=1 queueTailIntegrityProbe=1 nullFaceCostGuard=1 nullResourceAcquireReleaseCompaction=1 boundedNullIndexTrace=1 resourceProducerWrapperRva=0x427860 resourceProducerTraceSlots=%u renderRecordLifecycleTrace=1 lifecycleRecordSlots=%u lifecycleThreadSlots=%u constructorTrace=1 builderEntryExitTrace=1 targetedFieldLookupTrace=1 rendererEntryTrace=1 releaseEntryTrace=1 callerRvaCapture=1 everyFlaggedFieldCompared=1 nonnullMismatchRepair=1 displacedResourceSidecar=2 sidecarNativeWrapperAcquireRelease=1 sidecarDeduplicate=1 repairRequiresMatchingBuilderCall=1 repairRequiresStableGeneration=1 repairRequiresVerifiedLookup=1 repairRequiresNoPriorRelease=1 manualResourceSnapshot=F8,F9 manualLifecycleSnapshot=F8,F9 manualCaptureSerial=rendererCall candidateReject=0 crashContextProbe=0 fullCandidatePassthrough=1 filtering=0 vehicleLimiter=0",
                    DOWNSTREAM_STABILIZE_MS,RESOURCE_PRODUCER_SLOTS,
                    LIFECYCLE_RECORD_SLOTS,LIFECYCLE_THREAD_SLOTS);
                append_log("TROPLO_LOAD module=%p error=%lu returnedMs=%.3f",
                    g_snapshot.troplo,(unsigned long)g_snapshot.troplo_error,
                    elapsed_ms(g_snapshot.troplo_return_time));
                append_log("TROPLO_EXPORTS interceptedVar=%p readyVar=%p originalRunGameVar=%p",
                    (void *)intercepted,(void *)ready,(void *)original_run_game);
                QueryPerformanceCounter(&now);
                append_log("DISRUPT_MAPPED module=%p elapsedMs=%.3f",disrupt,elapsed_ms(now));
            }
        }
        if(current_intercepted!=last_intercepted) {
            QueryPerformanceCounter(&now);
            append_log("RUN_GAME_INTERCEPTED value=%u elapsedMs=%.3f",
                (unsigned)current_intercepted,elapsed_ms(now));
            last_intercepted=current_intercepted;
        }
        if(current_ready!=last_ready) {
            QueryPerformanceCounter(&now);
            append_log("NEXUS_READY value=%u elapsedMs=%.3f",
                (unsigned)current_ready,elapsed_ms(now));
            last_ready=current_ready;
        }
        if(current_original!=last_original) {
            QueryPerformanceCounter(&now);
            append_log("ORIGINAL_RUN_GAME value=%p elapsedMs=%.3f",
                current_original,elapsed_ms(now));
            last_original=current_original;
        }
        if(!install_attempted && disrupt && current_intercepted && current_original) {
            install_attempted=1;
            if(current_ready) {
                append_log("STAGE_E_ABORT_LATE nexusReady=1 noEngineMutation=1");
            } else {
                install_early_construction_hooks(disrupt);
            }
        }
        if(current_resource!=last_resource) {
            log_handle_snapshot();
            last_resource=current_resource;
        }
        if(current_register!=last_register) {
            append_log("REGISTER_PASS_ACTIVITY nativeHookCalls=%ld nativeShadow=%ld nativeShadowAlpha=%ld nativeLastShadow=%ld nativeLastAlpha=%ld stageCPassState=%ld extraRegisteredByPatch=%u",
                (long)current_register,(long)g_shadow_hits,(long)g_shadow_alpha_hits,
                (long)g_last_native_shadow_seen,(long)g_last_native_alpha_seen,
                (long)g_range_pass_state,g_range_pass_state==2 ? 16U : 0U);
            last_register=current_register;
        }
        if(!constructors_ready_elapsed && g_range_resource_state==2 &&
           g_range_pass_state==2) {
            constructors_ready_elapsed=elapsed ? elapsed : 1;
            append_log("STAGE_E_CONSTRUCTORS_COMPLETE downstreamStillDeferred=1 stabilizeForMs=%u",
                DOWNSTREAM_STABILIZE_MS);
        }
        if(constructors_ready_elapsed && g_downstream_state==0 &&
           elapsed-constructors_ready_elapsed>=DOWNSTREAM_STABILIZE_MS) {
            install_deferred_downstream();
        }
        if(g_hooks_installed && g_resource_hits>0 && g_register_hits>0 &&
           g_last_native_shadow_seen && g_last_native_alpha_seen &&
           g_pass_table_state==2 &&
           g_pass_table_slots>=(LONG)REQUIRED_PASS_TABLE_SLOTS &&
           g_pass_table_stored_max_key>=(LONG)REQUIRED_PASS_MAX_KEY &&
           g_pass_table_added_nonzero==(LONG)EXTRA_PASS_SLOT_COUNT &&
           g_pass_table_added_distinct==(LONG)EXTRA_PASS_SLOT_COUNT &&
           g_range_resource_state==2 && g_range_nonzero_handles==8 &&
           g_range_distinct_handles==8 && g_range_pass_state==2 &&
           g_routing_state==2 && g_downstream_state==2 && g_profile_state==2 &&
           g_lifecycle_hooks_state==2 &&
           g_renderer_hook_state==2 && g_observed_scheduler_b4==TARGET_DYNAMIC_B4 &&
           g_observed_cache_a8==TARGET_CACHE_A8 && g_owner_reserve_state==2 &&
           g_owner_capacity_after>=(LONG)OWNER_VECTOR_RESERVE_CAPACITY &&
           g_owner_reserve_fail_closed==0 &&
           g_owner_base_changed_during_manager==0) {
            append_log("STAGE_M_COMPLETE resourceConstructorReached=1 passConstructorReached=1 passTableAllocatorHook=0 passTableStoredMaxKey=0x%04lX physicalPassSlots=%ld requiredPassSlots=%u addedPassSlotsNonZero=%ld addedPassSlotsDistinct=%ld directPassStorageVerified=1 resourceLifecycleTraceActive=1 resourceProducerTraceActive=1 resourceProducerTraceSlots=%u renderRecordLifecycleTraceActive=1 nonnullSidecarRepairActive=1 lifecycleRecordSlots=%u lifecycleThreadSlots=%u extraMaps16to23NonZero=8 pairedPasses17to24RegistrationReturned=16 downstreamInstalledAfterConstructors=1 stabilizationMs=%u physicalQueueEntries=25 physicalFaceBudget=24 wholeRecordPriorityPrefix=1 observedB4=%ld observedA8=%ld ownerReserveState=%ld ownerCapacityBefore=%ld ownerCapacityAfter=%ld ownerReserveFailClosed=%ld ownerBaseChangesDuringManager=%ld targetOrdinaryDynamicPositions=15 targetCachedOwners=4 ownerPhysicalReserve=8 rendererProbeActive=1 queueTailIntegrityProbe=1 tailInvalidBeforeRenderer=%ld tailInvalidAfterRenderer=%ld extraFaceUseSeen=%ld faceBudgetClamps=%ld faceBudgetDroppedEntries=%ld faceBudgetPeakRequested=%ld nullFaceCostSkips=%ld nullSubmitDrops=%ld nullAcquireDrops=%ld nullReleaseDrops=%ld lifecycleConstructorCalls=%ld lifecycleBuilderCalls=%ld lifecycleLookupCalls=%ld lifecycleRendererCalls=%ld lifecycleReleaseCalls=%ld fullCandidatePassthrough=1 filtering=0 limiter=0",
                (long)g_pass_table_stored_max_key,(long)g_pass_table_slots,
                REQUIRED_PASS_TABLE_SLOTS,
                (long)g_pass_table_added_nonzero,
                (long)g_pass_table_added_distinct,RESOURCE_PRODUCER_SLOTS,
                LIFECYCLE_RECORD_SLOTS,LIFECYCLE_THREAD_SLOTS,
                DOWNSTREAM_STABILIZE_MS,
                (long)g_observed_scheduler_b4,(long)g_observed_cache_a8,
                (long)g_owner_reserve_state,(long)g_owner_capacity_before,
                (long)g_owner_capacity_after,(long)g_owner_reserve_fail_closed,
                (long)g_owner_base_changed_during_manager,
                (long)g_tail_invalid_before_renderer,
                (long)g_tail_invalid_after_renderer,
                (long)g_extra_face_use_seen,(long)g_face_budget_clamps,
                (long)g_face_budget_dropped_entries,
                (long)g_face_budget_peak_requested,(long)g_null_face_cost_skips,
                (long)g_null_submit_drops,(long)g_null_acquire_drops,
                (long)g_null_release_drops,
                (long)g_lifecycle_constructor_calls,
                (long)g_lifecycle_builder_calls,
                (long)g_lifecycle_lookup_calls,
                (long)g_lifecycle_renderer_calls,
                (long)g_lifecycle_release_calls);
            return 0;
        }
        Sleep(POLL_INTERVAL_MS);
        elapsed+=POLL_INTERVAL_MS;
    }
    append_log("STAGE_M_TIMEOUT hooksInstalled=%ld passTableState=%ld passTableStoredMaxKey=0x%04lX passTableSlots=%ld addedPassSlotsNonZero=%ld addedPassSlotsDistinct=%ld resourceHits=%ld registerHits=%ld nativeLastShadow=%ld nativeLastAlpha=%ld resourceState=%ld extraNonZero=%ld extraDistinct=%ld passState=%ld routingState=%ld downstreamState=%ld profileState=%ld rendererHookState=%ld lifecycleHooksState=%ld rendererCalls=%ld resourceWrapperCalls=%ld resourceSubmitCalls=%ld nullResourceEvents=%ld nullEventsWithRendererContext=%ld nullEventsWithExtraMaps=%ld tailInvalidBeforeRenderer=%ld tailInvalidAfterRenderer=%ld extraFaceUseSeen=%ld nullFaceCostSkips=%ld nullSubmitDrops=%ld nullAcquireDrops=%ld nullReleaseDrops=%ld ownerReserveState=%ld ownerCapacityBefore=%ld ownerCapacityAfter=%ld ownerReserveFailClosed=%ld ownerBaseChangesDuringManager=%ld observedB4=%ld observedA8=%ld lifecycleConstructorCalls=%ld lifecycleBuilderCalls=%ld lifecycleLookupCalls=%ld lifecycleRendererCalls=%ld lifecycleReleaseCalls=%ld lifecycleBuilderNullEvents=%ld lifecycleRendererNullEvents=%ld lifecycleReleaseNullEvents=%ld lifecycleRecordSlotMisses=%ld lifecycleThreadSlotMisses=%ld limiter=0",
        (long)g_hooks_installed,
        (long)g_pass_table_state,
        (long)g_pass_table_stored_max_key,(long)g_pass_table_slots,
        (long)g_pass_table_added_nonzero,(long)g_pass_table_added_distinct,
        (long)g_resource_hits,(long)g_register_hits,
        (long)g_last_native_shadow_seen,(long)g_last_native_alpha_seen,
        (long)g_range_resource_state,(long)g_range_nonzero_handles,
        (long)g_range_distinct_handles,(long)g_range_pass_state,
        (long)g_routing_state,(long)g_downstream_state,(long)g_profile_state,
        (long)g_renderer_hook_state,(long)g_lifecycle_hooks_state,
        (long)g_renderer_calls,
        (long)g_resource_wrapper_calls,(long)g_resource_submit_calls,
        (long)g_null_resource_events,
        (long)g_null_events_with_renderer_context,
        (long)g_null_events_with_extra_maps,
        (long)g_tail_invalid_before_renderer,
        (long)g_tail_invalid_after_renderer,
        (long)g_extra_face_use_seen,(long)g_null_face_cost_skips,
        (long)g_null_submit_drops,(long)g_null_acquire_drops,
        (long)g_null_release_drops,(long)g_owner_reserve_state,
        (long)g_owner_capacity_before,(long)g_owner_capacity_after,
        (long)g_owner_reserve_fail_closed,
        (long)g_owner_base_changed_during_manager,
        (long)g_observed_scheduler_b4,(long)g_observed_cache_a8,
        (long)g_lifecycle_constructor_calls,
        (long)g_lifecycle_builder_calls,(long)g_lifecycle_lookup_calls,
        (long)g_lifecycle_renderer_calls,(long)g_lifecycle_release_calls,
        (long)g_lifecycle_builder_null_events,
        (long)g_lifecycle_renderer_null_events,
        (long)g_lifecycle_release_null_events,
        (long)g_lifecycle_record_slot_misses,
        (long)g_lifecycle_thread_slot_misses);
    return 0;
}

__declspec(dllexport) BOOL WINAPI InitializeShadowEnginePatch(HMODULE proxy)
{
    wchar_t path[MAX_PATH];
    wchar_t *slash;
    HANDLE thread;
    if(InterlockedCompareExchange(&g_initialized,1,0)!=0) return TRUE;
    QueryPerformanceFrequency(&g_snapshot.frequency);
    QueryPerformanceCounter(&g_snapshot.initialize_time);
    g_snapshot.initialize_thread=GetCurrentThreadId();
    g_snapshot.proxy=proxy;
    g_snapshot.disrupt_before=GetModuleHandleW(L"Disrupt_b64.dll");
    if(!g_snapshot.disrupt_before) g_snapshot.disrupt_before=GetModuleHandleW(L"Disrupt_64.dll");
    if(!GetModuleFileNameW(g_self,path,MAX_PATH)) return FALSE;
    slash=path+lstrlenW(path);
    while(slash>path && slash[-1]!=L'\\' && slash[-1]!=L'/') --slash;
    *slash=L'\0';
    lstrcpynW(g_snapshot.bin_directory,path,MAX_PATH);
    if(lstrlenW(path)+30>=MAX_PATH) return FALSE;
    lstrcatW(path,L"TroploAsiInjectionHelper.asi");
    SetLastError(ERROR_SUCCESS);
    g_snapshot.troplo=LoadLibraryW(path);
    g_snapshot.troplo_error=g_snapshot.troplo ? ERROR_SUCCESS : GetLastError();
    QueryPerformanceCounter(&g_snapshot.troplo_return_time);
    thread=CreateThread(NULL,0,stage_e_worker,NULL,0,NULL);
    if(thread) CloseHandle(thread);
    return g_snapshot.troplo!=NULL;
}

BOOL WINAPI DllMain(HINSTANCE instance,DWORD reason,LPVOID reserved)
{
    (void)reserved;
    if(reason==DLL_PROCESS_ATTACH) {
        g_self=instance;
        DisableThreadLibraryCalls(instance);
        InitializeShadowEnginePatch(GetModuleHandleW(L"dinput8.dll"));
    }
    return TRUE;
}
