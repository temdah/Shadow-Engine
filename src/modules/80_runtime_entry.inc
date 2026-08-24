/* Module 80: NexusTools lifecycle monitoring, runtime completion proof,
 * exported initialization entry points, fallback worker, and DllMain. */
static void log_handle_snapshot(void)
{
    append_log("RESOURCE_CONSTRUCTOR_HIT count=%ld manager=%p nativeNonZero=%ld nativeDistinct=%ld handles=%08X,%08X,%08X,%08X,%08X,%08X,%08X,%08X,%08X,%08X,%08X,%08X,%08X,%08X,%08X,%08X extraCreation=8 externalNonZero=%ld externalDistinct=%ld",
        (long)g_shadow_engine.resources.resource_hits,g_shadow_engine.resources.last_resource_manager,
        (long)g_shadow_engine.resources.native_nonzero_handles,(long)g_shadow_engine.resources.native_distinct_handles,
        g_shadow_engine.resources.native_handles[0],g_shadow_engine.resources.native_handles[1],g_shadow_engine.resources.native_handles[2],g_shadow_engine.resources.native_handles[3],
        g_shadow_engine.resources.native_handles[4],g_shadow_engine.resources.native_handles[5],g_shadow_engine.resources.native_handles[6],g_shadow_engine.resources.native_handles[7],
        g_shadow_engine.resources.native_handles[8],g_shadow_engine.resources.native_handles[9],g_shadow_engine.resources.native_handles[10],g_shadow_engine.resources.native_handles[11],
        g_shadow_engine.resources.native_handles[12],g_shadow_engine.resources.native_handles[13],g_shadow_engine.resources.native_handles[14],g_shadow_engine.resources.native_handles[15],
        (long)g_shadow_engine.resources.range_nonzero_handles,(long)g_shadow_engine.resources.range_distinct_handles);
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
    if(g_shadow_engine.bootstrap.snapshot.troplo) {
        intercepted=(volatile unsigned char *)GetProcAddress(g_shadow_engine.bootstrap.snapshot.troplo,"g_RunGameIntercepted");
        ready=(volatile unsigned char *)GetProcAddress(g_shadow_engine.bootstrap.snapshot.troplo,"g_NexusReady");
        original_run_game=(void **)GetProcAddress(g_shadow_engine.bootstrap.snapshot.troplo,"OriginalRunGame");
    }

    while(elapsed<=POLL_TIMEOUT_MS) {
        LARGE_INTEGER now;
        unsigned char current_intercepted=intercepted ? *intercepted : 0;
        unsigned char current_ready=ready ? *ready : 0;
        void *current_original=original_run_game ? *original_run_game : NULL;
        LONG current_resource=g_shadow_engine.resources.resource_hits;
        LONG current_register=g_shadow_engine.resources.register_hits;
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
                    (unsigned long)g_shadow_engine.bootstrap.snapshot.initialize_thread,g_shadow_engine.bootstrap.snapshot.proxy,g_shadow_engine.bootstrap.self);
                LONG nexus_entry_kind=InterlockedCompareExchange(&g_shadow_engine.bootstrap.nexus_entry_kind,0,0);
                const char *nexus_entry=nexus_entry_kind==2 ? "OnBeforeEngineInit" :
                    (nexus_entry_kind==1 ? "Init" :
                    (nexus_entry_kind==3 ? "DllMainDeferredFallback" : "unknown"));
                append_log("NEXUSTOOLS_EARLY_CALLBACK_COMPAT entry=%s callbackCalls=%ld interface=%p interfaceUsed=0 dllMainInit=0 dllMainDeferredFallback=%u fallbackAttempts=%ld manualHelperLoad=0 helperAlreadyLoaded=%u exports=Init,OnBeforeEngineInit shippedNexusTools112Entry=OnBeforeEngineInit",
                    nexus_entry,
                    (long)g_shadow_engine.bootstrap.nexus_init_calls,(void *)g_shadow_engine.bootstrap.nexus_interface,
                    nexus_entry_kind==3 ? 1U:0U,
                    (long)g_shadow_engine.bootstrap.dllmain_fallback_attempts,
                    g_shadow_engine.bootstrap.snapshot.troplo ? 1U:0U);
                append_log("SESSION_ROLE_CONFIRMED runGameIntercepted=1 originalRunGame=%p nexusReady=0 helperWritesSuppressed=1 logMode=create-always",
                    current_original);
                append_log("DESIGN role=engine-only stage=Z-post-fix-performance-control earlyNativeMaps=24 earlyPasses17to24=1 passTableAllocatorHook=0 passTableVerification=readOnly requiredPassMaxKey=0x310C requiredPassSlots=50 directAddedSlotVerification=1 failClosedBeforeExtraPassRegistration=1 queueAllocationAndTailRelocationsBeforeConstruction=1 remainingDownstreamRoutingDeferredUntilAfterConstructors=1 stabilizationMs=%u schedulerInitThen=16 schedulerFloorThen=16 schedulerUpperThen=16 schedulerUpperRva=0x2E9C3B cacheOwnerCeilingA8=4 ownerVectorPhysicalReserve=8 ownerRecordStride=0x700 reserveRva=0x319160 failClosedOnReserveMismatch=1 physicalFaceBudget=24 wholeRecordPriorityPrefix=1 queueCountRestoredAfterRender=1 targetOrdinaryDynamicPositions=15 rendererProbeActive=1 queueTailIntegrityProbe=1 nativeSliceResultCount=17 externalSliceResultCount=7 sliceResultBoundsRelays=2 routedSliceIndices=17-23 externalConsumer=postCallObjectRenderers releaseFallback=renderRecordRelease nonNullStoreRva=0x2CC316 nullStoreRva=0x2CC32B rejectedIndexedWriterDetourRemoved=1 nullFaceCostGuard=1 resourceProducerWrapperRva=0x427860 nativeWrapperDirectCall=1 resourceWrapperDetour=0 resourceSubmitDetour=0 renderRecordLifecycleHooks=constructor,builder,postCallObjectRenderers,release lifecycleRecordSlots=%u repair=0 sidecars=0 taskProvenance=0 resourceTrace=0 sharedTextureTrace=0 frameGraphTrace=0 acquireHook=0 diagnostics=minimal stageYSummaryPeriodMs=%u manualQueueAndStageYCapture=F8,F9 fieldMutation=externalSliceResultRouting fullCandidatePassthrough=1 filtering=0 vehicleLimiter=0",
                    DOWNSTREAM_STABILIZE_MS,LIFECYCLE_RECORD_SLOTS,
                    EXTERNAL_SLICE_SUMMARY_PERIOD_MS);
                append_log("TROPLO_BIND module=%p error=%lu boundMs=%.3f",
                    g_shadow_engine.bootstrap.snapshot.troplo,(unsigned long)g_shadow_engine.bootstrap.snapshot.troplo_error,
                    elapsed_ms(g_shadow_engine.bootstrap.snapshot.troplo_return_time));
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
                (long)current_register,(long)g_shadow_engine.resources.shadow_hits,(long)g_shadow_engine.resources.shadow_alpha_hits,
                (long)g_shadow_engine.resources.last_native_shadow_seen,(long)g_shadow_engine.resources.last_native_alpha_seen,
                (long)g_shadow_engine.resources.range_pass_state,g_shadow_engine.resources.range_pass_state==2 ? 16U : 0U);
            last_register=current_register;
        }
        if(!constructors_ready_elapsed && g_shadow_engine.resources.range_resource_state==2 &&
           g_shadow_engine.resources.range_pass_state==2) {
            constructors_ready_elapsed=elapsed ? elapsed : 1;
            append_log("STAGE_E_CONSTRUCTORS_COMPLETE downstreamStillDeferred=1 stabilizeForMs=%u",
                DOWNSTREAM_STABILIZE_MS);
        }
        if(constructors_ready_elapsed && g_shadow_engine.bootstrap.downstream_state==0 &&
           elapsed-constructors_ready_elapsed>=DOWNSTREAM_STABILIZE_MS) {
            install_deferred_downstream();
        }
        if(g_shadow_engine.bootstrap.hooks_installed && g_shadow_engine.resources.resource_hits>0 && g_shadow_engine.resources.register_hits>0 &&
           g_shadow_engine.resources.last_native_shadow_seen && g_shadow_engine.resources.last_native_alpha_seen &&
           g_shadow_engine.resources.pass_table_state==2 &&
           g_shadow_engine.resources.pass_table_slots>=(LONG)REQUIRED_PASS_TABLE_SLOTS &&
           g_shadow_engine.resources.pass_table_stored_max_key>=(LONG)REQUIRED_PASS_MAX_KEY &&
           g_shadow_engine.resources.pass_table_added_nonzero==(LONG)EXTRA_PASS_SLOT_COUNT &&
           g_shadow_engine.resources.pass_table_added_distinct==(LONG)EXTRA_PASS_SLOT_COUNT &&
           g_shadow_engine.resources.range_resource_state==2 && g_shadow_engine.resources.range_nonzero_handles==8 &&
           g_shadow_engine.resources.range_distinct_handles==8 && g_shadow_engine.resources.range_pass_state==2 &&
           g_shadow_engine.resources.routing_state==2 && g_shadow_engine.bootstrap.downstream_state==2 && g_shadow_engine.renderer.profile_state==2 &&
           g_shadow_engine.external_results.lifecycle_hooks_state==2 &&
           g_shadow_engine.renderer.renderer_hook_state==2 && g_shadow_engine.renderer.observed_scheduler_b4==TARGET_DYNAMIC_B4 &&
           g_shadow_engine.renderer.observed_cache_a8==TARGET_CACHE_A8 && g_shadow_engine.renderer.owner_reserve_state==2 &&
           g_shadow_engine.renderer.owner_capacity_after>=(LONG)OWNER_VECTOR_RESERVE_CAPACITY &&
           g_shadow_engine.renderer.owner_reserve_fail_closed==0 &&
           g_shadow_engine.renderer.owner_base_changed_during_manager==0) {
            append_log("STAGE_M_COMPLETE resourceConstructorReached=1 passConstructorReached=1 passTableAllocatorHook=0 passTableStoredMaxKey=0x%04lX physicalPassSlots=%ld requiredPassSlots=%u addedPassSlotsNonZero=%ld addedPassSlotsDistinct=%ld directPassStorageVerified=1 extraMaps16to23NonZero=8 pairedPasses17to24RegistrationReturned=16 downstreamInstalledAfterConstructors=1 stabilizationMs=%u physicalQueueEntries=25 physicalFaceBudget=24 wholeRecordPriorityPrefix=1 observedB4=%ld observedA8=%ld ownerReserveState=%ld ownerCapacityBefore=%ld ownerCapacityAfter=%ld ownerReserveFailClosed=%ld ownerBaseChangesDuringManager=%ld targetOrdinaryDynamicPositions=15 targetCachedOwners=4 ownerPhysicalReserve=8 rendererProbeActive=1 queueTailIntegrityProbe=1 tailInvalidBeforeRenderer=%ld tailInvalidAfterRenderer=%ld extraFaceUseSeen=%ld faceBudgetClamps=%ld faceBudgetDroppedEntries=%ld faceBudgetPeakRequested=%ld nullFaceCostSkips=%ld lifecycleConstructorCalls=%ld lifecycleBuilderCalls=%ld postCallConsumerCalls=%ld lifecycleReleaseCalls=%ld externalResultRouting=1 nativeWrapperDirectCall=1 resourceWrapperDetour=0 resourceSubmitDetour=0 repair=0 sidecars=0 taskProvenance=0 resourceTrace=0 sharedTextureTrace=0 frameGraphTrace=0 acquireHook=0 diagnostics=minimal fullCandidatePassthrough=1 filtering=0 limiter=0",
                (long)g_shadow_engine.resources.pass_table_stored_max_key,(long)g_shadow_engine.resources.pass_table_slots,
                REQUIRED_PASS_TABLE_SLOTS,
                (long)g_shadow_engine.resources.pass_table_added_nonzero,
                (long)g_shadow_engine.resources.pass_table_added_distinct,
                DOWNSTREAM_STABILIZE_MS,
                (long)g_shadow_engine.renderer.observed_scheduler_b4,(long)g_shadow_engine.renderer.observed_cache_a8,
                (long)g_shadow_engine.renderer.owner_reserve_state,(long)g_shadow_engine.renderer.owner_capacity_before,
                (long)g_shadow_engine.renderer.owner_capacity_after,(long)g_shadow_engine.renderer.owner_reserve_fail_closed,
                (long)g_shadow_engine.renderer.owner_base_changed_during_manager,
                (long)g_shadow_engine.renderer.tail_invalid_before_renderer,
                (long)g_shadow_engine.renderer.tail_invalid_after_renderer,
                (long)g_shadow_engine.renderer.extra_face_use_seen,(long)g_shadow_engine.renderer.face_budget_clamps,
                (long)g_shadow_engine.renderer.face_budget_dropped_entries,
                (long)g_shadow_engine.renderer.face_budget_peak_requested,(long)g_shadow_engine.renderer.null_face_cost_skips,
                (long)g_shadow_engine.external_results.lifecycle_constructor_calls,
                (long)g_shadow_engine.external_results.lifecycle_builder_calls,
                (long)g_shadow_engine.external_results.postcall_calls,
                (long)g_shadow_engine.external_results.lifecycle_release_calls);
            return 0;
        }
        Sleep(POLL_INTERVAL_MS);
        elapsed+=POLL_INTERVAL_MS;
    }
    append_log("STAGE_M_TIMEOUT hooksInstalled=%ld passTableState=%ld passTableStoredMaxKey=0x%04lX passTableSlots=%ld addedPassSlotsNonZero=%ld addedPassSlotsDistinct=%ld resourceHits=%ld registerHits=%ld nativeLastShadow=%ld nativeLastAlpha=%ld resourceState=%ld extraNonZero=%ld extraDistinct=%ld passState=%ld routingState=%ld downstreamState=%ld profileState=%ld rendererHookState=%ld lifecycleHooksState=%ld rendererCalls=%ld tailInvalidBeforeRenderer=%ld tailInvalidAfterRenderer=%ld extraFaceUseSeen=%ld nullFaceCostSkips=%ld ownerReserveState=%ld ownerCapacityBefore=%ld ownerCapacityAfter=%ld ownerReserveFailClosed=%ld ownerBaseChangesDuringManager=%ld observedB4=%ld observedA8=%ld lifecycleConstructorCalls=%ld lifecycleBuilderCalls=%ld postCallConsumerCalls=%ld lifecycleReleaseCalls=%ld lifecycleRecordSlotMisses=%ld resourceWrapperDetour=0 resourceSubmitDetour=0 diagnostics=minimal limiter=0",
        (long)g_shadow_engine.bootstrap.hooks_installed,
        (long)g_shadow_engine.resources.pass_table_state,
        (long)g_shadow_engine.resources.pass_table_stored_max_key,(long)g_shadow_engine.resources.pass_table_slots,
        (long)g_shadow_engine.resources.pass_table_added_nonzero,(long)g_shadow_engine.resources.pass_table_added_distinct,
        (long)g_shadow_engine.resources.resource_hits,(long)g_shadow_engine.resources.register_hits,
        (long)g_shadow_engine.resources.last_native_shadow_seen,(long)g_shadow_engine.resources.last_native_alpha_seen,
        (long)g_shadow_engine.resources.range_resource_state,(long)g_shadow_engine.resources.range_nonzero_handles,
        (long)g_shadow_engine.resources.range_distinct_handles,(long)g_shadow_engine.resources.range_pass_state,
        (long)g_shadow_engine.resources.routing_state,(long)g_shadow_engine.bootstrap.downstream_state,(long)g_shadow_engine.renderer.profile_state,
        (long)g_shadow_engine.renderer.renderer_hook_state,(long)g_shadow_engine.external_results.lifecycle_hooks_state,
        (long)g_shadow_engine.renderer.renderer_calls,
        (long)g_shadow_engine.renderer.tail_invalid_before_renderer,
        (long)g_shadow_engine.renderer.tail_invalid_after_renderer,
        (long)g_shadow_engine.renderer.extra_face_use_seen,(long)g_shadow_engine.renderer.null_face_cost_skips,
        (long)g_shadow_engine.renderer.owner_reserve_state,
        (long)g_shadow_engine.renderer.owner_capacity_before,(long)g_shadow_engine.renderer.owner_capacity_after,
        (long)g_shadow_engine.renderer.owner_reserve_fail_closed,
        (long)g_shadow_engine.renderer.owner_base_changed_during_manager,
        (long)g_shadow_engine.renderer.observed_scheduler_b4,(long)g_shadow_engine.renderer.observed_cache_a8,
        (long)g_shadow_engine.external_results.lifecycle_constructor_calls,
        (long)g_shadow_engine.external_results.lifecycle_builder_calls,(long)g_shadow_engine.external_results.postcall_calls,
        (long)g_shadow_engine.external_results.lifecycle_release_calls,
        (long)g_shadow_engine.external_results.lifecycle_record_slot_misses);
    return 0;
}

