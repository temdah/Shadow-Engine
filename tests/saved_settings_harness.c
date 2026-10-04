/* Actual unity settings owner; Windows files only under this executable's
 * fixture directory. No bootstrap/game work and no real background thread. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wchar.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
static unsigned checks,opens,writes,flushes,replaces;
static int fault;
static int population_clock_fixture;
static LARGE_INTEGER population_clock_value;
static BOOL WINAPI test_qpc(LARGE_INTEGER *value)
{
    if(population_clock_fixture) { *value=population_clock_value; return TRUE; }
    return QueryPerformanceCounter(value);
}
static HANDLE settings_file;
static HANDLE WINAPI test_create_file(LPCWSTR path,DWORD access,DWORD share,
    LPSECURITY_ATTRIBUTES security,DWORD disposition,DWORD flags,HANDLE template_file)
{
    int target=wcsstr(path,L"ShadowEngineSettings.cfg")!=NULL;
    if(target && access==GENERIC_WRITE) {
        ++opens;
        if(fault==1) { SetLastError(ERROR_ACCESS_DENIED); return INVALID_HANDLE_VALUE; }
    }
    HANDLE file=CreateFileW(path,access,share,security,disposition,flags,template_file);
    if(target) settings_file=file;
    return file;
}
static BOOL WINAPI test_write(HANDLE file,LPCVOID data,DWORD bytes,LPDWORD written,LPOVERLAPPED overlap)
{
    if(file==settings_file) {
        ++writes;
        if(fault==2) { SetLastError(ERROR_WRITE_FAULT); return FALSE; }
        if(fault==3) return WriteFile(file,data,bytes-1U,written,overlap);
    }
    return WriteFile(file,data,bytes,written,overlap);
}
static BOOL WINAPI test_flush(HANDLE file)
{
    if(file==settings_file) {
        ++flushes;
        if(fault==4) { SetLastError(ERROR_WRITE_FAULT); return FALSE; }
    }
    return FlushFileBuffers(file);
}
static BOOL WINAPI test_close(HANDLE file)
{
    BOOL result=CloseHandle(file);
    if(file==settings_file) {
        settings_file=NULL;
        if(fault==5 || fault==8) { SetLastError(ERROR_WRITE_FAULT); return FALSE; }
    }
    return result;
}
static BOOL WINAPI test_move(LPCWSTR from,LPCWSTR to,DWORD flags)
{
    if(wcsstr(to,L"ShadowEngineSettings.cfg")) {
        ++replaces;
        if(fault==6) { SetLastError(ERROR_ACCESS_DENIED); return FALSE; }
    }
    return MoveFileExW(from,to,flags);
}
static BOOL WINAPI test_read(HANDLE file,LPVOID data,DWORD bytes,LPDWORD read,LPOVERLAPPED overlap)
{
    if(file==settings_file && fault==7) { SetLastError(ERROR_READ_FAULT); return FALSE; }
    return ReadFile(file,data,bytes,read,overlap);
}
static HANDLE WINAPI test_thread(LPSECURITY_ATTRIBUTES security,SIZE_T stack,
    LPTHREAD_START_ROUTINE fn,LPVOID arg,DWORD flags,LPDWORD id)
{
    (void)security;(void)stack;(void)fn;(void)arg;(void)flags;(void)id;
    if(fault==9) return NULL;
    return CreateEventW(NULL,FALSE,FALSE,NULL);
}
static BOOL WINAPI test_pin(DWORD flags,LPCWSTR name,HMODULE *module)
{
    (void)flags;(void)name;
    if(fault==10) return FALSE;
    *module=(HMODULE)1; return TRUE;
}
static HANDLE WINAPI test_event(LPSECURITY_ATTRIBUTES security,BOOL manual,BOOL initial,LPCWSTR name)
{
    if(fault==11) return NULL;
    return CreateEventW(security,manual,initial,name);
}
static BOOL WINAPI test_signal(HANDLE event)
{
    if(fault==12) return FALSE;
    return SetEvent(event);
}
static DWORD WINAPI test_wait(HANDLE event,DWORD milliseconds)
{
    if(fault==13) { SetLastError(ERROR_INVALID_HANDLE); return WAIT_FAILED; }
    return WaitForSingleObject(event,milliseconds);
}
#define CreateFileW test_create_file
#define WriteFile test_write
#define ReadFile test_read
#define FlushFileBuffers test_flush
#define CloseHandle test_close
#define MoveFileExW test_move
#define CreateThread test_thread
#define GetModuleHandleExW test_pin
#define CreateEventW test_event
#define SetEvent test_signal
#define WaitForSingleObject test_wait
#define QueryPerformanceCounter test_qpc
#include "../src/shadow_engine_patch.c"
#undef QueryPerformanceCounter
#undef CreateFileW
#undef WriteFile
#undef ReadFile
#undef FlushFileBuffers
#undef CloseHandle
#undef MoveFileExW
#undef CreateThread
#undef GetModuleHandleExW
#undef CreateEventW
#undef SetEvent
#undef WaitForSingleObject
#define CHECK(x) do { ++checks; assert(x); } while(0)
static wchar_t directory[MAX_PATH],target[MAX_PATH];
static DWORD sequence;
static void reset_owner(void)
{
    SavedSettingsState *s=&g_shadow_engine.saved_settings;
    if(s->event) CHECK(CloseHandle(s->event));
    memset(s,0,sizeof(*s));
    memset(&g_shadow_engine.internal_tools,0,sizeof(g_shadow_engine.internal_tools));
    memset(&g_shadow_engine.policy_control,0,sizeof(g_shadow_engine.policy_control));
    saved_settings_initialize(directory);
    CHECK(s->initialized==2);
}
static void raw_file(const char *text,DWORD bytes)
{
    DWORD written=0;
    HANDLE file=CreateFileW(target,GENERIC_WRITE,0,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);
    CHECK(file!=INVALID_HANDLE_VALUE); CHECK(WriteFile(file,text,bytes,&written,NULL));
    CHECK(written==bytes); CHECK(CloseHandle(file));
}
static DWORD disk_copy(char *out)
{
    DWORD bytes=0;
    HANDLE file=CreateFileW(target,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
    CHECK(file!=INVALID_HANDLE_VALUE); CHECK(ReadFile(file,out,128U,&bytes,NULL)); CHECK(CloseHandle(file));
    return bytes;
}
static SavedSettingsRequest change(unsigned mask,unsigned limit,unsigned vehicle,unsigned world)
{
    SavedSettingsRequest r={0};
    r.mask=mask;r.limit=limit;r.vehicle_quality=vehicle;r.world_quality=world;r.sequence=++sequence;
    return r;
}
static void submit_and_pump(SavedSettingsRequest *r,int expected)
{
    SavedSettingsStatus status;
    CHECK(saved_settings_submit(r)==0); CHECK(saved_settings_process_pending());
    CHECK(saved_settings_status(&status)); CHECK(status.processed_sequence==r->sequence);
    CHECK(status.result==expected); CHECK(!status.pending_sequence);
}
static void test_parser(void)
{
    SavedSettingsRecord r;
    char good[128],bad[128];
    int length=_snprintf(good,sizeof(good),"SESAVE1 1 %lu 768\n",(unsigned long)saved_settings_defaults().policy);
    CHECK(saved_settings_parse(good,(DWORD)length,&r));
    CHECK(r.revision==1 && r.world==768);
    for(int i=0;i<length;++i) CHECK(!saved_settings_parse(good,(DWORD)i,&r));
    const char *invalid[]={"SESAVE4 1 197632 768\n","SESAVE1 0 197632 768\n",
        "SESAVE1 4294967296 197632 768\n","SESAVE1 -1 197632 768\n",
        "SESAVE1 1 0 768\n","SESAVE1 1 197632 0\n","SESAVE1 1 197632 769 extra\n",
        "SESAVE1 1 197632 768\r\n","SESAVE1 1 197632 768\nX"};
    for(unsigned i=0;i<ARRAY_COUNT(invalid);++i)
        CHECK(!saved_settings_parse(invalid[i],(DWORD)strlen(invalid[i]),&r));
    memcpy(bad,good,(size_t)length); bad[9]=0;
    CHECK(!saved_settings_parse(bad,(DWORD)length,&r));
    memset(bad,'9',sizeof(bad)); CHECK(!saved_settings_parse(bad,sizeof(bad),&r));
}
static void test_limit_formats_and_choices(void)
{
    SavedSettingsRecord old,next,parsed;
    SavedSettingsRequest r;
    char text[128];
    unsigned disabled,limit,vehicle,world,world_enabled,quality_disabled;
    int length;
    /* Every historical valid policy retains its quality and revision. Only
     * active10 changes semantics; a disabled retained10 is still selected0. */
    for(limit=1;limit<=10;++limit) for(disabled=0;disabled<=1;++disabled)
    for(vehicle=1;vehicle<=4;++vehicle) for(world=1;world<=4;++world)
    for(world_enabled=0;world_enabled<=1;++world_enabled)
    for(quality_disabled=0;quality_disabled<=1;++quality_disabled) {
        old.policy=((LONG)limit<<SHADOW_POLICY_LIMIT_SHIFT)|
            ((LONG)vehicle<<SHADOW_POLICY_RESOLUTION_SHIFT)|
            (disabled?SHADOW_POLICY_DISABLE_LIMITER:0)|
            (quality_disabled?SHADOW_POLICY_DISABLE_QUALITY:0);
        old.world=((LONG)world<<SHADOW_WORLD_RESOLUTION_SHIFT)|(LONG)world_enabled;
        old.revision=73;
        length=_snprintf(text,sizeof(text),"SESAVE1 73 %lu %lu\n",
            (unsigned long)old.policy,(unsigned long)old.world);
        CHECK(saved_settings_parse(text,(DWORD)length,&parsed));
        CHECK(parsed.revision==old.revision && parsed.world==old.world);
        CHECK(shadow_policy_selected_limit_from(parsed.policy)==(disabled?0U:limit));
        CHECK(parsed.policy==(old.policy|(!disabled && limit==10U?
            SHADOW_POLICY_TEN_UNLIMITED|SHADOW_POLICY_DISABLE_LIMITER:0)));
        length=_snprintf(text,sizeof(text),"SESAVE2 73 %lu %lu\n",
            (unsigned long)parsed.policy,(unsigned long)parsed.world);
        CHECK(saved_settings_parse(text,(DWORD)length,&next));
        CHECK(!memcmp(&parsed,&next,sizeof(parsed)));
    }
    {
        const char *invalid[]={
            "SESAVE1 1 199173 768\n", /* New marker is not a version1 bit. */
            "SESAVE2 1 199168 768\n", /* Active finite10 is noncanonical. */
            "SESAVE2 1 199172 768\n", /* Marker without bypass. */
            "SESAVE2 1 197637 768\n", /* Marker with retained4. */
            "SESAVE2 1 197640 768\n", /* Unknown policy bit3. */
            "SESAVE2 0 199173 768\n"};
        for(unsigned i=0;i<ARRAY_COUNT(invalid);++i)
            CHECK(!saved_settings_parse(invalid[i],(DWORD)strlen(invalid[i]),&parsed));
    }
    old=saved_settings_defaults();
    for(limit=0;limit<=10;++limit) for(vehicle=0;vehicle<=4;++vehicle)
    for(world=0;world<=4;++world) {
        r=change(7,limit,vehicle,world);
        CHECK(saved_settings_merge(&old,&r,&next));
        CHECK(shadow_policy_selected_limit_from(next.policy)==limit);
        CHECK(((next.policy&SHADOW_POLICY_DISABLE_LIMITER)!=0)==(limit==0 || limit==10));
        CHECK(((next.policy&SHADOW_POLICY_TEN_UNLIMITED)!=0)==(limit==10));
        CHECK((next.policy&SHADOW_POLICY_RESOLUTION_MASK)==
            (vehicle?(LONG)vehicle<<SHADOW_POLICY_RESOLUTION_SHIFT:old.policy&SHADOW_POLICY_RESOLUTION_MASK));
        CHECK(((next.policy&SHADOW_POLICY_DISABLE_QUALITY)!=0)==!vehicle);
        CHECK(next.world==(world?((LONG)world<<SHADOW_WORLD_RESOLUTION_SHIFT)|SHADOW_WORLD_ENABLED:old.world));
        r=change(1,0,0,0); parsed=next;
        CHECK(saved_settings_merge(&parsed,&r,&next));
        CHECK(shadow_policy_selected_limit_from(next.policy)==0 && !(next.policy&SHADOW_POLICY_TEN_UNLIMITED));
        CHECK((next.policy&SHADOW_POLICY_LIMIT_MASK)==(parsed.policy&SHADOW_POLICY_LIMIT_MASK));
    }
}
static void test_migration_persistence(void)
{
    SavedSettingsState *s=&g_shadow_engine.saved_settings;
    SavedSettingsRequest r;
    char original[128],after[128];
    unsigned before_opens;
    DWORD bytes,after_bytes;
    SavedSettingsRecord migrated;
    const char *active="SESAVE1 7 199168 768\n";
    raw_file(active,(DWORD)strlen(active)); bytes=disk_copy(original); before_opens=opens;
    reset_owner(); migrated=s->current;
    CHECK(s->load_reason==0 && s->current.revision==7);
    CHECK(shadow_policy_selected_limit()==10 && !shadow_policy_enabled(SHADOW_POLICY_DISABLE_LIMITER));
    CHECK(shadow_policy_stable_limit()==10 && opens==before_opens);
    after_bytes=disk_copy(after); CHECK(after_bytes==bytes && !memcmp(original,after,bytes));
    /* No implicit rewrite merely to change the format, even on a no-op edit. */
    r=change(1,10,0,0); submit_and_pump(&r,2);
    CHECK(opens==before_opens && s->current.revision==7);
    CHECK(!ShadowEngine_SetVehicleHeadlightLimiterEnabled(1) && !s->busy);
    CHECK(s->current.policy==migrated.policy);
    /* The first real edit uses the existing save-before-publication transaction. */
    for(int f=1;f<=6;++f) {
        fault=f; r=change(2,0,4,0); submit_and_pump(&r,-3); fault=0;
        CHECK(!memcmp(&s->current,&migrated,sizeof(migrated)));
        CHECK(shadow_policy_selected_limit()==10);
        after_bytes=disk_copy(after); CHECK(after_bytes==bytes && !memcmp(original,after,bytes));
    }
    r=change(2,0,4,0); submit_and_pump(&r,1);
    CHECK(shadow_policy_selected_limit()==10 && shadow_policy_resolution()==4096);
    after_bytes=disk_copy(after); CHECK(after_bytes>8 && !memcmp(after,"SESAVE2 ",8));
    reset_owner(); CHECK(s->current.revision==8 && shadow_policy_selected_limit()==10);
    CHECK(ShadowEngine_SetVehicleHeadlightLimiterEnabled(0)); CHECK(saved_settings_process_pending());
    CHECK(shadow_policy_selected_limit()==0 && shadow_policy_stable_limit()==10);
    CHECK(!ShadowEngine_SetVehicleHeadlightLimiterEnabled(1));
    r=change(1,9,0,0); submit_and_pump(&r,1);
    CHECK(shadow_policy_selected_limit()==9 && shadow_policy_enabled(SHADOW_POLICY_DISABLE_LIMITER));
    CHECK(shadow_policy_set_suite_settings(10,0)); CHECK(saved_settings_process_pending());
    CHECK(shadow_policy_selected_limit()==10);
    CHECK(shadow_policy_set_comparison(0,1,1)); CHECK(saved_settings_process_pending());
    CHECK(shadow_policy_selected_limit()==0 && shadow_policy_stable_limit()==10 && shadow_policy_resolution()==512);
    active="SESAVE1 11 199169 768\n";
    raw_file(active,(DWORD)strlen(active)); before_opens=opens;
    reset_owner(); CHECK(shadow_policy_selected_limit()==0 && shadow_policy_stable_limit()==10);
    CHECK(opens==before_opens && s->current.revision==11 && !(s->current.policy&SHADOW_POLICY_TEN_UNLIMITED));
    r=change(1,10,0,0); submit_and_pump(&r,1);
    CHECK(shadow_policy_selected_limit()==10 && s->current.revision==12);
    CHECK(DeleteFileW(target)); reset_owner();
}
static void test_owner(void)
{
    SavedSettingsState *s=&g_shadow_engine.saved_settings;
    SavedSettingsRequest r;
    SavedSettingsRecord before,restored;
    char old_disk[128],new_disk[128];
    DWORD old_bytes,new_bytes;
    unsigned old_opens;
    CHECK(s->load_reason==1 && s->worker_ready && !s->current.revision);
    CHECK(shadow_policy_stable_limit()==4 && shadow_policy_resolution()==2048 && !shadow_policy_world_enabled());
    CHECK(!g_shadow_engine.policy_control.quality_ready);
    r=change(7,0,4,4); before=s->current;
    CHECK(saved_settings_submit(&r)==0);
    CHECK(s->current.policy==before.policy && g_shadow_engine.policy_control.disabled==before.policy);
    CHECK(saved_settings_submit(&r)==-2); CHECK(saved_settings_process_pending());
    CHECK(s->result==1 && s->current.revision==1);
    CHECK(!shadow_policy_enabled(SHADOW_POLICY_DISABLE_LIMITER) && shadow_policy_resolution()==4096 && shadow_policy_world_enabled());
    old_bytes=disk_copy(old_disk); CHECK(saved_settings_parse(old_disk,old_bytes,&restored));
    CHECK(restored.policy==s->current.policy && restored.world==s->current.world);
    old_opens=opens; g_shadow_engine.vehicle_diagnostics.residency_reset_requested=0;
    r=change(7,0,4,4); submit_and_pump(&r,2);
    CHECK(opens==old_opens && !g_shadow_engine.vehicle_diagnostics.residency_reset_requested && s->current.revision==1);
    r=change(2,10,1,0); submit_and_pump(&r,1);
    CHECK(!shadow_policy_enabled(SHADOW_POLICY_DISABLE_LIMITER) && shadow_policy_world_resolution()==4096 && shadow_policy_world_enabled());
    CHECK(shadow_policy_resolution()==512);
    r=change(6,10,0,0); submit_and_pump(&r,1);
    CHECK(!shadow_policy_enabled(SHADOW_POLICY_DISABLE_QUALITY) && !shadow_policy_world_enabled());
    CHECK(shadow_policy_resolution()==512 && shadow_policy_world_resolution()==4096);
    before=s->current; old_bytes=disk_copy(old_disk);
    for(int f=1;f<=6;++f) {
        fault=f; r=change(2,0,3,0); submit_and_pump(&r,-3); fault=0;
        CHECK(s->current.policy==before.policy && s->current.world==before.world && s->current.revision==before.revision);
        CHECK(g_shadow_engine.policy_control.disabled==before.policy && !g_shadow_engine.policy_control.quality_ready);
        CHECK(GetFileAttributesW(s->temporary)==INVALID_FILE_ATTRIBUTES);
        new_bytes=disk_copy(new_disk); CHECK(new_bytes==old_bytes && !memcmp(new_disk,old_disk,old_bytes));
    }
    r=change(2,0,3,0); submit_and_pump(&r,1); restored=s->current;
    reset_owner(); CHECK(s->load_reason==0 && s->current.revision==restored.revision);
    CHECK(s->current.policy==restored.policy && s->current.world==restored.world);
    CHECK(g_shadow_engine.policy_control.disabled==restored.policy && !g_shadow_engine.policy_control.quality_ready);
    r=change(2,0,99,0); CHECK(saved_settings_submit(&r)==-1);
    r=change(0,0,0,0); CHECK(saved_settings_submit(&r)==-1);
    r=change(1,11,0,0); CHECK(saved_settings_submit(&r)==-1);
    s->lock=1; r=change(1,8,0,0); CHECK(saved_settings_submit(&r)==-2); s->lock=0;
    fault=12; CHECK(saved_settings_submit(&r)==-4); fault=0; CHECK(!s->busy && !s->worker_ready);
    reset_owner();
    r=change(1,8,0,0); before=s->current;
    CHECK(saved_settings_submit(&r)==0); fault=13;
    CHECK(saved_settings_worker(NULL)==0); fault=0;
    CHECK(!s->busy && !s->worker_ready && s->processed_sequence==r.sequence && s->result==-4);
    CHECK(s->error==ERROR_INVALID_HANDLE && s->current.policy==before.policy);
    for(int f=7;f<=11;++f) {
        fault=f; reset_owner(); fault=0;
        if(f<=8) CHECK(s->load_reason==3 && !s->current.revision);
        else CHECK(!s->worker_ready && !s->event);
        if(f>=9) CHECK(saved_settings_submit(&r)==-4);
    }
    raw_file("broken\n",7); reset_owner(); CHECK(s->load_reason==2 && !s->current.revision);
    new_bytes=disk_copy(new_disk); CHECK(new_bytes==7 && !memcmp(new_disk,"broken\n",7));
    CHECK(DeleteFileW(target)); reset_owner();
}
#if SHADOW_ENGINE_INTERNAL_DIAGNOSTICS
static void request(const char *protocol,DWORD session,DWORD serial,const char *body,int accepted)
{
    char text[128];
    _snprintf(text,sizeof(text),"%s %lu %lu %lu %s\n",protocol,(unsigned long)GetCurrentProcessId(),
        (unsigned long)session,(unsigned long)serial,body);
    CHECK(internal_tool_accept(text)==accepted);
}
static void test_transport(void)
{
    InternalToolState *t=&g_shadow_engine.internal_tools;
    SavedSettingsState *s=&g_shadow_engine.saved_settings;
    char status[384];
    t->session=77;
    request("SES2",76,1,"7 0 4 4",0); CHECK(!s->busy && !t->acknowledged);
    request("SES2",77,1,"7 0 4 4",1); CHECK(s->busy && t->acknowledged==1 && !t->settings_processed);
    CHECK(internal_tool_status_text(status,sizeof(status))); CHECK(strstr(status,"SE10 ")==status);
    request("SES2",77,1,"7 10 1 1",0);
    request("SES2",77,2,"2 10 2 0",1); CHECK(t->settings_processed==2 && t->settings_result==-2);
    CHECK(saved_settings_process_pending()); CHECK(internal_tool_status_text(status,sizeof(status)));
    CHECK(t->settings_processed==2 && t->settings_result==-2); /* older completion never overwrites rejection */
    request("SES2",77,3,"2 10 2 0",1); CHECK(saved_settings_process_pending());
    CHECK(internal_tool_status_text(status,sizeof(status))); CHECK(t->settings_processed==3 && t->settings_result==1);
    CHECK(!shadow_policy_enabled(1) && shadow_policy_world_resolution()==4096);
    request("SES2",77,4,"2 0 4294967296 0",1); CHECK(t->settings_processed==4 && t->settings_result==-1);
    request("SES2",77,5,"2 0 5 0",1); CHECK(t->settings_processed==5 && t->settings_result==-1);
    request("SEW2",77,6,"1 1",1); CHECK(saved_settings_process_pending()); CHECK(shadow_policy_world_resolution()==512);
    request("SES2",77,7,"3 4 3 0",1); CHECK(saved_settings_process_pending()); CHECK(shadow_policy_stable_limit()==4 && shadow_policy_resolution()==2048);
    request("SES2",77,8,"3 4 3 0",1); CHECK(saved_settings_process_pending());
    CHECK(shadow_policy_world_enabled() && shadow_policy_world_resolution()==512);
    CHECK(internal_tool_status_text(status,sizeof(status))); CHECK(!internal_tool_status_text(status,8U));
    fault=4; request("SES2",77,9,"4 10 1 4",1);
    CHECK(saved_settings_process_pending()); fault=0;
    CHECK(internal_tool_status_text(status,sizeof(status)));
    CHECK(t->settings_processed==9 && t->settings_result==-3 && t->settings_error==ERROR_WRITE_FAULT);
    CHECK(shadow_policy_world_enabled() && shadow_policy_world_resolution()==512);
    request("SES2",77,10,"4 10 1 4",1); CHECK(saved_settings_process_pending());
    CHECK(internal_tool_status_text(status,sizeof(status)));
    CHECK(t->settings_processed==10 && t->settings_result==1 && !t->settings_error && shadow_policy_world_enabled());
    {
        DWORD fields[33];
        CHECK(internal_tool_values(status+5,fields,33));
        CHECK(fields[0]==GetCurrentProcessId() && fields[1]==77 && fields[2]==10);
        CHECK(fields[9]==4 && fields[10]==2048 && fields[11]==1 && fields[12]==4096);
        CHECK(fields[15]==10 && fields[16]==1 && fields[17]==s->current.revision && fields[18]==fields[17]);
        CHECK(fields[20]==0 && fields[21]==0);
        CHECK(fields[22]==4 && fields[23]==0 && fields[24]==2);
        CHECK(!fields[25] && !fields[26] && !fields[27] && !fields[28]);
        CHECK(fields[29]==60000U && fields[30]==50U && !fields[31]);
    }
    s->worker_ready=0;
    request("SES2",77,11,"1 0 0 0",1);
    CHECK(t->settings_processed==11 && t->settings_result==-4 && !s->busy);
    s->worker_ready=1;
    CHECK(ShadowEngine_GetControlApiVersion()==2);
    CHECK(ShadowEngine_SetVehicleHeadlightLimiterEnabled(0)); CHECK(ShadowEngine_GetVehicleHeadlightLimiterEnabled());
    CHECK(saved_settings_process_pending()); CHECK(!ShadowEngine_GetVehicleHeadlightLimiterEnabled());
    {
        DWORD serial=12,fields[33];
        SavedSettingsRecord before;
        unsigned old_opens;
        char action_text[16];
        request("SES2",77,serial++,"1 10 0 0",1); CHECK(saved_settings_process_pending());
        CHECK(shadow_policy_selected_limit()==10 && !ShadowEngine_GetVehicleHeadlightLimiterEnabled());
        CHECK(internal_tool_status_text(status,sizeof(status)));
        CHECK(internal_tool_values(status+5,fields,33));
        CHECK(fields[3]%2==1 && fields[9]==10 && fields[22]==10 && fields[23]==0 && fields[24]==2);
        g_shadow_engine.vehicle_diagnostics.driver_proof_ready=1;
        CHECK(internal_tool_status_text(status,sizeof(status)));
        CHECK(internal_tool_values(status+5,fields,33) && fields[23]==1 && fields[24]==2);
        g_shadow_engine.vehicle_diagnostics.driver_proof_ready=0;
        before=s->current; old_opens=opens;
        request("SES2",77,serial-1,"1 4 0 0",0);
        CHECK(!s->busy && !memcmp(&before,&s->current,sizeof(before)));
        /* Obsolete limiter commands are acknowledged as terminal rejections,
         * so old UI labels cannot change the new meaning of10. */
        request("SES1",77,serial++,"1 10 0 0",1);
        CHECK(t->settings_result==-1 && !s->busy);
        request("SES1",77,serial++,"7 4 1 1",1);
        CHECK(t->settings_result==-1 && !s->busy);
        request("SEV1",77,serial++,"4 1 3",1);
        CHECK(t->settings_result==-1 && !s->busy);
        for(unsigned action=1;action<=187;++action) {
            if(!(action==1 || action==2 || action==5 || action==6 ||
                 (action>=20 && action<=30) || (action>=40 && action<=61) ||
                 (action>=100 && action<=187))) continue;
            _snprintf(action_text,sizeof(action_text),"%u",action);
            request("SE1",77,serial++,action_text,1);
            CHECK(t->settings_result==-1 && !s->busy);
        }
        CHECK(opens==old_opens && !memcmp(&before,&s->current,sizeof(before)));
        /* The compatibility quality-only route does not carry limiter intent. */
        request("SES1",77,serial++,"2 4 1 0",1); CHECK(saved_settings_process_pending());
        CHECK(shadow_policy_selected_limit()==10 && shadow_policy_resolution()==512);
        request("SES2",77,serial++,"1 0 0 0",1); CHECK(saved_settings_process_pending());
        CHECK(internal_tool_status_text(status,sizeof(status)));
        CHECK(internal_tool_values(status+5,fields,33));
        CHECK(fields[3]%2==1 && fields[9]==10 && fields[22]==0 && fields[23]==0 && fields[24]==2);
        request("SES2",77,serial++,"1 4 0 0",1); CHECK(saved_settings_process_pending());
        request("SES2",77,serial++,"2 0 3 0",1); CHECK(saved_settings_process_pending());
        CHECK(shadow_policy_selected_limit()==4 && shadow_policy_resolution()==2048);
    }
    {
        unsigned long pid,session,ack;
        long disabled,ready,downstream;
        LONG states[3]={0,2,-1};
        for(unsigned reason=1;reason<=6;++reason) {
            g_shadow_engine.bootstrap.startup_failure=(LONG)reason;
            for(unsigned i=0;i<3;++i) {
                g_shadow_engine.bootstrap.downstream_state=states[i];
                CHECK(internal_tool_status_text(status,sizeof(status)));
                CHECK(sscanf(status,"SE10 %lu %lu %lu %ld %ld %ld",
                    &pid,&session,&ack,&disabled,&ready,&downstream)==6);
                CHECK(downstream==(states[i]?states[i]:-3));
                CHECK(g_shadow_engine.bootstrap.downstream_state==states[i]);
            }
        }
        g_shadow_engine.bootstrap.startup_failure=0;
        g_shadow_engine.bootstrap.downstream_state=0;
        CHECK(internal_tool_status_text(status,sizeof(status)));
        CHECK(sscanf(status,"SE10 %lu %lu %lu %ld %ld %ld",
            &pid,&session,&ack,&disabled,&ready,&downstream)==6 && downstream==0);
    }
}

