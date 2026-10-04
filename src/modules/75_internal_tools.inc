/* Internal-only menu transport. One process-lifetime worker owns a fixed,
 * bounded mailbox beside the ASI. The Lua menu never loads/calls native code.
 * Settings requests route to the common durable owner in module17. */
#define INTERNAL_TOOL_POLL_MS 250U
#define INTERNAL_TOOL_REQUEST_BYTES 96U
#include "../internal/support_report.h"

/* Zero is queued settings, one is a queued diagnostic, negative is terminal. */
static int internal_tool_dispatch(unsigned action,DWORD sequence)
{
    LONG capture=0;
    /* Legacy numeric actions predate10=Unlimited. Require the versioned
     * settings request for limiter changes; diagnostics remain compatible. */
    (void)sequence;
    if(action==17U) return support_report_request()?1:-1;
    if(support_report_active()) return -1; /* One report owns the diagnostic collectors until saved. */
    if((action>=20U && action<=30U) || (action>=40U && action<=61U) ||
       (action>=100U && action<=187U)) return -1;
    switch(action) {
    case 1: case 2: case 5: case 6: return -1;
    case 3: return saved_settings_queue_policy(SHADOW_POLICY_DISABLE_QUALITY,0,sequence);
    case 4: return saved_settings_queue_policy(SHADOW_POLICY_DISABLE_QUALITY,SHADOW_POLICY_DISABLE_QUALITY,sequence);
    case 7: detail_window_toggle(); capture=SHADOW_CAPTURE_ARM; break;
    case 8: detail_window_mark(); capture=SHADOW_CAPTURE_MARK; break;
    case 9: detail_window_finish(); capture=SHADOW_CAPTURE_FINISH; break;
    case 10: capture=SHADOW_CAPTURE_VEHICLE; break;
    case 11:
        if(!workload_request()) append_log("STAGE_WORKLOAD_REQUEST skipped=busyOrUnavailable");
        return 1; /* Always acknowledge transport; a busy capture must not jam the menu. */
#if SHADOW_ENGINE_INTERNAL_DIAGNOSTICS
    case 12: return vehicle_selection_set_extra_slots(0U)?1:-1;
    case 13: return vehicle_selection_set_extra_slots(2U)?1:-1;
    case 14: case 15: case 16: {
        PopulationCaptureState *population=population_state();
        int accepted=0;
        /* Shadow onset owns separate controls. In particular, an explicit
         * start never toggles an active capture off or replaces a saving one. */
        if(action==14U) {
            if(!population || !population->phase) accepted=population_toggle();
        } else if(population && population->phase==1U) {
            if(action==15U && !population->marked) {
                population_mark(); accepted=1;
            } else if(action==16U) {
                population_finish(); accepted=1;
            }
        }
        population=population_state();
        append_log("STAGE_POPULATION_CONTROL action=%u sequence=%lu accepted=%u serial=%u state=%u blackWorldCaptureChanged=0",
            action,(unsigned long)sequence,(unsigned)accepted,
            population?population->serial:0U,population?population->notice:0U);
        return accepted?1:-1;
    }
#endif
    default: return -1;
    }
    if(capture) atomic_or_long(&g_shadow_engine.internal_tools.capture_requests,capture);
    return 1;
}

static int internal_tool_values(const char *text,DWORD *values,unsigned count)
{
    unsigned i;
    for(i=0;i<count;++i) {
        const char *p=text;
        DWORD n=0;
        if(*p<'0' || *p>'9') return 0;
        do {
            unsigned digit=(unsigned)(*p-'0');
            if(n>(0xffffffffUL-digit)/10U) return 0;
            n=n*10U+digit; ++p;
        } while(*p>='0' && *p<='9');
        values[i]=n;
        if(i+1U<count) { if(*p!=' ') return 0; text=p+1; }
        else return !*p || !strcmp(p,"\n") || !strcmp(p,"\r\n");
    }
    return 0;
}
/* ACK is receipt, not durability. A recognizable current-session sequence
 * always receives either a queued request or a terminal rejection. */
