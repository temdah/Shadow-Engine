/* H4: one fixed copied residency record between renderer and existing tools
 * worker. Publication never waits, allocates, formats or performs file I/O.
 * Formatting consumes only the worker's local scalar copy. */
#include "../internal/periodic_summary.h"

static int periodic_summary_publish(const PeriodicResidencySummary *snapshot)
{
    PeriodicSummaryState *s=&g_shadow_engine.periodic_summary;
    if(!snapshot) return 0;
    if(InterlockedCompareExchange(&s->slot_state,1,0)!=0) {
        InterlockedIncrement(&s->dropped_full);
        return 0;
    }
    memcpy(&s->residency,snapshot,sizeof(*snapshot));
    InterlockedIncrement(&s->accepted);
    InterlockedExchange(&s->slot_state,2);
    return 1;
}

/* Only the existing tools worker calls this function. Releasing the mailbox
 * before file I/O keeps an occupied/file-blocked worker from holding the
 * publisher's storage. A failed output is counted and not retried: it may
 * already have written part of the record. Pending data is best effort at
 * process exit; explicit reports retain their separate durability boundary. */
static void periodic_summary_drain(void)
{
    PeriodicSummaryState *s=&g_shadow_engine.periodic_summary;
    PeriodicResidencySummary snapshot;
    DWORD emitted;
    int ok,metadata_ok;
    if(InterlockedCompareExchange(&s->slot_state,3,2)!=2) return;
    memcpy(&snapshot,&s->residency,sizeof(snapshot));
    InterlockedExchange(&s->slot_state,0);
    InterlockedIncrement(&s->drained);
    emitted=GetTickCount();
    ok=append_log("STAGE_F_RESIDENCY rendererCall=%ld admissionCoverage=%s latestMayBeStale=1 queue=0x%llX entries=%u activeEntries=%u faces=%u extraMapsInUse=%u physicalMaps=%u overflow=%u B4=%ld A8=%ld latestCandidates=%ld peakCandidates=%ld latestAdmitted=%ld peakAdmitted=%ld latestDynamicOrSpecial=%ld latestCachedBindings=%ld peakCachedBindings=%ld latestOwnerRecords=%ld peakOwnerRecords=%ld admissionScanState=%ld ownerReserveState=%ld ownerCapacity=%ld ownerBaseChangesDuringManager=%ld",
        (long)snapshot.renderer_call,snapshot.detail_phase<0?"unknown":
            (snapshot.detail_epoch?"recordedWindowsOnly":"notRecorded"),
        (unsigned long long)snapshot.queue_identity,
        snapshot.entries,snapshot.active_entries,snapshot.faces,
        snapshot.faces>ORIGINAL_LOCAL_MAPS?snapshot.faces-ORIGINAL_LOCAL_MAPS:0U,
        TOTAL_LOCAL_MAPS,snapshot.faces>TOTAL_LOCAL_MAPS?snapshot.faces-TOTAL_LOCAL_MAPS:0U,
        (long)snapshot.b4,(long)snapshot.a8,
        (long)snapshot.latest_candidates,(long)snapshot.peak_candidates,
        (long)snapshot.latest_admitted,(long)snapshot.peak_admitted,
        (long)snapshot.latest_dynamic_or_special,(long)snapshot.latest_cached_bindings,
        (long)snapshot.peak_cached_bindings,(long)snapshot.latest_owner_records,
        (long)snapshot.peak_owner_records,(long)snapshot.admission_scan_state,
        (long)snapshot.owner_reserve_state,(long)snapshot.owner_capacity,
        (long)snapshot.owner_base_changes);
    if(!ok) InterlockedIncrement(&s->write_failures);
    metadata_ok=append_log("STAGE_PERIODIC_TRANSPORT workRegister=H4 kind=residency rendererCall=%ld capturedTick=%lu emittedTick=%lu deliveryDelayMs=%lu accepted=%ld drained=%ld fullDrops=%ld writeFailures=%ld recordWritten=%u pending=%ld capacity=1 fullPolicy=dropNew writeFailurePolicy=countNoRetry copiedScalarsOnly=1 normalOutputThread=worker globallyAtomicSnapshot=0 detailEpoch=%ld detailPhase=%ld admissionCoverage=%s admissionPeaks=cumulativeRecordedWindows latestMayBeStale=1",
        (long)snapshot.renderer_call,(unsigned long)snapshot.captured_tick,
        (unsigned long)emitted,(unsigned long)(emitted-snapshot.captured_tick),
        (long)InterlockedCompareExchange(&s->accepted,0,0),
        (long)InterlockedCompareExchange(&s->drained,0,0),
        (long)InterlockedCompareExchange(&s->dropped_full,0,0),
        (long)InterlockedCompareExchange(&s->write_failures,0,0),(unsigned)ok,
        (long)InterlockedCompareExchange(&s->slot_state,0,0),
        (long)snapshot.detail_epoch,(long)snapshot.detail_phase,
        snapshot.detail_phase<0?"unknown":
            (snapshot.detail_epoch?"recordedWindowsOnly":"notRecorded"));
    if(!metadata_ok && ok) InterlockedIncrement(&s->write_failures);
}
