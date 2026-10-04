import pathlib
import unittest

ROOT=pathlib.Path(__file__).resolve().parents[1]
SHARED=(ROOT/'src/modules/00_shared_config_state.inc').read_text()
SOURCE=(ROOT/'src/modules/18_vehicle_light_diagnostics.inc').read_text()
MANAGER=(ROOT/'src/modules/20_manager_owner_profile.inc').read_text()

class VehicleSelectionTests(unittest.TestCase):
    def test_transactional_limiter_before_manager_contract(self):
        original='g_shadow_engine.hooks.original_manager(manager,vehicle_limiter.head,'
        self.assertLess(MANAGER.index('prepare_vehicle_limiter_chain('),MANAGER.index(original))
        self.assertLess(MANAGER.index(original),MANAGER.index('restore_vehicle_limiter_chain('))
        self.assertIn('atomic_add_long(&g_shadow_engine.renderer.manager_calls,1);',MANAGER)
        self.assertIn('call=InterlockedCompareExchange(&g_shadow_engine.renderer.manager_calls,0,0);',MANAGER)
        self.assertNotIn('LONG call=InterlockedIncrement(&g_shadow_engine.renderer.manager_calls)',MANAGER)
        self.assertIn('candidateMutation=1 worldUnknownPassthrough=1',SOURCE)
        self.assertIn('fullHandleGenerationProven=1',SOURCE)
        self.assertIn('reviewRendererEpochCoverageAndHashSaturation',SOURCE)

    def test_proven_owner_and_full_handle_inputs(self):
        update=SOURCE.split('static void __fastcall hooked_vehicle_light_update')[1].split(
            'static int install_vehicle_owner_observer')[0]
        self.assertLess(update.index('d->original_light_update('),
                        update.index('observe_vehicle_selection('))
        batch=SOURCE.split('static void __fastcall hooked_vehicle_light_batch')[1].split(
            'static const unsigned char g_vehicle_link_copy_prefix')[0]
        self.assertLess(batch.index('d->original_light_batch('),
                        batch.index('publish_vehicle_selection_batch(&scope)'))
        observe=SOURCE.split('static void observe_vehicle_selection')[1].split('static int vehicle_capture_lock')[0]
        for proof in ['skeleton!=scope->skeleton','world_transform!=scope->world_transform',
                      'VEHICLE_BATCH_RETURN_OFFSET','distance%VEHICLE_BATCH_ELEMENT_BYTES',
                      'VEHICLE_OWNER_DESCRIPTOR_HANDLE_OFFSET']:
            self.assertIn(proof,observe)
        self.assertNotIn('append_log',observe)
        self.assertNotIn('WaitFor',observe)
        self.assertNotIn('Sleep(',observe)

    def test_manager_comparison_renderer_epoch_grouping_and_distance_rank(self):
        classify=SOURCE.split('static void classify_vehicle_selection_projected_fields')[1].split(
            'static void update_vehicle_limiter_peak')[0]
        self.assertIn('delta=sample->manager_call-match->publish_manager_call',classify)
        self.assertIn('candidate->selection_current=delta==1',classify)
        self.assertIn('renderer_delta=sample->renderer_epoch-match->publish_renderer_epoch',classify)
        self.assertIn('candidate->selection_renderer_recent=renderer_delta<=3',classify)
        for bucket in ['selection_delta_zero','selection_delta_one',
                       'selection_delta_two_to_four','selection_delta_five_plus',
                       'selection_delta_future']:
            self.assertIn(bucket,classify)
        self.assertIn('renderer_type!=3',classify)
        self.assertIn('selection_candidate_instances!=1U',classify)
        self.assertIn('vehicle_lights[i]>=2U',classify)
        self.assertIn('distances[order[j]]<distances[order[i]]',classify)
        self.assertIn('candidate->selection_would_keep=rank_by_group[j]<=',classify)
        self.assertIn('sample->selection_extra_slots=vehicle_selection_extra_slots()',classify)
        self.assertIn('sample->selection_main_limit+sample->selection_extra_slots',classify)
        self.assertIn('sample->selection_main_limit=shadow_policy_stable_limit()',classify)
        self.assertIn('if(!candidate->selection_renderer_recent || candidate->selection_driver) continue',classify)

    def test_active_registry_and_exact_coverage_are_observational(self):
        classify=SOURCE.split('static void classify_vehicle_selection_projected_fields')[1].split(
            'static void update_vehicle_limiter_peak')[0]
        for field in ['selection_snapshot_vehicles','selection_snapshot_age_le_16',
                      'selection_snapshot_age_17_50','selection_snapshot_age_51_250',
                      'selection_snapshot_age_over_250','selection_exact_matches',
                      'selection_exact_vehicles','selection_exact_complete_vehicles']:
            self.assertIn(field,classify)
        self.assertNotIn('selection_count=',classify)
        self.assertIn('exactMatches=%u exactVehicles=%u exactCompleteVehicles=%u',SOURCE)
        self.assertIn('managerDelta=%ld publishRendererEpoch=%ld rendererDelta=%ld ageMs=%lu',SOURCE)

    def test_bounded_nonwaiting_epoch_hash_registry(self):
        for constant in ['VEHICLE_SELECTION_EPOCH_BANKS 4U',
                         'VEHICLE_SELECTION_SLOTS_PER_EPOCH 512U',
                         'VEHICLE_SELECTION_MAX_VEHICLES 128U',
                         'VEHICLE_SELECTION_STABLE_VEHICLES 4U',
                         'VEHICLE_SELECTION_OVERRIDE_VEHICLES 2U',
                         'VEHICLE_SHADOW_MAX_DIMENSION 2048U']:
            self.assertIn(constant,SHARED)
        self.assertIn('InterlockedCompareExchange(&d->selection_writer_active,1,0)',SOURCE)
        self.assertIn('InterlockedIncrement(&d->selection_gaps)',SOURCE)
        self.assertIn('selection_gap_epoch_tag[bank],renderer_epoch+1',SOURCE)
        self.assertIn('InterlockedIncrement(&d->selection_publish_inflight)',SOURCE)
        self.assertIn('snapshot->publisher_overlap=InterlockedCompareExchange(',SOURCE)
        self.assertIn('vehicle_selection_hash(',SOURCE)
        self.assertIn('InterlockedIncrement(&d->selection_overflow)',SOURCE)
        self.assertIn('selection_overflow_epoch_tag[bank],renderer_epoch+1',SOURCE)
        self.assertNotIn('selection_evictions',SOURCE)
        snapshot=SOURCE.split('static void copy_vehicle_selection_records_locked')[1].split(
            'static void classify_vehicle_selection_sample')[0]
        self.assertIn('VEHICLE_SELECTION_EPOCH_BANKS',snapshot)
        self.assertIn('record->publish_renderer_epoch!=active_epoch',snapshot)
        self.assertNotIn('readable_memory',snapshot)

    def test_unknown_world_and_ambiguity_fail_open(self):
        classify=SOURCE.split('static void classify_vehicle_selection_projected_fields')[1].split(
            'static void update_vehicle_limiter_peak')[0]
        self.assertIn('++sample->selection_unknown',classify)
        self.assertIn('++sample->selection_ambiguous',classify)
        self.assertNotIn('admitted=',classify)
        self.assertNotIn('binding_flags=',classify)
        self.assertNotIn('descriptor_flags=',classify)

    def test_active_filter_is_exact_grouped_and_transactional(self):
        limiter=SOURCE.split('static void prepare_enabled_vehicle_limiter_chain')[1].split(
            'static VehicleCaptureTicket begin_vehicle_candidate_sample')[0]
        self.assertNotIn('snapshot.gaps || snapshot.overflow',limiter)
        self.assertIn('!snapshot.lock_acquired',limiter)
        self.assertIn('snapshot.publisher_overlap',limiter)
        self.assertIn('snapshot.gap_epochs',limiter)
        self.assertIn('snapshot.overflow_epochs',limiter)
        self.assertIn('candidate_count>VEHICLE_CAPTURE_MAX_CANDIDATES',limiter)
        self.assertIn('record->renderer_type==3',limiter)
        self.assertIn('record->selection_group_complete',limiter)
        self.assertIn('record->selection_renderer_recent',limiter)
        self.assertIn('!record->selection_would_keep',limiter)
        self.assertIn('apply_vehicle_residency_policy(chain)',limiter)
        self.assertIn('*(void **)tail=NULL',limiter)
        self.assertIn('*(void **)chain->nodes[i]=chain->original_next[i]',limiter)

    def test_owner_residency_and_partial_evidence_contract(self):
        residency=SOURCE.split('static int apply_vehicle_residency_policy')[1].split(
            'static void prepare_enabled_vehicle_limiter_chain')[0]
        for constant in ['VEHICLE_TRANSITION_HOLD_MS 1000U',
                         'VEHICLE_RESIDENCY_DISTANCE_NUMERATOR 9U',
                         'VEHICLE_RESIDENCY_DISTANCE_DENOMINATOR 10U']:
            self.assertIn(constant,SHARED)
        self.assertIn('if(!chain->degraded)',residency)
        self.assertIn('now-old.admitted_tick>=VEHICLE_TRANSITION_HOLD_MS',residency)
        self.assertIn('if(slot<stable_limit) old.admitted_tick=now',residency)
        self.assertIn('d->residency_slots[slot].vehicle==record->selection_vehicle',residency)
        self.assertIn('record->selection_role=d->residency_slots[slot].role',residency)
        self.assertIn('stable_limit=shadow_policy_stable_limit()',residency)
        self.assertIn('d->residency_stable_count!=stable_limit',residency)
        self.assertNotIn('residency_challenger',residency)
        self.assertNotIn('readable_memory',residency)
        limiter=SOURCE.split('static void prepare_enabled_vehicle_limiter_chain')[1].split(
            'static void restore_vehicle_limiter_chain')[0]
        self.assertIn('chain->degraded=snapshot.publisher_overlap || snapshot.gap_epochs ||',limiter)
        self.assertIn('limiter_degraded_overflow_epoch',limiter)
        pre_copy=limiter.split('for(i=0;i<candidate_count')[0]
        self.assertNotIn('limiter_fail_gap_epoch',pre_copy)
        self.assertNotIn('limiter_fail_overflow_epoch',pre_copy)
        self.assertIn('residency_reset_requested',
                      (ROOT/'src/modules/17_saved_settings.inc').read_text())
        quality=SOURCE.split('static void apply_vehicle_shadow_quality')[1].split(
            'static int vehicle_capture_lock')[0]
        self.assertIn('renderer_type!=1 && renderer_type!=3',quality)
        self.assertIn('lookup_vehicle_quality_identity(',quality)
        self.assertNotIn('distance_squared', quality)
        self.assertIn('width>selected_cap',quality)
        self.assertIn('height>selected_cap',quality)
        self.assertIn('shadow_policy_resolution_from(policy)',quality)
        self.assertIn('quality_unknown_entries',quality)
        self.assertIn('quality_ambiguous_entries',quality)
        batch=SOURCE.split('static void publish_vehicle_selection_batch')[1].split(
            'static int lookup_vehicle_quality_identity')[0]
        self.assertLess(batch.index('if(!vehicle_selection_lock())'),
                        batch.index('publish_vehicle_quality_identity('))
        self.assertLess(batch.index('publish_vehicle_quality_identity('),
                        batch.rindex('vehicle_selection_unlock();'))
        self.assertLess(quality.index('if(!vehicle_selection_lock())'),
                        quality.index('lookup_vehicle_quality_identity('))
        self.assertLess(quality.index('lookup_vehicle_quality_identity('),
                        quality.index('vehicle_selection_unlock();'))
        self.assertNotIn('copy_policy_driver_identity',quality)
        self.assertNotIn('copy_current_driver_identity',quality)
        self.assertNotIn('VehicleQualityCap',quality)
        self.assertIn('QUALITY_DECISION_CAPPED',quality)
        renderer=(ROOT/'src/modules/30_renderer_queue_diagnostics.inc').read_text()
        stages = ['begin_vehicle_queue_sample(queue,original_entries,admitted_entries)',
                  'apply_vehicle_shadow_quality(queue,admitted_entries)',
                  'sample_vehicle_queue_policy_request(vehicle_capture)',
                  'g_shadow_engine.hooks.original_renderer_queue(manager,queue',
                  'finish_vehicle_queue_sample(vehicle_capture)']
        for before, after in zip(stages, stages[1:]):
            self.assertLess(renderer.index(before), renderer.index(after))

    def test_supported_runtime_control_keeps_classification_alive(self):
        runtime=(ROOT/'src/modules/80_runtime_entry.inc').read_text()
        self.assertIn('ShadowEngine_GetControlApiVersion',runtime)
        self.assertIn('ShadowEngine_GetVehicleHeadlightLimiterEnabled',runtime)
        self.assertIn('ShadowEngine_SetVehicleHeadlightLimiterEnabled',runtime)
        limiter=SOURCE.split('static void prepare_vehicle_limiter_chain')[1].split(
            'static void restore_vehicle_limiter_chain')[0]
        self.assertIn('shadow_policy_enabled(SHADOW_POLICY_DISABLE_LIMITER)',limiter)
        self.assertIn('limiter_bypass_disabled',limiter)
        bypass=limiter.index('if(!chain->enabled)')
        enabled_call=limiter.index('prepare_enabled_vehicle_limiter_chain(')
        self.assertLess(bypass,enabled_call)
        self.assertIn('return;',limiter[bypass:enabled_call])
        self.assertNotIn('VehicleSelectionSnapshot',limiter)
        self.assertNotIn('VehicleSelectionProjection',limiter)
        observe=SOURCE.split('static void observe_vehicle_selection')[1].split(
            'static int vehicle_capture_lock')[0]
        self.assertNotIn('limiter_control_state',observe)

    def test_f10_reports_actual_protected_admission_and_vehicle_type1(self):
        classify=SOURCE.split('static void classify_vehicle_selection_projected_fields')[1].split(
            'static void update_vehicle_limiter_peak')[0]
        self.assertIn('candidate->renderer_type!=1',classify)
        self.assertIn('candidate->selection_vehicle_type1=1U',classify)
        self.assertIn('vehicle_diagnostics.state,0,0)!=2',classify)
        summary=SOURCE.split('static void summarize_vehicle_policy_admission')[1].split(
            'static VehicleCaptureTicket begin_vehicle_candidate_sample')[0]
        self.assertIn('source->renderer_type==3 && source->selection_would_keep',summary)
        self.assertIn('source->selection_vehicle_type1',summary)
        self.assertIn('protected_loss_with_vehicle_type1_admitted',summary)
        self.assertIn('finish_vehicle_candidate_sample(vehicle_capture,bindings,&vehicle_limiter)',
                      MANAGER)
        self.assertIn('STAGE_VEHICLE_ADMISSION_SUMMARY',SOURCE)
        self.assertIn('coincidenceNotCausation=1',SOURCE)
        self.assertIn('vehicleType1SirenLikeRoleNotDirectlyProven=1',SOURCE)
        self.assertIn('STAGE_VEHICLE_QUEUE_CORRELATION',SOURCE)
        limiter=SOURCE.split('static void prepare_enabled_vehicle_limiter_chain')[1].split(
            'static void restore_vehicle_limiter_chain')[0]
        self.assertNotIn('selection_vehicle_type1',limiter)

if __name__=='__main__': unittest.main()
