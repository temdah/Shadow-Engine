/* Execute the actual unity lifecycle with optional diagnostics both compiled
 * in and out. Deterministic transition injection checks the writer claim's
 * second consumer-state read; it does not prove every native interleaving.
 * No hooks, game, DLL entry point, or worker threads are started. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <assert.h>

static volatile LONG *close_after_claim,*claimed_consumer;
static LONG WINAPI lifecycle_test_increment(volatile LONG *target)
{
    LONG result=InterlockedIncrement(target);
    if(target==close_after_claim) {
        close_after_claim=NULL;
        InterlockedExchange(claimed_consumer,1);
    }
    return result;
}
#undef InterlockedIncrement
#define InterlockedIncrement lifecycle_test_increment
#include "../src/shadow_engine_patch.c"
#undef InterlockedIncrement

static unsigned char test_record[0x200],other_record[0x200];
static unsigned char test_renderer[0x1400],test_context[0xC00];
static int test_resources[EXTRA_SLICE_RESULT_COUNT+1];
static unsigned int released_count;
static unsigned int released_by_resource[EXTRA_SLICE_RESULT_COUNT+1];
static unsigned int scenarios;
static uint32_t ownership_digest=2166136261U;
static unsigned int detail_fixture_mode,detail_observed_scenarios;
static LONG detail_contention_before;

static void *__fastcall fake_constructor(void *record) { return record; }
static void __fastcall fake_builder(void *a,void *b,void *record,
    uint64_t d,uint64_t e,uint64_t f,uint64_t g)
{ (void)a; (void)b; (void)record; (void)d; (void)e; (void)f; (void)g; }
static int64_t __fastcall fake_wrapper(unsigned char active,void **resources,
    uint32_t count,void **other,uint32_t other_count,unsigned char *context,
    uint64_t unused,uint32_t mode)
{
    uint32_t i,j;
    (void)other; (void)other_count; (void)unused; (void)mode;
    assert(active==0 && context==test_context);
    for(i=0;i<count;++i) {
        for(j=0;j<EXTRA_SLICE_RESULT_COUNT+1U;++j)
            if(resources[i]==&test_resources[j]) break;
        assert(j<EXTRA_SLICE_RESULT_COUNT+1U);
        ++released_by_resource[j];
    }
    released_count+=count;
    return 0;
}
static void publish(uint32_t index,void *resource)
{ store_external_slice_result(test_record+0x70,index,resource,test_context); }

static LifecycleRecordSnapshot *start_fixture(void)
{
    LifecycleRecordSnapshot *slot;
#if SHADOW_ENGINE_OVERHEAD_MEASUREMENT
    OverheadState *measurement=overhead_state();
    assert(overhead_ready());
#endif
#if SHADOW_ENGINE_DETAIL_WINDOWS
    DetailWindowState *detail=detail_state();
    assert(!detail || (!detail->lock && !detail->report_lock &&
        !detail->report_pending && detail->phase==DETAIL_WINDOW_FROZEN));
#endif
    memset(&g_shadow_engine,0,sizeof(g_shadow_engine));
#if SHADOW_ENGINE_DETAIL_WINDOWS
    /* Keep the real process-lifetime owner and non-reused window epoch.
     * Every prior scenario completed synchronously and froze before reuse. */
    g_shadow_engine.detail_window=detail;
    if(detail_fixture_mode) {
        detail_window_toggle();
        detail_window_poll();
        detail=detail_state();
        assert(detail && detail_window_status().phase==DETAIL_WINDOW_OPEN);
        if(detail_fixture_mode==2) {
            /* Deterministic contention: observation must refuse this token
             * while mandatory acquisition/retirement continues unchanged. */
            detail_contention_before=detail->contention;
            InterlockedExchange(&detail->lock,1);
        }
    } else assert(!detail);
#endif
#if SHADOW_ENGINE_OVERHEAD_MEASUREMENT
    /* Preserve the real process-lifetime probe owner across scenario resets.
     * Fresh windows give accepted stores and early rejection paths a
     * reserved sample; a missing END must keep the window from draining. */
    g_shadow_engine.overhead=measurement;
    assert(overhead_request(scenarios+1U,0,5000));
