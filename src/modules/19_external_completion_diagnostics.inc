/* Module 19: bounded schedule-to-completion telemetry for external shadow
 * jobs. This is observation only: it never waits, retains a resource, or
 * changes the native 17-result array or the external-result consumer. */
#include "../internal/overhead_probe.h"
#include "../internal/detail_window.h"

#if SHADOW_ENGINE_DETAIL_WINDOWS
#define DETAIL_WINDOW_DRAIN_MS 5000U
typedef struct DetailWindowState {
    volatile LONG lock,report_lock,epoch,phase,change,requests;
    DWORD started,marked,closed;
    unsigned marker_seen,timed_out,report_pending,report_failed;
    /* Cumulative across windows: delayed refused entrants never write a reset
     * counter. Losses are diagnostic coverage, never resource safety failures. */
    volatile LONG contention,stale_entries,ring_reuse_incomplete,unadmitted_batches;
    volatile LONG contaminated_completions,serial_exhausted,discarded_failed_reports;
    /* The worker allocates TLS before first OPEN. Neither this index nor these
     * atomic control cells is reset with window history or freed in process.
     * A control index may remain busy across rearm; its unique serial owns CAS
     * closure even if the optional history seal could not acquire the lock. */
    DWORD producer_tls;
    volatile LONG tls_failure,tls_failures,missing_seals,control_close_failures;
    volatile LONG producer_control[COMPLETION_DIAGNOSTIC_SLOTS];
    LONG kind_samples[DETAIL_WINDOW_KIND_COUNT];
    unsigned contaminated[COMPLETION_DIAGNOSTIC_SLOTS];
    CompletionDiagnosticState snapshot;
} DetailWindowState;
static void detail_save_frozen_report(void);

static DetailWindowState *detail_state(void)
{
    return (DetailWindowState *)InterlockedCompareExchangePointer(
        (void *volatile *)&g_shadow_engine.detail_window,NULL,NULL);
}
enum DetailTlsFailure {
    DETAIL_TLS_READ_FAILED=1, DETAIL_TLS_BIND_FAILED=2,
    DETAIL_TLS_PUBLISH_FAILED=3, DETAIL_TLS_RESTORE_FAILED=4
};
static void detail_tls_failed(DetailWindowState *s,LONG reason)
{
    InterlockedCompareExchange(&s->tls_failure,reason,0);
    InterlockedIncrement(&s->tls_failures);
}
/* TlsGetValue legitimately returns NULL for an unrecorded producer. Preserve
 * the engine's last-error value while distinguishing that case from failure. */
static int detail_tls_read(DetailWindowState *s,uintptr_t *serial)
{
    DWORD saved=GetLastError(),error;
    void *value;
    SetLastError(ERROR_SUCCESS);
    value=TlsGetValue(s->producer_tls);
    error=GetLastError(); SetLastError(saved);
    if(!value && error!=ERROR_SUCCESS) {
        detail_tls_failed(s,DETAIL_TLS_READ_FAILED); return 0;
    }
    *serial=(uintptr_t)value; return 1;
}
static int detail_tls_write(DetailWindowState *s,DWORD index,
    uintptr_t serial,LONG reason)
{
    DWORD saved=GetLastError();
    int success=TlsSetValue(index,(void *)serial)!=0;
    SetLastError(saved);
    if(!success) detail_tls_failed(s,reason);
    return success;
}
static int detail_deadline_passed(const DetailWindowState *s,DWORD now)
{
    return now-s->started>=INTERSECTION_TIMEOUT_MS ||
        (s->marker_seen && now-s->marked>=INTERSECTION_POST_MS);
}
/* Token is read before lock admission. No metadata address may escape this
 * short lock. A failed attempt skips observation and never waits. */
static int detail_history_enter(void)
{
    DetailWindowState *s=detail_state();
    LONG epoch,phase;
    if(!s || InterlockedCompareExchange(&s->tls_failure,0,0)) return 0;
    epoch=InterlockedCompareExchange(&s->epoch,0,0);
    phase=InterlockedCompareExchange(&s->phase,0,0);
    if(phase!=DETAIL_WINDOW_OPEN && phase!=DETAIL_WINDOW_DRAINING) return 0;
    if(InterlockedCompareExchange(&s->lock,1,0)) {
        InterlockedIncrement(&s->contention); return 0;
    }
    if(s->tls_failure || s->epoch!=epoch ||
       (s->phase!=DETAIL_WINDOW_OPEN && s->phase!=DETAIL_WINDOW_DRAINING)) {
        InterlockedIncrement(&s->stale_entries);
        InterlockedExchange(&s->lock,0); return 0;
    }
    return 1;
}
static void detail_window_leave(void)
{ InterlockedExchange(&detail_state()->lock,0); }
static LONG detail_window_epoch(void)
{
    LONG epoch=0;
    if(!detail_history_enter()) return 0;
    if(detail_state()->phase==DETAIL_WINDOW_OPEN &&
       !detail_deadline_passed(detail_state(),GetTickCount())) epoch=detail_state()->epoch;
    detail_window_leave();
    return epoch;
}
static int detail_window_enter(LONG epoch,unsigned kind)
{
    DetailWindowState *s;
    if(epoch<=0 || kind>=DETAIL_WINDOW_KIND_COUNT || !detail_history_enter()) return 0;
    s=detail_state();
    if(s->epoch!=epoch) {
        InterlockedIncrement(&s->stale_entries); detail_window_leave(); return 0;
    }
    InterlockedIncrement(&s->kind_samples[kind]);
    return 1;
}
static DetailWindowStatus detail_window_status(void)
{
    DetailWindowStatus status={0};
    DetailWindowState *s=detail_state();
    if(!s) return status;
    if(InterlockedCompareExchange(&s->lock,1,0)) {
        status.epoch=InterlockedCompareExchange(&s->epoch,0,0);
        status.change=InterlockedCompareExchange(&s->change,0,0);
        status.phase=-1; /* A caller must report unavailable, not a guessed mode. */
        return status;
    }
    status.epoch=s->epoch; status.phase=s->phase; status.change=s->change;
    InterlockedExchange(&s->lock,0);
    return status;
}
static void detail_close_locked(DetailWindowState *s,DWORD now)
{
    if(s->phase!=DETAIL_WINDOW_OPEN) return;
    s->closed=now;
    InterlockedExchange(&s->phase,DETAIL_WINDOW_DRAINING);
    InterlockedIncrement(&s->change);
}
/* Existing actions are queued as bounded bits. Metadata contention cannot
 * discard an acknowledged stop/mark/toggle; only this worker consumes them. */
