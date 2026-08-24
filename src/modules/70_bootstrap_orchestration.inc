/* Module 70: immutable patch-plan preparation and transactional early/deferred
 * installation orchestration. Runtime entry and monitoring live in module 80. */
typedef struct ShadowPatchPlan {
    const RuntimeProfile *profile;
    const unsigned char *frame_builder_prologue;
    size_t frame_builder_prologue_size;
    int prepared;
} ShadowPatchPlan;

static ShadowPatchPlan g_patch_plan;

static int prepare_early_patch_plan(HMODULE disrupt,ShadowPatchPlan *plan)
{
    unsigned char *base=(unsigned char *)disrupt;
    if(!plan) return 0;
    memset(plan,0,sizeof(*plan));
    g_disrupt_base=base;
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
    append_log("STAGE_M_PREFLIGHT_COMPLETE signatures=resource,pass,mapLoop,earlyQueueLayout,sliceResultStores,deferredHandleConsumers,deferredPassIndexConsumers,deferredV1216Scheduler,deferredShadowManager,rendererQueue,faceCostReader,resourceWrapperDirect,renderRecordConstructor,frameBuilder,postCallObjectRenderers,renderRecordRelease requiredPassMaxKey=0x%04X requiredPassSlots=%u passTableVerification=readOnly diagnostics=minimal tailRelocations=%u noEngineMutationBeforeThisMarker=1",
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
    append_log("STAGE_E_EARLY_QUEUE_LAYOUT_APPLIED beforeQueueConstruction=1 physicalQueueEntries=25 allocationBytes=0x397C0 relocatedTailReferences=%u tailInitializedByNativeConstructor=1",
        (unsigned)(sizeof(g_tail_patches)/sizeof(g_tail_patches[0])));
    if(!install_map_range_loop_extension()) {
        failed_step="native_map_loop_extension";
        goto rollback_early;
    }
    if(!install_detour(ENGINE_ADDRESS(RENDER_RECORD_CONSTRUCTOR_RVA),
        g_render_record_constructor_prologue,
        sizeof(g_render_record_constructor_prologue),
        hooked_external_render_record_constructor,
        (void **)&g_original_render_record_constructor)) {
        failed_step="render_record_constructor";
        goto rollback_early;
    }
    if(!install_detour(ENGINE_ADDRESS(RESOURCE_CONSTRUCTOR_RVA),g_resource_prologue,
                       sizeof(g_resource_prologue),hooked_resource_constructor,
                       (void **)&g_original_resource_constructor)) {
        failed_step="resource_constructor";
        goto rollback_early;
    }
    if(!install_detour(ENGINE_ADDRESS(REGISTER_PASS_RVA),g_register_pass_prologue,
                       sizeof(g_register_pass_prologue),hooked_register_pass,
                       (void **)&g_original_register_pass)) {
        failed_step="register_pass";
        goto rollback_early;
    }
    if(!patch_transaction_commit(&transaction)) {
        failed_step="transaction_commit";
        goto rollback_early;
    }
    InterlockedExchange(&g_hooks_installed,1);
    append_log("STAGE_M_EARLY_HOOKS_INSTALLED nativeLoopMaps=24 nativeManagerMaps=16 externalMaps=8 pairedExtraPasses=16 passTableAllocatorHook=0 readOnlyPassTableVerification=1 requiredPassSlots=50 earlyQueueLayout=25 renderRecordConstructorExternalState=1 diagnostics=minimal downstreamHandleRouting=0 downstreamPassRouting=0 schedulerChanged=0 managerHook=0 transactionWrites=%u transactionAllocations=%u rollbackAvailableUntilCommit=1",
        (unsigned)transaction.write_count,
        (unsigned)transaction.allocation_count);
    return 1;

rollback_early:
    rollback=patch_transaction_rollback(&transaction);
    InterlockedExchange(&g_hooks_installed,
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
    HMODULE user32;
    PatchTransaction transaction;
    PatchRollbackResult rollback;
    const char *failed_step=NULL;
    const unsigned char *frame_builder_prologue=
        g_patch_plan.frame_builder_prologue;
    size_t frame_builder_prologue_size=
        g_patch_plan.frame_builder_prologue_size;
    if(InterlockedCompareExchange(&g_downstream_state,1,0)!=0) return 0;
    if(!g_patch_plan.prepared ||
       g_patch_plan.profile!=g_runtime_state.selected ||
       !frame_builder_prologue || !frame_builder_prologue_size) {
        append_log("STAGE_E_DOWNSTREAM_ABORT invalidPreparedPlan=1 noDeferredMutation=1");
        InterlockedExchange(&g_downstream_state,-1);
        return 0;
    }
    if(!validate_deferred_sites_after_early_queue(g_disrupt_base)) {
        append_log("STAGE_E_DOWNSTREAM_ABORT signaturesChangedDuringDeferral=1");
        InterlockedExchange(&g_downstream_state,-1);
        return 0;
    }
    if(!patch_transaction_begin(&transaction,"deferred",-1)) {
        append_log("STAGE_E_DOWNSTREAM_ABORT transactionBusy=1 noDeferredMutation=1");
        InterlockedExchange(&g_downstream_state,-1);
        return 0;
    }
    if(!install_slice_result_bounds_relays()) {
        failed_step="slice_result_bounds_relays";
        goto rollback_deferred;
    }
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
    g_profile_callback=(ProfileCallbackFn)ENGINE_ADDRESS(PROFILE_CALLBACK_RVA);
    g_owner_vector_reserve=(OwnerVectorReserveFn)
        (g_disrupt_base+g_runtime_state.owner_vector_reserve_rva);
    if(!install_manager_detour()) {
        failed_step="manager_detour";
        goto rollback_deferred;
    }
    if(!install_detour(ENGINE_ADDRESS(RENDER_QUEUE_PROCESS_RVA),
        g_renderer_queue_prologue,sizeof(g_renderer_queue_prologue),
        hooked_renderer_queue,(void **)&g_original_renderer_queue)) {
        failed_step="renderer_queue";
        goto rollback_deferred;
    }
    if(!install_detour(ENGINE_ADDRESS(FACE_COST_READER_RVA),
        g_face_cost_reader_prologue,sizeof(g_face_cost_reader_prologue),
        hooked_face_cost_reader,(void **)&g_original_face_cost_reader)) {
        failed_step="face_cost_reader";
        goto rollback_deferred;
    }
    g_original_resource_wrapper=(ResourceWrapperFn)
        ENGINE_ADDRESS(RESOURCE_WRAPPER_RVA);
    if(!install_detour(ENGINE_ADDRESS(FRAME_BUILDER_RVA),
        frame_builder_prologue,frame_builder_prologue_size,
        hooked_external_frame_builder,(void **)&g_original_frame_builder)) {
        failed_step="frame_builder";
        goto rollback_deferred;
    }
    if(!install_detour(ENGINE_ADDRESS(POST_CALL_OBJECT_RENDERERS_RVA),
        g_post_call_object_renderers_prologue,
        sizeof(g_post_call_object_renderers_prologue),
        hooked_external_post_call_object_renderers,
        (void **)&g_original_post_call_object_renderers)) {
        failed_step="post_call_object_renderers";
        goto rollback_deferred;
    }
    if(!install_detour(ENGINE_ADDRESS(RENDER_RECORD_RELEASE_RVA),
        g_render_record_release_prologue,
        sizeof(g_render_record_release_prologue),
        hooked_external_render_record_release,
        (void **)&g_original_render_record_release)) {
        failed_step="render_record_release";
        goto rollback_deferred;
    }
    if(!patch_transaction_commit(&transaction)) {
        failed_step="transaction_commit";
        goto rollback_deferred;
    }
    InterlockedExchange(&g_lifecycle_hooks_state,2);
    user32=GetModuleHandleW(L"user32.dll");
    if(!user32) user32=LoadLibraryW(L"user32.dll");
    if(user32) g_get_async_key_state=(GetAsyncKeyStateFn)
        GetProcAddress(user32,"GetAsyncKeyState");
    InterlockedExchange(&g_renderer_hook_state,2);
    InterlockedExchange(&g_routing_state,2);
    InterlockedExchange(&g_downstream_state,2);
    append_log("STAGE_M_DOWNSTREAM_INSTALLED afterConstructors=1 stabilizationMs=%u physicalQueueEntries=25 queueLayoutAppliedBeforeConstruction=1 physicalFaceBudget=24 wholeRecordPriorityPrefix=1 queueCountRestoredAfterRender=1 relocatedTailReferences=%u handleConsumers=3 passIndexConsumers=2 sliceResultBoundsRelays=2 nativeSliceResults=%u externalSliceResults=7 routedSliceIndices=17-23 externalConsumer=postCallObjectRenderers releaseFallback=renderRecordRelease nonNullStoreRva=0x2CC316 nullStoreRva=0x2CC32B schedulerInit=16 schedulerFloor=16 schedulerUpper=16 schedulerUpperRva=0x2E9C3B cacheOwnerCeiling=4 ownerVectorPhysicalReserve=8 ownerRecordStride=0x700 reserveRva=0x319160 rendererCapacityProbe=1 queueTailIntegrityProbe=1 nullFaceCostGuard=1 nativeWrapperDirectCall=1 resourceWrapperDetour=0 resourceSubmitDetour=0 renderRecordLifecycleHooks=constructor,builder,postCallObjectRenderers,release lifecycleRecordSlots=%u manualQueueCapture=%u blackKey=F8 recoveredKey=F9 repair=0 sidecars=0 taskProvenance=0 resourceTrace=0 sharedTextureTrace=0 frameGraphTrace=0 acquireHook=0 diagnostics=minimal firstManagerProfileCallback=1 fullCandidatePassthrough=1 filtering=0 limiter=0 transactionWrites=%u transactionAllocations=%u rollbackAvailableUntilCommit=1",
        DOWNSTREAM_STABILIZE_MS,
        (unsigned)(sizeof(g_tail_patches)/sizeof(g_tail_patches[0])),
        NATIVE_SLICE_RESULT_COUNT,
        LIFECYCLE_RECORD_SLOTS,
        g_get_async_key_state ? 1U : 0U,
        (unsigned)transaction.write_count,
        (unsigned)transaction.allocation_count);
    return 1;

rollback_deferred:
    rollback=patch_transaction_rollback(&transaction);
    if(rollback.failed_restores==0) {
        g_profile_callback=NULL;
        g_owner_vector_reserve=NULL;
        g_original_resource_wrapper=NULL;
        InterlockedExchange(&g_slice_result_guard_state,-1);
        InterlockedExchange(&g_lifecycle_hooks_state,-1);
        InterlockedExchange(&g_renderer_hook_state,-1);
        InterlockedExchange(&g_routing_state,-1);
        InterlockedExchange(&g_downstream_state,-1);
    } else {
        /* Preserve helper/original pointers for any hook whose executable
         * bytes could not be restored. Mark the phase unrecoverable rather
         * than creating a use-after-free/null-call path. */
        InterlockedExchange(&g_slice_result_guard_state,-2);
        InterlockedExchange(&g_lifecycle_hooks_state,-2);
        InterlockedExchange(&g_renderer_hook_state,-2);
        InterlockedExchange(&g_routing_state,-2);
        InterlockedExchange(&g_downstream_state,-2);
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