#endif
    memset(released_by_resource,0,sizeof(released_by_resource));
    released_count=0;
    close_after_claim=NULL;
    claimed_consumer=NULL;
    g_shadow_engine.hooks.original_render_record_constructor=fake_constructor;
    g_shadow_engine.hooks.original_frame_builder=fake_builder;
    g_shadow_engine.hooks.original_resource_wrapper=fake_wrapper;
    *(unsigned char **)(test_renderer+0x13B0)=test_context;
    hooked_external_render_record_constructor(test_record);
    slot=find_existing_lifecycle_record_fast(test_record);
    assert(slot && slot->generation==1 && slot->builder_call==0);
    assert(begin_external_slice_producer_cycle(test_record));
    return slot;
}

static void digest_value(LONG value)
{ ownership_digest=(ownership_digest^(uint32_t)value)*16777619U; }

static void finish_scenario(LifecycleRecordSnapshot *slot)
{
    ExternalResultState *state=&g_shadow_engine.external_results;
    uint32_t i;
    assert(!slot->extra_slice_result_written_mask);
    assert(!slot->extra_slice_result_active_writers);
    assert(slot->extra_slice_result_consumer_state==2);
    for(i=0;i<EXTRA_SLICE_RESULT_COUNT;++i)
        assert(!slot->extra_slice_results[i]);
    assert(state->slice_result_external_nonnull+
        state->slice_result_external_null==
        state->slice_result_external_consumed_results);
    assert(state->slice_result_external_nonnull==
        state->slice_result_external_release_resources);
    assert(state->slice_result_external_rejected_nonnull==
        state->slice_result_external_orphan_release_resources);
    assert(state->slice_result_external_store_failures==
        state->slice_result_external_rejected_nonnull+
        state->slice_result_external_rejected_null);
    assert(!state->slice_result_external_orphan_release_failures);
    assert(released_count==(unsigned int)(state->slice_result_external_nonnull+
        state->slice_result_external_rejected_nonnull));
    /* Compare an address-independent ownership trace across the two builds. */
    digest_value(slot->generation);
    digest_value(slot->producer_cycle);
    digest_value(state->slice_result_external_nonnull);
    digest_value(state->slice_result_external_null);
    digest_value(state->slice_result_external_consumed_results);
    digest_value(state->slice_result_external_release_resources);
    digest_value(state->slice_result_external_store_failures);
    digest_value(state->slice_result_external_release_failures);
    digest_value(state->slice_result_external_reset_pending);
    for(i=0;i<EXTERNAL_REJECT_REASON_COUNT;++i)
        digest_value(state->rejection_reasons[i]);
    for(i=0;i<EXTRA_SLICE_RESULT_COUNT+1U;++i)
        digest_value((LONG)released_by_resource[i]);
#if SHADOW_ENGINE_DETAIL_WINDOWS
    if(detail_fixture_mode) {
        DetailWindowState *detail=detail_state();
        assert(detail && detail->phase==DETAIL_WINDOW_OPEN);
        if(detail_fixture_mode==2) {
            assert(detail->lock==1 && detail->contention>detail_contention_before);
            InterlockedExchange(&detail->lock,0);
            ++detail_observed_scenarios;
        } else if(g_shadow_engine.completion_diagnostics.completions_unmatched>0)
            ++detail_observed_scenarios;
        detail_window_finish();
        detail_window_poll();
        assert(detail_window_status().phase==DETAIL_WINDOW_FROZEN);
        assert(!detail->lock && !detail->report_lock && !detail->report_pending);
        /* No session log is opened by this fixture. Frozen output correctly
         * fails its durability gate without file I/O or changing ownership. */
        assert(detail->report_failed);
    }
#endif
#if SHADOW_ENGINE_OVERHEAD_MEASUREMENT
    {
        OverheadState *measurement=overhead_state();
        unsigned category,bucket,published=0;
        assert(measurement && !measurement->inflight);
        assert(measurement->streams[OH_EXTERNAL_STORE].calls>0);
        assert(measurement->streams[OH_EXTERNAL_CONSUME].calls>0);
        for(category=0;category<OH_CATEGORY_COUNT;++category) {
            for(bucket=0;bucket<OVERHEAD_SAMPLE_SLOTS;++bucket) {
                LONG state=measurement->streams[category].samples[bucket].state;
                assert(state!=1); /* No hook left an admitted ticket outstanding. */
                if(state==2) ++published;
            }
        }
        assert(published && !overhead_ready());
        overhead_close();
        assert(overhead_ready());
    }
#endif
    ++scenarios;
}

