#ifndef SHADOW_ENGINE_PERIODIC_SUMMARY_H
#define SHADOW_ENGINE_PERIODIC_SUMMARY_H

/* H4 copied-only residency transport. All fields are scalars; queue_identity
 * is a diagnostic number and must never be converted into a later read. */
typedef struct PeriodicResidencySummary {
    uint64_t queue_identity;
    DWORD captured_tick;
    uint32_t entries,active_entries,faces;
    LONG renderer_call,b4,a8;
    LONG latest_candidates,peak_candidates,latest_admitted,peak_admitted;
    LONG latest_dynamic_or_special,latest_cached_bindings,peak_cached_bindings;
    LONG latest_owner_records,peak_owner_records,admission_scan_state;
    LONG owner_reserve_state,owner_capacity,owner_base_changes;
    LONG detail_epoch,detail_phase;
} PeriodicResidencySummary;

typedef struct PeriodicSummaryState {
    /* One process-lifetime mailbox. Producers own 1, the worker owns 3.
     * No reset/reuse is permitted until its owner publishes the next state. */
    volatile LONG slot_state; /* 0 empty, 1 writing, 2 ready, 3 reading */
    PeriodicResidencySummary residency;
    volatile LONG accepted,dropped_full,drained,write_failures;
} PeriodicSummaryState;

static int periodic_summary_publish(const PeriodicResidencySummary *snapshot);
static void periodic_summary_drain(void);

#endif
