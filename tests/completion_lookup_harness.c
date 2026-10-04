/* Actual unity source with synthetic patch-owned records. No DLL entry point,
 * hooks, game, or worker threads are started. The known-serial lookup oracle
 * below is frozen from v2.0.70, not derived from the candidate's indexing. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <assert.h>
#include <limits.h>

static volatile LONG *watched_serials[8];
static unsigned watched_count,serial_reads;
static volatile LONG *replacement_target;
static LONG replacement_serial;

static LONG WINAPI completion_test_compare_exchange(volatile LONG *target,
    LONG exchange,LONG comparand)
{
    unsigned i;
    for(i=0;i<watched_count;++i) {
        if(target!=watched_serials[i]) continue;
        if(exchange==0 && comparand==0) {
            ++serial_reads;
            /* Deterministically clear/replace immediately before the real
             * atomic read. This checks rejection, not general race freedom. */
            if(target==replacement_target) {
                InterlockedExchange(target,replacement_serial);
                replacement_target=NULL;
            }
        }
        break;
    }
    return InterlockedCompareExchange(target,exchange,comparand);
}

#undef InterlockedCompareExchange
#define InterlockedCompareExchange completion_test_compare_exchange
#include "../src/shadow_engine_patch.c"

/* Frozen v2.0.70 reference: renamed only. */
static CompletionGenerationRecord *reference_find_completion_generation(
    CompletionDiagnosticSlot *slot,LONG serial)
{
    uint32_t index;
    if(!slot || serial<=0) return NULL;
    for(index=0;index<COMPLETION_DIAGNOSTIC_HISTORY;++index) {
        CompletionGenerationRecord *entry=&slot->history[index];
        if(InterlockedCompareExchange(&entry->serial,0,0)==serial)
            return entry;
    }
    return NULL;
}
#undef InterlockedCompareExchange

static unsigned comparison_cases,writer_batches,scheduler_calls;
static unsigned char synthetic_records[3][0x200];
static unsigned char synthetic_renderer[16],synthetic_queue[16];

static void watch_history(CompletionDiagnosticSlot *slot)
{
    uint32_t i;
    watched_count=0;
    serial_reads=0;
    replacement_target=NULL;
    if(!slot) return;
    assert(COMPLETION_DIAGNOSTIC_HISTORY==
        sizeof(watched_serials)/sizeof(watched_serials[0]));
    for(i=0;i<COMPLETION_DIAGNOSTIC_HISTORY;++i)
        watched_serials[watched_count++]=&slot->history[i].serial;
}

static CompletionGenerationRecord *compare_lookup(
    CompletionDiagnosticSlot *slot,LONG serial)
{
    CompletionGenerationRecord *before,*after;
    unsigned before_reads,expected_reads;
    watch_history(slot);
    before=reference_find_completion_generation(slot,serial);
    before_reads=serial_reads;
    serial_reads=0;
    after=find_completion_generation(slot,serial);
    assert(before==after);
    if(!slot || serial<=0) {
        assert(before_reads==0 && serial_reads==0);
    } else {
        expected_reads=before ? (unsigned)(before-slot->history)+1U:
            COMPLETION_DIAGNOSTIC_HISTORY;
        assert(before_reads==expected_reads && serial_reads==1U);
    }
    ++comparison_cases;
    return after;
}

static void publish_synthetic_serial(CompletionDiagnosticSlot *slot,LONG serial)
{
    /* Independent modulo placement exercises invariant-respecting states.
     * Real producer placement is verified separately below. */
    uint32_t index=(uint32_t)serial%COMPLETION_DIAGNOSTIC_HISTORY;
    memset(&slot->history[index],0,sizeof(slot->history[index]));
    slot->history[index].serial=serial;
}