static void test_existing_lifecycle(void)
{
    LifecycleRecordSnapshot *slot;
    LONG cycle,generation,rejections,total=0;
    uint32_t i;
    slot=start_fixture();
    cycle=slot->producer_cycle;

    /* All configured external entries, including a synchronous completion before the
     * builder and a null result, survive the later builder untouched. */
    publish(17,&test_resources[0]);
    assert(slot->extra_slice_result_written_mask==1);
    hooked_external_frame_builder(NULL,NULL,test_record,0,0,0,0);
    assert(slot->producer_cycle==cycle && slot->extra_slice_results[0]==&test_resources[0]);
    for(i=1;i<EXTRA_SLICE_RESULT_COUNT;++i)
        publish(17+i,i==1 ? NULL:&test_resources[i]);
    assert((uint32_t)slot->extra_slice_result_written_mask==
           (UINT32_MAX>>(32U-EXTRA_SLICE_RESULT_COUNT)));
    assert(g_shadow_engine.external_results.slice_result_external_store_failures==0);
    assert(consume_external_slice_results(test_renderer,slot));
    assert(released_count==EXTRA_SLICE_RESULT_COUNT-1U &&
           slot->extra_slice_result_consumer_state==2);
    publish(17,&test_resources[0]); /* closed consumer: retired once */
    assert(g_shadow_engine.external_results.rejection_reasons[EXTERNAL_REJECT_CLOSED_CONSUMER]==1);

    assert(begin_external_slice_producer_cycle(test_record));
    assert(slot->producer_cycle>cycle);
    publish(17,&test_resources[0]);
    publish(17,&test_resources[1]); /* duplicate cannot replace ownership */
    assert(slot->extra_slice_results[0]==&test_resources[0]);
    cycle=slot->producer_cycle;
    assert(!begin_external_slice_producer_cycle(test_record));
    assert(slot->producer_cycle==cycle && slot->extra_slice_result_written_mask==1);
    publish(18,&test_resources[1]); /* overlap quarantines publication only */
    assert(g_shadow_engine.external_results.rejection_reasons[EXTERNAL_REJECT_QUARANTINED_CYCLE]==1);
    assert(consume_external_slice_results(test_renderer,slot));

    assert(begin_external_slice_producer_cycle(test_record));
    cycle=slot->producer_cycle;
    generation=slot->generation;
    slot->extra_slice_result_active_writers=1;
    assert(!reset_external_slice_cycle(slot));
    assert(slot->producer_cycle==cycle && slot->extra_slice_result_generation==generation);
    slot->extra_slice_result_active_writers=0;
    assert(consume_external_slice_results(test_renderer,slot));
    assert(begin_external_slice_producer_cycle(test_record));
    cycle=slot->producer_cycle;
    generation=slot->generation;

    slot->generation=0; publish(17,NULL); slot->generation=generation;
    slot->producer_cycle=0; publish(17,NULL); slot->producer_cycle=cycle;
    slot->extra_slice_result_generation=generation+1; publish(17,NULL);
    slot->extra_slice_result_generation=generation;
    slot->extra_slice_result_cycle=cycle+1; publish(17,NULL);
    slot->extra_slice_result_cycle=cycle;
    publish(0,NULL);
    store_external_slice_result(other_record+0x70,17,NULL,test_context);
    for(i=0;i<EXTERNAL_REJECT_REASON_COUNT;++i) {
        assert(g_shadow_engine.external_results.rejection_reasons[i]==1);
        total+=g_shadow_engine.external_results.rejection_reasons[i];
    }
    rejections=g_shadow_engine.external_results.slice_result_external_store_failures;
    assert(total==rejections && rejections==9);
    assert(consume_external_slice_results(test_renderer,slot));

    /* A constructor/reset failure must not erase an accepted resource. */
    assert(begin_external_slice_producer_cycle(test_record));
    publish(17,&test_resources[0]);
    generation=slot->generation;
    hooked_external_render_record_constructor(test_record);
    assert(slot->generation==generation && slot->extra_slice_results[0]==&test_resources[0]);
    assert(consume_external_slice_results(test_renderer,slot));
    hooked_external_render_record_constructor(test_record);
    assert(slot->generation==generation+1 && slot->producer_cycle==0);
    assert(begin_external_slice_producer_cycle(test_record));
    assert(consume_external_slice_results(test_renderer,slot));
    assert(released_count==(unsigned int)(
        g_shadow_engine.external_results.slice_result_external_nonnull+
        g_shadow_engine.external_results.slice_result_external_rejected_nonnull));
    assert(g_shadow_engine.external_results.slice_result_external_release_failures==0);
    assert(g_shadow_engine.external_results.slice_result_external_orphan_release_failures==0);
    finish_scenario(slot);
}