static int internal_tool_accept(const char *text)
{
    InternalToolState *t=&g_shadow_engine.internal_tools;
    DWORD pid,session,sequence,values[4];
    const char *body;
    unsigned prefix;
    int result=-1;
    SavedSettingsRequest request={0};
    if(!text || strlen(text)>=INTERNAL_TOOL_REQUEST_BYTES) return 0;
    prefix=!strncmp(text,"SES3 ",5)||!strncmp(text,"SES2 ",5)||!strncmp(text,"SES1 ",5)||!strncmp(text,"SEV1 ",5)||!strncmp(text,"SEW2 ",5)?5U:
        (!strncmp(text,"SE1 ",4)?4U:0U);
    if(!prefix) return 0;
    body=text+prefix;
    if(!saved_settings_number(&body,&pid,' ') || !saved_settings_number(&body,&session,' ') ||
       !saved_settings_number(&body,&sequence,' ') || pid!=GetCurrentProcessId() ||
       session!=t->session || !sequence || sequence<=t->acknowledged) return 0;
    request.sequence=sequence;
    if(!strncmp(text,"SES3 ",5)) {
        result=-1; /* Retired experiment: reject without queuing a settings write. */
    } else if(!strncmp(text,"SES2 ",5) || !strncmp(text,"SES1 ",5)) {
        if(internal_tool_values(body,values,4U) && values[0] && !(values[0]&~7U) &&
           (strncmp(text,"SES1 ",5) || !(values[0]&SETTINGS_CHANGE_LIMIT))) {
            request.mask=values[0]; request.limit=values[1];
            request.vehicle_quality=values[2]; request.world_quality=values[3];
            result=saved_settings_submit(&request);
        }
    } else if(!strncmp(text,"SEV1 ",5)) {
        result=-1; /* Old combined requests cannot express modern limit semantics. */
    } else if(!strncmp(text,"SEW2 ",5)) {
        if(internal_tool_values(body,values,2U) && values[0]<=1U && values[1]>=1U && values[1]<=4U) {
            request.mask=SETTINGS_CHANGE_WORLD_WORD;
            request.world_value=((LONG)values[1]<<SHADOW_WORLD_RESOLUTION_SHIFT)|(values[0]?SHADOW_WORLD_ENABLED:0L);
            result=saved_settings_submit(&request);
        }
    } else if(internal_tool_values(body,values,1U)) result=internal_tool_dispatch(values[0],sequence);
    t->acknowledged=sequence;
    if(result) { t->settings_processed=sequence; t->settings_result=result; t->settings_error=0; }
    append_log("STAGE_INTERNAL_TOOL_REQUEST sequence=%lu receipt=1 result=%d completion=%s",
        (unsigned long)sequence,result,result<0?"rejected":"queued");
    return 1;
}

static int internal_tool_path(wchar_t *path,const wchar_t *name)
{
    const wchar_t *directory=g_shadow_engine.bootstrap.snapshot.bin_directory;
    if(lstrlenW(directory)+lstrlenW(name)>=MAX_PATH) return 0;
    lstrcpyW(path,directory); lstrcatW(path,name);
    return 1;
}

static int internal_tool_status_text(char *text,size_t capacity)
{
    InternalToolState *t=&g_shadow_engine.internal_tools;
    SavedSettingsStatus settings;
    ShadowWorkloadState *w=workload_state();
    PopulationCaptureState *population=population_state();
    LONG policy,world,downstream;
    int length;
    if(!saved_settings_status(&settings)) return 0;
    if(settings.processed_sequence>t->settings_processed) {
        t->settings_processed=settings.processed_sequence;
        t->settings_result=settings.result; t->settings_error=settings.error;
    }
    policy=settings.current.policy; world=settings.current.world;
    downstream=InterlockedCompareExchange(&g_shadow_engine.bootstrap.downstream_state,0,0);
    /* SE10 reports accepted controls, capture state and Nexus compatibility.
     * -3 denotes startup unavailable before deferred installation.
     * Keep the actual transactional state untouched and preserve its failures. */
    if(!downstream && InterlockedCompareExchange(&g_shadow_engine.bootstrap.startup_failure,0,0))
        downstream=-3;
    length=_snprintf(text,capacity,"SE10 %lu %lu %lu %ld %ld %ld %ld %ld %ld %u %u %u %u %u %u %lu %d %lu %lu %u %lu %lu %u %ld %u %u %u %u %u %u %u %u %ld\n",
        (unsigned long)GetCurrentProcessId(),(unsigned long)t->session,t->acknowledged,
        (long)(policy&SHADOW_POLICY_DISABLE_ALL),
        (long)InterlockedCompareExchange(&g_shadow_engine.policy_control.quality_ready,0,0),
        (long)downstream,
        (long)InterlockedCompareExchange(&g_shadow_engine.renderer.profile_state,0,0),
        (long)InterlockedCompareExchange(&g_shadow_engine.vehicle_diagnostics.state,0,0),
        (long)InterlockedCompareExchange(&g_shadow_engine.intersection.state,0,0),
        (unsigned)((policy&SHADOW_POLICY_LIMIT_MASK)>>SHADOW_POLICY_LIMIT_SHIFT),
        shadow_policy_resolution_from(policy),(unsigned)((world&SHADOW_WORLD_ENABLED)!=0),
        256U<<((unsigned)(world&SHADOW_WORLD_RESOLUTION_MASK)>>SHADOW_WORLD_RESOLUTION_SHIFT),
        w?w->serial:0U,w?w->notice:0U,t->settings_processed,t->settings_result,
        (unsigned long)settings.current.revision,(unsigned long)settings.current.revision,
        settings.load_reason,(unsigned long)t->settings_error,(unsigned long)settings.pending_sequence,
        shadow_policy_selected_limit_from(policy),
        (long)InterlockedCompareExchange(&g_shadow_engine.vehicle_diagnostics.driver_proof_ready,0,0),
        vehicle_selection_extra_slots(),population?population->serial:0U,
        population?population->notice:0U,population?population->report_partial:0U,
        population?population->marked:0U,POPULATION_WINDOW_MS,POPULATION_SAMPLE_MS,
        population_elapsed_ms(),
        (long)InterlockedCompareExchange(&g_shadow_engine.bootstrap.host_compatibility,0,0));
    if(length<=0 || (size_t)length>=capacity) { if(capacity) text[0]=0; return 0; }
    return 1;
}

