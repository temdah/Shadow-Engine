/* Actual unity H4 path with captured log I/O. No game, worker, file or hook
 * is started. A freed synthetic queue proves delayed output uses copies. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

static DWORD test_tick=60000U;
static unsigned allow_io,create_calls,write_calls,close_calls;
static unsigned reject_create,reject_write,short_write;
static char output[32768];
static size_t output_size;
static void (*during_create)(void);
#define TEST_FILE ((HANDLE)(uintptr_t)0x1234U)

static DWORD WINAPI test_get_tick(void) { return test_tick; }
static HANDLE WINAPI test_create_file(LPCWSTR path,DWORD access,DWORD share,
    LPSECURITY_ATTRIBUTES security,DWORD creation,DWORD flags,HANDLE template_file)
{
    void (*callback)(void)=during_create;
    (void)path; (void)access; (void)share; (void)security;
    (void)creation; (void)flags; (void)template_file;
    assert(allow_io); ++create_calls;
    during_create=NULL;
    if(callback) callback();
    return reject_create?INVALID_HANDLE_VALUE:TEST_FILE;
}
static BOOL WINAPI test_write_file(HANDLE file,LPCVOID data,DWORD bytes,
    LPDWORD written,LPOVERLAPPED overlapped)
{
    (void)overlapped;
    assert(allow_io && file==TEST_FILE); ++write_calls;
    if(write_calls==reject_write) { *written=0; return FALSE; }
    if(write_calls==short_write) { *written=bytes?bytes-1U:0; return TRUE; }
    assert(output_size+bytes<sizeof(output));
    memcpy(output+output_size,data,bytes); output_size+=bytes;
    output[output_size]=0; *written=bytes;
    return TRUE;
}
static BOOL WINAPI test_close_handle(HANDLE file)
{
    if(file!=TEST_FILE) return CloseHandle(file);
    assert(allow_io); ++close_calls; return TRUE;
}

#undef GetTickCount
#undef CreateFileW
#undef WriteFile
#undef CloseHandle
#define GetTickCount test_get_tick
#define CreateFileW test_create_file
#define WriteFile test_write_file
#define CloseHandle test_close_handle
#include "../src/shadow_engine_patch.c"
#undef GetTickCount
#undef CreateFileW
#undef WriteFile
#undef CloseHandle

static PeriodicResidencySummary next_record;

static void reset_fixture(void)
{
    memset(&g_shadow_engine,0,sizeof(g_shadow_engine));
    g_shadow_engine.bootstrap.session_log_state=2;
    lstrcpyW(g_shadow_engine.bootstrap.snapshot.bin_directory,L"fixture\\");
    test_tick=60000U; allow_io=0;
    create_calls=write_calls=close_calls=0;
    reject_create=reject_write=short_write=0;
    output_size=0; output[0]=0; during_create=NULL;
}

static PeriodicResidencySummary record(LONG serial)
{
    PeriodicResidencySummary snapshot={0};
    snapshot.queue_identity=UINT64_MAX; /* Deliberately never dereferenceable. */
    snapshot.captured_tick=test_tick;
    snapshot.renderer_call=serial;
    snapshot.entries=2; snapshot.active_entries=2; snapshot.faces=6;
    snapshot.b4=21; snapshot.a8=4;
    snapshot.latest_candidates=100; snapshot.peak_candidates=120;
    return snapshot;
}

