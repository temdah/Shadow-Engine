/* One user action, worker-owned stages, then a bounded shareable archive.
 * Render callbacks keep using their existing capture tickets/command mailbox.
 * No engine policy, native pointer lifetime or patch-site behavior changes. */
#include "../internal/support_zip.h"
#define SUPPORT_STAGE_TIMEOUT_MS 20000U
#define SUPPORT_OBSERVATION_TIMEOUT_MS 85000U
enum SupportPhase { SUPPORT_IDLE=0,SUPPORT_WORKLOAD=1,SUPPORT_OBSERVING=2,
    SUPPORT_VEHICLE=3,SUPPORT_SAVING=4,SUPPORT_SAVED=5,SUPPORT_PARTIAL=6,SUPPORT_FAILED=7 };
enum SupportGaps { SUPPORT_NO_WORKLOAD=1,SUPPORT_BAD_WORKLOAD=2,SUPPORT_NO_DETAIL=4,
    SUPPORT_BAD_DETAIL=8,SUPPORT_NO_POPULATION=16,SUPPORT_BAD_POPULATION=32,
    SUPPORT_NO_VEHICLE=64,SUPPORT_BAD_VEHICLE=128,SUPPORT_TIMEOUT=256,
    SUPPORT_MISSING_LOG=512,SUPPORT_MISSING_SETTINGS=1024,SUPPORT_LOG_FAILURE=2048,
    SUPPORT_BAD_INTERSECTION=4096 };
