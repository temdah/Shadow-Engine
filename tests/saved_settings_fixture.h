/* Existing renderer/selection fixtures inspect applied state synchronously.
 * Drive the real asynchronous settings owner explicitly between their steps;
 * the dedicated settings harness separately tests pending/failure behavior. */
static int fixture_settings_started;
static wchar_t fixture_settings_path[MAX_PATH],fixture_settings_temporary[MAX_PATH];
static HANDLE fixture_settings_event;
static void fixture_settings_cleanup(void)
{
    if(fixture_settings_started) {
        DeleteFileW(fixture_settings_path);
        DeleteFileW(fixture_settings_temporary);
        CloseHandle(fixture_settings_event);
    }
}
static void fixture_settings_prepare(void)
{
    SavedSettingsState *s=&g_shadow_engine.saved_settings;
    LONG policy=g_shadow_engine.policy_control.disabled;
    LONG world=g_shadow_engine.policy_control.world_configuration;
    if(!fixture_settings_started) {
        wchar_t *slash;
        wchar_t name[64];
        assert(GetModuleFileNameW(NULL,fixture_settings_path,MAX_PATH));
        slash=fixture_settings_path+lstrlenW(fixture_settings_path);
        while(slash>fixture_settings_path && slash[-1]!=L'\\' && slash[-1]!=L'/') --slash;
        *slash=0;
        _snwprintf(name,64,L"vehicle-settings-fixture-%lu.cfg",(unsigned long)GetCurrentProcessId());
        assert(lstrlenW(fixture_settings_path)+lstrlenW(name)+5<MAX_PATH);
        lstrcatW(fixture_settings_path,name);
        assert(GetFileAttributesW(fixture_settings_path)==INVALID_FILE_ATTRIBUTES);
        lstrcpyW(fixture_settings_temporary,fixture_settings_path);
        lstrcatW(fixture_settings_temporary,L".tmp");
        fixture_settings_event=CreateEventW(NULL,FALSE,FALSE,NULL);
        assert(fixture_settings_event);
        fixture_settings_started=1;
        atexit(fixture_settings_cleanup);
    }
    assert(!s->busy);
    s->event=fixture_settings_event; s->initialized=2; s->worker_ready=1;
    lstrcpyW(s->path,fixture_settings_path);
    lstrcpyW(s->temporary,fixture_settings_temporary);
    if(!(policy&SHADOW_POLICY_LIMIT_MASK)) policy|=4L<<SHADOW_POLICY_LIMIT_SHIFT;
    if(!(policy&SHADOW_POLICY_RESOLUTION_MASK)) policy|=3L<<SHADOW_POLICY_RESOLUTION_SHIFT;
    if(!(world&SHADOW_WORLD_RESOLUTION_MASK)) world|=3L<<SHADOW_WORLD_RESOLUTION_SHIFT;
    s->current.policy=policy; s->current.world=world;
    if(!s->current.revision) s->current.revision=1;
}
static void fixture_settings_finish(void)
{
    SavedSettingsState *s=&g_shadow_engine.saved_settings;
    if(s->busy) {
        SavedSettingsRecord expected;
        assert(saved_settings_merge(&s->current,&s->pending,&expected));
        assert(saved_settings_process_pending());
        assert(!s->busy && s->current.policy==expected.policy && s->current.world==expected.world);
    }
}
#define FIXTURE_SETTING_WRAPPER(name,params,args) \
    static int fixture_##name params { int result; fixture_settings_prepare(); \
        result=name args; fixture_settings_finish(); return result; }
FIXTURE_SETTING_WRAPPER(shadow_policy_set_disabled,(LONG mask,LONG value),(mask,value))
FIXTURE_SETTING_WRAPPER(shadow_policy_set_vehicle_limit,(unsigned value),(value))
FIXTURE_SETTING_WRAPPER(shadow_policy_set_suite_settings,(unsigned a,unsigned b),(a,b))
FIXTURE_SETTING_WRAPPER(shadow_policy_set_comparison,(unsigned a,unsigned b,unsigned c),(a,b,c))
FIXTURE_SETTING_WRAPPER(shadow_policy_set_world_comparison,(unsigned a,unsigned b),(a,b))
FIXTURE_SETTING_WRAPPER(ShadowEngine_SetVehicleHeadlightLimiterEnabled,(int enabled),(enabled))
FIXTURE_SETTING_WRAPPER(internal_tool_accept,(const char *text),(text))
static int fixture_internal_tool_dispatch(unsigned action)
{
    int result;
    fixture_settings_prepare(); result=internal_tool_dispatch(action,0);
    fixture_settings_finish(); return result>=0;
}
#define shadow_policy_set_disabled fixture_shadow_policy_set_disabled
#define shadow_policy_set_vehicle_limit fixture_shadow_policy_set_vehicle_limit
#define shadow_policy_set_suite_settings fixture_shadow_policy_set_suite_settings
#define shadow_policy_set_comparison fixture_shadow_policy_set_comparison
#define shadow_policy_set_world_comparison fixture_shadow_policy_set_world_comparison
#define ShadowEngine_SetVehicleHeadlightLimiterEnabled fixture_ShadowEngine_SetVehicleHeadlightLimiterEnabled
#define internal_tool_accept fixture_internal_tool_accept
#define internal_tool_dispatch fixture_internal_tool_dispatch
