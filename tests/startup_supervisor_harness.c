/* Actual module80 loop, virtual time and native-installer boundary stubs.
 * No game, hooks, native constructors, OS sleeps, threads or file writes. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
static DWORD tick,start_tick;
static unsigned elapsed,checks,early_calls,deferred_calls,sleeps,slow_sleeps,pin_calls;
static unsigned resource_at,pass_at,profile_at,proof_lost_at,proof_back_at,failure_at;
static int failure_kind,early_result,deferred_result,pin_result,owner_recovers,module_available;
static unsigned deferred_at;
static unsigned char intercepted,ready;
static void *original_run_game;
static char log_text[131072];
static size_t log_size;
static void advance_state(void);
static DWORD WINAPI fake_tick(void) { return tick; }
static void WINAPI fake_sleep(DWORD milliseconds)
{
    assert(milliseconds==10U || milliseconds==250U);
    ++sleeps; if(milliseconds==250U) ++slow_sleeps;
    tick+=milliseconds; elapsed+=milliseconds;
    assert(elapsed<400000U);
    advance_state();
}
static HMODULE WINAPI fake_module(LPCWSTR name)
{
    return module_available && !wcscmp(name,L"Disrupt_b64.dll")?(HMODULE)3:NULL;
}
static FARPROC WINAPI fake_export(HMODULE module,LPCSTR name)
{
    (void)module;
    if(!strcmp(name,"g_RunGameIntercepted")) return (FARPROC)&intercepted;
    if(!strcmp(name,"g_NexusReady")) return (FARPROC)&ready;
    if(!strcmp(name,"OriginalRunGame")) return (FARPROC)&original_run_game;
    return NULL;
}
static BOOL WINAPI fake_pin(DWORD flags,LPCWSTR address,HMODULE *out)
{
    (void)address; ++pin_calls;
    assert(flags==(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN));
    if(!pin_result) { SetLastError(ERROR_ACCESS_DENIED); return FALSE; }
    *out=(HMODULE)7; return TRUE;
}
static BOOL WINAPI fake_counter(LARGE_INTEGER *value)
{ value->QuadPart=(uint64_t)elapsed*1000U; return TRUE; }
static HANDLE WINAPI fake_file(LPCWSTR name,DWORD access,DWORD share,
    LPSECURITY_ATTRIBUTES security,DWORD disposition,DWORD flags,HANDLE template_file)
{
    (void)name;(void)access;(void)share;(void)security;(void)disposition;(void)flags;(void)template_file;
    return (HANDLE)5;
}
static BOOL WINAPI fake_write(HANDLE file,LPCVOID data,DWORD bytes,LPDWORD written,LPOVERLAPPED overlap)
{
    (void)file;(void)overlap;
    assert(log_size+bytes<sizeof(log_text));
    memcpy(log_text+log_size,data,bytes); log_size+=bytes; log_text[log_size]=0;
    *written=bytes; return TRUE;
}
static BOOL WINAPI fake_close(HANDLE handle) { (void)handle; return TRUE; }
static BOOL WINAPI fake_move(LPCWSTR old_name,LPCWSTR new_name,DWORD flags)
{ (void)old_name;(void)new_name;(void)flags; return TRUE; }
#define GetTickCount fake_tick
#define Sleep fake_sleep
#define GetModuleHandleW fake_module
#define GetProcAddress fake_export
#define GetModuleHandleExW fake_pin
#define QueryPerformanceCounter fake_counter
#define CreateFileW fake_file
#define WriteFile fake_write
#define FlushFileBuffers fake_close
#define CloseHandle fake_close
#define MoveFileExW fake_move
#include "../src/shadow_engine_patch.c"
#undef GetTickCount
#undef Sleep
#undef GetModuleHandleW
#undef GetProcAddress
#undef GetModuleHandleExW
#undef QueryPerformanceCounter
#undef CreateFileW
#undef WriteFile
#undef FlushFileBuffers
#undef CloseHandle
#undef MoveFileExW
#define CHECK(value) do { ++checks; assert(value); } while(0)
static void runtime_proof(void)
{
    g_shadow_engine.renderer.profile_state=2;
    g_shadow_engine.renderer.owner_reserve_state=2;
    g_shadow_engine.renderer.owner_reserve_fail_closed=0;
    g_shadow_engine.renderer.owner_capacity_after=OWNER_VECTOR_RESERVE_CAPACITY;
    g_shadow_engine.renderer.observed_scheduler_b4=TARGET_DYNAMIC_B4;
    g_shadow_engine.renderer.observed_cache_a8=TARGET_CACHE_A8;
}
static void advance_state(void)
{
    if(!early_calls || early_result!=1) return;
    ready=1;
    if(elapsed>=resource_at) {
        g_shadow_engine.resources.resource_hits=1;
        g_shadow_engine.resources.range_resource_state=2;
        g_shadow_engine.resources.range_nonzero_handles=EXTRA_LOCAL_MAPS;
        g_shadow_engine.resources.range_distinct_handles=EXTRA_LOCAL_MAPS;
    }
    if(elapsed>=pass_at) {
        g_shadow_engine.resources.register_hits=1;
        g_shadow_engine.resources.last_native_shadow_seen=1;
        g_shadow_engine.resources.last_native_alpha_seen=1;
        g_shadow_engine.resources.range_pass_state=2;
        g_shadow_engine.resources.pass_table_state=2;
        g_shadow_engine.resources.pass_table_slots=64;
        g_shadow_engine.resources.pass_table_stored_max_key=0x3F00;
        g_shadow_engine.resources.pass_table_added_nonzero=EXTRA_PASS_SLOT_COUNT;
        g_shadow_engine.resources.pass_table_added_distinct=EXTRA_PASS_SLOT_COUNT;
    }
    if(proof_lost_at && elapsed>=proof_lost_at && elapsed<proof_back_at)
        g_shadow_engine.resources.range_pass_state=0;
    if(failure_at && elapsed>=failure_at) {
        if(failure_kind==1) g_shadow_engine.resources.range_resource_state=-1;
        if(failure_kind==2) g_shadow_engine.resources.range_pass_state=-1;
        if(failure_kind==3) g_shadow_engine.resources.pass_table_state=-1;
        if(failure_kind==4) g_shadow_engine.bootstrap.downstream_state=-2;
        if(failure_kind==5) g_shadow_engine.bootstrap.hooks_installed=-1;
    }
    if(deferred_calls && deferred_result==1 && elapsed>=profile_at) runtime_proof();
}
static int fake_early(HMODULE module)
{
    CHECK(module==(HMODULE)3); CHECK(!ready); CHECK(elapsed<POLL_TIMEOUT_MS);
    CHECK(++early_calls==1);
    g_shadow_engine.bootstrap.hooks_installed=early_result==1?1:(early_result<0?-1:0);
    return early_result==1;
}
static int fake_deferred(void)
{
    CHECK(++deferred_calls==1);
    CHECK(g_shadow_engine.bootstrap.hooks_installed==1);
    CHECK(g_shadow_engine.resources.range_resource_state==2 &&
        g_shadow_engine.resources.range_pass_state==2 && g_shadow_engine.resources.pass_table_state==2);
    CHECK(g_shadow_engine.bootstrap.downstream_state==0);
    deferred_at=elapsed;
    g_shadow_engine.bootstrap.downstream_state=deferred_result==1?2:-1;
    if(deferred_result!=1) return 0;
    g_shadow_engine.resources.routing_state=2;
    g_shadow_engine.external_results.lifecycle_hooks_state=2;
    g_shadow_engine.renderer.renderer_hook_state=2;
    if(owner_recovers) {
        g_shadow_engine.renderer.profile_state=2;
        g_shadow_engine.renderer.owner_reserve_state=-1;
        g_shadow_engine.renderer.owner_reserve_fail_closed=1;
    }
    if(elapsed>=profile_at) runtime_proof();
    return 1;
}
static unsigned occurrences(const char *needle)
{
    unsigned count=0;
    const char *p=log_text;
    while((p=strstr(p,needle))!=NULL) { ++count; p+=strlen(needle); }
    return count;
}
static void reset(DWORD initial_tick)
{
    memset(&g_shadow_engine,0,sizeof(g_shadow_engine));
    tick=start_tick=initial_tick; elapsed=0;
    early_calls=deferred_calls=sleeps=slow_sleeps=pin_calls=0;
    resource_at=100;pass_at=200;profile_at=0;
    proof_lost_at=proof_back_at=failure_at=0;
    failure_kind=owner_recovers=0;
    early_result=deferred_result=pin_result=module_available=1; deferred_at=0;
    intercepted=1; ready=0; original_run_game=(void *)9;
    log_size=0;log_text[0]=0;
    g_shadow_engine.bootstrap.snapshot.troplo=(HMODULE)2;
    g_shadow_engine.bootstrap.snapshot.frequency.QuadPart=1000000;
    g_shadow_engine.bootstrap.session_log_state=2;
}
static void run(void)
{ CHECK(run_startup_worker(fake_early,fake_deferred)==0); }
static void successful(unsigned minimum_deferred,unsigned finish)
{
    run();
    CHECK(early_calls==1 && deferred_calls==1 && deferred_at>=minimum_deferred && elapsed>=finish);
    CHECK(g_shadow_engine.bootstrap.downstream_state==2);
    CHECK(!g_shadow_engine.bootstrap.startup_failure);
    CHECK(occurrences("STAGE_M_COMPLETE resourceConstructorReached=1")==1);
    CHECK(!strstr(log_text,"STAGE_M_STARTUP_STOP"));
}
int main(void)
{
    reset(0); successful(1200,1200); CHECK(!slow_sleeps);
    reset(0); resource_at=180000; pass_at=185000; profile_at=240000;
    successful(186000,240000);
    CHECK(slow_sleeps>500 && occurrences("STAGE_M_STARTUP_WAITING")==1);
    CHECK(!strstr(log_text,"STAGE_M_TIMEOUT"));
    reset(0); resource_at=pass_at=100;
    proof_lost_at=600; proof_back_at=1600;
    successful(2600,2600); CHECK(occurrences("STAGE_E_CONSTRUCTORS_COMPLETE")==2);
    reset(0xffffff00U); successful(1200,1200); CHECK(tick<start_tick);
    reset(0); intercepted=0; run();
    CHECK(!early_calls && !deferred_calls && elapsed==POLL_TIMEOUT_MS);
    CHECK(strstr(log_text,"reason=earlyWindowTimeout"));
    CHECK(g_shadow_engine.bootstrap.startup_failure==STARTUP_FAILURE_EARLY_WINDOW_TIMEOUT);
    reset(0); ready=1; run(); CHECK(!early_calls && !deferred_calls && !sleeps);
    CHECK(strstr(log_text,"reason=earlyWindowAlreadyClosed"));
    CHECK(g_shadow_engine.bootstrap.startup_failure==STARTUP_FAILURE_EARLY_WINDOW_CLOSED);
    for(int result=0;result>=-1;--result) {
        reset(0);early_result=result;run();
        CHECK(early_calls==1 && !deferred_calls && !sleeps);
        CHECK(strstr(log_text,"reason=earlyInstallationFailed"));
        CHECK(g_shadow_engine.bootstrap.startup_failure==STARTUP_FAILURE_EARLY_INSTALLATION);
    }
    for(int kind=1;kind<=5;++kind) {
        reset(0);failure_at=500;failure_kind=kind;run();
        CHECK(early_calls==1 && !deferred_calls && elapsed==500);
        CHECK(strstr(log_text,"reason=terminalInitializationFailure"));
        CHECK(g_shadow_engine.bootstrap.startup_failure==STARTUP_FAILURE_INITIALIZATION_PROOF);
    }
    reset(0);deferred_result=0;run();
    CHECK(early_calls==1 && deferred_calls==1 && g_shadow_engine.bootstrap.downstream_state==-1);
    CHECK(strstr(log_text,"reason=deferredInstallationFailed"));
    CHECK(g_shadow_engine.bootstrap.startup_failure==STARTUP_FAILURE_DEFERRED_INSTALLATION);
    reset(0);resource_at=pass_at=300000;failure_at=180000;failure_kind=1;run();
    CHECK(!deferred_calls && elapsed==180000 && slow_sleeps>0);
    CHECK(occurrences("STAGE_M_STARTUP_WAITING")==1);
    reset(0);owner_recovers=1;profile_at=5000;successful(1200,5000);
    CHECK(!g_shadow_engine.renderer.owner_reserve_fail_closed);
    reset(0);pin_result=0;g_shadow_engine.bootstrap.session_log_state=0;
    CHECK(stage_e_worker(NULL)==0);
    CHECK(pin_calls==1 && !early_calls && !deferred_calls && !sleeps);
    CHECK(strstr(log_text,"bootstrapModulePinFailed") && !g_shadow_engine.bootstrap.hooks_installed);
    CHECK(g_shadow_engine.bootstrap.startup_failure==STARTUP_FAILURE_PIN_UNAVAILABLE);
    CHECK(g_shadow_engine.bootstrap.session_log_state==2 && occurrences("SESSION_BEGIN")==1);
    CHECK(strstr(log_text,"error=5 noEngineMutation=1"));
    for(int unproven=0;unproven<5;++unproven) {
        reset(0);pin_result=0;g_shadow_engine.bootstrap.session_log_state=0;
        if(unproven==0) intercepted=0;
        if(unproven==1) ready=1;
        if(unproven==2) original_run_game=NULL;
        if(unproven==3) g_shadow_engine.bootstrap.snapshot.troplo=NULL;
        if(unproven==4) module_available=0;
        CHECK(stage_e_worker(NULL)==0);
        CHECK(g_shadow_engine.bootstrap.startup_failure==STARTUP_FAILURE_PIN_UNAVAILABLE);
        CHECK(!g_shadow_engine.bootstrap.session_log_state && !log_size);
        CHECK(pin_calls==1 && !sleeps && !g_shadow_engine.bootstrap.hooks_installed);
    }
    reset(0);intercepted=0;CHECK(stage_e_worker(NULL)==0);
    CHECK(pin_calls==1 && !early_calls && !deferred_calls && elapsed==POLL_TIMEOUT_MS);
    CHECK(g_shadow_engine.bootstrap.startup_failure==STARTUP_FAILURE_EARLY_WINDOW_TIMEOUT);
    printf("PASS startup supervisor diagnostics=%d checks=%u: actual wait loop, late180/185s constructors +240s profile, stable proof, tick wrap, early deadline/late rejection, one-shot failures, owner recovery, independent module pin; no game/I/O/wall-clock waits.\n",
        SHADOW_ENGINE_INTERNAL_DIAGNOSTICS,checks);
    return 0;
}