static void test_lookup_equivalence(void)
{
    CompletionDiagnosticSlot slot;
    static const LONG rejected[]={0,-1,LONG_MIN};
    LONG serial,query;
    unsigned i;
    memset(&slot,0,sizeof(slot));
    for(i=0;i<sizeof(rejected)/sizeof(rejected[0]);++i) {
        assert(!compare_lookup(NULL,rejected[i]));
        assert(!compare_lookup(&slot,rejected[i]));
    }
    assert(!compare_lookup(NULL,1));
    for(serial=1;serial<=32;++serial) assert(!compare_lookup(&slot,serial));

    /* Every residue, full histories, overwrites and positive absent serials. */
    for(serial=1;serial<=128;++serial) {
        publish_synthetic_serial(&slot,serial);
        for(query=1;query<=136;++query) compare_lookup(&slot,query);
    }

    memset(&slot,0,sizeof(slot));
    for(i=0;i<16U;++i) {
        serial=LONG_MAX-15+(LONG)i;
        publish_synthetic_serial(&slot,serial);
        for(query=0;query<16;++query)
            compare_lookup(&slot,LONG_MAX-15+query);
    }
    /* Seed wrap-adjacent values without executing signed integer overflow.
     * The unchanged <=0 guard rejects the entire nonpositive serial domain. */
    for(i=0;i<8U;++i) {
        serial=LONG_MIN+(LONG)i;
        publish_synthetic_serial(&slot,serial);
        assert(!compare_lookup(&slot,serial));
    }
    publish_synthetic_serial(&slot,0);
    assert(!compare_lookup(&slot,0));
    for(serial=1;serial<=8;++serial) {
        publish_synthetic_serial(&slot,serial);
        assert(compare_lookup(&slot,serial));
    }
    printf("PASS known-serial lookup: frozen linear reference, all 8 residues, "
        "empty/null/nonpositive, overwrite/miss and seeded serial boundaries\n");
}

static void test_atomic_replacement(void)
{
    CompletionDiagnosticSlot slot;
    unsigned mode,reference;
    for(mode=0;mode<2U;++mode) {
        for(reference=0;reference<2U;++reference) {
            CompletionGenerationRecord *result;
            memset(&slot,0,sizeof(slot));
            publish_synthetic_serial(&slot,7);
            watch_history(&slot);
            replacement_target=&slot.history[7].serial;
            replacement_serial=mode ? 15:0;
            result=reference ? reference_find_completion_generation(&slot,7):
                find_completion_generation(&slot,7);
            assert(!result && !replacement_target);
            assert(slot.history[7].serial==replacement_serial);
            assert(serial_reads==(reference ? 8U:1U));
        }
    }
    watch_history(NULL);
    printf("PASS atomic equality: publication cleared/replaced at observed read "
        "is rejected by both paths\n");
}

static void reset_synthetic_context(void)
{
    unsigned i;
    watch_history(NULL);
#if SHADOW_ENGINE_DETAIL_WINDOWS
    if(g_shadow_engine.detail_window) TlsFree(detail_state()->producer_tls);
    free(g_shadow_engine.detail_window);
#endif
    memset(&g_shadow_engine,0,sizeof(g_shadow_engine));
#if SHADOW_ENGINE_DETAIL_WINDOWS
    detail_window_toggle();
    assert(detail_window_status().phase==DETAIL_WINDOW_OPEN);
#endif
    for(i=0;i<3U;++i) {
        LifecycleRecordSnapshot *lifecycle=find_lifecycle_record(
            synthetic_records[i],1);
        assert(lifecycle);
        lifecycle->generation=1;
        lifecycle->builder_call=2;
        lifecycle->producer_cycle=3;
    }
}

