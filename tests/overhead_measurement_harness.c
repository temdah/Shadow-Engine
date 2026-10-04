#if defined(OVERHEAD_WORKLOAD_INTEGRATION)
/* The actual unity composition supplies both workload orchestration and probe
 * lifetime. Only OS time, log I/O, allocation failure and ETW discovery are
 * controlled here. No file, trace session, game hook or worker is created. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

static DWORD integration_tick=50000U;
static int64_t integration_counter=100000;
static unsigned integration_allocation_failure;
static unsigned integration_etw_attempts,integration_flushes,integration_checks;
static unsigned integration_flush_failure,integration_write_failure;
static size_t integration_output_size;
static char integration_output[300000];
static void (*integration_etw_check)(void);
static void (*integration_flush_check)(void);
#define INTEGRATION_FILE ((HANDLE)(uintptr_t)0x1234)
#define REQUIRE(condition) do { assert(condition); ++integration_checks; } while(0)

static DWORD WINAPI integration_get_tick(void) { return integration_tick; }
static BOOL WINAPI integration_qpc(LARGE_INTEGER *value)
{
    value->QuadPart=integration_counter; return TRUE;
}
static BOOL WINAPI integration_frequency(LARGE_INTEGER *value)
{
    value->QuadPart=1000000; return TRUE;
}
static UINT WINAPI integration_system_directory(LPWSTR path,UINT count)
{
    (void)path; (void)count; ++integration_etw_attempts;
    if(integration_etw_check) integration_etw_check();
    return 0; /* Actual present_start reports unsupported; no trace OS call. */
}
static HANDLE WINAPI integration_create_file(LPCWSTR path,DWORD access,DWORD share,
    LPSECURITY_ATTRIBUTES security,DWORD creation,DWORD flags,HANDLE template_file)
{
    (void)path; (void)access; (void)share; (void)security;
    (void)creation; (void)flags; (void)template_file;
    return INTEGRATION_FILE;
}
static BOOL WINAPI integration_write_file(HANDLE file,LPCVOID data,DWORD bytes,
    LPDWORD written,LPOVERLAPPED overlapped)
{
    (void)overlapped;
    assert(file==INTEGRATION_FILE);
    if(integration_write_failure) { *written=0; return FALSE; }
    assert(integration_output_size+bytes<sizeof(integration_output));
    memcpy(integration_output+integration_output_size,data,bytes);
    integration_output_size+=bytes; integration_output[integration_output_size]=0;
    *written=bytes; return TRUE;
}
static BOOL WINAPI integration_flush_file(HANDLE file)
{
    assert(file==INTEGRATION_FILE); ++integration_flushes;
    if(integration_flush_check) integration_flush_check();
    return !integration_flush_failure;
}
static BOOL WINAPI integration_close_handle(HANDLE handle)
{
    if(handle==INTEGRATION_FILE) return TRUE;
    return CloseHandle(handle);
}
static void *integration_calloc(size_t count,size_t size)
{
    if(integration_allocation_failure && !--integration_allocation_failure) return NULL;
    return calloc(count,size);
}

#undef GetTickCount
#define GetTickCount integration_get_tick
#define QueryPerformanceCounter integration_qpc
#define QueryPerformanceFrequency integration_frequency
#define GetSystemDirectoryW integration_system_directory
#define CreateFileW integration_create_file
#define WriteFile integration_write_file
#define FlushFileBuffers integration_flush_file
#define CloseHandle integration_close_handle
#define calloc integration_calloc
#include "../src/shadow_engine_patch.c"
#undef calloc

static void integration_check_probe_before_etw(void)
{
    OverheadState *s=overhead_state();
    ShadowWorkloadState *w=workload_state();
    REQUIRE(s && s->active>0 && s->serial==w->serial);
    REQUIRE(s->requested_at==integration_tick);
    REQUIRE(w->start==integration_counter+3*w->frequency);
}
static void integration_check_report_before_flush(void)
{
    const char *workload_end=strstr(integration_output,"STAGE_WORKLOAD_END");
    const char *overhead_begin=strstr(integration_output,"STAGE_OVERHEAD_BEGIN");
    const char *overhead_end=strstr(integration_output,"STAGE_OVERHEAD_END");
    REQUIRE(overhead_ready() && overhead_state()->reported);
    REQUIRE(workload_end && overhead_begin && overhead_end);
    REQUIRE(workload_end<overhead_begin && overhead_begin<overhead_end);
    REQUIRE(workload_state()->phase==2 && workload_state()->notice==3);
}
static void integration_clear_output(void)
{
    integration_output_size=0; integration_output[0]=0;
}
static void integration_advance_to_end(void)
{
    ShadowWorkloadState *w=workload_state();
    OverheadState *s=overhead_state();
    integration_counter=w->end+1;
    integration_tick=s->requested_at+s->lead_ms+s->window_ms;
}