typedef struct SupportReportState {
    unsigned serial,phase,gaps,workload_serial,population_serial;
    LONG detail_epoch,vehicle_serial,vehicle_flush_failures,log_failures;
    LONG intersection_capture;
    DWORD started,stage_started,error,finished_elapsed;
    char archive_name[96],previous_status[256];
    int detail_started,population_started,vehicle_started,finish_requested,intersection_started;
} SupportReportState;
static void support_report_publish(SupportReportState *s);
static SupportReportState *support_report_state(void)
{ return g_shadow_engine.internal_tools.support_report; }
static int support_report_active(void)
{
    SupportReportState *s=support_report_state();
    return s && s->phase>=SUPPORT_WORKLOAD && s->phase<=SUPPORT_SAVING;
}
static int support_report_busy(void)
{
    ShadowWorkloadState *w=workload_state();
    PopulationCaptureState *p=population_state();
    DetailWindowStatus d=detail_window_status();
    return (w && w->phase) || (p && p->phase) || d.phase<0 ||
        d.phase==DETAIL_WINDOW_OPEN || d.phase==DETAIL_WINDOW_DRAINING ||
        (detail_state() && (detail_state()->report_pending || detail_state()->requests)) ||
        InterlockedCompareExchange(&g_shadow_engine.intersection.state,0,0) ||
        (g_shadow_engine.intersection.tls_valid && InterlockedCompareExchange(&g_shadow_engine.intersection.requests,0,0)) ||
        InterlockedCompareExchange(&g_shadow_engine.vehicle_diagnostics.state,0,0) ||
        InterlockedCompareExchange(&g_shadow_engine.internal_tools.capture_requests,0,0);
}
static int support_report_request(void)
{
    SupportReportState *s=support_report_state();
    unsigned serial;
    if(support_report_active() || support_report_busy() ||
       InterlockedCompareExchange(&g_shadow_engine.bootstrap.downstream_state,0,0)!=2) return 0;
    if(!s) {
        s=(SupportReportState *)calloc(1,sizeof(*s)); if(!s) return 0;
        g_shadow_engine.internal_tools.support_report=s;
    }
    if(s->serial>=16777215U) return 0; /* Exact in the target Lua number format. */
    serial=s->serial+1; memset(s,0,sizeof(*s)); s->serial=serial;
    s->started=s->stage_started=GetTickCount(); s->phase=SUPPORT_WORKLOAD;
    s->log_failures=InterlockedCompareExchange(&g_shadow_engine.bootstrap.log_write_failures,0,0);
    if(!workload_request()) s->gaps|=SUPPORT_NO_WORKLOAD;
    else s->workload_serial=workload_state()->serial;
    append_log("STAGE_SUPPORT_BEGIN serial=%u version=%s stages=workload,detailAndPopulation,vehicle,archive policyChanged=0 fullProcessDump=0",
        s->serial,PATCH_VERSION);
    return 1;
}
static void support_report_observe(SupportReportState *s)
{
    DetailWindowState *d=detail_state();
    LONG prior=d?InterlockedCompareExchange(&d->epoch,0,0):0;
    IntersectionDiagnosticState *ix=&g_shadow_engine.intersection;
    s->phase=SUPPORT_OBSERVING; s->stage_started=GetTickCount();
    detail_window_toggle(); d=detail_state();
    /* A queued arm can be delayed by a producer's short lock. Own the next
     * epoch and wait for it, rather than mistaking lock contention for failure. */
    s->detail_started=d && prior<0x7FFFFFFFL && !d->tls_failure;
    s->detail_epoch=s->detail_started?prior+1:0;
    if(!s->detail_started) s->gaps|=SUPPORT_NO_DETAIL;
    s->intersection_started=ix->tls_valid && ix->capture<0x7FFFFFFFL;
    if(s->intersection_started) {
        s->intersection_capture=ix->capture+1;
        atomic_or_long(&g_shadow_engine.internal_tools.capture_requests,SHADOW_CAPTURE_ARM);
    } else s->gaps|=SUPPORT_BAD_INTERSECTION;
    s->population_started=population_toggle();
    if(s->population_started) s->population_serial=population_state()->serial;
    else s->gaps|=SUPPORT_NO_POPULATION;
    append_log("STAGE_SUPPORT_OBSERVE serial=%u windowMs=60000 detail=%u population=%u unsupportedIsNotSuccess=1",
        s->serial,(unsigned)s->detail_started,(unsigned)s->population_started);
}
static void support_report_vehicle(SupportReportState *s)
{
    VehicleLightDiagnosticState *v=&g_shadow_engine.vehicle_diagnostics;
    s->phase=SUPPORT_VEHICLE; s->stage_started=GetTickCount();
    s->vehicle_serial=InterlockedCompareExchange(&v->serial,0,0);
    s->vehicle_flush_failures=InterlockedCompareExchange(&v->flush_failures,0,0);
    atomic_or_long(&g_shadow_engine.internal_tools.capture_requests,SHADOW_CAPTURE_VEHICLE|SHADOW_CAPTURE_FINISH);
}
static void support_report_package(SupportReportState *s)
{
    SupportZip *zip=(SupportZip *)calloc(1,sizeof(*zip));
    wchar_t path[MAX_PATH],temporary[MAX_PATH],final[MAX_PATH],name[96];
    char manifest[2048],settings[384];
    PopulationCaptureState *p=population_state();
    int ok=0,population_added=0,log_added=0,settings_added=0,intersection_added=0,n;
    unsigned i;
    if(!zip) { s->error=ERROR_NOT_ENOUGH_MEMORY; goto done; }
    s->phase=SUPPORT_SAVING;
    support_report_publish(s);
    _snprintf(s->archive_name,sizeof(s->archive_name),"ShadowEngine-Support-%lu-%lu-%u.zip",
        (unsigned long)GetCurrentProcessId(),(unsigned long)g_shadow_engine.internal_tools.session,s->serial);
    s->archive_name[sizeof(s->archive_name)-1]=0;
    for(i=0;i<=strlen(s->archive_name);++i) name[i]=(unsigned char)s->archive_name[i];
    if(!internal_tool_path(final,name) || lstrlenW(final)+5>=MAX_PATH) {
        s->error=ERROR_FILENAME_EXCED_RANGE; goto done;
    }
    lstrcpyW(temporary,final); lstrcatW(temporary,L".tmp");
    zip->file=CreateFileW(temporary,GENERIC_WRITE,0,NULL,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,NULL);
    if(zip->file==INVALID_HANDLE_VALUE) { s->error=GetLastError(); goto done; }
    if(!flush_session_log()) s->gaps|=SUPPORT_LOG_FAILURE;
    if(internal_tool_path(path,L"ShadowEnginePatch.log")) log_added=support_zip_file(zip,"ShadowEnginePatch.log",path);
    if(!log_added) s->gaps|=SUPPORT_MISSING_LOG;
    if(internal_tool_path(path,L"ShadowEngineSettings.cfg")) settings_added=support_zip_file(zip,"ShadowEngineSettings.cfg",path);
    if(!settings_added) s->gaps|=SUPPORT_MISSING_SETTINGS;
    if(s->population_started && p && p->serial==s->population_serial && !p->phase && p->notice==4U)
        population_added=support_zip_file(zip,"shadow-population.bin",p->artifact_path);
    if(s->population_started && !population_added) s->gaps|=SUPPORT_BAD_POPULATION;
    if(s->intersection_started &&
       !InterlockedCompareExchange(&g_shadow_engine.intersection.state,0,0) &&
       InterlockedCompareExchange(&g_shadow_engine.intersection.capture,0,0)==s->intersection_capture) {
        _snwprintf(name,96,L"ShadowEngineIntersection-%lu-%ld.log",
            (unsigned long)GetCurrentProcessId(),(long)s->intersection_capture);
        if(internal_tool_path(path,name)) intersection_added=support_zip_file(zip,"intersection.log",path);
    }
    if(!intersection_added) s->gaps|=SUPPORT_BAD_INTERSECTION;
    if(InterlockedCompareExchange(&g_shadow_engine.bootstrap.log_write_failures,0,0)!=s->log_failures)
        s->gaps|=SUPPORT_LOG_FAILURE;
    settings[0]=0;
    if(!internal_tool_status_text(settings,sizeof(settings))) strcpy(settings,"unavailable\n");
    n=_snprintf(manifest,sizeof(manifest),
        "Shadow Engine support report schema=1\r\nVersion: %s\r\nProfile: %s\r\n"
        "Report serial: %u\r\nElapsed ms: %lu\r\nCoverage gaps bitmask: %u\r\n"
        "Included log=%u savedSettings=%u population=%u intersection=%u\r\n"
        "Workload serial=%u detailEpoch=%ld populationSerial=%u vehicleSerial=%ld\r\n"
        "Stages run in sequence; detail and population intentionally share their observation window.\r\n"
        "This is diagnostic evidence, not a visual pass or guaranteed FPS measurement.\r\n"
        "Gap bits:1 workload unavailable;2 workload failed;4 detail unavailable;8 detail incomplete;"
        "16 population unsupported;32 population incomplete;64 vehicle unavailable;128 vehicle incomplete;"
        "256 timeout;512 log missing;1024 no saved settings file;2048 log write/flush failure;4096 intersection unavailable/incomplete.\r\n"
        "Only named mod files and copied diagnostic records are included; no saves/assets/full memory dump.\r\n"
        "Current settings/status: %s",
        PATCH_VERSION,g_runtime_state.selected?g_runtime_state.selected->name:"unknown",s->serial,
        (unsigned long)(GetTickCount()-s->started),s->gaps,(unsigned)log_added,(unsigned)settings_added,
        (unsigned)population_added,(unsigned)intersection_added,s->workload_serial,(long)s->detail_epoch,s->population_serial,
        (long)s->vehicle_serial,settings);
    if(n<=0 || n>=(int)sizeof(manifest)) zip->error=ERROR_BUFFER_OVERFLOW;
    else support_zip_memory(zip,"report.txt",manifest,(uint32_t)n);
    ok=support_zip_finish(zip);
    if(!CloseHandle(zip->file)) { ok=0; if(!zip->error) zip->error=ERROR_WRITE_FAULT; }
    if(ok && !MoveFileW(temporary,final)) { ok=0; zip->error=GetLastError(); }
    if(!ok) { s->error=zip->error?zip->error:ERROR_WRITE_FAULT; DeleteFileW(temporary); }
done:
    free(zip);
    /* The report owns these bits. A timed-out, paused renderer must not start
     * a stale sample later, after the archive has already been finalized. */
    {
        LONG before,after;
        do {
            before=InterlockedCompareExchange(&g_shadow_engine.internal_tools.capture_requests,0,0);
            after=before&~(SHADOW_CAPTURE_ARM|SHADOW_CAPTURE_FINISH|SHADOW_CAPTURE_VEHICLE);
        } while(InterlockedCompareExchange(&g_shadow_engine.internal_tools.capture_requests,after,before)!=before);
        /* Drop only our still-unconsumed arm after timeout. Existing records
         * and producer tickets retain their normal owner and drain lifetime. */
        if(s->detail_started && detail_state() && detail_state()->epoch<s->detail_epoch)
            InterlockedExchange(&detail_state()->requests,0);
        if(s->intersection_started && g_shadow_engine.intersection.capture<s->intersection_capture)
            InterlockedExchange(&g_shadow_engine.intersection.requests,0);
    }
    s->finished_elapsed=GetTickCount()-s->started;
    s->phase=ok?(s->gaps?SUPPORT_PARTIAL:SUPPORT_SAVED):SUPPORT_FAILED;
    append_log("STAGE_SUPPORT_END serial=%u saved=%u gaps=%u error=%lu file=%s",
        s->serial,(unsigned)ok,s->gaps,(unsigned long)s->error,ok?s->archive_name:"none");
}
static void support_report_publish(SupportReportState *s)
{
    char text[256]; wchar_t path[MAX_PATH],temporary[MAX_PATH];
    DWORD written,length; HANDLE file;
    int n=_snprintf(text,sizeof(text),"SEB1 %lu %lu %u %u %lu %u %lu %s\n",
        (unsigned long)GetCurrentProcessId(),(unsigned long)g_shadow_engine.internal_tools.session,
        s->serial,s->phase,(unsigned long)((s->phase>=SUPPORT_SAVED?s->finished_elapsed:GetTickCount()-s->started)/1000U),s->gaps,
        (unsigned long)s->error,s->phase==SUPPORT_SAVED || s->phase==SUPPORT_PARTIAL?s->archive_name:"none");
    if(n<=0 || n>=(int)sizeof(text) || !strcmp(text,s->previous_status) ||
       !internal_tool_path(path,L"ShadowEngineSupport.status") ||
       !internal_tool_path(temporary,L"ShadowEngineSupport.status.tmp")) return;
    length=(DWORD)n;
    file=CreateFileW(temporary,GENERIC_WRITE,0,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);
    if(file==INVALID_HANDLE_VALUE) return;
    n=WriteFile(file,text,length,&written,NULL) && written==length;
    if(!CloseHandle(file)) n=0;
    if(n && MoveFileExW(temporary,path,MOVEFILE_REPLACE_EXISTING)) strcpy(s->previous_status,text);
}
static void support_report_poll(void)
{
    SupportReportState *s=support_report_state();
    ShadowWorkloadState *w=workload_state(); PopulationCaptureState *p=population_state();
    DetailWindowState *d=detail_state();
    DWORD elapsed;
    if(!s || !s->serial) return;
    elapsed=GetTickCount()-s->stage_started;
    if(s->phase==SUPPORT_WORKLOAD) {
        if(!s->workload_serial || !w || w->serial!=s->workload_serial || !w->phase) {
            if(s->workload_serial && (!w || w->serial!=s->workload_serial || w->notice!=4U)) s->gaps|=SUPPORT_BAD_WORKLOAD;
            support_report_observe(s);
        } else if(elapsed>=SUPPORT_STAGE_TIMEOUT_MS) {
            s->gaps|=SUPPORT_BAD_WORKLOAD|SUPPORT_TIMEOUT;
            support_report_package(s); /* Never free an outstanding native ticket or overlap observations. */
        }
    } else if(s->phase==SUPPORT_OBSERVING) {
        int detail_done=!s->detail_started || (d && d->epoch==s->detail_epoch && d->phase==DETAIL_WINDOW_FROZEN && !d->report_pending);
        int population_done=!s->population_started || (p && p->serial==s->population_serial && !p->phase);
        int intersection_done=!s->intersection_started ||
            (InterlockedCompareExchange(&g_shadow_engine.intersection.capture,0,0)==s->intersection_capture &&
             !InterlockedCompareExchange(&g_shadow_engine.intersection.state,0,0));
        if(elapsed>=60000U && !s->finish_requested) {
            detail_window_finish(); population_finish();
            atomic_or_long(&g_shadow_engine.internal_tools.capture_requests,SHADOW_CAPTURE_FINISH);
            s->finish_requested=1;
        }
        if((elapsed>=60000U && detail_done && population_done && intersection_done) || elapsed>=SUPPORT_OBSERVATION_TIMEOUT_MS) {
            if(s->detail_started && (!detail_done || d->report_failed || d->timed_out)) s->gaps|=SUPPORT_BAD_DETAIL;
            if(s->population_started && (!population_done || p->notice!=4U || p->report_partial)) s->gaps|=SUPPORT_BAD_POPULATION;
            if(!intersection_done) s->gaps|=SUPPORT_BAD_INTERSECTION;
            if(!detail_done || !population_done || !intersection_done) s->gaps|=SUPPORT_TIMEOUT;
            if(!detail_done || !population_done || !intersection_done) support_report_package(s);
            else support_report_vehicle(s);
        }
    } else if(s->phase==SUPPORT_VEHICLE) {
        VehicleLightDiagnosticState *v=&g_shadow_engine.vehicle_diagnostics;
        LONG serial=InterlockedCompareExchange(&v->serial,0,0),state=InterlockedCompareExchange(&v->state,0,0);
        if(serial>s->vehicle_serial) { s->vehicle_serial=serial; s->vehicle_started=1; }
        if((s->vehicle_started && !state) || elapsed>=SUPPORT_STAGE_TIMEOUT_MS) {
            if(!s->vehicle_started) s->gaps|=SUPPORT_NO_VEHICLE;
            if(state || InterlockedCompareExchange(&v->flush_failures,0,0)!=s->vehicle_flush_failures) s->gaps|=SUPPORT_BAD_VEHICLE;
            if(elapsed>=SUPPORT_STAGE_TIMEOUT_MS) s->gaps|=SUPPORT_TIMEOUT;
            support_report_package(s);
        }
    }
    support_report_publish(s);
}
