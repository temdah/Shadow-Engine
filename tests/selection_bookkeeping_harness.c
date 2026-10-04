/* Actual classifier versus the frozen accepted implementation. No game hooks,
 * files, worker threads, settings requests or native rendering are invoked. */
#include "../src/shadow_engine_patch.c"
#include <assert.h>

#define classify_vehicle_selection_projected_fields reference_selection_classifier_v77
#include "reference/selection_classifier_v77.inc"
#undef classify_vehicle_selection_projected_fields

#define REQUIRE(condition) do { assert(condition); ++checks; } while(0)
static unsigned checks,comparisons,case_number;
static uint64_t collision_owners[VEHICLE_SELECTION_MAX_VEHICLES+1U];
static unsigned char anchor_root[16],anchor_array[8],anchor_controller[16];
static unsigned char anchor_data[24],anchor_reference[16],anchor_entity[96];
static void *anchor_address;

typedef struct BookkeepingFixture {
    VehicleCandidateSample input,reference,actual;
    VehicleSelectionSnapshot snapshot,snapshot_before;
    VehicleSelectionProjection projection,projection_before;
} BookkeepingFixture;

static float float_bits(uint32_t bits)
{
    float result;
    memcpy(&result,&bits,sizeof(result));
    return result;
}

static void select_limit(unsigned limit)
{
    /* This harness owns isolated policy storage. Do not start the production
     * asynchronous settings worker just to establish a classifier input. */
    g_shadow_engine.policy_control.disabled=limit ?
        (LONG)(limit<<SHADOW_POLICY_LIMIT_SHIFT):SHADOW_POLICY_DISABLE_LIMITER;
}

static void select_anchor(unsigned mode)
{
    memset(anchor_root,0,sizeof(anchor_root));
    memset(anchor_array,0,sizeof(anchor_array));
    memset(anchor_controller,0,sizeof(anchor_controller));
    memset(anchor_data,0,sizeof(anchor_data));
    memset(anchor_reference,0,sizeof(anchor_reference));
    memset(anchor_entity,0,sizeof(anchor_entity));
    *(void **)anchor_root=anchor_array;
    *(uint32_t *)(anchor_root+8)=1U;
    *(void **)anchor_array=anchor_controller;
    *(void **)anchor_controller=anchor_root; /* Unused field stays valid. */
    *(void **)(anchor_controller+8)=anchor_data;
    *(void **)(anchor_data+16)=anchor_reference;
    *(uint64_t *)anchor_reference=mode==2U?UINT64_MAX:123U;
    *(void **)(anchor_reference+8)=anchor_entity;
    *(float *)(anchor_entity+0x50)=10.0f;
    *(float *)(anchor_entity+0x54)=20.0f;
    *(float *)(anchor_entity+0x58)=30.0f;
    anchor_address=anchor_root;
    g_shadow_engine.vehicle_diagnostics.player_root_address=mode ?
        (unsigned char *)&anchor_address:NULL;
}

static void initialize_collision_owners(void)
{
    uint64_t owner=1;
    unsigned count=0;
    while(count<ARRAY_COUNT(collision_owners)) {
        if((vehicle_selection_hash(owner,0U)&(VEHICLE_GROUP_BUCKETS-1U))==0U)
            collision_owners[count++]=owner;
        ++owner;
    }
}