int main(void)
{
    ShadowWorkloadState *w;
    OverheadState *s;
    OverheadTicket ticket;
    unsigned serial,etw_before,flush_before;
    size_t output_before;
    LONG previous_token;
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);
    memset(&g_shadow_engine,0,sizeof(g_shadow_engine));
    g_shadow_engine.bootstrap.session_log_state=2;
    lstrcpyW(g_shadow_engine.bootstrap.snapshot.bin_directory,L"C:\\synthetic-only\\");

    integration_allocation_failure=2; /* Workload allocation succeeds; probe fails. */
    REQUIRE(!workload_request());
    w=workload_state();
    REQUIRE(w && w->serial==1 && w->notice==5 && !w->phase && !w->active);
    REQUIRE(!g_shadow_engine.overhead && !integration_etw_attempts && !integration_flushes);
    REQUIRE(strstr(integration_output,"failed=overheadUnavailable captureArmed=0")!=NULL);
    REQUIRE(strstr(integration_output,"STAGE_OVERHEAD_BEGIN")==NULL);
    integration_clear_output();
    integration_etw_check=integration_check_probe_before_etw;
    REQUIRE(workload_request());
    s=overhead_state();
    REQUIRE(w->serial==2 && w->notice==1 && w->phase==1 && w->active);
    REQUIRE(s && s->serial==2 && s->active>0 && integration_etw_attempts==1);
    REQUIRE(w->present.error==ERROR_PATH_NOT_FOUND);
    REQUIRE(w->start-integration_counter==3000000 && w->end-w->start==5000000);
    workload_poll(); REQUIRE(w->notice==1 && w->phase==1);
    ticket=overhead_begin(OH_WORLD_QUALITY); REQUIRE(!ticket.state);
    integration_counter=w->start; integration_tick+=3000;
    workload_poll(); REQUIRE(w->notice==2 && w->phase==1);
    ticket=overhead_begin(OH_WORLD_QUALITY);
    REQUIRE(ticket.state && s->inflight==1);
    serial=w->serial; previous_token=s->active;
    integration_advance_to_end();
    integration_counter+=6000000; integration_tick+=6000;
    workload_poll();
    REQUIRE(w->phase==2 && w->notice==3 && !w->active && !s->active);
    REQUIRE(s->inflight==1 && !s->reported && !integration_flushes);
    REQUIRE(strstr(integration_output,"reason=overheadTickets")!=NULL);
    REQUIRE(strstr(integration_output,"STAGE_WORKLOAD_END")==NULL);
    REQUIRE(strstr(integration_output,"STAGE_OVERHEAD_BEGIN")==NULL);
    etw_before=integration_etw_attempts;
    REQUIRE(!workload_request());
    REQUIRE(w->serial==serial && s->next_token==previous_token);
    REQUIRE(integration_etw_attempts==etw_before && s->streams[OH_WORLD_QUALITY].samples[0].state==1);
    output_before=integration_output_size;
    workload_poll(); REQUIRE(integration_output_size==output_before); /* One drain notice. */
    integration_counter+=33; overhead_end(&ticket);
    REQUIRE(!s->inflight && s->streams[OH_WORLD_QUALITY].samples[0].ended_after_window);
    integration_flush_check=integration_check_report_before_flush;
    workload_poll();
    REQUIRE(!w->phase && w->notice==4 && s->reported && integration_flushes==1);
    output_before=integration_output_size;
    workload_poll(); REQUIRE(integration_output_size==output_before && integration_flushes==1);

    integration_clear_output();
    REQUIRE(workload_request());
    REQUIRE(w->serial==3 && s->active!=previous_token);
    REQUIRE(s->streams[OH_WORLD_QUALITY].calls==0 && !s->streams[OH_WORLD_QUALITY].samples[0].state);
    integration_advance_to_end();
    integration_flush_failure=1;
    workload_poll();
    REQUIRE(!w->phase && w->notice==5 && s->reported && integration_flushes==2);
    integration_flush_failure=0;

    /* A completed earlier report must not be replayed when the next probe
     * request fails. Failure owns a new workload serial but no new ETW/window. */
    integration_clear_output();
    serial=w->serial; etw_before=integration_etw_attempts; flush_before=integration_flushes;
    s->next_token=0x7FFFFFFFL;
    REQUIRE(!workload_request());
    REQUIRE(w->serial==serial+1U && w->notice==5 && !w->phase && !w->active);
    REQUIRE(integration_etw_attempts==etw_before && integration_flushes==flush_before);
    REQUIRE(strstr(integration_output,"failed=overheadUnavailable")!=NULL);
    REQUIRE(strstr(integration_output,"STAGE_OVERHEAD_BEGIN")==NULL);
    output_before=integration_output_size;
    workload_poll(); REQUIRE(integration_output_size==output_before);

    /* A writer error is independently fatal to saved state even if flush is
     * successful. Reset only fixture-owned token exhaustion for this case. */
    s->next_token=100;
    integration_clear_output(); REQUIRE(workload_request());
    integration_advance_to_end();
    integration_write_failure=1; integration_flush_check=NULL;
    workload_poll();
    REQUIRE(!w->phase && w->notice==5 && g_shadow_engine.bootstrap.log_write_failures>0);
    REQUIRE(integration_flushes==flush_before+1U && s->reported);
    integration_write_failure=0;

    REQUIRE(overhead_ready() && !w->inflight && !w->lock);
    free(g_shadow_engine.overhead); free(g_shadow_engine.workload);
    g_shadow_engine.overhead=NULL; g_shadow_engine.workload=NULL;
    printf("PASS: overhead/workload actual-unity integration, %u checks; request ordering/failure identity, late-ticket no-block drain, no report/reuse before release, report-before-flush and flush/write failure notice. No ETW session or files created.\n",integration_checks);
    return 0;
}