static DWORD WINAPI internal_tool_worker(void *unused)
{
    InternalToolState *t=&g_shadow_engine.internal_tools;
    wchar_t request[MAX_PATH],status[MAX_PATH],temporary[MAX_PATH];
    char previous[384]={0};
    (void)unused;
    if(!internal_tool_path(request,L"ShadowEngineDiagnostics.request") ||
       !internal_tool_path(status,L"ShadowEngineDiagnostics.status") ||
       !internal_tool_path(temporary,L"ShadowEngineDiagnostics.status.tmp")) return 0;
    t->session=GetTickCount();
    if(!t->session) t->session=1;
    for(;;) {
        detail_window_poll();
        population_poll();
        periodic_summary_drain();
        maybe_log_external_slice_summary();
        completion_diagnostic_log_summary();
        workload_poll();
        support_report_poll();
        char text[384];
        HANDLE file=CreateFileW(request,GENERIC_READ,0,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
        if(file!=INVALID_HANDLE_VALUE) {
            DWORD bytes=0;
            int read=ReadFile(file,text,INTERNAL_TOOL_REQUEST_BYTES,&bytes,NULL)!=0;
            CloseHandle(file);
            /* Lua serializes one request until acknowledgment. Oversized or
             * stale input is discarded without policy changes or repeated logs. */
            if(read && bytes<INTERNAL_TOOL_REQUEST_BYTES) {
                text[bytes]=0;
                if(!memchr(text,0,bytes)) internal_tool_accept(text);
            }
            DeleteFileW(request);
        }
        if(internal_tool_status_text(text,sizeof(text)) && strcmp(text,previous)) {
            DWORD written=0,length=(DWORD)strlen(text);
            int ok=0;
            file=CreateFileW(temporary,GENERIC_WRITE,0,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);
            if(file!=INVALID_HANDLE_VALUE) {
                ok=WriteFile(file,text,length,&written,NULL) && written==length;
                CloseHandle(file);
            }
            if(ok && MoveFileExW(temporary,status,MOVEFILE_REPLACE_EXISTING))
                strcpy(previous,text);
        }
        Sleep(INTERNAL_TOOL_POLL_MS);
    }
}

static void start_internal_tools(void)
{
    HMODULE pinned=NULL;
    HANDLE thread;
    if(InterlockedCompareExchange(&g_shadow_engine.internal_tools.started,1,0)) return;
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
        (LPCWSTR)(uintptr_t)internal_tool_worker,&pinned)) {
        append_log("STAGE_INTERNAL_TOOLS unavailable=moduleLifetimePinFailed");
        return;
    }
    thread=CreateThread(NULL,0,internal_tool_worker,NULL,0,NULL);
    if(!thread) { append_log("STAGE_INTERNAL_TOOLS unavailable=workerFailed"); return; }
    CloseHandle(thread);
}