static void make_input(BookkeepingFixture *fixture,unsigned count,unsigned pattern)
{
    unsigned i;
    VehicleCandidateSample *sample=&fixture->input;
    VehicleSelectionSnapshot *snapshot=&fixture->snapshot;
    memset(fixture,0,sizeof(*fixture));
    sample->copied_candidates=count;
    sample->reported_candidates=count;
    sample->manager_call=120;
    sample->renderer_epoch=100;
    sample->tick=pattern==9U?4U:1000U;
    snapshot->count=count;
    snapshot->lock_acquired=1U;
    snapshot->gaps=7; snapshot->overflow=3; snapshot->rotations=19;
    snapshot->same_epoch=count/2U;
    snapshot->previous_epoch=count/4U;
    snapshot->older_epoch=count-snapshot->same_epoch-snapshot->previous_epoch;
    for(i=0;i<count;++i) {
        VehicleCandidateRecord *candidate=&sample->records[i];
        VehicleSelectionRecord *record=&snapshot->records[i];
        unsigned group=i/2U;
        candidate->candidate=(void *)(uintptr_t)(0x100000U+32U*i);
        candidate->next=(void *)(uintptr_t)(0x100020U+32U*i);
        candidate->descriptor=(void *)(uintptr_t)(0x200000U+64U*i);
        candidate->spatial=(void *)(uintptr_t)(0x300000U+64U*i);
        candidate->renderer_type=3;
        candidate->readable=1U;
        candidate->descriptor_handle_valid=1U;
        candidate->descriptor_handle=0x1234567800000000ULL+i;
        record->descriptor_handle=candidate->descriptor_handle;
        record->vehicle=0xABC00000000ULL+group;
        record->distance_squared=(float)((count/2U-group)*4U+1U);
        record->position[0]=(float)group;
        record->position[1]=2.0f; record->position[2]=3.0f;
        record->position_valid=1U;
        record->publish_manager_call=119;
        record->publish_renderer_epoch=100;
        record->tick=990U;
        record->observations=1U;
        if(pattern==1U) { /* Equal ranks, signed zero and finite extremes. */
            const uint32_t bits[]={0U,0x80000000U,0x3F800000U,0x7F7FFFFFU};
            record->distance_squared=float_bits(bits[group%ARRAY_COUNT(bits)]);
        } else if(pattern==2U) { /* Invalid identities and mixed native types. */
            candidate->renderer_type=(i%7U==0U)?1:((i%11U==0U)?0:3);
            if(i%13U==0U) candidate->descriptor_handle_valid=0U;
            if(i%17U==0U) record->descriptor_handle^=0x100000000ULL;
            if(i%19U==0U) record->position_valid=0U;
        } else if(pattern==3U) { /* Duplicate candidates and owner conflict. */
            if(i && i%9U==0U)
                candidate->descriptor_handle=sample->records[i-1U].descriptor_handle;
            if(i%10U==0U) {
                VehicleSelectionRecord *conflict=&snapshot->records[snapshot->count++];
                *conflict=*record; conflict->vehicle^=0x8000000000000000ULL;
            }
        } else if(pattern==4U) { /* Freshness, generation and tick boundaries. */
            record->publish_renderer_epoch=101-(LONG)(group%7U);
            record->publish_manager_call=121-(LONG)(group%9U);
            record->tick=1000U-(DWORD)(group*9U);
        } else if(pattern==5U) { /* Every owner hits the same hash bucket. */
            record->vehicle=collision_owners[group];
        } else if(pattern==6U) { /* Owners pair only after all groups exist. */
            record->vehicle=0xABC00000000ULL+(i%128U);
        } else if(pattern==7U) { /* Saturation:129+ distinct owners remain safe. */
            record->vehicle=0xABC00000000ULL+i;
        } else if(pattern==8U) { /* Preserve existing non-total float ordering. */
            const uint32_t bits[]={0x7FC00000U,0x7F800000U,0xFF800000U,
                0x3F800000U,0x80000000U,0U,0x40000000U};
            record->distance_squared=float_bits(bits[group%ARRAY_COUNT(bits)]);
        } else if(pattern==9U) { /* Newest same-owner selection and wrapped age. */
            VehicleSelectionRecord *newer=&snapshot->records[snapshot->count++];
            record->tick=0xFFFFFFF0U;
            *newer=*record; newer->tick=3U;
            newer->publish_renderer_epoch=101-(LONG)(i%4U);
            newer->distance_squared=(float)(group%5U);
        }
    }
    if(pattern==7U && count==256U) {
        /* The late pair is outside the128 tracked owners and must not acquire
         * a group merely because it now has two exact recent headlights. */
        snapshot->records[255].vehicle=snapshot->records[254].vehicle;
    }
    /* Permute complete records, preserving exact matching inputs and exposing
     * tie-sensitive native encounter order. */
    if(pattern%2U==0U)
        for(i=0;i<count/2U;++i) {
            VehicleCandidateRecord swap=sample->records[i];
            sample->records[i]=sample->records[count-1U-i];
            sample->records[count-1U-i]=swap;
        }
}

static void make_projection(BookkeepingFixture *fixture)
{
    VehicleSelectionIndex index;
    unsigned i;
    memset(&fixture->projection,0,sizeof(fixture->projection));
    index_vehicle_selection(&fixture->snapshot,&index);
    for(i=0;i<fixture->input.copied_candidates;++i) {
        const VehicleCandidateRecord *candidate=&fixture->input.records[i];
        const VehicleSelectionRecord *record;
        uint32_t matches;
        if(candidate->renderer_type!=3 || !candidate->descriptor_handle_valid) continue;
        record=match_vehicle_selection(&fixture->snapshot,&index,
            candidate->descriptor_handle,&matches);
        fixture->projection.matches[i]=matches;
        if(record) fixture->projection.records[i]=*record;
    }
}