static void test_renderer_copy_and_callback_boundary(void)
{
    unsigned char *queue;
    unsigned before;
    reset_fixture();
    queue=VirtualAlloc(NULL,RENDER_QUEUE_ALLOCATION_BYTES,
        MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    assert(queue);
    *(uint32_t *)(queue+RENDER_QUEUE_COUNT_OFFSET)=2;
    *(uint32_t *)(queue+0x20A0)=3;
    *(uint32_t *)(queue+RENDER_QUEUE_ENTRY_BYTES+0x20A0)=3;
    g_shadow_engine.renderer.renderer_calls=42;
    g_shadow_engine.renderer.observed_scheduler_b4=21;
    g_shadow_engine.renderer.observed_cache_a8=4;
    g_shadow_engine.renderer.latest_candidate_count=100;
    g_shadow_engine.renderer.peak_candidate_count=120;
    trace_renderer_capacity(queue);
    assert(!create_calls && g_shadow_engine.periodic_summary.accepted==1);
    assert(g_shadow_engine.periodic_summary.slot_state==2);
    assert(g_shadow_engine.periodic_summary.residency.queue_identity==(uint64_t)(uintptr_t)queue);
    assert(g_shadow_engine.periodic_summary.residency.faces==6);
    assert(g_shadow_engine.periodic_summary.residency.detail_epoch==0);
    assert(VirtualFree(queue,0,MEM_RELEASE));
    memset(&g_shadow_engine.renderer,0xA5,sizeof(g_shadow_engine.renderer));
    /* Tail periodic Y/Z were removed from this Internal callback. */
    consume_external_slice_results_at_frame_graph_tail(NULL,NULL);
    assert(!create_calls);
    allow_io=1; test_tick+=250U; periodic_summary_drain();
    assert(strstr(output,"STAGE_F_RESIDENCY rendererCall=42"));
    assert(strstr(output,"entries=2 activeEntries=2 faces=6"));
    assert(strstr(output,"B4=21 A8=4 latestCandidates=100 peakCandidates=120"));
    assert(strstr(output,"capturedTick=60000 emittedTick=60250 deliveryDelayMs=250"));
    assert(strstr(output,"admissionCoverage=notRecorded"));
    assert(g_shadow_engine.periodic_summary.drained==1);
    assert(!g_shadow_engine.periodic_summary.slot_state && create_calls==2);
    before=create_calls; periodic_summary_drain(); assert(create_calls==before);
}

static void test_occupied_mailbox_preserves_pending_record(void)
{
    PeriodicResidencySummary snapshot,original;
    unsigned i;
    reset_fixture(); snapshot=record(7); original=snapshot;
    assert(periodic_summary_publish(&snapshot));
    memset(&snapshot,0xCC,sizeof(snapshot));
    for(i=0;i<8;++i) assert(!periodic_summary_publish(&snapshot));
    assert(!create_calls && g_shadow_engine.periodic_summary.dropped_full==8);
    assert(!memcmp(&g_shadow_engine.periodic_summary.residency,&original,sizeof(original)));
    allow_io=1; periodic_summary_drain();
    assert(strstr(output,"rendererCall=7"));
    assert(strstr(output,"fullDrops=8"));
    allow_io=0;
    for(i=1;i<=3;i+=2) {
        g_shadow_engine.periodic_summary.slot_state=(LONG)i;
        assert(!periodic_summary_publish(&snapshot));
        periodic_summary_drain();
        assert(g_shadow_engine.periodic_summary.slot_state==(LONG)i);
        assert(!memcmp(&g_shadow_engine.periodic_summary.residency,&original,sizeof(original)));
    }
    assert(g_shadow_engine.periodic_summary.dropped_full==10);
    g_shadow_engine.periodic_summary.slot_state=0;
    assert(!periodic_summary_publish(NULL));
}

static void publish_while_worker_is_writing(void)
{
    assert(!g_shadow_engine.periodic_summary.slot_state);
    assert(periodic_summary_publish(&next_record));
}

static void test_release_before_io_and_recorded_coverage(void)
{
    PeriodicResidencySummary first;
    reset_fixture(); first=record(7); next_record=record(8);
    next_record.detail_epoch=3; next_record.detail_phase=3;
    assert(periodic_summary_publish(&first));
    during_create=publish_while_worker_is_writing;
    allow_io=1; periodic_summary_drain();
    assert(strstr(output,"STAGE_F_RESIDENCY rendererCall=7"));
    assert(!strstr(output,"STAGE_F_RESIDENCY rendererCall=8"));
    assert(g_shadow_engine.periodic_summary.slot_state==2);
    assert(g_shadow_engine.periodic_summary.accepted==2);
    periodic_summary_drain();
    assert(strstr(output,"STAGE_F_RESIDENCY rendererCall=8"));
    assert(strstr(output,"detailEpoch=3 detailPhase=3 admissionCoverage=recordedWindowsOnly"));
    assert(strstr(output,"admissionPeaks=cumulativeRecordedWindows latestMayBeStale=1"));
    assert(!g_shadow_engine.periodic_summary.slot_state);
    assert(g_shadow_engine.periodic_summary.drained==2);
    next_record.renderer_call=9; next_record.detail_phase=-1;
    assert(periodic_summary_publish(&next_record)); periodic_summary_drain();
    assert(strstr(output,"detailEpoch=3 detailPhase=-1 admissionCoverage=unknown"));
}

static void test_checked_write_failures_and_no_retry(void)
{
    unsigned mode;
    for(mode=0;mode<4;++mode) {
        PeriodicResidencySummary snapshot;
        unsigned calls;
        reset_fixture(); snapshot=record(9);
        assert(periodic_summary_publish(&snapshot));
        if(mode==0) reject_create=1;
        else if(mode==1) reject_write=1;
        else if(mode==2) short_write=1;
        else reject_write=2; /* Metadata failure still counts the packet. */
        allow_io=1; periodic_summary_drain();
        assert(g_shadow_engine.periodic_summary.write_failures==1);
        assert(g_shadow_engine.bootstrap.log_write_failures==(mode==0?2:1));
        assert(g_shadow_engine.periodic_summary.drained==1);
        assert(!g_shadow_engine.periodic_summary.slot_state);
        calls=create_calls; periodic_summary_drain(); assert(create_calls==calls);
        reject_create=reject_write=short_write=0;
        snapshot.renderer_call=10;
        assert(periodic_summary_publish(&snapshot)); periodic_summary_drain();
        assert(g_shadow_engine.periodic_summary.write_failures==1);
        assert(strstr(output,"STAGE_F_RESIDENCY rendererCall=10"));
        assert(strstr(output,"writeFailures=1 recordWritten=1"));
        assert(g_shadow_engine.periodic_summary.drained==2);
    }
}

static void test_first_use_stays_synchronous(void)
{
    unsigned char *queue;
    uint32_t i;
    reset_fixture(); allow_io=1;
    queue=VirtualAlloc(NULL,RENDER_QUEUE_ALLOCATION_BYTES,
        MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    assert(queue);
    *(uint32_t *)(queue+RENDER_QUEUE_COUNT_OFFSET)=3;
    for(i=0;i<3;++i) *(uint32_t *)(queue+(size_t)i*RENDER_QUEUE_ENTRY_BYTES+0x20A0)=6;
    g_shadow_engine.renderer.last_residency_log_tick=test_tick;
    trace_renderer_capacity(queue);
    assert(strstr(output,"STAGE_F_EXTRA_CAPACITY_FIRST_USE"));
    assert(create_calls==1 && !g_shadow_engine.periodic_summary.accepted);
    assert(VirtualFree(queue,0,MEM_RELEASE));
}

int main(void)
{
    test_renderer_copy_and_callback_boundary();
    test_occupied_mailbox_preserves_pending_record();
    test_release_before_io_and_recorded_coverage();
    test_checked_write_failures_and_no_retry();
    test_first_use_stays_synchronous();
    printf("PASS H4 actual-source transport: freed queue and poisoned source, no ordinary callback I/O, full/writing/reading exclusion, copied detail coverage, release before I/O, create/short/write/metadata failure, no retry and retained first-use output\n");
    return 0;
}
