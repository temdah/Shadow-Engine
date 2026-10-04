/* Actual-source H3 control/history fixture. Synthetic records only; never
 * installs hooks, starts the tool worker, launches ETW or touches game files. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdlib.h>
#include <assert.h>
static DWORD fixture_tick=1000;
static int reject_allocation,replace_on_lock,reject_tls_allocation,reject_tls_read,reject_tls_write;
static unsigned tls_reads,tls_writes;
static int foreign_metadata_lock,interleave_on_lock,rearm_on_publish;
static unsigned checks,native_calls;
static void fixture_replace_epoch(void);
static void fixture_log_guard(void);
static void fixture_tls_guard(void);
static void fixture_interleave_new_producer(void);
static void fixture_rearm_before_publish(void);
static DWORD WINAPI fixture_get_tick(void) { return fixture_tick; }
static DWORD WINAPI fixture_tls_alloc(void)
{
    if(reject_tls_allocation) { reject_tls_allocation=0; return TLS_OUT_OF_INDEXES; }
    return TlsAlloc();
}
static void *WINAPI fixture_tls_get(DWORD index)
{
    fixture_tls_guard(); ++tls_reads;
    if(reject_tls_read) { reject_tls_read=0; SetLastError(ERROR_INVALID_PARAMETER); return NULL; }
    return TlsGetValue(index);
}
static BOOL WINAPI fixture_tls_set(DWORD index,void *value)
{
    fixture_tls_guard(); ++tls_writes;
    if(rearm_on_publish && value) {
        rearm_on_publish=0; fixture_rearm_before_publish();
    }
    if(reject_tls_write && --reject_tls_write==0) {
        SetLastError(ERROR_INVALID_PARAMETER); return FALSE;
    }
    return TlsSetValue(index,value);
}
static HMODULE WINAPI fixture_no_library(LPCWSTR path) { (void)path; return NULL; }
static void *fixture_calloc(size_t count,size_t bytes)
{
    if(reject_allocation) { reject_allocation=0; return NULL; }
    return calloc(count,bytes);
}
static LONG WINAPI fixture_compare_exchange(volatile LONG *value,LONG exchange,LONG comparand)
{
    if(replace_on_lock && exchange==1 && comparand==0) {
        replace_on_lock=0; fixture_replace_epoch();
    }
    if(interleave_on_lock && exchange==1 && comparand==0) {
        interleave_on_lock=0; fixture_interleave_new_producer();
    }
    return InterlockedCompareExchange(value,exchange,comparand);
}
static HANDLE WINAPI fixture_create_file(LPCWSTR path,DWORD access,DWORD share,
    LPSECURITY_ATTRIBUTES security,DWORD creation,DWORD flags,HANDLE template_file)
{
    fixture_log_guard();
    return CreateFileW(path,access,share,security,creation,flags,template_file);
}
#define GetTickCount fixture_get_tick
#define LoadLibraryW fixture_no_library
#define calloc fixture_calloc
#define CreateFileW fixture_create_file
#define TlsAlloc fixture_tls_alloc
#define TlsGetValue fixture_tls_get
#define TlsSetValue fixture_tls_set
#undef InterlockedCompareExchange
#define InterlockedCompareExchange fixture_compare_exchange
#include "../src/shadow_engine_patch.c"
#undef InterlockedCompareExchange
#undef calloc
#undef GetTickCount
#undef LoadLibraryW
#undef CreateFileW
#undef TlsAlloc
#undef TlsGetValue
#undef TlsSetValue
#define CHECK(value) do { ++checks; assert(value); } while(0)
static unsigned char records[2][0x200];
static DetailProducerTicket interleaved_ticket;
static int complete_synchronously;
static void fixture_log_guard(void)
{ CHECK(!detail_state() || !detail_state()->lock); }
static void fixture_tls_guard(void)
{ CHECK(!detail_state() || !detail_state()->lock || foreign_metadata_lock); }
static void fixture_replace_epoch(void)
{
    DetailWindowState *s=detail_state();
    CHECK(s && !s->lock);
    ++s->epoch; ++s->change;
    s->phase=DETAIL_WINDOW_OPEN;
    memset(g_shadow_engine.completion_diagnostics.slots,0,
        sizeof(g_shadow_engine.completion_diagnostics.slots));
}
static void __fastcall fixture_scheduler(void *renderer,void *queue,
    uint32_t index,uint32_t face,void *results)
{
    (void)renderer; (void)queue; (void)face;
    CHECK(!detail_state() || !detail_state()->lock || foreign_metadata_lock);
    ++native_calls;
    if(complete_synchronously)
        completion_diagnostic_record_completion(results,index,1,1,0);
}
static void reset_context(void)
{
    if(detail_state()) TlsFree(detail_state()->producer_tls);
    free(g_shadow_engine.detail_window);
    free(g_shadow_engine.workload);
    free(g_shadow_engine.overhead);
    memset(&g_shadow_engine,0,sizeof(g_shadow_engine));
    fixture_tick=1000; complete_synchronously=0; replace_on_lock=0;
    reject_tls_allocation=reject_tls_read=reject_tls_write=0;
    foreign_metadata_lock=0;
    interleave_on_lock=rearm_on_publish=0;
    g_shadow_engine.hooks.original_shadow_face_scheduler=fixture_scheduler;
    QueryPerformanceFrequency(&g_shadow_engine.completion_diagnostics.qpc_frequency);
}
static void arm(void)
{
    detail_window_toggle();
    CHECK(detail_window_status().phase==DETAIL_WINDOW_OPEN);
    CHECK(detail_window_epoch()>0);
}
static DetailProducerTicket producer(unsigned which)
{
    LifecycleRecordSnapshot *life=find_lifecycle_record(records[which],1);
    CHECK(life!=NULL); life->generation=1; life->producer_cycle=1;
    return completion_diagnostic_begin_producer_batch(records[which]);
}
static int refused_producer(unsigned which)
{
    DetailProducerTicket ticket=producer(which);
    int refused=ticket.serial==0;
    completion_diagnostic_seal_producer_batch(records[which],&ticket);
    return refused;
}
static void schedule(unsigned which)
{
    hooked_shadow_face_scheduler(NULL,NULL,NATIVE_SLICE_RESULT_COUNT,0,records[which]+0x70U);
}
static void mark_both(unsigned which)
{
    completion_diagnostic_mark_pre_finalizer(records[which]);
    completion_diagnostic_mark_reset(records[which]);
}
static void fixture_interleave_new_producer(void)
{
    /* A second synthetic thread starts after old control closure/TLS restore,
     * then yields with its own numeric TLS value saved in the scalar ticket. */
    interleaved_ticket=producer(0);
    CHECK(interleaved_ticket.serial>0);
    CHECK(TlsSetValue(detail_state()->producer_tls,NULL));
}
static void fixture_rearm_before_publish(void)
{
    detail_window_finish();
    fixture_tick+=DETAIL_WINDOW_DRAIN_MS; detail_window_poll();
    CHECK(detail_state()->phase==DETAIL_WINDOW_FROZEN);
    arm();
}
static void test_producer_closure_recovery(void)
{
    DetailProducerTicket first,next;
    CompletionGenerationRecord *old_entry,*new_entry;
    LONG old_serial;
    unsigned control_index;
    reset_context(); arm(); first=producer(0); old_serial=first.serial;
    control_index=first.control_index;
    CHECK(old_serial>0); schedule(0);
    old_entry=current_completion_generation(records[0]);
    detail_state()->lock=1; foreign_metadata_lock=1;
    completion_diagnostic_seal_producer_batch(records[0],&first);
    CHECK(!first.serial && !first.bound);
    CHECK(!detail_state()->producer_control[control_index]);
    detail_state()->lock=0; foreign_metadata_lock=0;
    CHECK(!old_entry->producer_sealed && old_entry->scheduled_calls==1);
    CHECK(detail_state()->missing_seals==1);
    next=producer(0); CHECK(next.serial>old_serial); schedule(0);
    new_entry=current_completion_generation(records[0]);
    CHECK(new_entry!=old_entry && new_entry->scheduled_calls==1 && old_entry->scheduled_calls==1);
    CHECK(!g_shadow_engine.completion_diagnostics.scheduler_calls_outside_producer);
    printf("PASS .74 missed-seal correction: firstSerial=%ld nextAdmission=%ld oldScheduledCalls=%ld newScheduledCalls=%ld missingSealMetadata=%ld\n",
        (long)old_serial,(long)next.serial,(long)old_entry->scheduled_calls,
        (long)new_entry->scheduled_calls,(long)detail_state()->missing_seals);
    completion_diagnostic_seal_producer_batch(records[0],&next);
    completion_diagnostic_seal_producer_batch(records[0],&next);
    CHECK(!detail_state()->control_close_failures);
}
static void test_nested_producer_identity(void)
{
    DetailProducerTicket outer,refused,third,other;
    CompletionGenerationRecord *outer_entry,*other_entry;
    reset_context(); arm(); outer=producer(0); schedule(0);
    outer_entry=current_completion_generation(records[0]);
    detail_state()->lock=1; foreign_metadata_lock=1;
    refused=producer(0); CHECK(!refused.serial && refused.bound);
    detail_state()->lock=0; foreign_metadata_lock=0;
    schedule(0); CHECK(outer_entry->scheduled_calls==1);
    third=producer(0); CHECK(!third.serial); schedule(0);
    CHECK(outer_entry->scheduled_calls==1);
    completion_diagnostic_seal_producer_batch(records[0],&third);
    schedule(0); CHECK(outer_entry->scheduled_calls==1); /* Refused parent still shadows outer. */
    completion_diagnostic_seal_producer_batch(records[0],&refused);
    schedule(0); CHECK(outer_entry->scheduled_calls==2);
    other=producer(1); CHECK(other.serial>0); schedule(1);
    other_entry=current_completion_generation(records[1]);
    CHECK(other_entry->scheduled_calls==1);
    schedule(0); CHECK(outer_entry->scheduled_calls==2); /* Wrong record cannot borrow TLS identity. */
    completion_diagnostic_seal_producer_batch(records[1],&other);
    schedule(0); CHECK(outer_entry->scheduled_calls==3);
    refused=producer(0); CHECK(!refused.serial); schedule(0);
    CHECK(outer_entry->scheduled_calls==3);
    completion_diagnostic_seal_producer_batch(records[0],&refused);
    completion_diagnostic_seal_producer_batch(records[0],&outer);
    CHECK(g_shadow_engine.completion_diagnostics.scheduler_calls_outside_producer==5);
    CHECK(!detail_state()->control_close_failures);
    printf("PASS refused and nested producers: contended begin, true overlap, third entrant, restored ancestor, distinct-record admission and record/serial mismatch exclusion.\n");
}
static void test_producer_window_boundaries(void)
{
    DetailProducerTicket old,next,stale;
    CompletionGenerationRecord *e;
    unsigned reads=tls_reads,writes=tls_writes,index;
    reset_context(); old=producer(0);
    CHECK(!old.bound && !old.serial && tls_reads==reads && tls_writes==writes);
    arm(); schedule(0); CHECK(!current_completion_generation(records[0]));
    next=producer(0); CHECK(next.serial>0); schedule(0);
    e=current_completion_generation(records[0]);
    completion_diagnostic_seal_producer_batch(records[0],&next);
    schedule(0); CHECK(e->scheduled_calls==1);
    completion_diagnostic_seal_producer_batch(records[0],&old);
    CHECK(!TlsGetValue(detail_state()->producer_tls));

    reset_context(); arm(); old=producer(0); index=old.control_index; schedule(0);
    detail_window_finish(); fixture_tick+=DETAIL_WINDOW_DRAIN_MS; detail_window_poll();
    CHECK(detail_state()->phase==DETAIL_WINDOW_FROZEN); arm();
    CHECK(detail_state()->producer_control[index]==old.serial);
    CHECK(refused_producer(0)); schedule(0);
    CHECK(!current_completion_generation(records[0]));
    stale=old; stale.bound=0; /* Saved numeric close identity, never a borrowed pointer. */
    completion_diagnostic_seal_producer_batch(records[0],&old);
    CHECK(!detail_state()->producer_control[index]);
    next=producer(0); CHECK(next.serial>stale.serial); schedule(0);
    completion_diagnostic_seal_producer_batch(records[0],&stale);
    CHECK(detail_state()->producer_control[next.control_index]==next.serial);
    CHECK(detail_state()->control_close_failures==1); /* Injected duplicate cannot clear new control. */
    e=current_completion_generation(records[0]); CHECK(!e->producer_sealed);
    completion_diagnostic_seal_producer_batch(records[0],&next);

    reset_context(); arm(); rearm_on_publish=1; old=producer(0);
    CHECK(old.serial>0 && old.epoch!=detail_state()->epoch); schedule(0);
    CHECK(!current_completion_generation(records[0]));
    completion_diagnostic_seal_producer_batch(records[0],&old);
    CHECK(refused_producer(1)==0); /* Helper also closes this valid recovery scope. */
    printf("PASS producer window boundaries: first-arm NULL default, live scope across freeze/rearm, non-reset controls, stale closure and delayed TLS publication.\n");
}
static void test_optional_seal_interleaving(void)
{
    DetailProducerTicket old;
    LONG old_serial;
    CompletionGenerationRecord *old_entry,*new_entry;
    reset_context(); arm(); old=producer(0); old_serial=old.serial; schedule(0);
    old_entry=current_completion_generation(records[0]); interleave_on_lock=1;
    completion_diagnostic_seal_producer_batch(records[0],&old);
    new_entry=current_completion_generation(records[0]);
    CHECK(old_entry->producer_sealed && new_entry->serial>old_serial);
    CHECK(detail_state()->producer_control[interleaved_ticket.control_index]==interleaved_ticket.serial);
    CHECK(!g_shadow_engine.completion_diagnostics.producer_active_clear_failures);
    CHECK(TlsSetValue(detail_state()->producer_tls,(void *)(uintptr_t)interleaved_ticket.serial));
    schedule(0); CHECK(new_entry->scheduled_calls==1);
    completion_diagnostic_seal_producer_batch(records[0],&interleaved_ticket);
    CHECK(!detail_state()->control_close_failures);
    printf("PASS separate closure/observation: newer producer can publish before old seal metadata without being cleared or reported as a closure failure.\n");
}
static void test_tls_faults_and_error_preservation(void)
{
    DetailProducerTicket ticket,nested;
    CompletionGenerationRecord *e;
    unsigned mode,index;
    uintptr_t value=99;
    reset_context(); reject_tls_allocation=1;
    detail_window_toggle(); CHECK(!detail_state()); arm();
    SetLastError(0x1234U);
    CHECK(detail_tls_read(detail_state(),&value) && !value && GetLastError()==0x1234U);
    ticket=producer(0); CHECK(ticket.serial>0 && GetLastError()==0x1234U);
    schedule(0); CHECK(GetLastError()==0x1234U);
    completion_diagnostic_seal_producer_batch(records[0],&ticket);
    CHECK(GetLastError()==0x1234U);
    for(mode=0;mode<4U;++mode) {
        reset_context(); arm(); ticket=producer(0); schedule(0);
        e=current_completion_generation(records[0]); index=ticket.control_index;
        SetLastError(0x5678U);
        if(mode==0) { reject_tls_read=1; nested=producer(0); }
        else if(mode==1) { reject_tls_write=1; nested=producer(0); }
        else if(mode==2) { reject_tls_write=2; nested=producer(1); }
        else { reject_tls_write=1; completion_diagnostic_seal_producer_batch(records[0],&ticket); memset(&nested,0,sizeof(nested)); }
        CHECK(GetLastError()==0x5678U);
        CHECK(detail_state()->tls_failure==(LONG)mode+1);
        schedule(0); CHECK(e->scheduled_calls==1 && GetLastError()==0x5678U);
        completion_diagnostic_seal_producer_batch(records[mode==2 ? 1:0],&nested);
        completion_diagnostic_seal_producer_batch(records[0],&ticket);
        CHECK(!detail_state()->producer_control[index]);
        CHECK(!detail_window_epoch() && !detail_history_enter());
        detail_window_poll(); fixture_tick+=DETAIL_WINDOW_DRAIN_MS; detail_window_poll();
        CHECK(detail_state()->phase==DETAIL_WINDOW_FROZEN);
        detail_window_toggle(); CHECK(detail_state()->phase==DETAIL_WINDOW_FROZEN);
        CHECK(!detail_state()->control_close_failures);
    }
    printf("PASS TLS faults: allocation failure, valid NULL, preserved last-error, read/NULL-bind/publication/restore failure, unconditional control closure and worker incomplete freeze.\n");
}
static void test_idle_and_controls(void)
{
    LONG epoch;
    reset_context();
    CHECK(detail_window_status().phase==DETAIL_WINDOW_OFF);
    CHECK(refused_producer(0)); schedule(0);
    CHECK(!g_shadow_engine.completion_diagnostics.scheduled_external);
    reject_allocation=1; detail_window_toggle(); CHECK(!detail_state());
    arm(); epoch=detail_window_epoch();
    CHECK(detail_window_enter(epoch,DETAIL_WINDOW_ADMISSION));
    detail_window_mark(); CHECK(detail_state()->requests==2);
    CHECK(!detail_state()->marker_seen && detail_window_status().phase==-1);
    detail_window_leave(); detail_window_poll();
    CHECK(detail_state()->marker_seen && !detail_state()->requests);
    fixture_tick+=INTERSECTION_POST_MS-1U; detail_window_poll();
    CHECK(detail_state()->phase==DETAIL_WINDOW_OPEN);
    CHECK(detail_window_enter(epoch,DETAIL_WINDOW_TAIL_BEFORE));
    detail_window_finish(); CHECK(detail_state()->requests==4);
    CHECK(detail_state()->phase==DETAIL_WINDOW_OPEN);
    detail_window_leave(); detail_window_poll();
    CHECK(detail_state()->phase==DETAIL_WINDOW_FROZEN);
    CHECK(detail_state()->report_failed && !detail_state()->report_pending);
    {
        LONG failures=g_shadow_engine.bootstrap.log_write_failures;
        detail_window_poll(); detail_window_poll();
        CHECK(g_shadow_engine.bootstrap.log_write_failures==failures);
        detail_state()->report_lock=1;
        detail_window_finish(); CHECK(detail_state()->report_pending);
        detail_state()->report_lock=0; detail_window_poll();
        CHECK(!detail_state()->report_pending && detail_state()->report_failed);
    }
    arm(); CHECK(detail_state()->discarded_failed_reports==1);
    CHECK(!detail_window_enter(epoch,DETAIL_WINDOW_TAIL_AFTER));
    epoch=detail_window_epoch();
    CHECK(detail_window_enter(epoch,DETAIL_WINDOW_ADMISSION));
    detail_window_toggle(); CHECK(detail_state()->requests==1);
    CHECK(detail_state()->phase==DETAIL_WINDOW_OPEN);
    detail_window_leave(); detail_window_poll();
    CHECK(detail_state()->phase==DETAIL_WINDOW_FROZEN && !detail_state()->requests);
}
static void test_draining_and_native_passthrough(void)
{
    DetailProducerTicket serial;
    LONG epoch;
    CompletionGenerationRecord *e;
    reset_context(); arm(); epoch=detail_window_epoch();
    serial=producer(0); CHECK(serial.serial>0); schedule(0);
    e=current_completion_generation(records[0]); CHECK(e && e->scheduled_external_mask==1);
    detail_window_finish(); CHECK(detail_state()->phase==DETAIL_WINDOW_DRAINING);
    CHECK(!detail_window_epoch());
    CHECK(detail_window_enter(epoch,DETAIL_WINDOW_TAIL_AFTER)); detail_window_leave();
    schedule(0); CHECK(e->scheduled_calls==2);
    completion_diagnostic_record_completion(records[0]+0x70U,NATIVE_SLICE_RESULT_COUNT,1,1,0);
    CHECK(e->completed_external_mask==1);
    completion_diagnostic_seal_producer_batch(records[0],&serial);
    mark_both(0); detail_window_poll();
    CHECK(detail_state()->phase==DETAIL_WINDOW_FROZEN && !detail_state()->timed_out);
    CHECK(!detail_window_enter(epoch,DETAIL_WINDOW_TAIL_AFTER));
    {
        CompletionDiagnosticState *before=malloc(sizeof(*before));
        unsigned native_before=native_calls;
        CHECK(before!=NULL); memcpy(before,&g_shadow_engine.completion_diagnostics,sizeof(*before));
        schedule(0);
        completion_diagnostic_record_completion(records[0]+0x70U,NATIVE_SLICE_RESULT_COUNT,1,1,0);
        completion_diagnostic_seal_producer_batch(records[0],&serial); mark_both(0);
        CHECK(native_calls==native_before+1U);
        CHECK(!memcmp(before,&g_shadow_engine.completion_diagnostics,sizeof(*before)));
        free(before);
    }
    arm(); CHECK(detail_window_epoch()!=epoch);
    completion_diagnostic_seal_producer_batch(records[0],&serial);
    CHECK(!current_completion_generation(records[0]));
    serial=producer(0); complete_synchronously=1; schedule(0);
    CHECK(current_completion_generation(records[0])->completed_external_mask==1);
    completion_diagnostic_seal_producer_batch(records[0],&serial); mark_both(0);
    detail_window_finish(); CHECK(detail_state()->phase==DETAIL_WINDOW_FROZEN);
}
static void test_boundary_timeout_and_reuse(void)
{
    DetailProducerTicket serial;
    CompletionGenerationRecord *e;
    unsigned i;
    reset_context(); arm(); serial=producer(0); schedule(0);
    completion_diagnostic_seal_producer_batch(records[0],&serial);
    detail_window_finish(); CHECK(detail_state()->phase==DETAIL_WINDOW_DRAINING);
    CHECK(refused_producer(0));
    e=current_completion_generation(records[0]);
    completion_diagnostic_record_completion(records[0]+0x70U,NATIVE_SLICE_RESULT_COUNT,1,1,0);
    CHECK(detail_state()->contaminated_completions==1 && !e->completed_external_mask);
    mark_both(0); fixture_tick+=DETAIL_WINDOW_DRAIN_MS; detail_window_poll();
    CHECK(detail_state()->phase==DETAIL_WINDOW_FROZEN && detail_state()->timed_out);
    arm();
    for(i=0;i<COMPLETION_DIAGNOSTIC_HISTORY+1U;++i) {
        serial=producer(0); CHECK(serial.serial>0);
        completion_diagnostic_seal_producer_batch(records[0],&serial);
    }
    CHECK(detail_state()->ring_reuse_incomplete>0);
    fixture_tick+=INTERSECTION_TIMEOUT_MS;
    CHECK(refused_producer(1) && detail_state()->phase==DETAIL_WINDOW_DRAINING);
    fixture_tick+=DETAIL_WINDOW_DRAIN_MS; detail_window_poll();
    CHECK(detail_state()->phase==DETAIL_WINDOW_FROZEN && detail_state()->timed_out);
}
static void test_epoch_recheck_and_copied_report(void)
{
    DetailReportMetadata metadata;
    CompletionDiagnosticState *copy;
    DetailProducerTicket serial;
    LONG old_epoch;
    reset_context(); arm(); old_epoch=detail_window_epoch();
    replace_on_lock=1;
    CHECK(!detail_history_enter());
    CHECK(!detail_state()->lock && detail_state()->epoch!=old_epoch);
    CHECK(detail_state()->stale_entries==1);
    serial=producer(0); CHECK(serial.serial>0); schedule(0);
    copy=detail_report_copy(&metadata); CHECK(copy!=NULL && !detail_state()->lock);
    CHECK(copy->scheduled_external==1 && metadata.epoch==detail_state()->epoch);
    CHECK(!detail_report_copy(&metadata)); /* One report buffer owner. */
    CHECK(detail_history_enter());
    g_shadow_engine.completion_diagnostics.scheduled_external=99;
    detail_window_leave();
    CHECK(copy->scheduled_external==1); /* Output cannot follow live metadata. */
    InterlockedExchange(&detail_state()->report_lock,0);
    detail_state()->lock=1; foreign_metadata_lock=1;
    CHECK(refused_producer(1));
    detail_state()->lock=0; foreign_metadata_lock=0; CHECK(detail_state()->contention>0);
    g_shadow_engine.completion_diagnostics.serial=0x7FFFFFFFL;
    CHECK(refused_producer(1) && detail_state()->serial_exhausted==1);
    detail_state()->epoch=0x7FFFFFFFL; detail_state()->phase=DETAIL_WINDOW_FROZEN;
    detail_state()->report_pending=0; detail_state()->report_failed=0;
    detail_window_toggle();
    CHECK(detail_state()->epoch==0x7FFFFFFFL && detail_state()->phase==DETAIL_WINDOW_FROZEN);
    completion_diagnostic_seal_producer_batch(records[0],&serial);
}
static int contains_log(const wchar_t *path,const char *needle)
{
    char text[32768]; DWORD bytes=0;
    HANDLE file=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,
        NULL,OPEN_EXISTING,0,NULL);
    CHECK(file!=INVALID_HANDLE_VALUE);
    CHECK(ReadFile(file,text,sizeof(text)-1U,&bytes,NULL));
    CloseHandle(file); text[bytes]=0;
    return strstr(text,needle)!=NULL;
}
static void test_action_independence(void)
{
    PopulationCaptureState *population;
    LONG capture_requests,detail_epoch;
    LARGE_INTEGER population_timer;
    reset_context();
    CHECK(!g_shadow_engine.intersection.tls_valid && !g_shadow_engine.intersection.state);
    CHECK(internal_tool_dispatch(7,0U)==1);
    CHECK(detail_window_status().phase==DETAIL_WINDOW_OPEN && !g_shadow_engine.intersection.state);
    CHECK(!population_state());
    CHECK(internal_tool_dispatch(8,0U)==1); CHECK(detail_state()->marker_seen);
    CHECK(!population_state());
    CHECK(internal_tool_dispatch(9,0U)==1);
    CHECK(detail_window_status().phase==DETAIL_WINDOW_FROZEN && !g_shadow_engine.intersection.state);
    CHECK(!population_state());

    /* Legacy controls also leave an existing population recording untouched. */
    population=(PopulationCaptureState *)calloc(1,sizeof(*population)); CHECK(population);
    population->serial=9U; population->phase=1U; population->active=1;
    population->notice=1U; population->end_tick=fixture_tick+60000U;
    population->started_tick=fixture_tick;
    CHECK(QueryPerformanceFrequency(&population_timer)); population->frequency=population_timer.QuadPart;
    CHECK(QueryPerformanceCounter(&population_timer)); population->started=population_timer.QuadPart;
    g_shadow_engine.population=population;
    CHECK(internal_tool_dispatch(7,0U)==1);
    CHECK(detail_window_status().phase==DETAIL_WINDOW_OPEN);
    CHECK(population->phase==1U && population->active && !population->marked && population->notice==1U);
    CHECK(internal_tool_dispatch(8,0U)==1); CHECK(detail_state()->marker_seen);
    CHECK(population->phase==1U && population->active && !population->marked && population->notice==1U);
    CHECK(internal_tool_dispatch(9,0U)==1);
    CHECK(detail_window_status().phase==DETAIL_WINDOW_FROZEN);
    CHECK(population->phase==1U && population->active && !population->marked && population->notice==1U);

    /* Dedicated controls cannot toggle H3 or request a black-world capture. */
    capture_requests=g_shadow_engine.internal_tools.capture_requests;
    detail_epoch=detail_state()->epoch;
    CHECK(internal_tool_dispatch(14,0U)==-1);
    CHECK(population->phase==1U && population->active && !population->marked);
    CHECK(internal_tool_dispatch(15,0U)==1);
    CHECK(population->phase==1U && population->active && population->marked && population->notice==2U);
    CHECK(population->end_tick==fixture_tick+60000U);
    CHECK(internal_tool_dispatch(15,0U)==-1);
    CHECK(internal_tool_dispatch(16,0U)==1);
    CHECK(population->phase==2U && !population->active && population->notice==3U);
    CHECK(internal_tool_dispatch(14,0U)==-1 && internal_tool_dispatch(16,0U)==-1);
    CHECK(population->serial==9U && population->marked);
    CHECK(detail_window_status().phase==DETAIL_WINDOW_FROZEN && detail_state()->epoch==detail_epoch);
    CHECK(g_shadow_engine.internal_tools.capture_requests==capture_requests);
    CHECK(!g_shadow_engine.intersection.tls_valid && !g_shadow_engine.intersection.state);
    CHECK(internal_tool_dispatch(11,0U)==1);
    CHECK(g_shadow_engine.workload && g_shadow_engine.workload->phase==1);
    CHECK(g_shadow_engine.workload->present.error==ERROR_MOD_NOT_FOUND);
    CHECK(detail_window_status().phase==DETAIL_WINDOW_FROZEN);
    CHECK(population->phase==2U && population->serial==9U && population->marked);
    g_shadow_engine.population=NULL; free(population);
    /* Missing library mock forbids any ETW session; H1 still arms independently. */
    overhead_close();
}
static void test_durable_report(void)
{
    char source[MAX_PATH],*tail;
    wchar_t directory[MAX_PATH],current[MAX_PATH];
    HANDLE locked;
    reset_context();
    CHECK(GetFullPathNameA(__FILE__,MAX_PATH,source,NULL)!=0);
    tail=strrchr(source,'\\'); CHECK(tail!=NULL); *tail=0;
    _snwprintf(directory,MAX_PATH,L"%hs\\detail-log-fixture-%lu",source,
        (unsigned long)GetCurrentProcessId());
    CHECK(CreateDirectoryW(directory,NULL)); /* Never reuse someone else's path. */
    lstrcpyW(g_shadow_engine.bootstrap.snapshot.bin_directory,directory);
    lstrcatW(g_shadow_engine.bootstrap.snapshot.bin_directory,L"\\");
    lstrcpyW(current,g_shadow_engine.bootstrap.snapshot.bin_directory);
    lstrcatW(current,L"ShadowEnginePatch.log");
    CHECK(begin_session_log());
    arm(); detail_window_finish();
    CHECK(detail_state()->phase==DETAIL_WINDOW_FROZEN);
    CHECK(!detail_state()->report_pending && !detail_state()->report_failed);
    CHECK(contains_log(current,"counterScope=recordedWindowsOnly boundaryAttribution=unknown"));
    CHECK(contains_log(current,"STAGE_DETAIL_REPORT epoch=1 saved=1"));
    arm();
    locked=CreateFileW(current,GENERIC_READ,0,NULL,OPEN_EXISTING,0,NULL);
    CHECK(locked!=INVALID_HANDLE_VALUE);
    detail_window_finish();
    CHECK(detail_state()->report_failed && !detail_state()->report_pending);
    CloseHandle(locked);
    detail_window_finish();
    CHECK(!detail_state()->report_failed && !detail_state()->report_pending);
    CHECK(contains_log(current,"STAGE_DETAIL_REPORT epoch=2 saved=1"));
    CHECK(DeleteFileW(current)); CHECK(RemoveDirectoryW(directory));
}
int main(void)
{
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);
    test_idle_and_controls();
    test_draining_and_native_passthrough();
    test_boundary_timeout_and_reuse();
    test_epoch_recheck_and_copied_report();
    test_producer_closure_recovery();
    test_nested_producer_identity();
    test_producer_window_boundaries();
    test_optional_seal_interleaving();
    test_tls_faults_and_error_preservation();
    test_action_independence();
    test_durable_report();
    CHECK(native_calls>0);
    printf("PASS H3 actual-source window fixture: %u checks; idle exclusion, queued controls, drain/timeout, boundary ambiguity, history reuse, epoch recheck, copied report ownership, failed-save explicit retry and native passthrough.\n",checks);
    if(detail_state()) TlsFree(detail_state()->producer_tls);
    free(g_shadow_engine.detail_window);
    return 0;
}