static void test_rejection_matrix(void)
{
    unsigned int reason,nonnull;
    for(reason=0;reason<EXTERNAL_REJECT_REASON_COUNT;++reason) {
        for(nonnull=0;nonnull<=1;++nonnull) {
            LifecycleRecordSnapshot *slot=start_fixture();
            void *inline_results=test_record+0x70;
            void *result=nonnull ? &test_resources[1]:NULL;
            uint32_t index=NATIVE_SLICE_RESULT_COUNT;
            LONG generation=slot->generation,cycle=slot->producer_cycle;
            switch(reason) {
            case EXTERNAL_REJECT_INVALID_INPUT: index=NATIVE_SLICE_RESULT_COUNT-1U; break;
            case EXTERNAL_REJECT_MISSING_RECORD: inline_results=other_record+0x70; break;
            case EXTERNAL_REJECT_CLOSED_CONSUMER: slot->extra_slice_result_consumer_state=1; break;
            case EXTERNAL_REJECT_ZERO_GENERATION: slot->generation=0; break;
            case EXTERNAL_REJECT_UNOPENED_CYCLE: slot->producer_cycle=0; break;
            case EXTERNAL_REJECT_GENERATION_MISMATCH: slot->extra_slice_result_generation=generation+1; break;
            case EXTERNAL_REJECT_CYCLE_MISMATCH: slot->extra_slice_result_cycle=cycle+1; break;
            case EXTERNAL_REJECT_DUPLICATE_INDEX: publish(index,&test_resources[0]); break;
            case EXTERNAL_REJECT_QUARANTINED_CYCLE: slot->producer_cycle_ready=0; break;
            default: assert(0);
            }
            store_external_slice_result(inline_results,index,result,test_context);
            assert(g_shadow_engine.external_results.rejection_reasons[reason]==1);
            assert(g_shadow_engine.external_results.slice_result_external_store_failures==1);
            assert(!slot->extra_slice_result_active_writers);
            assert(released_by_resource[1]==nonnull);
            assert(released_by_resource[0]==0);
            if(reason==EXTERNAL_REJECT_DUPLICATE_INDEX) {
                assert(slot->extra_slice_result_written_mask==1);
                assert(slot->extra_slice_results[0]==&test_resources[0]);
            } else assert(!slot->extra_slice_result_written_mask);
            slot->generation=generation;
            slot->producer_cycle=cycle;
            slot->extra_slice_result_generation=generation;
            slot->extra_slice_result_cycle=cycle;
            slot->extra_slice_result_consumer_state=0;
            assert(consume_external_slice_results(test_renderer,slot));
            assert(released_by_resource[0]==
                (unsigned int)(reason==EXTERNAL_REJECT_DUPLICATE_INDEX));
            assert(!g_shadow_engine.external_results.slice_result_external_release_failures);
            finish_scenario(slot);
        }
    }
}

static void test_invalid_boundaries(void)
{
    unsigned int boundary,nonnull;
    for(boundary=0;boundary<3;++boundary) {
        for(nonnull=0;nonnull<=1;++nonnull) {
            LifecycleRecordSnapshot *slot=start_fixture();
            void *inline_results=boundary==0 ? NULL:test_record+0x70;
            uint32_t index=boundary==0 ? NATIVE_SLICE_RESULT_COUNT:
                (boundary==1 ? TOTAL_LOCAL_MAPS:UINT32_MAX);
            store_external_slice_result(inline_results,index,
                nonnull ? &test_resources[0]:NULL,test_context);
            assert(g_shadow_engine.external_results.rejection_reasons[
                EXTERNAL_REJECT_INVALID_INPUT]==1);
            assert(released_count==nonnull);
            assert(consume_external_slice_results(test_renderer,slot));
            finish_scenario(slot);
        }
    }
}

