/* Frozen from the accepted v2.0.77 working-source classifier. Test-only.
 * Exact function bytes SHA256: 75141862a6c10369aeb8ffb1b42bf368a694f7e4da4e19910d3af10de0fde58f
 * Callers rename the function through a preprocessor definition. */
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
    DWORD now=sample->tick;
    uint32_t i,j,k,vehicle_count=0,complete_count=0,exact_vehicle_count=0;
    uint32_t registry_vehicle_count=0;
    float player_position[3];
    int player_position_valid=copy_local_player_position(player_position);
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
        candidate->selection_publish_manager_call=match->publish_manager_call;
        candidate->selection_publish_renderer_epoch=match->publish_renderer_epoch;
        candidate->selection_age_ms=now-match->tick;
        candidate->selection_distance_squared=match->distance_squared;
        if(g_shadow_engine.vehicle_diagnostics.player_root_address) {
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
        if(vehicle_lights[i]>=2U) order[complete_count++]=i;
        else ++sample->selection_partial_vehicles;
    }
    for(i=0;i<complete_count;++i)
        for(j=i+1U;j<complete_count;++j)
            if(distances[order[j]]<distances[order[i]]) {
                k=order[i]; order[i]=order[j]; order[j]=k;
            }
    for(i=0;i<sample->copied_candidates;++i) {
        VehicleCandidateRecord *candidate=&sample->records[i];
        if(!candidate->selection_renderer_recent) continue;
        for(j=0;j<vehicle_count;++j)
            if(vehicles[j]==candidate->selection_vehicle) {
                candidate->selection_group_lights=vehicle_lights[j];
                candidate->selection_group_complete=vehicle_lights[j]>=2U;
                break;
            }
        if(!candidate->selection_group_complete) continue;
        for(j=0;j<complete_count;++j)
            if(vehicles[order[j]]==candidate->selection_vehicle) {
                candidate->selection_rank=j+1U;
                candidate->selection_would_keep=j<sample->selection_main_limit+VEHICLE_SELECTION_OVERRIDE_VEHICLES;
                if(candidate->selection_would_keep) ++sample->selection_would_keep_lights;
                break;
            }
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
    }
    sample->selection_vehicles=complete_count;
}