static void compare_case(BookkeepingFixture *fixture,unsigned mode)
{
    const VehicleSelectionProjection *projection=NULL;
    int detailed=(mode==1U || mode==2U || mode==3U || mode==4U);
    make_projection(fixture);
    if(mode==2U || mode==3U) {
        projection=&fixture->projection;
        fixture->projection.fallback_full_snapshot=mode==3U;
    }
    g_shadow_engine.vehicle_diagnostics.state=mode==4U?0:2;
    fixture->reference=fixture->input;
    fixture->actual=fixture->input;
    fixture->snapshot_before=fixture->snapshot;
    fixture->projection_before=fixture->projection;
    reference_selection_classifier_v77(&fixture->reference,&fixture->snapshot,projection,detailed);
    classify_vehicle_selection_projected_fields(&fixture->actual,&fixture->snapshot,projection,detailed);
    /* v77 predates the temporary extras control and reader trace. Validate
     * these new diagnostic fields explicitly, then compare frozen policy. */
    REQUIRE(fixture->actual.selection_extra_slots==2U);
    fixture->reference.selection_extra_slots=2U;
    REQUIRE(fixture->actual.driver_trace.stage==
        ((SHADOW_ENGINE_INTERNAL_DIAGNOSTICS && mode!=4U)?DRIVER_TRACE_PROOF:DRIVER_TRACE_NONE));
    memset(&fixture->actual.driver_trace,0,sizeof(fixture->actual.driver_trace));
    ++case_number;
    if(memcmp(&fixture->reference,&fixture->actual,sizeof(fixture->actual))) {
        const unsigned char *expected=(const unsigned char *)&fixture->reference;
        const unsigned char *actual=(const unsigned char *)&fixture->actual;
        size_t offset;
        for(offset=0;offset<sizeof(fixture->actual);++offset)
            if(expected[offset]!=actual[offset]) break;
        fprintf(stderr,"Classifier mismatch case=%u mode=%u count=%u firstByte=%llu expected=%u actual=%u\n",
            case_number,mode,fixture->input.copied_candidates,(unsigned long long)offset,
            (unsigned)expected[offset],(unsigned)actual[offset]);
        abort();
    }
    ++comparisons;
    REQUIRE(!memcmp(&fixture->snapshot_before,&fixture->snapshot,sizeof(fixture->snapshot)));
    REQUIRE(!memcmp(&fixture->projection_before,&fixture->projection,sizeof(fixture->projection)));
}

static void test_expected_group_results(BookkeepingFixture *fixture)
{
    unsigned i;
    select_anchor(0); select_limit(1);
    make_input(fixture,10U,0U);
    compare_case(fixture,0U);
    REQUIRE(fixture->actual.selection_vehicles==5U);
    REQUIRE(fixture->actual.selection_would_keep_lights==6U);
    for(i=0;i<10U;++i) {
        const VehicleCandidateRecord *record=&fixture->actual.records[i];
        REQUIRE(record->selection_group_complete && record->selection_group_lights==2U);
        REQUIRE(record->selection_rank==5U-(unsigned)(record->selection_vehicle-0xABC00000000ULL));
    }
    make_input(fixture,256U,6U);
    compare_case(fixture,0U);
    REQUIRE(fixture->actual.selection_vehicles==128U);
    REQUIRE(fixture->actual.selection_partial_vehicles==0U);
    make_input(fixture,256U,7U);
    compare_case(fixture,0U);
    REQUIRE(fixture->actual.selection_vehicles==0U);
    REQUIRE(fixture->actual.selection_partial_vehicles==128U);
}

static void test_limit_zero_bypass(void)
{
    VehicleLimiterChain *chain=calloc(1,sizeof(*chain));
    REQUIRE(chain!=NULL);
    select_limit(0);
    g_shadow_engine.vehicle_diagnostics.selection_enabled=1;
    /* Any inspection of this deliberately invalid candidate would fail. */
    prepare_vehicle_limiter_chain(chain,(void *)(uintptr_t)1,256U,1,0);
    REQUIRE(chain->head==(void *)(uintptr_t)1 && chain->count==256U);
    REQUIRE(!chain->enabled && !chain->evaluated && !chain->active && !chain->captured);
    REQUIRE(!chain->validation_queries && !chain->links_changed && !chain->fail_open);
    restore_vehicle_limiter_chain(chain);
    free(chain);
}

int main(void)
{
    static const unsigned counts[]={0,1,2,3,7,16,40,80,128,255,256};
    BookkeepingFixture *fixture=calloc(1,sizeof(*fixture));
    unsigned pattern,limit,anchor,c,mode;
    REQUIRE(fixture!=NULL);
    initialize_collision_owners();
    for(pattern=0;pattern<10U;++pattern)
        for(c=0;c<ARRAY_COUNT(counts);++c)
            for(limit=0;limit<=10U;++limit) {
                select_limit(limit);
                /* Anchor scenarios alternate across the matrix; each limit
                 * still sees every anchor state across counts and patterns. */
                anchor=(pattern+c+limit)%3U;
                select_anchor(anchor);
                make_input(fixture,counts[c],pattern);
                for(mode=0;mode<5U;++mode) compare_case(fixture,mode);
            }
    test_expected_group_results(fixture);
    test_limit_zero_bypass();
    printf("PASS selection bookkeeping: %u complete-sample frozen-v77 comparisons, %u invariant checks, diagnostics=%d; no gameplay/FPS claim\n",
        comparisons,checks,SHADOW_ENGINE_INTERNAL_DIAGNOSTICS);
    free(fixture);
    return 0;
}