static void test_actual_producer_placement(void)
{
    static const unsigned record_order[]={0,0,1,2,2,0,2,1,0,1,1};
    unsigned step,r,index;
    reset_synthetic_context();
    for(step=0;step<96U;++step) {
        unsigned which=record_order[step%
            (sizeof(record_order)/sizeof(record_order[0]))];
        void *record=synthetic_records[which];
        CompletionDiagnosticSlot *slot;
        DetailProducerTicket ticket=completion_diagnostic_begin_producer_batch(record);
        LONG serial=ticket.serial;
        assert(serial==(LONG)step+1);
        slot=find_completion_diagnostic_slot(record,0);
        assert(slot && slot->active_producer_serial==serial);
        /* Scan first, then verify the writer invariant independently. */
        assert(compare_lookup(slot,serial));
        for(r=0;r<3U;++r) {
            CompletionDiagnosticSlot *other=find_completion_diagnostic_slot(
                synthetic_records[r],0);
            LONG query;
            if(!other) continue;
            for(index=0;index<COMPLETION_DIAGNOSTIC_HISTORY;++index) {
                LONG seen=other->history[index].serial;
                assert(seen==0 || (uint32_t)seen%
                    COMPLETION_DIAGNOSTIC_HISTORY==index);
            }
            for(query=1;query<=serial+1;++query) compare_lookup(other,query);
        }
        watch_history(slot);
        {
            DetailProducerTicket refused=completion_diagnostic_begin_producer_batch(record);
            assert(refused.serial==0);
            completion_diagnostic_seal_producer_batch(record,&refused);
        }
        assert(slot->active_producer_serial==serial);
        completion_diagnostic_seal_producer_batch(record,&ticket);
        assert(slot->active_producer_serial==0);
        assert(slot->current_serial==serial);
        assert(compare_lookup(slot,serial)->producer_sealed==1);
        ++writer_batches;
    }
    assert(g_shadow_engine.completion_diagnostics.producer_batches_started==96);
    assert(g_shadow_engine.completion_diagnostics.producer_batches_sealed==96);
    assert(g_shadow_engine.completion_diagnostics.producer_batch_overlaps==96);
    assert(g_shadow_engine.completion_diagnostics.producer_batches_without_lifecycle==0);
    assert(g_shadow_engine.completion_diagnostics.producer_active_clear_failures==0);
    assert(g_shadow_engine.completion_diagnostics.producer_batches_without_external_work==96);
    watch_history(NULL);
    printf("PASS actual producer: %u batches across 3 records, sparse global "
        "serials, indexed publication, overlap rejection and seal counters\n",
        writer_batches);
}

static void __fastcall fake_scheduler(void *renderer,void *queue,
    uint32_t entry_index,uint32_t face_index,void *inline_results)
{
    assert(renderer==synthetic_renderer && queue==synthetic_queue);
    assert(entry_index==0 || entry_index==NATIVE_SLICE_RESULT_COUNT);
    assert(face_index<=1U && inline_results==synthetic_records[0]+0x70U);
    ++scheduler_calls;
}

static LONG begin_and_seal(void *record)
{
    DetailProducerTicket ticket=completion_diagnostic_begin_producer_batch(record);
    LONG serial=ticket.serial;
    assert(serial>0);
    completion_diagnostic_seal_producer_batch(record,&ticket);
    return serial;
}