static void test_extra_slots_transport(void)
{
    InternalToolState *t=&g_shadow_engine.internal_tools;
    SavedSettingsState *s=&g_shadow_engine.saved_settings;
    VehicleLightDiagnosticState *d=&g_shadow_engine.vehicle_diagnostics;
    SavedSettingsRecord before=s->current;
    LONG policy=g_shadow_engine.policy_control.disabled;
    LONG world=g_shadow_engine.policy_control.world_configuration;
    unsigned old_opens=opens,old_writes=writes,old_flushes=flushes,old_replaces=replaces;
    DWORD serial=t->acknowledged+1U,fields[33],before_bytes,after_bytes;
    char status[384],before_disk[128],after_disk[128];
    CHECK(!s->busy && vehicle_selection_extra_slots()==2U);
    before_bytes=disk_copy(before_disk);
    d->residency_reset_requested=0;
    CHECK(!vehicle_selection_set_extra_slots(1U));
    CHECK(!vehicle_selection_set_extra_slots(3U));
    CHECK(!vehicle_selection_set_extra_slots(UINT32_MAX));
    CHECK(vehicle_selection_extra_slots()==2U && !d->residency_reset_requested);

    /* Session and replay gates apply before the temporary control can change. */
    request("SE1",t->session+1U,serial,"12",0);
    request("SE1",t->session,serial-1U,"12",0);
    CHECK(vehicle_selection_extra_slots()==2U && !d->residency_reset_requested);
    request("SE1",t->session,serial++,"12",1);
    CHECK(vehicle_selection_extra_slots()==0U &&
        d->residency_reset_requested==VEHICLE_RESIDENCY_RESET_EXTRAS);
    CHECK(t->acknowledged==serial-1U && t->settings_processed==serial-1U && t->settings_result==1);
    CHECK(internal_tool_status_text(status,sizeof(status)));
    CHECK(!strncmp(status,"SE10 ",5) && internal_tool_values(status+5,fields,33));
    CHECK(fields[24]==0U && fields[17]==before.revision && fields[18]==before.revision);

    d->residency_reset_requested=0;
    request("SE1",t->session,serial-1U,"13",0);
    request("SE1",t->session+1U,serial,"13",0);
    CHECK(vehicle_selection_extra_slots()==0U && !d->residency_reset_requested);
    request("SE1",t->session,serial++,"12",1);
    CHECK(vehicle_selection_extra_slots()==0U && !d->residency_reset_requested && t->settings_result==1);
    request("SE1",t->session,serial++,"13 extra",1);
    CHECK(vehicle_selection_extra_slots()==0U && !d->residency_reset_requested && t->settings_result==-1);
    request("SE1",t->session,serial++,"13",1);
    CHECK(vehicle_selection_extra_slots()==2U &&
        d->residency_reset_requested==VEHICLE_RESIDENCY_RESET_EXTRAS && t->settings_result==1);
    d->residency_reset_requested=0;
    request("SE1",t->session,serial++,"13",1);
    CHECK(vehicle_selection_extra_slots()==2U && !d->residency_reset_requested && t->settings_result==1);
    CHECK(internal_tool_status_text(status,sizeof(status)));
    CHECK(internal_tool_values(status+5,fields,33) && fields[24]==2U);

    /* An extras request cannot replace an already pending full policy reset. */
    d->residency_reset_requested=1L;
    request("SE1",t->session,serial++,"12",1);
    CHECK(vehicle_selection_extra_slots()==0U &&
        d->residency_reset_requested==(1L|VEHICLE_RESIDENCY_RESET_EXTRAS));
    request("SE1",t->session,serial++,"13",1);
    CHECK(vehicle_selection_extra_slots()==2U &&
        d->residency_reset_requested==(1L|VEHICLE_RESIDENCY_RESET_EXTRAS));
    d->residency_reset_requested=0;

    /* No settings work, revision, applied policy word or disk bytes changed. */
    CHECK(!s->busy && !memcmp(&before,&s->current,sizeof(before)));
    CHECK(g_shadow_engine.policy_control.disabled==policy &&
        g_shadow_engine.policy_control.world_configuration==world);
    CHECK(opens==old_opens && writes==old_writes && flushes==old_flushes && replaces==old_replaces);
    after_bytes=disk_copy(after_disk);
    CHECK(after_bytes==before_bytes && !memcmp(before_disk,after_disk,before_bytes));
}