static void test_writer_consumer_exclusion(void)
{
    LifecycleRecordSnapshot *slot=start_fixture();
    LONG generation=slot->generation,cycle=slot->producer_cycle;
    publish(17,&test_resources[0]);
    slot->extra_slice_result_active_writers=1;
    assert(!consume_external_slice_results(test_renderer,slot));
    assert(slot->extra_slice_result_consumer_state==0);
    assert(slot->extra_slice_result_written_mask==1);
    assert(slot->extra_slice_results[0]==&test_resources[0]);
    assert(!released_count);
    assert(!reset_external_slice_cycle(slot));
    assert(slot->generation==generation && slot->producer_cycle==cycle);
    assert(!slot->producer_cycle_ready && slot->extra_slice_result_active_writers==1);
    assert(g_shadow_engine.external_results.slice_result_external_release_failures==1);
    slot->extra_slice_result_active_writers=0;
    assert(consume_external_slice_results(test_renderer,slot));
    assert(released_by_resource[0]==1);
    finish_scenario(slot);

    slot=start_fixture();
    close_after_claim=&slot->extra_slice_result_active_writers;
    claimed_consumer=&slot->extra_slice_result_consumer_state;
    publish(17,&test_resources[0]);
    assert(!close_after_claim && slot->extra_slice_result_consumer_state==1);
    assert(!slot->extra_slice_result_active_writers);
    assert(!slot->extra_slice_result_written_mask);
    assert(g_shadow_engine.external_results.rejection_reasons[
        EXTERNAL_REJECT_CLOSED_CONSUMER]==1);
    assert(released_by_resource[0]==1);
    slot->extra_slice_result_consumer_state=0;
    assert(consume_external_slice_results(test_renderer,slot));
    finish_scenario(slot);
}

static void test_repeated_resource_acquisitions(void)
{
    LifecycleRecordSnapshot *slot=start_fixture();
    publish(17,&test_resources[0]);
    publish(18,&test_resources[0]);
    publish(19,NULL);
    assert(!released_count);
    assert(consume_external_slice_results(test_renderer,slot));
    assert(released_by_resource[0]==2);
    assert(g_shadow_engine.external_results.slice_result_external_duplicate_resources==1);
    finish_scenario(slot);
}

int main(int argc,char **argv)
{
    const char *mode=argc>1?argv[1]:"idle";
    assert(argc<=2);
    if(!strcmp(mode,"recording")) detail_fixture_mode=1;
    else if(!strcmp(mode,"contended")) detail_fixture_mode=2;
    else assert(!strcmp(mode,"idle"));
#if !SHADOW_ENGINE_DETAIL_WINDOWS
    assert(!detail_fixture_mode);
#endif
    test_existing_lifecycle();
    test_rejection_matrix();
    test_invalid_boundaries();
    test_writer_consumer_exclusion();
    test_repeated_resource_acquisitions();
    printf("PASS actual lifecycle diagnostics=%d detail=%s: %u scenarios, all %u external entries, null/non-null rejection matrix, duplicate ownership, quarantine, writer/consumer exclusion, constructor preservation and exact retirement\n",
        SHADOW_ENGINE_INTERNAL_DIAGNOSTICS,mode,scenarios,EXTRA_SLICE_RESULT_COUNT);
    printf("OWNERSHIP_RESULT scenarios=%u digest=%08X\n",scenarios,ownership_digest);
#if SHADOW_ENGINE_OVERHEAD_MEASUREMENT
    assert(overhead_ready());
    free(g_shadow_engine.overhead);
    g_shadow_engine.overhead=NULL;
    printf("PASS H1 active lifecycle windows: %u windows drain with no reserved samples or outstanding tickets\n",scenarios);
#endif
#if SHADOW_ENGINE_DETAIL_WINDOWS
    if(detail_fixture_mode) {
        DetailWindowState *detail=detail_state();
        assert(detail_observed_scenarios && detail && !detail->lock &&
            !detail->report_lock && !detail->report_pending &&
            detail->phase==DETAIL_WINDOW_FROZEN);
        assert(TlsFree(detail->producer_tls));
        free(detail); g_shadow_engine.detail_window=NULL;
        printf("PASS H3 optional detail=%s: %u scenarios reached diagnostic observation/refusal; every window froze without touching resource ownership\n",
            mode,detail_observed_scenarios);
    }
#endif
    return 0;
}
