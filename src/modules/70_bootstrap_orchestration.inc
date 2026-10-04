/* Module 70: immutable patch-plan preparation and transactional early/deferred
 * installation orchestration. Runtime entry and monitoring live in module 80. */
typedef struct ShadowPatchPlan {
    const RuntimeProfile *profile;
    const unsigned char *frame_builder_prologue;
    size_t frame_builder_prologue_size;
    int lua_frame_enabled;
    int prepared;
} ShadowPatchPlan;

static ShadowPatchPlan g_patch_plan;

static int prepare_early_patch_plan(HMODULE disrupt,ShadowPatchPlan *plan)
{
    unsigned char *base=(unsigned char *)disrupt;
    if(!plan) return 0;
    memset(plan,0,sizeof(*plan));
    g_shadow_engine.bootstrap.disrupt_base=base;
    if(!select_runtime_profile(base)) {
        run_signature_probe(base);
        append_log("STAGE_D_ABORT reason=unknownRuntimeProfile diagnosticMapComplete=1 noEngineMutation=1");
        return 0;
    }
    if(!recover_owner_vector_reserve()) {
        append_log("RUNTIME_PROFILE_REJECTED profile=%ld reason=ownerReserveCallsiteRecoveryFailed action=readOnlyFullSiteMap noEngineMutation=1",
            (long)runtime_profile_id());
        run_signature_probe(base);
        append_log("STAGE_D_ABORT reason=ownerReserveCallsiteRecoveryFailed diagnosticMapComplete=1 noEngineMutation=1");
        return 0;
    }
    if(!validate_selected_profile_helpers()) {
        append_log("RUNTIME_PROFILE_REJECTED profile=%ld reason=helperValidationFailed action=readOnlyFullSiteMap noEngineMutation=1",
            (long)runtime_profile_id());
        run_signature_probe(base);
        append_log("STAGE_D_ABORT reason=selectedProfileHelperMismatch diagnosticMapComplete=1 noEngineMutation=1");
        return 0;
    }
    if(memcmp(ENGINE_ADDRESS(RESOURCE_CONSTRUCTOR_RVA),g_resource_prologue,
              sizeof(g_resource_prologue))!=0) {
        append_log("STAGE_D_ABORT signature=resource_constructor expectedBuildMismatch=1 noEngineMutation=1");
        return 0;
    }
    if(memcmp(ENGINE_ADDRESS(REGISTER_PASS_RVA),g_register_pass_prologue,
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
#if SHADOW_ENGINE_INTERNAL_DIAGNOSTICS
    if(!prepare_lua_frame_repair(g_runtime_state.selected,&plan->lua_frame_enabled)) return 0;
    if(!validate_intersection_sites()) {
        append_log("STAGE_E_ABORT signature=intersectionLookup noEngineMutation=1");
        return 0;
    }
#endif
    append_log("STAGE_M_PREFLIGHT_COMPLETE signatures=resource,pass,mapLoop,earlyQueueLayout,sliceResultStores,deferredHandleConsumers,deferredPassIndexConsumers,deferredV1216Scheduler,deferredShadowManager,rendererQueue,faceCostReader,resourceWrapperDirect,renderRecordConstructor,frameBuilder,renderRecordRelease requiredPassMaxKey=0x%04X requiredPassSlots=%u passTableVerification=readOnly diagnostics=minimal tailRelocations=%u noEngineMutationBeforeThisMarker=1",
        REQUIRED_PASS_MAX_KEY,REQUIRED_PASS_TABLE_SLOTS,
        (unsigned)(sizeof(g_tail_patches)/sizeof(g_tail_patches[0])));
    plan->profile=g_runtime_state.selected;
    plan->frame_builder_prologue=runtime_frame_builder_prologue(
        &plan->frame_builder_prologue_size);
    plan->prepared=plan->profile && plan->frame_builder_prologue &&
        plan->frame_builder_prologue_size>0;
    return plan->prepared;
}
static int commit_early_patch_plan(const ShadowPatchPlan *plan)
{
    PatchTransaction transaction;
    PatchRollbackResult rollback;
    const char *failed_step=NULL;
#if SHADOW_ENGINE_INTERNAL_DIAGNOSTICS
    int lua_frame_enabled=plan?plan->lua_frame_enabled:0;
#endif
    if(!plan || !plan->prepared || plan->profile!=g_runtime_state.selected) {
        append_log("STAGE_E_ABORT mutation=early_commit invalidPreparedPlan=1 noEngineMutation=1");
        return 0;
    }
    if(!patch_transaction_begin(&transaction,"early",-1)) {
        append_log("STAGE_E_ABORT mutation=early_commit transactionBusy=1 noEngineMutation=1");
        return 0;
    }
    if(!apply_queue_layout()) {
        failed_step="early_queue_layout";
        goto rollback_early;
    }
    append_log("STAGE_E_EARLY_QUEUE_LAYOUT_APPLIED beforeQueueConstruction=1 physicalQueueEntries=%u allocationBytes=0x%X relocatedTailReferences=%u tailInitializedByNativeConstructor=1",
        PHYSICAL_QUEUE_ENTRIES,RENDER_QUEUE_ALLOCATION_BYTES,
        (unsigned)(sizeof(g_tail_patches)/sizeof(g_tail_patches[0])));
    if(!install_map_range_loop_extension()) {
        failed_step="native_map_loop_extension";
        goto rollback_early;
    }
    if(!install_detour(ENGINE_ADDRESS(RENDER_RECORD_CONSTRUCTOR_RVA),
        g_render_record_constructor_prologue,
        sizeof(g_render_record_constructor_prologue),
        hooked_external_render_record_constructor,
        (void **)&g_shadow_engine.hooks.original_render_record_constructor)) {
        failed_step="render_record_constructor";
        goto rollback_early;
    }
    if(!install_detour(ENGINE_ADDRESS(RESOURCE_CONSTRUCTOR_RVA),g_resource_prologue,
                       sizeof(g_resource_prologue),hooked_resource_constructor,
                       (void **)&g_shadow_engine.hooks.original_resource_constructor)) {
        failed_step="resource_constructor";
        goto rollback_early;
    }
    if(!install_detour(ENGINE_ADDRESS(REGISTER_PASS_RVA),g_register_pass_prologue,
                       sizeof(g_register_pass_prologue),hooked_register_pass,
                       (void **)&g_shadow_engine.hooks.original_register_pass)) {
        failed_step="register_pass";
        goto rollback_early;
    }
#if SHADOW_ENGINE_INTERNAL_DIAGNOSTICS
    if(!apply_lua_frame_repair(plan->profile,&lua_frame_enabled)) {
        failed_step="lua_frame_repair";
        goto rollback_early;
    }
#endif
    if(!patch_transaction_commit(&transaction)) {
        failed_step="transaction_commit";
        goto rollback_early;
    }
    InterlockedExchange(&g_shadow_engine.bootstrap.hooks_installed,1);
#if SHADOW_ENGINE_INTERNAL_DIAGNOSTICS
    if(lua_frame_enabled)
        append_log("LUA_FRAME_REPAIR_ACTIVE version=%s profile=%s sites=%u metadataByte=12 tracing=0 experimental=1 runtimeAcceptancePending=1",
            PATCH_VERSION,plan->profile->name,(unsigned)plan->profile->lua_frame_site_count);
#endif
    append_log("STAGE_M_EARLY_HOOKS_INSTALLED nativeLoopMaps=%u nativeManagerMaps=%u externalMaps=%u pairedExtraPasses=%u passTableAllocatorHook=0 readOnlyPassTableVerification=1 requiredPassSlots=%u earlyQueueLayout=%u renderRecordConstructorExternalState=1 diagnostics=minimal downstreamHandleRouting=0 downstreamPassRouting=0 schedulerChanged=0 managerHook=0 transactionWrites=%u transactionAllocations=%u rollbackAvailableUntilCommit=1",
        TOTAL_LOCAL_MAPS,ORIGINAL_LOCAL_MAPS,EXTRA_LOCAL_MAPS,
        EXTRA_PASS_SLOT_COUNT,REQUIRED_PASS_TABLE_SLOTS,
        PHYSICAL_QUEUE_ENTRIES,(unsigned)transaction.write_count,
        (unsigned)transaction.allocation_count);
    return 1;

rollback_early:
    rollback=patch_transaction_rollback(&transaction);
    InterlockedExchange(&g_shadow_engine.bootstrap.hooks_installed,
        rollback.failed_restores==0 ? 0:-1);
    append_log("STAGE_E_TRANSACTION_ROLLBACK phase=early failedStep=%s appliedWrites=%u restoredWrites=%u failedRestores=%u releasedAllocations=%u rollbackComplete=%u beforeQueueConstruction=1",
        failed_step ? failed_step:"unknown",
        (unsigned)transaction.write_count,
        (unsigned)rollback.restored_writes,
        (unsigned)rollback.failed_restores,
        (unsigned)rollback.released_allocations,
        rollback.failed_restores==0 ? 1U:0U);
    return 0;
}

static int install_early_construction_hooks(HMODULE disrupt)
{
    if(!prepare_early_patch_plan(disrupt,&g_patch_plan)) return 0;
    return commit_early_patch_plan(&g_patch_plan);
}

static int install_deferred_downstream(void)
{
    PatchTransaction transaction;
    PatchRollbackResult rollback;
    const char *failed_step=NULL;
    const unsigned char *frame_builder_prologue=
        g_patch_plan.frame_builder_prologue;
    size_t frame_builder_prologue_size=
        g_patch_plan.frame_builder_prologue_size;
    if(InterlockedCompareExchange(&g_shadow_engine.bootstrap.downstream_state,1,0)!=0) return 0;
    if(!g_patch_plan.prepared ||
       g_patch_plan.profile!=g_runtime_state.selected ||
       !frame_builder_prologue || !frame_builder_prologue_size) {
        append_log("STAGE_E_DOWNSTREAM_ABORT invalidPreparedPlan=1 noDeferredMutation=1");
        InterlockedExchange(&g_shadow_engine.bootstrap.downstream_state,-1);
        return 0;
    }
    if(!validate_deferred_sites_after_early_queue(g_shadow_engine.bootstrap.disrupt_base)) {
        append_log("STAGE_E_DOWNSTREAM_ABORT signaturesChangedDuringDeferral=1");
        InterlockedExchange(&g_shadow_engine.bootstrap.downstream_state,-1);
        return 0;
    }
#if SHADOW_ENGINE_INTERNAL_DIAGNOSTICS
    QueryPerformanceFrequency(
        &g_shadow_engine.completion_diagnostics.qpc_frequency);
#endif
    /* Optional observer: complete read-only discovery before deferred writes.
     * Unproven profiles retain the original F10 and all existing engine gates. */
    validate_vehicle_owner_sites();
    InterlockedExchange(&g_shadow_engine.policy_control.quality_ready,
        validate_vehicle_resolution_sites() && validate_vehicle_handle_link_sites());
    if(!patch_transaction_begin(&transaction,"deferred",-1)) {
        append_log("STAGE_E_DOWNSTREAM_ABORT transactionBusy=1 noDeferredMutation=1");
        InterlockedExchange(&g_shadow_engine.bootstrap.downstream_state,-1);
        return 0;
    }
#if SHADOW_ENGINE_INTERNAL_DIAGNOSTICS
    if(!install_intersection_observer()) {
        failed_step="intersection_lookup";
        goto rollback_deferred;
    }
#endif
    if(!install_vehicle_owner_observer()) {
        failed_step="vehicle_owner_observer";
        goto rollback_deferred;
    }
    /* Inline SliceExecute relays become callable as soon as their transaction
     * writes land. Publish the already-preflighted native wrapper first so a
     * completion rejected during the remaining install window can still
     * retire its acquisition. */
    g_shadow_engine.hooks.original_resource_wrapper=(ResourceWrapperFn)
        ENGINE_ADDRESS(RESOURCE_WRAPPER_RVA);
    if(!install_frame_graph_tail_relay()) {
        failed_step="frame_graph_tail_relay";
        goto rollback_deferred;
    }
    if(!install_slice_result_bounds_relays()) {
        failed_step="slice_result_bounds_relays";
        goto rollback_deferred;
    }
#if SHADOW_ENGINE_INTERNAL_DIAGNOSTICS
    if(!install_detour(ENGINE_ADDRESS(SHADOW_FACE_SCHEDULER_RVA),
        g_shadow_face_scheduler_prologue,
        sizeof(g_shadow_face_scheduler_prologue),
        hooked_shadow_face_scheduler,
        (void **)&g_shadow_engine.hooks.original_shadow_face_scheduler)) {
        failed_step="shadow_face_scheduler_diagnostic";
        goto rollback_deferred;
    }
#endif
    if(!install_handle_relays()) {
        failed_step="handle_relays";
        goto rollback_deferred;
    }
    if(!install_pass_index_relays()) {
        failed_step="pass_index_relays";
        goto rollback_deferred;
    }
    if(!apply_v1216_scheduler()) {
        failed_step="scheduler";
        goto rollback_deferred;
    }
    g_shadow_engine.hooks.profile_callback=(ProfileCallbackFn)ENGINE_ADDRESS(PROFILE_CALLBACK_RVA);
    g_shadow_engine.hooks.owner_vector_reserve=(OwnerVectorReserveFn)
        (g_shadow_engine.bootstrap.disrupt_base+g_runtime_state.owner_vector_reserve_rva);
    if(!install_manager_detour()) {
        failed_step="manager_detour";
        goto rollback_deferred;
    }
    if(!install_detour(ENGINE_ADDRESS(RENDER_QUEUE_PROCESS_RVA),
        g_renderer_queue_prologue,sizeof(g_renderer_queue_prologue),
        hooked_renderer_queue,(void **)&g_shadow_engine.hooks.original_renderer_queue)) {
        failed_step="renderer_queue";
        goto rollback_deferred;
    }
    if(!install_detour(ENGINE_ADDRESS(FACE_COST_READER_RVA),
        g_face_cost_reader_prologue,sizeof(g_face_cost_reader_prologue),
        hooked_face_cost_reader,(void **)&g_shadow_engine.hooks.original_face_cost_reader)) {
        failed_step="face_cost_reader";
        goto rollback_deferred;
    }
    if(!install_detour(ENGINE_ADDRESS(FRAME_BUILDER_RVA),
        frame_builder_prologue,frame_builder_prologue_size,
        hooked_external_frame_builder,(void **)&g_shadow_engine.hooks.original_frame_builder)) {
        failed_step="frame_builder";
        goto rollback_deferred;
    }
    if(!install_detour(ENGINE_ADDRESS(RENDER_RECORD_RELEASE_RVA),
        g_render_record_release_prologue,
        sizeof(g_render_record_release_prologue),
        hooked_external_render_record_release,
        (void **)&g_shadow_engine.hooks.original_render_record_release)) {
        failed_step="render_record_release";
        goto rollback_deferred;
    }
    if(!patch_transaction_commit(&transaction)) {
        failed_step="transaction_commit";
        goto rollback_deferred;
    }
    InterlockedExchange(&g_shadow_engine.external_results.lifecycle_hooks_state,2);
#if SHADOW_ENGINE_INTERNAL_DIAGNOSTICS
    InterlockedExchange(&g_shadow_engine.completion_diagnostics.hook_state,2);
#endif
    InterlockedExchange(&g_shadow_engine.renderer.renderer_hook_state,2);
    InterlockedExchange(&g_shadow_engine.resources.routing_state,2);
    InterlockedExchange(&g_shadow_engine.bootstrap.downstream_state,2);
#if SHADOW_ENGINE_INTERNAL_DIAGNOSTICS
    append_log("STAGE_INTERSECTION_READY controls=ShadowEngineMenu functionKeys=disabled default=off policyChange=0");
#endif
    append_log("STAGE_QUALITY_READY enabled=%ld preflight=startup independentOfCapture=1",
        (long)g_shadow_engine.policy_control.quality_ready);
    append_log("STAGE_M_DOWNSTREAM_INSTALLED afterConstructors=1 stabilizationMs=%u physicalQueueEntries=%u queueLayoutAppliedBeforeConstruction=1 physicalFaceBudget=%u wholeRecordPriorityPrefix=1 queueCountRestoredAfterRender=1 relocatedTailReferences=%u handleConsumers=3 passIndexConsumers=2 sliceResultBoundsRelays=2 nativeSliceResults=%u externalSliceResults=%u routedSliceIndices=%u-%u externalConsumer=frameGraphPreFinalizer callsiteRva=0x%08X executeFrameGraphEntryDetour=0 rejectedNonNullCleanup=nativeWrapperExactContext nonNullStoreRva=0x2CC316 nullStoreRva=0x2CC32B shadowFaceSchedulerDiagnostic=%u schedulerDiagnosticRva=0x%08X schedulerInit=%u schedulerFloor=%u schedulerUpper=%u schedulerUpperRva=0x2E9C3B cacheOwnerCeiling=%u ownerVectorPhysicalReserve=%u lifecycleRecordSlots=%u manualQueueCapture=%u repair=0 sidecars=0 diagnostics=boundedCompletion transactionWrites=%u transactionAllocations=%u rollbackAvailableUntilCommit=1",
        DOWNSTREAM_STABILIZE_MS,PHYSICAL_QUEUE_ENTRIES,TOTAL_LOCAL_MAPS,
        (unsigned)(sizeof(g_tail_patches)/sizeof(g_tail_patches[0])),
        NATIVE_SLICE_RESULT_COUNT,EXTRA_SLICE_RESULT_COUNT,
        NATIVE_SLICE_RESULT_COUNT,TOTAL_LOCAL_MAPS-1U,
        (unsigned)RESOLVED_ENGINE_RVA(
            FRAME_GRAPH_PRE_FINALIZER_RVA),
        SHADOW_ENGINE_INTERNAL_DIAGNOSTICS,
        (unsigned)RESOLVED_ENGINE_RVA(SHADOW_FACE_SCHEDULER_RVA),
        TARGET_DYNAMIC_B4,TARGET_DYNAMIC_B4,TARGET_DYNAMIC_B4,
        TARGET_CACHE_A8,OWNER_VECTOR_RESERVE_CAPACITY,
        LIFECYCLE_RECORD_SLOTS,
        SHADOW_ENGINE_INTERNAL_DIAGNOSTICS,
        (unsigned)transaction.write_count,
        (unsigned)transaction.allocation_count);
    return 1;

rollback_deferred:
    rollback=patch_transaction_rollback(&transaction);
    if(rollback.failed_restores==0) {
#if SHADOW_ENGINE_INTERNAL_DIAGNOSTICS
        rollback_intersection_observer();
#endif
        rollback_vehicle_owner_observer();
        g_shadow_engine.hooks.profile_callback=NULL;
        g_shadow_engine.hooks.owner_vector_reserve=NULL;
        g_shadow_engine.hooks.original_resource_wrapper=NULL;
        g_shadow_engine.hooks.original_shadow_face_scheduler=NULL;
        InterlockedExchange(&g_shadow_engine.external_results.slice_result_guard_state,-1);
        InterlockedExchange(&g_shadow_engine.external_results.lifecycle_hooks_state,-1);
#if SHADOW_ENGINE_INTERNAL_DIAGNOSTICS
        InterlockedExchange(&g_shadow_engine.completion_diagnostics.hook_state,-1);
#endif
        InterlockedExchange(&g_shadow_engine.renderer.renderer_hook_state,-1);
        InterlockedExchange(&g_shadow_engine.resources.routing_state,-1);
        InterlockedExchange(&g_shadow_engine.bootstrap.downstream_state,-1);
    } else {
        /* Preserve helper/original pointers for any hook whose executable
         * bytes could not be restored. Mark the phase unrecoverable rather
         * than creating a use-after-free/null-call path. */
        InterlockedExchange(&g_shadow_engine.external_results.slice_result_guard_state,-2);
        InterlockedExchange(&g_shadow_engine.external_results.lifecycle_hooks_state,-2);
#if SHADOW_ENGINE_INTERNAL_DIAGNOSTICS
        InterlockedExchange(&g_shadow_engine.completion_diagnostics.hook_state,-2);
#endif
        InterlockedExchange(&g_shadow_engine.renderer.renderer_hook_state,-2);
        InterlockedExchange(&g_shadow_engine.resources.routing_state,-2);
        InterlockedExchange(&g_shadow_engine.bootstrap.downstream_state,-2);
    }
    append_log("STAGE_E_TRANSACTION_ROLLBACK phase=deferred failedStep=%s appliedWrites=%u restoredWrites=%u failedRestores=%u releasedAllocations=%u rollbackComplete=%u earlyPhaseRetained=1",
        failed_step ? failed_step:"unknown",
        (unsigned)transaction.write_count,
        (unsigned)rollback.restored_writes,
        (unsigned)rollback.failed_restores,
        (unsigned)rollback.released_allocations,
        rollback.failed_restores==0 ? 1U:0U);
    return 0;
}