#else
/* Actual probe source with controlled clocks and injected worker interleavings.
 * This fixture owns only synthetic process-lifetime state, never a game process. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <assert.h>

#define SHADOW_ENGINE_OVERHEAD_MEASUREMENT 1
#define PATCH_VERSION "2.0.72-fixture"
#include "../src/internal/overhead_probe.h"

static struct { void *overhead; } g_shadow_engine;
static DWORD fake_tick=1000;
static int64_t fake_counter;
static unsigned fail_counter,fail_frequency,fail_allocation;
static unsigned counter_calls,log_lines,assertions;
static char output[100000];
static void (*compare_event)(volatile LONG *,LONG,LONG);
static void (*add_event)(volatile LONG *,LONG,int);

static LONG fixture_compare(volatile LONG *target,LONG exchange,LONG comparand)
{
    LONG previous=InterlockedCompareExchange(target,exchange,comparand);
    if(compare_event) compare_event(target,exchange,comparand);
    return previous;
}

static LONG atomic_add_long(volatile LONG *target,LONG value)
{
    LONG current,next;
    if(add_event) add_event(target,value,0);
    do {
        current=InterlockedCompareExchange(target,0,0);
        next=current+value;
    } while(InterlockedCompareExchange(target,next,current)!=current);
    if(add_event) add_event(target,value,1);
    return current;
}

static BOOL WINAPI fixture_counter(LARGE_INTEGER *out)
{
    ++counter_calls;
    if(fail_counter) { --fail_counter; return 0; }
    out->QuadPart=fake_counter;
    return 1;
}
static BOOL WINAPI fixture_frequency(LARGE_INTEGER *out)
{
    if(fail_frequency) return 0;
    out->QuadPart=1000000;
    return 1;
}
static DWORD WINAPI fixture_tick(void) { return fake_tick; }
static DWORD WINAPI fixture_thread(void) { return 17U; }
static void *fixture_calloc(size_t count,size_t size)
{
    if(fail_allocation && !--fail_allocation) return NULL;
    return calloc(count,size);
}
static void append_log(const char *format,...)
{
    char line[2048];
    va_list args;
    va_start(args,format);
    _vsnprintf(line,sizeof(line)-2,format,args);
    va_end(args);
    line[sizeof(line)-2]=0;
    assert(strlen(output)+strlen(line)+2<sizeof(output));
    strcat(output,line); strcat(output,"\n"); ++log_lines;
}

#undef InterlockedCompareExchange
#define InterlockedCompareExchange fixture_compare
#define QueryPerformanceCounter fixture_counter
#define QueryPerformanceFrequency fixture_frequency
#define GetTickCount fixture_tick
#define GetCurrentThreadId fixture_thread
#define calloc fixture_calloc
#include "../src/modules/12_overhead_measurement.inc"
#undef calloc
#undef InterlockedCompareExchange

#define CHECK(condition) do { assert(condition); ++assertions; } while(0)

static OverheadState *state(void) { return (OverheadState *)g_shadow_engine.overhead; }
static void new_window(unsigned serial,DWORD start)
{
    overhead_close();
    CHECK(overhead_ready());
    fake_tick=start;
    CHECK(overhead_request(serial,3000,5000));
    fake_tick=start+3000U;
}

static unsigned race_stage,race_reads;
static void race_after_token_read(volatile LONG *target,LONG exchange,LONG comparand)
{
    OverheadState *s=state();
    if(target!=&s->active || exchange || comparand) return;
    ++race_reads;
    if((race_stage==1U && race_reads==1U) ||
       (race_stage==3U && race_reads==2U)) {
        compare_event=NULL;
        overhead_close();
        CHECK(overhead_ready()==(race_stage==1U));
    } else if(race_stage==4U && race_reads==1U) {
        compare_event=NULL;
        overhead_close();
        CHECK(overhead_ready());
        CHECK(overhead_request(400,0,5000));
    }
}
static void race_after_reference(volatile LONG *target,LONG value,int after)
{
    if(target!=&state()->inflight || value!=1 || !after) return;
    add_event=NULL;
    overhead_close();
    CHECK(!overhead_ready());
    CHECK(!overhead_request(999,0,5000));
}

static void test_boundaries_and_timers(void)
{
    OverheadTicket ticket,collision;
    OverheadState *s;
    unsigned i,before;
    CHECK(overhead_ready());
    fail_allocation=1;
    CHECK(!overhead_request(1,3000,5000));
    CHECK(!g_shadow_engine.overhead);
    fail_frequency=1;
    CHECK(!overhead_request(1,3000,5000));
    fail_frequency=0;
    CHECK(!overhead_request(1,0,0));
    CHECK(!overhead_request(1,0,5001));
    CHECK(!overhead_request(1,60001,5000));
    new_window(1,1000); s=state();
    CHECK(s->calibration_valid);
    before=counter_calls;
    fake_tick=3999;
    ticket=overhead_begin(OH_WORLD_QUALITY);
    CHECK(!ticket.state && counter_calls==before);
    fake_tick=4000;
    ticket=overhead_begin(OH_WORLD_QUALITY);
    CHECK(ticket.state && s->inflight==1);
    CHECK(counter_calls==before+1);
    collision=overhead_begin(OH_WORLD_QUALITY);
    CHECK(!collision.state && counter_calls==before+1);
    CHECK(s->streams[OH_WORLD_QUALITY].calls==2);
    CHECK(s->streams[OH_WORLD_QUALITY].collisions==1);
    fake_counter+=10;
    overhead_pause(&ticket);
    overhead_pause(&ticket);
    fake_counter+=100000; /* A native interval excluded by pause/resume. */
    overhead_resume(&ticket);
    overhead_resume(&ticket);
    fake_counter+=30;
    overhead_end(&ticket);
    CHECK(!ticket.state && s->inflight==0);
    CHECK(s->streams[OH_WORLD_QUALITY].samples[0].ticks==40);
    CHECK(s->streams[OH_WORLD_QUALITY].samples[0].segments==2);
    before=counter_calls;
    overhead_end(&ticket);
    CHECK(s->inflight==0 && counter_calls==before);
    for(i=1;i<500;++i) {
        fake_tick=4000+i*10;
        ticket=overhead_begin(OH_WORLD_QUALITY);
        CHECK(ticket.state && ticket.slot==i);
        fake_counter+=i;
        overhead_end(&ticket);
    }
    fake_tick=9000;
    before=counter_calls;
    ticket=overhead_begin(OH_WORLD_QUALITY);
    CHECK(!ticket.state && counter_calls==before);
    ticket=overhead_begin((OverheadCategory)-1);
    CHECK(!ticket.state && counter_calls==before);
    CHECK(s->streams[OH_WORLD_QUALITY].calls==501);
    CHECK(!overhead_ready());
    CHECK(!overhead_request(2,0,5000));
    overhead_report(); CHECK(!log_lines);
    overhead_close(); CHECK(overhead_ready());
    overhead_report(); CHECK(log_lines==OH_CATEGORY_COUNT+4);
    CHECK(strstr(output,"workRegister=P1 purpose=wholePassPopulationRecording")!=NULL);
    CHECK(strstr(output,"measurementArmsDetail=0")!=NULL);
    CHECK(strstr(output,"category=worldQuality calls=501")!=NULL);
    CHECK(strstr(output,"category=driverIdentity calls=0")!=NULL);
    CHECK(strstr(output,"category=driverPublication calls=0")!=NULL);
    CHECK(strstr(output,"samples=500 occupiedBuckets=500 invalidSamples=0 collisions=1")!=NULL);
    CHECK(strstr(output,"coverage=notObserved")!=NULL);
    CHECK(strstr(output,"exactTotalCpu=notMeasured")!=NULL);
    CHECK(strstr(output,"calibrationSubtracted=0")!=NULL);
    before=log_lines; overhead_report(); CHECK(log_lines==before);

    new_window(2,0xFFFFF000U);
    fake_tick=(DWORD)(0xFFFFF000U+3000U+1200U);
    ticket=overhead_begin(OH_QUEUE_CLAMP);
    CHECK(ticket.state && ticket.slot==120);
    fake_counter+=5; overhead_end(&ticket);
    CHECK(s->streams[OH_QUEUE_CLAMP].samples[120].ticks==5);
    CHECK(s->streams[OH_QUEUE_CLAMP].calls==1);
    CHECK(s->streams[OH_WORLD_QUALITY].calls==0);

    new_window(3,10000);
    fail_counter=1;
    ticket=overhead_begin(OH_FACE_COST_READER);
    CHECK(ticket.state && ticket.invalid);
    overhead_end(&ticket);
    CHECK(!s->streams[OH_FACE_COST_READER].samples[0].valid);
    CHECK(s->timer_failures==1);
    fake_tick+=10;
    ticket=overhead_begin(OH_FACE_COST_READER);
    --fake_counter;
    overhead_end(&ticket);
    CHECK(!s->streams[OH_FACE_COST_READER].samples[1].valid);
    CHECK(s->timer_failures==2);
    fake_tick+=10;
    ticket=overhead_begin(OH_FACE_COST_READER);
    fail_counter=1;
    overhead_end(&ticket);
    CHECK(!s->streams[OH_FACE_COST_READER].samples[2].valid);
    CHECK(s->timer_failures==3);
}