static void test_asynchronous_completion_history(void)
{
    CompletionDiagnosticSlot *slot;
    CompletionGenerationRecord *older,*newer;
    void *record=synthetic_records[0];
    void *inline_results=synthetic_records[0]+0x70U;
    LONG old_serial,new_serial;
    DetailProducerTicket old_ticket,new_ticket;
    unsigned i;
    reset_synthetic_context();
    g_shadow_engine.hooks.original_shadow_face_scheduler=fake_scheduler;
    QueryPerformanceFrequency(&g_shadow_engine.completion_diagnostics.qpc_frequency);
    for(i=0;i<6U;++i) begin_and_seal(synthetic_records[1]);
    old_ticket=completion_diagnostic_begin_producer_batch(record); old_serial=old_ticket.serial;
    assert(old_serial==7);
    hooked_shadow_face_scheduler(synthetic_renderer,synthetic_queue,0,0,inline_results);
    hooked_shadow_face_scheduler(synthetic_renderer,synthetic_queue,
        NATIVE_SLICE_RESULT_COUNT,0,inline_results);
    hooked_shadow_face_scheduler(synthetic_renderer,synthetic_queue,
        NATIVE_SLICE_RESULT_COUNT,1,inline_results);
    completion_diagnostic_seal_producer_batch(record,&old_ticket);
    assert(begin_and_seal(synthetic_records[1])==8);
    new_ticket=completion_diagnostic_begin_producer_batch(record); new_serial=new_ticket.serial;
    assert(new_serial==9);
    hooked_shadow_face_scheduler(synthetic_renderer,synthetic_queue,
        NATIVE_SLICE_RESULT_COUNT,0,inline_results);
    completion_diagnostic_seal_producer_batch(record,&new_ticket);
    slot=find_completion_diagnostic_slot(record,0);
    older=compare_lookup(slot,old_serial);
    newer=compare_lookup(slot,new_serial);
    assert(older && newer && older>newer);
    assert(older->scheduled_calls==3 && newer->scheduled_calls==1);
    assert(older->scheduled_external_mask==1 && newer->scheduled_external_mask==1);
    watch_history(slot);
    completion_diagnostic_record_completion(inline_results,NATIVE_SLICE_RESULT_COUNT,1,3,0);
    assert(serial_reads==COMPLETION_DIAGNOSTIC_HISTORY);
    assert(older->completed_external_mask==1 && newer->completed_external_mask==0);
    assert(g_shadow_engine.completion_diagnostics.completions_matched==1);
    assert(g_shadow_engine.completion_diagnostics.completions_ambiguous==1);
    assert(g_shadow_engine.completion_diagnostics.completions_multiple_generations_late==1);
    serial_reads=0;
    completion_diagnostic_record_completion(inline_results,NATIVE_SLICE_RESULT_COUNT,1,3,0);
    assert(serial_reads==COMPLETION_DIAGNOSTIC_HISTORY);
    assert(newer->completed_external_mask==1);
    assert(g_shadow_engine.completion_diagnostics.completions_matched==2);
    assert(g_shadow_engine.completion_diagnostics.completions_ambiguous==1);
    assert(g_shadow_engine.completion_diagnostics.completions_current_generation==1);
    completion_diagnostic_record_completion(inline_results,NATIVE_SLICE_RESULT_COUNT,1,3,0);
    assert(g_shadow_engine.completion_diagnostics.completions_unmatched==1);
    completion_diagnostic_mark_pre_finalizer(record);
    completion_diagnostic_mark_reset(record);
    assert(newer->pre_finalizer_seen==1 && newer->reset_seen==1);
    assert(newer->pre_finalizer_mask==1 && newer->reset_mask==1);
    assert(g_shadow_engine.completion_diagnostics.pre_finalizer_outstanding_generations==0);
    assert(g_shadow_engine.completion_diagnostics.reset_outstanding_generations==0);
    assert(g_shadow_engine.completion_diagnostics.scheduled_native==1);
    assert(g_shadow_engine.completion_diagnostics.scheduled_external==2);
    assert(g_shadow_engine.completion_diagnostics.completions_after_producer_seal==2);
    assert(g_shadow_engine.completion_diagnostics.completions_same_lifecycle==2);
    assert(scheduler_calls==4);
    watch_history(NULL);
    printf("PASS asynchronous completion: full 8-record search retained, oldest "
        "pending before newest, ambiguity/unmatched counters and pass-through scheduling\n");
}

int main(void)
{
    test_lookup_equivalence();
    test_atomic_replacement();
    test_actual_producer_placement();
    test_asynchronous_completion_history();
    printf("PASS %u differential lookups; atomic serial reads hit 1..8 -> 1, "
        "positive miss 8 -> 1, rejected input 0 -> 0\n",comparison_cases);
#if SHADOW_ENGINE_DETAIL_WINDOWS
    assert(TlsFree(detail_state()->producer_tls));
    free(g_shadow_engine.detail_window);
#endif
    return 0;
}