static void detail_window_toggle(void)
{
    DetailWindowState *s=detail_state();
    if(!s) {
        DWORD saved;
        s=(DetailWindowState *)calloc(1,sizeof(*s));
        if(!s) { append_log("STAGE_DETAIL_WINDOW failed=allocation observationArmed=0"); return; }
        saved=GetLastError(); s->producer_tls=TlsAlloc(); SetLastError(saved);
        if(s->producer_tls==TLS_OUT_OF_INDEXES) {
            free(s); append_log("STAGE_DETAIL_WINDOW failed=producerTlsAllocation observationArmed=0"); return;
        }
        InterlockedExchangePointer((void *volatile *)&g_shadow_engine.detail_window,s);
    }
    atomic_or_long(&s->requests,1);
    detail_window_poll();
}
static void detail_window_mark(void)
{
    if(!detail_state()) return;
    atomic_or_long(&detail_state()->requests,2);
    detail_window_poll();
}
static void detail_window_finish(void)
{
    if(!detail_state()) return;
    atomic_or_long(&detail_state()->requests,4);
    detail_window_poll();
}
static void detail_apply_commands(DetailWindowState *s,LONG commands,DWORD now)
{
    if(commands&1) {
        if(s->phase==DETAIL_WINDOW_OPEN) detail_close_locked(s,now);
        else if(s->phase!=DETAIL_WINDOW_DRAINING && !s->report_pending &&
                !s->tls_failure && s->epoch<0x7FFFFFFFL) {
            if(s->report_failed) InterlockedIncrement(&s->discarded_failed_reports);
            /* All history users hold this lock. IDs and lifetime loss counters
             * remain non-reused; a new explicit recording replaces old detail. */
            memset(g_shadow_engine.completion_diagnostics.slots,0,
                sizeof(g_shadow_engine.completion_diagnostics.slots));
            memset(s->contaminated,0,sizeof(s->contaminated));
            s->started=now; s->marked=0; s->closed=0;
            s->marker_seen=0; s->timed_out=0; s->report_failed=0;
            InterlockedIncrement(&s->epoch);
            InterlockedExchange(&s->phase,DETAIL_WINDOW_OPEN);
            InterlockedIncrement(&s->change);
        }
    }
    if((commands&2) && s->phase==DETAIL_WINDOW_OPEN && !s->marker_seen) {
        s->marked=now; s->marker_seen=1;
    }
    if(commands&4) {
        detail_close_locked(s,now);
        if(s->phase==DETAIL_WINDOW_FROZEN && s->report_failed) s->report_pending=1;
    }
}
static int detail_history_pending(void)
{
    uint32_t i,j;
    for(i=0;i<COMPLETION_DIAGNOSTIC_SLOTS;++i)
        for(j=0;j<COMPLETION_DIAGNOSTIC_HISTORY;++j) {
            CompletionGenerationRecord *e=&g_shadow_engine.completion_diagnostics.slots[i].history[j];
            if(e->serial>0 && (!e->producer_sealed || !e->pre_finalizer_seen ||
               !e->reset_seen || (e->scheduled_external_mask&~e->completed_external_mask))) return 1;
        }
    return 0;
}
static void detail_window_poll(void)
{
    DetailWindowState *s=detail_state();
    LONG epoch,phase,commands;
    unsigned timeout;
    int frozen=0;
    DWORD now;
    if(!s) return;
    if(InterlockedCompareExchange(&s->lock,1,0)) {
        InterlockedIncrement(&s->contention); return;
    }
    now=GetTickCount();
    commands=InterlockedExchange(&s->requests,0);
    detail_apply_commands(s,commands,now);
    if(s->phase==DETAIL_WINDOW_OPEN && (s->tls_failure || detail_deadline_passed(s,now)))
        detail_close_locked(s,now);
    if(s->phase==DETAIL_WINDOW_DRAINING) {
        int pending=detail_history_pending();
        if(!pending || now-s->closed>=DETAIL_WINDOW_DRAIN_MS) {
            s->timed_out=pending!=0; s->report_pending=1;
            InterlockedExchange(&s->phase,DETAIL_WINDOW_FROZEN);
            InterlockedIncrement(&s->change); frozen=1;
        }
    }
    epoch=s->epoch; phase=s->phase; timeout=s->timed_out;
    detail_window_leave();
    if(commands) append_log("STAGE_DETAIL_WINDOW commands=%ld epoch=%ld phase=%ld timeoutMs=%u postMarkMs=%u drainMs=%u workRegister=H3 correction=producerClosure retained=H4 commandMeaning=toggleMarkFinish activeOrPendingReportMayRefuseNewRecording=1 epochExhausted=%u tlsFailure=%ld",
        (long)commands,(long)epoch,(long)phase,INTERSECTION_TIMEOUT_MS,INTERSECTION_POST_MS,DETAIL_WINDOW_DRAIN_MS,
        epoch==0x7FFFFFFFL?1U:0U,(long)InterlockedCompareExchange(&s->tls_failure,0,0));
    if(frozen) append_log("STAGE_DETAIL_WINDOW epoch=%ld phase=frozen drainTimedOut=%u nativeWait=0 pendingInterpretation=heuristicOnly boundaryAttribution=unknown workRegister=H3 correction=producerClosure retained=H4",
        (long)epoch,timeout);
    if(phase==DETAIL_WINDOW_FROZEN) detail_save_frozen_report();
}
#else
static int detail_history_enter(void) { return 1; }
#endif
static CompletionDiagnosticSlot *find_completion_diagnostic_slot(
    void *record,int create)
{
    uint32_t start,index;
    if(!record) return NULL;
    start=(uint32_t)(((uintptr_t)record>>4U)&
        (COMPLETION_DIAGNOSTIC_SLOTS-1U));
    for(index=0;index<COMPLETION_DIAGNOSTIC_SLOTS;++index) {
        CompletionDiagnosticSlot *slot=
            &g_shadow_engine.completion_diagnostics.slots[
            (start+index)&(COMPLETION_DIAGNOSTIC_SLOTS-1U)];
        void *current=(void *)slot->record;
        if(current==record) return slot;
        if(!current && create) {
            void *prior=InterlockedCompareExchangePointer(
                (PVOID volatile *)&slot->record,record,NULL);
            if(!prior || prior==record) return slot;
        }
    }
    if(create) InterlockedIncrement(
        &g_shadow_engine.completion_diagnostics.slot_misses);
    return NULL;
}

static CompletionGenerationRecord *find_completion_generation(
    CompletionDiagnosticSlot *slot,LONG serial)
{
    CompletionGenerationRecord *entry;
    if(!slot || serial<=0) return NULL;
    /* The producer publishes each serial only in this ring position. Retain
     * the atomic identity check: the slot may be unpublished or overwritten.
     * Completions without a known serial still require their full history scan. */
    entry=&slot->history[(uint32_t)serial&(COMPLETION_DIAGNOSTIC_HISTORY-1U)];
    return InterlockedCompareExchange(&entry->serial,0,0)==serial ? entry:NULL;
}