static void test_population_status_transport(void)
{
    PopulationCaptureState *population;
    SavedSettingsRecord settings=g_shadow_engine.saved_settings.current;
    unsigned old_opens=opens,old_writes=writes,old_flushes=flushes,old_replaces=replaces;
    DWORD before[33],fields[33];
    char status[384];
    CHECK(!population_state());
    CHECK(internal_tool_status_text(status,sizeof(status)));
    CHECK(!strncmp(status,"SE10 ",5) && internal_tool_values(status+5,before,33));
    CHECK(!before[25] && !before[26] && !before[27] && !before[28]);
    CHECK(before[29]==60000U && before[30]==50U && !before[31]);
    /* Old fixed-width readers must reject the appended fields explicitly. */
    CHECK(!internal_tool_values(status+5,fields,25));
    CHECK(!internal_tool_values(status+5,fields,29));
    population=(PopulationCaptureState *)calloc(1,sizeof(*population));
    CHECK(population);
    g_shadow_engine.population=population;
    for(unsigned notice=0;notice<=6U;++notice) {
        population->serial=UINT32_MAX;
        population->notice=notice;
        population->report_partial=notice&1U;
        population->marked=(notice>>1U)&1U;
        CHECK(internal_tool_status_text(status,sizeof(status)));
        CHECK(!strncmp(status,"SE10 ",5) && internal_tool_values(status+5,fields,33));
        CHECK(!memcmp(before,fields,25U*sizeof(*fields)));
        CHECK(fields[25]==UINT32_MAX && fields[26]==notice);
        CHECK(fields[27]==(notice&1U) && fields[28]==((notice>>1U)&1U));
        CHECK(fields[29]==60000U && fields[30]==50U && !fields[31]);
    }
    {
        const DWORD elapsed_cases[]={0U,1U,49U,50U,59999U,60000U,61000U};
        population_clock_fixture=1; population->frequency=1000000;
        population->started=1000000; population->phase=1; population->notice=1;
        for(unsigned i=0;i<sizeof(elapsed_cases)/sizeof(elapsed_cases[0]);++i) {
            DWORD expected=elapsed_cases[i]>60000U?60000U:elapsed_cases[i];
            population_clock_value.QuadPart=population->started+(int64_t)elapsed_cases[i]*1000;
            CHECK(internal_tool_status_text(status,sizeof(status)));
            CHECK(internal_tool_values(status+5,fields,33));
            CHECK(!memcmp(before,fields,25U*sizeof(*fields)));
            CHECK(fields[29]==60000U && fields[30]==50U && fields[31]==expected);
        }
        population_clock_value.QuadPart=population->started-1;
        CHECK(internal_tool_status_text(status,sizeof(status)));
        CHECK(internal_tool_values(status+5,fields,33) && !fields[31]);
        population->elapsed_ms=12345U;
        population_clock_value.QuadPart=population->started+90000000;
        for(unsigned phase=0;phase<=2U;phase+=2U) {
            population->phase=phase;
            CHECK(internal_tool_status_text(status,sizeof(status)));
            CHECK(internal_tool_values(status+5,fields,33) && fields[31]==12345U);
        }
        population_clock_fixture=0;
    }
    g_shadow_engine.population=NULL;
    free(population);
    CHECK(!memcmp(&settings,&g_shadow_engine.saved_settings.current,sizeof(settings)));
    CHECK(opens==old_opens && writes==old_writes && flushes==old_flushes && replaces==old_replaces);
}
#endif
static void test_retired_background_migration(void)
{
    SavedSettingsRecord ordinary=saved_settings_defaults(),parsed,next;
    SavedSettingsRequest r={0};
    char text[128],disk[128];
    DWORD bytes;
    /* Exercise a non-default retained limit, disabled vehicle quality and an
     * enabled world cap. Legacy fields must not reset independent choices. */
    ordinary.policy=(7L<<SHADOW_POLICY_LIMIT_SHIFT)|(2L<<SHADOW_POLICY_RESOLUTION_SHIFT)|SHADOW_POLICY_DISABLE_QUALITY;
    ordinary.world=(4L<<SHADOW_WORLD_RESOLUTION_SHIFT)|SHADOW_WORLD_ENABLED;
    for(unsigned index=0;index<=3;++index) {
        for(unsigned version=1;version<=3;++version) {
            int n=_snprintf(text,sizeof(text),"SESAVE%u 17 %lu %lu\n",version,
                (unsigned long)(ordinary.policy|(index<<20U)),(unsigned long)ordinary.world);
            int expected=index==0U || (version==3U && index<=2U);
            CHECK(saved_settings_parse(text,n,&parsed)==expected);
            if(expected) CHECK(parsed.policy==ordinary.policy && parsed.world==ordinary.world && parsed.revision==17U);
        }
    }
    {
        int n=_snprintf(text,sizeof(text),"SESAVE3 17 %lu %lu\n",
            (unsigned long)(ordinary.policy|(1L<<22U)),(unsigned long)ordinary.world);
        CHECK(!saved_settings_parse(text,n,&parsed));
    }
    for(unsigned index=0;index<=2;++index) {
        unsigned before_opens=opens,before_writes=writes,before_flushes=flushes,before_replaces=replaces;
        int n=_snprintf(text,sizeof(text),"SESAVE3 17 %lu %lu\n",
            (unsigned long)(ordinary.policy|(index<<20U)),(unsigned long)ordinary.world);
        fault=0; raw_file(text,n); reset_owner();
        CHECK(g_shadow_engine.saved_settings.load_reason==0U);
        CHECK(g_shadow_engine.saved_settings.current.policy==ordinary.policy);
        CHECK(g_shadow_engine.saved_settings.current.world==ordinary.world);
        CHECK(g_shadow_engine.saved_settings.current.revision==17U);
        CHECK(g_shadow_engine.policy_control.disabled==ordinary.policy);
        CHECK(opens==before_opens && writes==before_writes && flushes==before_flushes && replaces==before_replaces);
        bytes=disk_copy(disk); CHECK(bytes==(DWORD)n && !memcmp(disk,text,bytes));
        r=change(32U,0,0,0);
        CHECK(!saved_settings_merge(&ordinary,&r,&next));
        CHECK(saved_settings_submit(&r)==-1);
        CHECK(!g_shadow_engine.saved_settings.busy && g_shadow_engine.saved_settings.current.revision==17U);
        r=change(SETTINGS_CHANGE_VEHICLE,0,4,0);
        submit_and_pump(&r,1);
        bytes=disk_copy(disk);
        CHECK(!memcmp(disk,"SESAVE2 ",8) && saved_settings_parse(disk,bytes,&parsed));
        CHECK(parsed.revision==18U && parsed.world==ordinary.world);
        CHECK((parsed.policy&SHADOW_POLICY_LIMIT_MASK)==(ordinary.policy&SHADOW_POLICY_LIMIT_MASK));
        reset_owner(); CHECK(g_shadow_engine.saved_settings.current.policy==parsed.policy);
    }
#if SHADOW_ENGINE_INTERNAL_DIAGNOSTICS
    g_shadow_engine.internal_tools.session=123U;
    _snprintf(text,sizeof(text),"SES3 %lu 123 1000 32 0 0 0 1\n",(unsigned long)GetCurrentProcessId());
    CHECK(internal_tool_accept(text) && g_shadow_engine.internal_tools.settings_result==-1);
    CHECK(!g_shadow_engine.saved_settings.busy);
    _snprintf(text,sizeof(text),"SES2 %lu 123 1001 32 0 0 0\n",(unsigned long)GetCurrentProcessId());
    CHECK(internal_tool_accept(text) && g_shadow_engine.internal_tools.settings_result==-1);
    CHECK(!g_shadow_engine.saved_settings.busy);
#endif
    printf("PASS retired settings migration: SESAVE3 preserves ordinary choices, drops only valid legacy bits, no startup rewrite, SESAVE2 on edit, retired requests rejected.\n");
}