static BOOL start_shadow_engine_patch(void)
{
    wchar_t path[MAX_PATH];
    wchar_t *slash;
    HANDLE thread;
    if(InterlockedCompareExchange(&g_shadow_engine.bootstrap.initialized,1,0)!=0) return TRUE;
    QueryPerformanceFrequency(&g_shadow_engine.bootstrap.snapshot.frequency);
    QueryPerformanceCounter(&g_shadow_engine.bootstrap.snapshot.initialize_time);
    g_shadow_engine.bootstrap.snapshot.initialize_thread=GetCurrentThreadId();
    g_shadow_engine.bootstrap.snapshot.proxy=GetModuleHandleW(L"dinput8.dll");
    g_shadow_engine.bootstrap.snapshot.disrupt_before=GetModuleHandleW(L"Disrupt_b64.dll");
    if(!g_shadow_engine.bootstrap.snapshot.disrupt_before) g_shadow_engine.bootstrap.snapshot.disrupt_before=GetModuleHandleW(L"Disrupt_64.dll");
    if(!GetModuleFileNameW(g_shadow_engine.bootstrap.self,path,MAX_PATH)) return FALSE;
    slash=path+lstrlenW(path);
    while(slash>path && slash[-1]!=L'\\' && slash[-1]!=L'/') --slash;
    *slash=L'\0';
    lstrcpynW(g_shadow_engine.bootstrap.snapshot.bin_directory,path,MAX_PATH);
    SetLastError(ERROR_SUCCESS);
    g_shadow_engine.bootstrap.snapshot.troplo=GetModuleHandleW(L"TroploAsiInjectionHelper.asi");
    g_shadow_engine.bootstrap.snapshot.troplo_error=g_shadow_engine.bootstrap.snapshot.troplo ? ERROR_SUCCESS : GetLastError();
    QueryPerformanceCounter(&g_shadow_engine.bootstrap.snapshot.troplo_return_time);
    if(!g_shadow_engine.bootstrap.snapshot.troplo) {
        LONG nexus_entry_kind=InterlockedCompareExchange(&g_shadow_engine.bootstrap.nexus_entry_kind,0,0);
        const char *nexus_entry=nexus_entry_kind==2 ? "OnBeforeEngineInit" :
            (nexus_entry_kind==1 ? "Init" :
            (nexus_entry_kind==3 ? "DllMainDeferredFallback" : "unknown"));
        if(begin_session_log())
            append_log("NEXUSTOOLS_EARLY_CALLBACK_ABORT entry=%s helperAlreadyLoaded=0 manualHelperLoad=0 error=%lu noEngineMutation=1",
                nexus_entry,
                (unsigned long)g_shadow_engine.bootstrap.snapshot.troplo_error);
        return FALSE;
    }
    thread=CreateThread(NULL,0,stage_e_worker,NULL,0,NULL);
    if(!thread) {
        LONG nexus_entry_kind=InterlockedCompareExchange(&g_shadow_engine.bootstrap.nexus_entry_kind,0,0);
        const char *nexus_entry=nexus_entry_kind==2 ? "OnBeforeEngineInit" :
            (nexus_entry_kind==1 ? "Init" :
            (nexus_entry_kind==3 ? "DllMainDeferredFallback" : "unknown"));
        if(begin_session_log())
            append_log("NEXUSTOOLS_EARLY_CALLBACK_ABORT entry=%s helperAlreadyLoaded=1 workerThreadCreated=0 error=%lu noEngineMutation=1",
                nexus_entry,
                (unsigned long)GetLastError());
        return FALSE;
    }
    CloseHandle(thread);
    return TRUE;
}