static CompletionGenerationRecord *current_completion_generation(
    void *record)
{
    CompletionDiagnosticSlot *slot=find_completion_diagnostic_slot(record,0);
    LONG serial=slot ? InterlockedCompareExchange(
        &slot->current_serial,0,0):0;
    return find_completion_generation(slot,serial);
}

static DetailProducerTicket completion_diagnostic_begin_producer_batch(void *record)
{
    DetailProducerTicket ticket={0};
    CompletionDiagnosticSlot *slot;
    CompletionGenerationRecord *entry;
    LifecycleRecordSnapshot *lifecycle;
    LONG lifecycle_generation=0,builder_call=0,producer_cycle=0,consumer_state=-1;
    LONG serial;
    uint32_t index;
#if SHADOW_ENGINE_DETAIL_WINDOWS
    DetailWindowState *s=detail_state();
    /* NULL shadows an admitted ancestor even when this producer is refused,
     * OFF/FROZEN, or cannot acquire metadata ownership. Before first arm there
     * is no TLS slot; an already-running producer later sees its NULL default. */
    if(!s || InterlockedCompareExchange(&s->tls_failure,0,0)) return ticket;
    if(!detail_tls_read(s,&ticket.previous_serial) ||
       !detail_tls_write(s,s->producer_tls,0,DETAIL_TLS_BIND_FAILED)) return ticket;
    ticket.tls_index=s->producer_tls; ticket.bound=1;
#endif
    if(!detail_history_enter()) {
#if SHADOW_ENGINE_DETAIL_WINDOWS
        if(s->phase==DETAIL_WINDOW_OPEN || s->phase==DETAIL_WINDOW_DRAINING)
            InterlockedIncrement(&s->unadmitted_batches);
#endif
        return ticket;
    }
#if SHADOW_ENGINE_DETAIL_WINDOWS
    if(s->phase!=DETAIL_WINDOW_OPEN || detail_deadline_passed(s,GetTickCount())) {
        detail_close_locked(s,GetTickCount());
        slot=find_completion_diagnostic_slot(record,0);
        if(slot) s->contaminated[slot-g_shadow_engine.completion_diagnostics.slots]=1;
        InterlockedIncrement(&s->unadmitted_batches);
        detail_window_leave(); return ticket;
    }
#endif
    slot=find_completion_diagnostic_slot(record,1);
    if(!slot) { detail_window_leave(); return ticket; }
#if SHADOW_ENGINE_DETAIL_WINDOWS
    ticket.control_index=(unsigned)(slot-g_shadow_engine.completion_diagnostics.slots);
    if(InterlockedCompareExchange(&s->producer_control[ticket.control_index],0,0)!=0) {
        s->contaminated[ticket.control_index]=1;
        InterlockedIncrement(&s->unadmitted_batches);
#else
    if(InterlockedCompareExchange(&slot->active_producer_serial,0,0)!=0) {
#endif
        InterlockedIncrement(
            &g_shadow_engine.completion_diagnostics.producer_batch_overlaps);
        detail_window_leave(); return ticket;
    }
    lifecycle=find_existing_lifecycle_record_fast(record);
    if(lifecycle) {
        lifecycle_generation=InterlockedCompareExchange(
            &lifecycle->generation,0,0);
        builder_call=InterlockedCompareExchange(&lifecycle->builder_call,0,0);
        producer_cycle=InterlockedCompareExchange(&lifecycle->producer_cycle,0,0);
        consumer_state=InterlockedCompareExchange(
            &lifecycle->extra_slice_result_consumer_state,0,0);
    } else {
        InterlockedIncrement(
            &g_shadow_engine.completion_diagnostics.producer_batches_without_lifecycle);
    }
    if(g_shadow_engine.completion_diagnostics.serial==0x7FFFFFFFL) {
#if SHADOW_ENGINE_DETAIL_WINDOWS
        InterlockedIncrement(&detail_state()->serial_exhausted);
#endif
        detail_window_leave(); return ticket;
    }
    serial=atomic_add_long(&g_shadow_engine.completion_diagnostics.serial,1)+1;
    index=(uint32_t)serial&(COMPLETION_DIAGNOSTIC_HISTORY-1U);
    entry=&slot->history[index];
    /* The metadata lock excludes existing readers as well as new entrants.
     * Serial-last publication alone cannot protect an already borrowed entry. */
#if SHADOW_ENGINE_DETAIL_WINDOWS
    if(entry->serial>0 && (!entry->producer_sealed || !entry->pre_finalizer_seen ||
       !entry->reset_seen || (entry->scheduled_external_mask&~entry->completed_external_mask)))
        InterlockedIncrement(&detail_state()->ring_reuse_incomplete);
#endif
    InterlockedExchange(&entry->serial,0);
    memset(entry,0,sizeof(*entry));
    entry->lifecycle_generation=lifecycle_generation;
    entry->builder_call=builder_call;
    entry->producer_cycle=producer_cycle;
    entry->producer_thread=(LONG)GetCurrentThreadId();
    entry->producer_begin_consumer_state=consumer_state;
    InterlockedExchange(&entry->serial,serial);
#if SHADOW_ENGINE_DETAIL_WINDOWS
    /* Only admission under metadata ownership can claim an empty control.
     * Return-side CAS may clear it concurrently, but can never fill it. */
    InterlockedExchange(&s->producer_control[ticket.control_index],serial);
    InterlockedExchange(&slot->active_producer_serial,serial);
    ticket.epoch=s->epoch;
#else
    if(InterlockedCompareExchange(&slot->active_producer_serial,serial,0)!=0) {
        InterlockedExchange(&entry->serial,0);
        InterlockedIncrement(
            &g_shadow_engine.completion_diagnostics.producer_batch_overlaps);
        detail_window_leave(); return ticket;
    }
#endif
    InterlockedExchange(&slot->current_serial,serial);
    InterlockedIncrement(
        &g_shadow_engine.completion_diagnostics.producer_batches_started);
    ticket.serial=serial;
    detail_window_leave();
#if SHADOW_ENGINE_DETAIL_WINDOWS
    /* No native/TLS call under the history lock. A freeze/rearm here cannot
     * resurrect the old history: globally unique serial lookup rejects it. */
    detail_tls_write(s,ticket.tls_index,(uintptr_t)serial,DETAIL_TLS_PUBLISH_FAILED);
#endif
    return ticket;
}

static void completion_diagnostic_seal_producer_batch(void *record,DetailProducerTicket *ticket)
{
    CompletionDiagnosticSlot *slot;
    CompletionGenerationRecord *entry;
    LifecycleRecordSnapshot *lifecycle;
    LONG scheduled,completed,end_generation=0,end_builder=0,end_cycle=0,end_consumer=-1;
    DetailProducerTicket owned=*ticket;
    LONG serial=owned.serial;
    memset(ticket,0,sizeof(*ticket));
#if SHADOW_ENGINE_DETAIL_WINDOWS
    DetailWindowState *s=detail_state();
    /* Mandatory diagnostic scope closure never depends on optional metadata
     * ownership, window phase, TLS health or a surviving history entry. The
     * expected non-reused serial cannot clear a later claim after rearm. */
    if(serial>0 && InterlockedCompareExchange(
       &s->producer_control[owned.control_index],0,serial)!=serial)
        InterlockedIncrement(&s->control_close_failures);
    if(owned.bound) detail_tls_write(s,owned.tls_index,
        owned.previous_serial,DETAIL_TLS_RESTORE_FAILED);
#endif
    if(serial<=0) return;
    if(!detail_history_enter()) {
#if SHADOW_ENGINE_DETAIL_WINDOWS
        InterlockedIncrement(&s->missing_seals);
#endif
        return;
    }
#if SHADOW_ENGINE_DETAIL_WINDOWS
    if(s->epoch!=owned.epoch) {
        InterlockedIncrement(&s->missing_seals);
        InterlockedIncrement(&s->stale_entries); detail_window_leave(); return;
    }
#endif
    slot=find_completion_diagnostic_slot(record,0);
    entry=find_completion_generation(slot,serial);
    if(!slot || !entry) {
#if SHADOW_ENGINE_DETAIL_WINDOWS
        InterlockedIncrement(&s->missing_seals);
#endif
        detail_window_leave(); return;
    }
    lifecycle=find_existing_lifecycle_record_fast(record);
    if(lifecycle) {
        end_generation=InterlockedCompareExchange(&lifecycle->generation,0,0);
        end_builder=InterlockedCompareExchange(&lifecycle->builder_call,0,0);
        end_cycle=InterlockedCompareExchange(&lifecycle->producer_cycle,0,0);
        end_consumer=InterlockedCompareExchange(
            &lifecycle->extra_slice_result_consumer_state,0,0);
    }
    entry->producer_end_generation=end_generation;
    entry->producer_end_builder_call=end_builder;
    entry->producer_end_cycle=end_cycle;
    entry->producer_end_consumer_state=end_consumer;
    scheduled=InterlockedCompareExchange(&entry->scheduled_external_mask,0,0);
    InterlockedExchange(&entry->producer_sealed,1);
    completed=InterlockedCompareExchange(&entry->completed_external_mask,0,0);
    InterlockedExchange(&entry->completed_at_producer_seal_mask,completed);
    if(!scheduled) InterlockedIncrement(
        &g_shadow_engine.completion_diagnostics.producer_batches_without_external_work);
    if((scheduled&~completed)!=0) InterlockedIncrement(
        &g_shadow_engine.completion_diagnostics.producer_batches_outstanding_at_seal);
    if(end_generation!=entry->lifecycle_generation ||
       end_cycle!=entry->producer_cycle) InterlockedIncrement(
        &g_shadow_engine.completion_diagnostics.producer_lifecycle_changed_before_seal);
#if SHADOW_ENGINE_DETAIL_WINDOWS
    /* The control was already closed. A newer producer may have published
     * this metadata mirror before the old optional seal acquired its lock. */
    InterlockedCompareExchange(&slot->active_producer_serial,0,serial);
#else
    if(InterlockedCompareExchange(&slot->active_producer_serial,0,serial)!=serial)
        InterlockedIncrement(
            &g_shadow_engine.completion_diagnostics.producer_active_clear_failures);
#endif
    InterlockedIncrement(
        &g_shadow_engine.completion_diagnostics.producer_batches_sealed);
    detail_window_leave();
}

static void record_completion_latency(CompletionGenerationRecord *entry,
    uint32_t external_index,LONGLONG completed_qpc)
{
    LONGLONG scheduled=entry->schedule_qpc[external_index];
    LONGLONG frequency=g_shadow_engine.completion_diagnostics.qpc_frequency.QuadPart;
    uint64_t microseconds;
    uint32_t bucket;
    if(scheduled<=0 || completed_qpc<=scheduled || frequency<=0) return;
    microseconds=(uint64_t)((completed_qpc-scheduled)*1000000ULL/
        (uint64_t)frequency);
    if(microseconds<250ULL) bucket=0;
    else if(microseconds<500ULL) bucket=1;
    else if(microseconds<1000ULL) bucket=2;
    else if(microseconds<2000ULL) bucket=3;
    else if(microseconds<4000ULL) bucket=4;
    else if(microseconds<8000ULL) bucket=5;
    else if(microseconds<16000ULL) bucket=6;
    else bucket=7;
    InterlockedIncrement(
        &g_shadow_engine.completion_diagnostics.latency_buckets[bucket]);
}

static void completion_diagnostic_record_completion(void *inline_results,
    uint32_t index,LONG lifecycle_generation,LONG producer_cycle,
    LONG consumer_state)
{
    CompletionDiagnosticSlot *slot;
    CompletionGenerationRecord *chosen=NULL;
    LONG current_serial,chosen_serial=0,matches=0,bit,lag,thread,prior_thread;
    uint32_t external_index,history_index;
    LARGE_INTEGER now;
    void *record;
    if(!inline_results || index<NATIVE_SLICE_RESULT_COUNT ||
       index>=TOTAL_LOCAL_MAPS) return;
    if(!detail_history_enter()) return;
    record=(unsigned char *)inline_results-0x70U;
    slot=find_completion_diagnostic_slot(record,0);
    if(!slot) {
        InterlockedIncrement(
            &g_shadow_engine.completion_diagnostics.completions_unmatched);
        detail_window_leave(); return;
    }
    external_index=index-NATIVE_SLICE_RESULT_COUNT;
#if SHADOW_ENGINE_DETAIL_WINDOWS
    if(detail_state()->contaminated[slot-g_shadow_engine.completion_diagnostics.slots]) {
        /* An unobserved producer can publish the same result index. Preserve
         * pending evidence rather than fabricating completion/latency/lag. */
        InterlockedIncrement(&detail_state()->contaminated_completions);
        detail_window_leave(); return;
    }
#endif
    bit=(LONG)(1U<<external_index);
    current_serial=InterlockedCompareExchange(&slot->current_serial,0,0);
    for(history_index=0;history_index<COMPLETION_DIAGNOSTIC_HISTORY;
        ++history_index) {
        CompletionGenerationRecord *entry=&slot->history[history_index];
        LONG serial=InterlockedCompareExchange(&entry->serial,0,0);
        LONG scheduled=InterlockedCompareExchange(
            &entry->scheduled_external_mask,0,0);
        LONG completed=InterlockedCompareExchange(
            &entry->completed_external_mask,0,0);
        if(serial>0 && (scheduled&bit) && !(completed&bit)) {
            ++matches;
            if(!chosen || serial<chosen_serial) {
                chosen=entry;
                chosen_serial=serial;
            }
        }
    }
    if(!chosen) {
        InterlockedIncrement(
            &g_shadow_engine.completion_diagnostics.completions_unmatched);
        detail_window_leave(); return;
    }
    if(matches>1) InterlockedIncrement(
        &g_shadow_engine.completion_diagnostics.completions_ambiguous);
    atomic_or_long(&chosen->completed_external_mask,bit);
    InterlockedIncrement(
        &g_shadow_engine.completion_diagnostics.completions_matched);
    if(InterlockedCompareExchange(&chosen->producer_sealed,0,0)!=0)
        InterlockedIncrement(
            &g_shadow_engine.completion_diagnostics.completions_after_producer_seal);
    else InterlockedIncrement(
        &g_shadow_engine.completion_diagnostics.completions_before_producer_seal);
    if(lifecycle_generation==chosen->lifecycle_generation &&
       producer_cycle==chosen->producer_cycle)
        InterlockedIncrement(
            &g_shadow_engine.completion_diagnostics.completions_same_lifecycle);
    else InterlockedIncrement(
        &g_shadow_engine.completion_diagnostics.completions_changed_lifecycle);
    if(consumer_state==0) InterlockedIncrement(
        &g_shadow_engine.completion_diagnostics.completions_consumer_open);
    else InterlockedIncrement(
        &g_shadow_engine.completion_diagnostics.completions_consumer_closed);
    lag=current_serial>=chosen_serial ? current_serial-chosen_serial:0;
    if(lag==0) InterlockedIncrement(
        &g_shadow_engine.completion_diagnostics.completions_current_generation);
    else if(lag==1) InterlockedIncrement(
        &g_shadow_engine.completion_diagnostics.completions_one_generation_late);
    else InterlockedIncrement(
        &g_shadow_engine.completion_diagnostics.completions_multiple_generations_late);
    if(lag>InterlockedCompareExchange(&chosen->maximum_generation_lag,0,0))
        InterlockedExchange(&chosen->maximum_generation_lag,lag);
    thread=(LONG)GetCurrentThreadId();
    prior_thread=InterlockedCompareExchange(&chosen->completion_thread,thread,0);
    if(prior_thread && prior_thread!=thread) {
        InterlockedExchange(&chosen->completion_thread,thread);
        InterlockedIncrement(&chosen->completion_thread_switches);
    }
    QueryPerformanceCounter(&now);
    record_completion_latency(chosen,external_index,now.QuadPart);
    detail_window_leave();
}

static void completion_diagnostic_mark_pre_finalizer(void *record)
{
    CompletionGenerationRecord *entry;
    LONG scheduled,completed;
    if(!detail_history_enter()) return;
    entry=current_completion_generation(record);
    if(!entry) { detail_window_leave(); return; }
    scheduled=InterlockedCompareExchange(&entry->scheduled_external_mask,0,0);
    completed=InterlockedCompareExchange(&entry->completed_external_mask,0,0);
    InterlockedExchange(&entry->pre_finalizer_mask,completed);
    if((scheduled&~completed)!=0) InterlockedIncrement(
        &g_shadow_engine.completion_diagnostics.pre_finalizer_outstanding_generations);
    InterlockedExchange(&entry->pre_finalizer_seen,1);
    detail_window_leave();
}

static void completion_diagnostic_mark_reset(void *record)
{
    CompletionGenerationRecord *entry;
    LONG scheduled,completed;
    if(!detail_history_enter()) return;
    entry=current_completion_generation(record);
    if(!entry || InterlockedCompareExchange(&entry->reset_seen,1,0)!=0) {
        detail_window_leave(); return;
    }
    scheduled=InterlockedCompareExchange(&entry->scheduled_external_mask,0,0);
    completed=InterlockedCompareExchange(&entry->completed_external_mask,0,0);
    InterlockedExchange(&entry->reset_mask,completed);
    if((scheduled&~completed)!=0) InterlockedIncrement(
        &g_shadow_engine.completion_diagnostics.reset_outstanding_generations);
    detail_window_leave();
}

static void __fastcall hooked_shadow_face_scheduler(void *renderer,void *queue,
    uint32_t entry_index,uint32_t face_index,void *inline_results)
{
    OVERHEAD_BEGIN(schedule_timing,OH_COMPLETION_SCHEDULE);
    CompletionDiagnosticSlot *slot=NULL;
    CompletionGenerationRecord *entry=NULL;
    LONG active_serial=0,bit,prior,thread;
    LARGE_INTEGER now;
    int observing;
#if SHADOW_ENGINE_DETAIL_WINDOWS
    DetailWindowState *s=detail_state();
    uintptr_t tls_serial=0;
    /* Scheduler entry is nested in the native producer on this thread. TLS
     * has no borrowed pointers and refused nested scopes contain NULL. */
    if(s && !InterlockedCompareExchange(&s->tls_failure,0,0) &&
       (s->phase==DETAIL_WINDOW_OPEN || s->phase==DETAIL_WINDOW_DRAINING))
        detail_tls_read(s,&tls_serial);
#endif
    observing=detail_history_enter();
    if(observing) {
    if(inline_results) {
        slot=find_completion_diagnostic_slot(
            (unsigned char *)inline_results-0x70U,0);
#if SHADOW_ENGINE_DETAIL_WINDOWS
        active_serial=tls_serial<=0x7FFFFFFFUL ? (LONG)tls_serial:0;
#else
        active_serial=slot ? InterlockedCompareExchange(&slot->active_producer_serial,0,0):0;
#endif
        entry=find_completion_generation(slot,active_serial);
    }
    if(!entry) InterlockedIncrement(
        &g_shadow_engine.completion_diagnostics.scheduler_calls_outside_producer);
    if(entry) {
        InterlockedIncrement(&entry->scheduled_calls);
        thread=(LONG)GetCurrentThreadId();
        InterlockedCompareExchange(&entry->schedule_thread,thread,0);
        /* Argument 3 is queue-entry/result identity. Record the first face for
         * each entry once; this preserves exact expected-result identity while
         * avoiding v2.0.8's VirtualQuery on every hot scheduler call. */
        if(entry_index>=TOTAL_LOCAL_MAPS) {
            InterlockedIncrement(
                &g_shadow_engine.completion_diagnostics.schedule_overflow);
        } else if(entry_index<NATIVE_SLICE_RESULT_COUNT) {
            bit=(LONG)(1U<<entry_index);
            prior=atomic_or_long(&entry->scheduled_native_mask,bit);
            if(!(prior&bit))
                InterlockedIncrement(
                    &g_shadow_engine.completion_diagnostics.scheduled_native);
        } else {
            uint32_t external_index=entry_index-NATIVE_SLICE_RESULT_COUNT;
            bit=(LONG)(1U<<external_index);
            prior=atomic_or_long(&entry->scheduled_external_mask,bit);
            if(!(prior&bit)) {
                QueryPerformanceCounter(&now);
                entry->schedule_qpc[external_index]=now.QuadPart;
                InterlockedIncrement(
                    &g_shadow_engine.completion_diagnostics.scheduled_external);
                if(InterlockedCompareExchange(
                   &entry->pre_finalizer_seen,0,0)!=0) {
                    atomic_or_long(
                        &entry->scheduled_after_pre_finalizer_mask,bit);
                    InterlockedIncrement(
                        &g_shadow_engine.completion_diagnostics.first_seen_after_pre_finalizer);
                } else {
                    InterlockedIncrement(
                        &g_shadow_engine.completion_diagnostics.first_seen_before_pre_finalizer);
                }
                if(InterlockedCompareExchange(&entry->reset_seen,0,0)!=0) {
                    atomic_or_long(&entry->scheduled_after_reset_mask,bit);
                    InterlockedIncrement(
                        &g_shadow_engine.completion_diagnostics.first_seen_after_reset);
                } else {
                    InterlockedIncrement(
                        &g_shadow_engine.completion_diagnostics.first_seen_before_reset);
                }
            }
        }
    }
    detail_window_leave();
    }
    OVERHEAD_END(schedule_timing);
    OVERHEAD_BEGIN(native_schedule_timing,OH_SCHEDULER_NATIVE);
    g_shadow_engine.hooks.original_shadow_face_scheduler(renderer,queue,
        entry_index,face_index,inline_results);
    OVERHEAD_END(native_schedule_timing);
}

#if SHADOW_ENGINE_DETAIL_WINDOWS
#define DETAIL_COUNTER_SCOPE "recordedWindowsOnly"
#else
#define DETAIL_COUNTER_SCOPE "continuousCompatibilityFixture"
#endif

static void completion_diagnostic_write_summary(CompletionDiagnosticState *d)
{
    append_log("STAGE_Z_COMPLETION_SUMMARY counterScope=" DETAIL_COUNTER_SCOPE " boundaryAttribution=unknown intervalMs=%u hookState=%ld scheduledNative=%ld scheduledExternal=%ld scheduleOverflow=%ld matched=%ld unmatched=%ld ambiguous=%ld currentGeneration=%ld oneGenerationLate=%ld multipleGenerationsLate=%ld preFinalizerOutstandingGenerations=%ld resetOutstandingGenerations=%ld firstSeenBeforePreFinalizer=%ld firstSeenAfterPreFinalizer=%ld firstSeenBeforeReset=%ld firstSeenAfterReset=%ld latencyLt250us=%ld latency250To500us=%ld latency500usTo1ms=%ld latency1To2ms=%ld latency2To4ms=%ld latency4To8ms=%ld latency8To16ms=%ld latencyGe16ms=%ld slotMisses=%ld latencyOrigin=firstFace nativeLoopProbe=staticOnly policyChange=0 waitAdded=0",
        EXTERNAL_SLICE_SUMMARY_PERIOD_MS,
        (long)d->hook_state,
        (long)d->scheduled_native,
        (long)d->scheduled_external,
        (long)d->schedule_overflow,
        (long)d->completions_matched,
        (long)d->completions_unmatched,
        (long)d->completions_ambiguous,
        (long)d->completions_current_generation,
        (long)d->completions_one_generation_late,
        (long)d->completions_multiple_generations_late,
        (long)d->pre_finalizer_outstanding_generations,
        (long)d->reset_outstanding_generations,
        (long)d->first_seen_before_pre_finalizer,
        (long)d->first_seen_after_pre_finalizer,
        (long)d->first_seen_before_reset,
        (long)d->first_seen_after_reset,
        (long)d->latency_buckets[0],
        (long)d->latency_buckets[1],
        (long)d->latency_buckets[2],
        (long)d->latency_buckets[3],
        (long)d->latency_buckets[4],
        (long)d->latency_buckets[5],
        (long)d->latency_buckets[6],
        (long)d->latency_buckets[7],
        (long)d->slot_misses);
    append_log("STAGE_Z_PRODUCER_SUMMARY counterScope=" DETAIL_COUNTER_SCOPE " boundaryAttribution=unknown intervalMs=%u batchesStarted=%ld batchesSealed=%ld batchesWithoutLifecycle=%ld overlaps=%ld activeClearFailures=%ld batchesWithoutExternal=%ld batchesOutstandingAtSeal=%ld lifecycleChangedBeforeSeal=%ld schedulerCallsOutsideProducer=%ld completionsBeforeSeal=%ld completionsAfterSeal=%ld completionsSameLifecycle=%ld completionsChangedLifecycle=%ld completionsConsumerOpen=%ld completionsConsumerClosed=%ld observationOnly=1 waitAdded=0 policyChange=0",
        EXTERNAL_SLICE_SUMMARY_PERIOD_MS,
        (long)d->producer_batches_started,
        (long)d->producer_batches_sealed,
        (long)d->producer_batches_without_lifecycle,
        (long)d->producer_batch_overlaps,
        (long)d->producer_active_clear_failures,
        (long)d->producer_batches_without_external_work,
        (long)d->producer_batches_outstanding_at_seal,
        (long)d->producer_lifecycle_changed_before_seal,
        (long)d->scheduler_calls_outside_producer,
        (long)d->completions_before_producer_seal,
        (long)d->completions_after_producer_seal,
        (long)d->completions_same_lifecycle,
        (long)d->completions_changed_lifecycle,
        (long)d->completions_consumer_open,
        (long)d->completions_consumer_closed);
}

static void completion_diagnostic_write_manual(const char *state,LONG capture_serial,
    CompletionDiagnosticState *d)
{
    uint32_t slot_index,history_index,logged=0;
    append_log("STAGE_Z_COMPLETION_MANUAL_BEGIN counterScope=" DETAIL_COUNTER_SCOPE " boundaryAttribution=unknown state=%s captureSerial=%ld diagnosticSerial=%ld scheduledNative=%ld scheduledExternal=%ld matched=%ld unmatched=%ld ambiguous=%ld currentGeneration=%ld oneGenerationLate=%ld multipleGenerationsLate=%ld preFinalizerOutstandingGenerations=%ld resetOutstandingGenerations=%ld firstSeenBeforePreFinalizer=%ld firstSeenAfterPreFinalizer=%ld firstSeenBeforeReset=%ld firstSeenAfterReset=%ld latencyLt250us=%ld latency250To500us=%ld latency500usTo1ms=%ld latency1To2ms=%ld latency2To4ms=%ld latency4To8ms=%ld latency8To16ms=%ld latencyGe16ms=%ld latencyOrigin=firstFace",
        state,(long)capture_serial,
        (long)d->serial,
        (long)d->scheduled_native,
        (long)d->scheduled_external,
        (long)d->completions_matched,
        (long)d->completions_unmatched,
        (long)d->completions_ambiguous,
        (long)d->completions_current_generation,
        (long)d->completions_one_generation_late,
        (long)d->completions_multiple_generations_late,
        (long)d->pre_finalizer_outstanding_generations,
        (long)d->reset_outstanding_generations,
        (long)d->first_seen_before_pre_finalizer,
        (long)d->first_seen_after_pre_finalizer,
        (long)d->first_seen_before_reset,
        (long)d->first_seen_after_reset,
        (long)d->latency_buckets[0],
        (long)d->latency_buckets[1],
        (long)d->latency_buckets[2],
        (long)d->latency_buckets[3],
        (long)d->latency_buckets[4],
        (long)d->latency_buckets[5],
        (long)d->latency_buckets[6],
        (long)d->latency_buckets[7]);
    append_log("STAGE_Z_PRODUCER_MANUAL counterScope=" DETAIL_COUNTER_SCOPE " boundaryAttribution=unknown state=%s captureSerial=%ld batchesStarted=%ld batchesSealed=%ld batchesWithoutLifecycle=%ld overlaps=%ld activeClearFailures=%ld batchesWithoutExternal=%ld batchesOutstandingAtSeal=%ld lifecycleChangedBeforeSeal=%ld schedulerCallsOutsideProducer=%ld completionsBeforeSeal=%ld completionsAfterSeal=%ld completionsSameLifecycle=%ld completionsChangedLifecycle=%ld completionsConsumerOpen=%ld completionsConsumerClosed=%ld observationOnly=1 waitAdded=0 policyChange=0",
        state,(long)capture_serial,
        (long)d->producer_batches_started,
        (long)d->producer_batches_sealed,
        (long)d->producer_batches_without_lifecycle,
        (long)d->producer_batch_overlaps,
        (long)d->producer_active_clear_failures,
        (long)d->producer_batches_without_external_work,
        (long)d->producer_batches_outstanding_at_seal,
        (long)d->producer_lifecycle_changed_before_seal,
        (long)d->scheduler_calls_outside_producer,
        (long)d->completions_before_producer_seal,
        (long)d->completions_after_producer_seal,
        (long)d->completions_same_lifecycle,
        (long)d->completions_changed_lifecycle,
        (long)d->completions_consumer_open,
        (long)d->completions_consumer_closed);
    for(slot_index=0;slot_index<COMPLETION_DIAGNOSTIC_SLOTS && logged<32U;
        ++slot_index) {
        CompletionDiagnosticSlot *slot=
            &d->slots[slot_index];
        LONG current_serial=InterlockedCompareExchange(
            &slot->current_serial,0,0);
        if(!slot->record || current_serial<=0) continue;
        for(history_index=0;
            history_index<COMPLETION_DIAGNOSTIC_HISTORY && logged<32U;
            ++history_index) {
            CompletionGenerationRecord *entry=&slot->history[history_index];
            LONG serial=InterlockedCompareExchange(&entry->serial,0,0);
            LONG scheduled,completed;
            if(serial<=0) continue;
            scheduled=InterlockedCompareExchange(
                &entry->scheduled_external_mask,0,0);
            completed=InterlockedCompareExchange(
                &entry->completed_external_mask,0,0);
            if(!scheduled && !completed) continue;
            append_log("STAGE_Z_COMPLETION_GENERATION historyScope=upTo8SlotsPerRecord slotIndex=globalSerialModulo8 correlation=heuristic boundaryAttribution=unknown state=%s captureSerial=%ld record=%p serial=%ld current=%u lifecycleGeneration=%ld builderCall=%ld scheduledCalls=%ld nativeEntryMask=0x%05lX expectedExternalMask=0x%04lX completedMask=0x%04lX producerSealed=%ld completedAtProducerSealMask=0x%04lX producerThread=%ld producerBeginConsumerState=%ld producerEndConsumerState=%ld producerEndGeneration=%ld producerEndBuilderCall=%ld preFinalizerMask=0x%04lX resetMask=0x%04lX firstSeenAfterPreFinalizerMask=0x%04lX firstSeenAfterResetMask=0x%04lX outstandingMask=0x%04lX preFinalizerSeen=%ld resetSeen=%ld maximumGenerationLag=%ld scheduleThread=%ld completionThread=%ld completionThreadSwitches=%ld producerCycle=%ld producerEndCycle=%ld lineageKey=constructorProducer",
                state,(long)capture_serial,(void *)slot->record,(long)serial,
                serial==current_serial ? 1U:0U,
                (long)entry->lifecycle_generation,(long)entry->builder_call,
                (long)entry->scheduled_calls,
                (unsigned long)entry->scheduled_native_mask,
                (unsigned long)scheduled,
                (unsigned long)completed,
                (long)entry->producer_sealed,
                (unsigned long)entry->completed_at_producer_seal_mask,
                (long)entry->producer_thread,
                (long)entry->producer_begin_consumer_state,
                (long)entry->producer_end_consumer_state,
                (long)entry->producer_end_generation,
                (long)entry->producer_end_builder_call,
                (unsigned long)entry->pre_finalizer_mask,
                (unsigned long)entry->reset_mask,
                (unsigned long)entry->scheduled_after_pre_finalizer_mask,
                (unsigned long)entry->scheduled_after_reset_mask,
                (unsigned long)(scheduled&~completed),
                (long)entry->pre_finalizer_seen,(long)entry->reset_seen,
                (long)entry->maximum_generation_lag,
                (long)entry->schedule_thread,
                (long)entry->completion_thread,
                (long)entry->completion_thread_switches,
                (long)entry->producer_cycle,(long)entry->producer_end_cycle);
            ++logged;
        }
    }
    append_log("STAGE_Z_COMPLETION_MANUAL_END counterScope=" DETAIL_COUNTER_SCOPE " boundaryAttribution=unknown state=%s captureSerial=%ld generationsLogged=%u maxGenerations=32 bounded=1",
        state,(long)capture_serial,logged);
}

#if SHADOW_ENGINE_DETAIL_WINDOWS
typedef struct DetailReportMetadata {
    LONG epoch,phase,change,contention,stale,ring_lost,unadmitted,contaminated,exhausted,discarded;
    LONG samples[DETAIL_WINDOW_KIND_COUNT];
    DWORD started,marked,closed;
    unsigned timed_out,marker_seen,pending;
    LONG tls_failure,tls_failures,missing_seals,control_close_failures;
} DetailReportMetadata;

/* Independent report ownership keeps one bounded copied buffer stable through
 * file output. Metadata lock is held only while copying patch-owned scalars. */
static CompletionDiagnosticState *detail_report_copy(DetailReportMetadata *m)
{
    DetailWindowState *s=detail_state();
    if(!s || !s->epoch || InterlockedCompareExchange(&s->report_lock,1,0)) return NULL;
    if(InterlockedCompareExchange(&s->lock,1,0)) {
        InterlockedExchange(&s->report_lock,0); return NULL;
    }
    memcpy(&s->snapshot,&g_shadow_engine.completion_diagnostics,sizeof(s->snapshot));
    m->epoch=s->epoch; m->phase=s->phase; m->change=s->change;
    m->contention=s->contention; m->stale=s->stale_entries;
    m->ring_lost=s->ring_reuse_incomplete; m->unadmitted=s->unadmitted_batches;
    m->contaminated=s->contaminated_completions; m->exhausted=s->serial_exhausted;
    m->discarded=s->discarded_failed_reports;
    memcpy(m->samples,s->kind_samples,sizeof(m->samples));
    m->started=s->started; m->marked=s->marked; m->closed=s->closed;
    m->timed_out=s->timed_out; m->marker_seen=s->marker_seen;
    m->pending=(unsigned)detail_history_pending();
    m->tls_failure=InterlockedCompareExchange(&s->tls_failure,0,0);
    m->tls_failures=InterlockedCompareExchange(&s->tls_failures,0,0);
    m->missing_seals=InterlockedCompareExchange(&s->missing_seals,0,0);
    m->control_close_failures=InterlockedCompareExchange(&s->control_close_failures,0,0);
    InterlockedExchange(&s->lock,0);
    return &s->snapshot;
}
static void detail_report_coverage(const DetailReportMetadata *m)
{
    append_log("STAGE_DETAIL_PRODUCER_SCOPE epoch=%ld schedulerIdentity=threadLocalSerial controlLifetime=process counterScope=processLifetime tlsFailure=%ld tlsFailures=%ld missingSealMetadata=%ld controlCloseFailures=%ld failedDetailDisabledUntilRestart=1 beforeFirstArmTlsWork=0 afterFirstArmProducerTlsBinding=1 nativeWait=0 workRegister=H3",
        (long)m->epoch,(long)m->tls_failure,(long)m->tls_failures,
        (long)m->missing_seals,(long)m->control_close_failures);
    append_log("STAGE_DETAIL_COVERAGE epoch=%ld phase=%ld change=%ld coverage=%s startedTick=%lu markedTick=%lu closedTick=%lu markerSeen=%u drainTimedOut=%u pendingRetainedHistory=%u counters=cumulativeRecordedWindows contention=%ld staleEpoch=%ld ringReuseIncomplete=%ld unadmittedBatches=%ld contaminatedCompletions=%ld serialExhausted=%ld discardedFailedReports=%ld admissionSamples=%ld tailBeforeSamples=%ld tailAfterSamples=%ld preArmHistory=notRecorded allSessionDetail=notRecorded boundaryAttribution=unknown oldestPendingMatch=heuristic retainedHistory=upTo8SlotsPerRecord slotIndex=globalSerialModulo8 samplePairsMayBeIncomplete=1 workRegister=H3 correction=producerClosure retained=H4",
        (long)m->epoch,(long)m->phase,(long)m->change,
        m->phase==DETAIL_WINDOW_FROZEN?"lastWindow":
        (m->phase==DETAIL_WINDOW_DRAINING?"drainingWindow":"openWindow"),
        (unsigned long)m->started,(unsigned long)m->marked,(unsigned long)m->closed,
        m->marker_seen,m->timed_out,m->pending,(long)m->contention,(long)m->stale,
        (long)m->ring_lost,(long)m->unadmitted,(long)m->contaminated,(long)m->exhausted,(long)m->discarded,
        (long)m->samples[DETAIL_WINDOW_ADMISSION],(long)m->samples[DETAIL_WINDOW_TAIL_BEFORE],
        (long)m->samples[DETAIL_WINDOW_TAIL_AFTER]);
}
static int completion_diagnostic_report_snapshot(const char *state,LONG capture_serial,int manual)
{
    DetailReportMetadata metadata;
    CompletionDiagnosticState *copy=detail_report_copy(&metadata);
    if(!copy) return 0;
    detail_report_coverage(&metadata);
    if(manual) completion_diagnostic_write_manual(state,capture_serial,copy);
    else completion_diagnostic_write_summary(copy);
    InterlockedExchange(&detail_state()->report_lock,0);
    return 1;
}
static void detail_save_frozen_report(void)
{
    DetailWindowState *s=detail_state();
    LONG before,epoch;
    int saved;
    if(!s || !s->report_pending || s->phase!=DETAIL_WINDOW_FROZEN) return;
    epoch=s->epoch;
    before=InterlockedCompareExchange(&g_shadow_engine.bootstrap.log_write_failures,0,0);
    if(!completion_diagnostic_report_snapshot("DETAIL_WINDOW",epoch,1)) return;
    saved=flush_session_log() && before==InterlockedCompareExchange(
        &g_shadow_engine.bootstrap.log_write_failures,0,0);
    /* Only the worker toggles/retries this state. Failed output is retained;
     * retry needs explicit finish/report, never an automatic disk-write loop. */
    s->report_pending=0; s->report_failed=!saved;
    append_log("STAGE_DETAIL_REPORT epoch=%ld saved=%d retry=%s frozenIsNotSaved=1 workRegister=H3 correction=producerClosure retained=H4",
        (long)epoch,saved,saved?"none":"explicitFinishReport");
}
#endif

static void completion_diagnostic_log_summary(void)
{
    DWORD now=GetTickCount();
    LONG prior=InterlockedCompareExchange(
        &g_shadow_engine.completion_diagnostics.last_summary_tick,0,0);
    if((DWORD)(now-(DWORD)prior)<EXTERNAL_SLICE_SUMMARY_PERIOD_MS) return;
    if(InterlockedCompareExchange(&g_shadow_engine.completion_diagnostics.last_summary_tick,
       (LONG)now,prior)!=prior) return;
#if SHADOW_ENGINE_DETAIL_WINDOWS
    if(!completion_diagnostic_report_snapshot("PERIODIC",0,0))
        append_log("STAGE_DETAIL_COVERAGE coverage=%s counters=notReported workRegister=H3 correction=producerClosure retained=H4",
            detail_state() && detail_state()->epoch?"snapshotBusy":"notRecorded");
#else
    completion_diagnostic_write_summary(&g_shadow_engine.completion_diagnostics);
#endif
}
static void completion_diagnostic_log_manual(const char *state,LONG capture_serial)
{
#if SHADOW_ENGINE_DETAIL_WINDOWS
    if(!completion_diagnostic_report_snapshot(state,capture_serial,1))
        append_log("STAGE_DETAIL_COVERAGE state=%s captureSerial=%ld coverage=%s counters=notReported workRegister=H3 correction=producerClosure retained=H4",
            state,(long)capture_serial,
            detail_state() && detail_state()->epoch?"snapshotBusy":"notRecorded");
#else
    completion_diagnostic_write_manual(state,capture_serial,&g_shadow_engine.completion_diagnostics);
#endif
}