int main(void)
{
    wchar_t *slash;
    CHECK(vehicle_selection_extra_slots()==2U); /* Internal process default and fixed Release behavior. */
    CHECK(GetModuleFileNameW(NULL,directory,MAX_PATH));
    slash=wcsrchr(directory,L'\\'); CHECK(slash); slash[1]=0;
    lstrcpyW(target,directory); lstrcatW(target,L"ShadowEngineSettings.cfg");
    DeleteFileW(target);
    test_parser(); test_limit_formats_and_choices(); reset_owner(); test_owner();
    test_migration_persistence();
#if SHADOW_ENGINE_INTERNAL_DIAGNOSTICS
    test_transport();
    test_extra_slots_transport();
    test_population_status_transport();
#endif
    test_retired_background_migration();
    {
        SavedSettingsState *s=&g_shadow_engine.saved_settings;
        SavedSettingsRequest r=change(2,0,1,0);
        unsigned old_opens=opens;
        s->current.revision=0xffffffffUL;
        submit_and_pump(&r,-3); CHECK(s->error==ERROR_ARITHMETIC_OVERFLOW && opens==old_opens);
        if(s->event) CHECK(CloseHandle(s->event));
        memset(s,0,sizeof(*s));
        wchar_t too_long[MAX_PATH];
        for(unsigned i=0;i<MAX_PATH-1U;++i) too_long[i]=L'x';
        too_long[MAX_PATH-1U]=0;
        saved_settings_initialize(too_long);
        CHECK(s->load_reason==4 && !s->worker_ready && !s->event && !s->path[0]);
        CHECK(saved_settings_submit(&r)==-4);
    }
    fault=0;
    if(g_shadow_engine.saved_settings.event) CHECK(CloseHandle(g_shadow_engine.saved_settings.event));
    DeleteFileW(target); DeleteFileW(g_shadow_engine.saved_settings.temporary);
    printf("PASS saved settings diagnostics=%d checks=%u writes=%u flushes=%u replacements=%u\n",
        SHADOW_ENGINE_INTERNAL_DIAGNOSTICS,checks,writes,flushes,replaces);
    return 0;
}