static void test_worker_interleavings(void)
{
    OverheadTicket ticket,nested;
    OverheadState *s=state();
    unsigned before;
    new_window(10,20000);
    race_stage=1; race_reads=0; compare_event=race_after_token_read;
    ticket=overhead_begin(OH_COMPLETION_RECORD);
    CHECK(!ticket.state && overhead_ready());
    CHECK(s->streams[OH_COMPLETION_RECORD].calls==0 && s->inflight==0);

    new_window(11,30000);
    add_event=race_after_reference;
    ticket=overhead_begin(OH_COMPLETION_RECORD);
    CHECK(!ticket.state && overhead_ready());
    CHECK(s->streams[OH_COMPLETION_RECORD].calls==0 && s->inflight==0);

    new_window(12,40000);
    race_stage=3; race_reads=0; compare_event=race_after_token_read;
    ticket=overhead_begin(OH_COMPLETION_RECORD);
    CHECK(ticket.state && !overhead_ready());
    CHECK(!overhead_request(13,0,5000));
    before=log_lines; overhead_report(); CHECK(log_lines==before);
    fake_tick+=6000; fake_counter+=50;
    overhead_end(&ticket);
    CHECK(overhead_ready());
    CHECK(s->streams[OH_COMPLETION_RECORD].samples[0].ticks==50);
    CHECK(s->streams[OH_COMPLETION_RECORD].samples[0].ended_after_window);

    new_window(14,50000);
    race_stage=4; race_reads=0; compare_event=race_after_token_read;
    ticket=overhead_begin(OH_COMPLETION_RECORD);
    CHECK(!ticket.state && s->serial==400 && s->active!=0);
    CHECK(s->streams[OH_COMPLETION_RECORD].calls==0 && s->inflight==0);
    ticket=overhead_begin(OH_COMPLETION_RECORD);
    nested=overhead_begin(OH_COMPLETION_RECORD);
    CHECK(ticket.state && !nested.state && s->inflight==1);
    overhead_close(); CHECK(!overhead_ready());
    overhead_end(&nested); CHECK(s->inflight==1);
    overhead_end(&ticket); CHECK(overhead_ready());

    fail_allocation=1; /* Existing main state: only calibration allocation fails. */
    CHECK(overhead_request(20,0,5000));
    CHECK(!s->calibration_valid);
    ticket=overhead_begin(OH_LOG_WRITE);
    CHECK(ticket.state);
    overhead_end(&ticket); overhead_close();
    s->next_token=0x7FFFFFFFL;
    CHECK(!overhead_request(21,0,5000));
    CHECK(overhead_ready());
}

int main(void)
{
    test_boundaries_and_timers();
    test_worker_interleavings();
    CHECK(state()->inflight==0);
    free(g_shadow_engine.overhead);
    g_shadow_engine.overhead=NULL;
    printf("PASS: overhead measurement actual-source fixture, %u checks; 500 buckets, pause/native exclusion, timer/allocation failures, tick wrap, close/reuse interleavings and no-block drain.\n",assertions);
    return 0;
}
#endif