static void nexus_early_entry(void *interface,LONG entry_kind)
{
    InterlockedIncrement(&g_shadow_engine.bootstrap.nexus_init_calls);
    InterlockedCompareExchange(&g_shadow_engine.bootstrap.nexus_entry_kind,entry_kind,0);
    InterlockedExchangePointer((PVOID volatile *)&g_shadow_engine.bootstrap.nexus_interface,interface);
    start_shadow_engine_patch();
}

__declspec(dllexport) void Init(void *interface)
{
    nexus_early_entry(interface,1);
}

__declspec(dllexport) void OnBeforeEngineInit(void *interface)
{
    nexus_early_entry(interface,2);
}

/* NexusTools 1.1.12 can load a native ASI without dispatching its early
 * callback during Watch Dogs' duplicated/subprocess startup.  Keep the
 * supported callback as the primary route: this worker yields briefly after
 * DllMain returns, then starts the same guarded bootstrap only if the callback
 * has not already claimed it.  start_shadow_engine_patch merely launches the
 * existing polling worker; all engine mutation still waits for intercepted
 * RunGame, a non-null OriginalRunGame, and g_NexusReady==0. */
static DWORD WINAPI dllmain_deferred_fallback(void *unused)
{
    (void)unused;
    Sleep(1U);
    if(InterlockedCompareExchange(&g_shadow_engine.bootstrap.initialized,0,0)!=0) return 0;
    InterlockedIncrement(&g_shadow_engine.bootstrap.dllmain_fallback_attempts);
    InterlockedCompareExchange(&g_shadow_engine.bootstrap.nexus_entry_kind,3,0);
    start_shadow_engine_patch();
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE instance,DWORD reason,LPVOID reserved)
{
    (void)reserved;
    if(reason==DLL_PROCESS_ATTACH) {
        HANDLE fallback_thread;
        g_shadow_engine.bootstrap.self=instance;
        DisableThreadLibraryCalls(instance);
        fallback_thread=CreateThread(NULL,0,dllmain_deferred_fallback,
            NULL,0,NULL);
        if(fallback_thread) CloseHandle(fallback_thread);
    }
    return TRUE;
}
