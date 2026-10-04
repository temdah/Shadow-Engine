/* Actual-source offline ZIP and report sequencing gates. No game/ETW calls. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
/* A failed clock keeps successful request-path tests out of live ETW. */
static BOOL WINAPI support_fixture_frequency(LARGE_INTEGER *value)
{ value->QuadPart=0; return FALSE; }
#define QueryPerformanceFrequency support_fixture_frequency
#include "../src/shadow_engine_patch.c"
#undef QueryPerformanceFrequency
#include <assert.h>
#include <stdio.h>

static unsigned checks;
#define CHECK(x) do { ++checks; if(!(x)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); return 1; } } while(0)
static void fixture_file(const wchar_t *name,const char *text)
{
    wchar_t path[MAX_PATH];DWORD written;
    HANDLE file;
    assert(internal_tool_path(path,name));
    file=CreateFileW(path,GENERIC_WRITE,0,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);
    assert(file!=INVALID_HANDLE_VALUE);
    assert(WriteFile(file,text,(DWORD)strlen(text),&written,NULL) && written==strlen(text));
    assert(CloseHandle(file));
}
int main(int argc,char **argv)
{
    SupportReportState state={0};
    SupportZip *z;
    wchar_t path[MAX_PATH];
    char saved[256];
    unsigned char binary[65539];
    unsigned i;
    DWORD started=GetTickCount();
    CHECK(argc==2 && strlen(argv[1])+2<MAX_PATH);
    for(i=0;i<strlen(argv[1]);++i) g_shadow_engine.bootstrap.snapshot.bin_directory[i]=(unsigned char)argv[1][i];
    g_shadow_engine.bootstrap.snapshot.bin_directory[i++]=L'\\';
    g_shadow_engine.bootstrap.snapshot.bin_directory[i]=0;
    g_shadow_engine.bootstrap.session_log_state=2;
    g_shadow_engine.bootstrap.downstream_state=2;
    g_shadow_engine.internal_tools.session=42;
    g_shadow_engine.internal_tools.support_report=&state;
    fixture_file(L"ShadowEnginePatch.log","SESSION_BEGIN synthetic fixture\r\n");
    fixture_file(L"ShadowEngineSettings.cfg","fixture settings\n");
    state.phase=SUPPORT_WORKLOAD;
    CHECK(!support_report_request());
    CHECK(internal_tool_dispatch(7,1)==-1 && !g_shadow_engine.internal_tools.capture_requests);
    state.phase=SUPPORT_IDLE;
    g_shadow_engine.bootstrap.downstream_state=0;
    CHECK(!support_report_request());
    g_shadow_engine.bootstrap.downstream_state=2;
    CHECK(support_report_request());
    CHECK(state.phase==SUPPORT_WORKLOAD && state.gaps==SUPPORT_NO_WORKLOAD);
    support_report_poll();
    CHECK(state.phase==SUPPORT_OBSERVING && state.detail_started && !state.population_started);
    CHECK(state.gaps==(SUPPORT_NO_WORKLOAD|SUPPORT_NO_POPULATION|SUPPORT_BAD_INTERSECTION));
    CHECK(!support_report_request());
    detail_state()->phase=DETAIL_WINDOW_FROZEN; detail_state()->report_pending=0;
    state.stage_started=GetTickCount()-60001U;
    support_report_poll();
    CHECK(state.phase==SUPPORT_VEHICLE && (g_shadow_engine.internal_tools.capture_requests&SHADOW_CAPTURE_VEHICLE));
    g_shadow_engine.vehicle_diagnostics.serial=1;
    support_report_poll();
    CHECK(state.phase==SUPPORT_PARTIAL && !state.error);
    CHECK(!g_shadow_engine.internal_tools.capture_requests);
    strcpy(saved,state.previous_status); Sleep(5); support_report_poll();
    CHECK(!strcmp(saved,state.previous_status)); /* Terminal status stops ticking/writing. */
    printf("ARTIFACT %s\n",state.archive_name);

    /* A completed archive is immutable, including a colliding request name. */
    support_report_package(&state);
    CHECK(state.phase==SUPPORT_FAILED && state.error==ERROR_ALREADY_EXISTS);

    /* A diagnostic writer still owns its buffer; no new run can reset it. */
    g_shadow_engine.intersection.state=4;
    CHECK(!support_report_request());
    g_shadow_engine.intersection.state=0;

    /* Queued detail arms retain their exact next epoch across contention. */
    memset(&state,0,sizeof(state));state.serial=5;state.started=GetTickCount();
    detail_state()->lock=1;
    support_report_observe(&state);
    CHECK(state.detail_started && state.detail_epoch==detail_state()->epoch+1);
    detail_state()->lock=0;detail_window_poll();
    CHECK(detail_state()->phase==DETAIL_WINDOW_OPEN && detail_state()->epoch==state.detail_epoch);
    detail_window_finish();detail_state()->phase=DETAIL_WINDOW_FROZEN;detail_state()->report_pending=0;
    state.phase=SUPPORT_IDLE;g_shadow_engine.internal_tools.capture_requests=0;

    /* A stale successful population file must never masquerade as this run. */
    g_shadow_engine.population=(PopulationCaptureState *)calloc(1,sizeof(PopulationCaptureState));
    CHECK(g_shadow_engine.population!=NULL);
    population_state()->serial=99; population_state()->notice=4;
    fixture_file(L"stale-population.bin","old private capture");
    CHECK(internal_tool_path(population_state()->artifact_path,L"stale-population.bin"));
    memset(&state,0,sizeof(state)); state.serial=2;state.phase=SUPPORT_VEHICLE;
    state.started=state.stage_started=GetTickCount();state.population_started=1;state.population_serial=2;
    support_report_poll();
    CHECK(state.phase==SUPPORT_PARTIAL && (state.gaps&SUPPORT_BAD_POPULATION));
    printf("ARTIFACT %s\n",state.archive_name);
    free(g_shadow_engine.population);g_shadow_engine.population=NULL;

    /* Paused/no-render callbacks expire without leaving a future capture bit. */
    memset(&state,0,sizeof(state));state.serial=3;state.phase=SUPPORT_VEHICLE;
    state.started=GetTickCount();state.stage_started=GetTickCount()-SUPPORT_STAGE_TIMEOUT_MS-1;
    state.vehicle_serial=g_shadow_engine.vehicle_diagnostics.serial;
    g_shadow_engine.internal_tools.capture_requests=SHADOW_CAPTURE_VEHICLE;
    support_report_poll();
    CHECK(state.phase==SUPPORT_PARTIAL && (state.gaps&SUPPORT_NO_VEHICLE) && (state.gaps&SUPPORT_TIMEOUT));
    CHECK(!g_shadow_engine.internal_tools.capture_requests);
    printf("ARTIFACT %s\n",state.archive_name);

    /* A held native workload ticket is retained, never overlapped or freed. */
    workload_state()->phase=2;workload_state()->serial=5;
    memset(&state,0,sizeof(state));state.serial=4;state.phase=SUPPORT_WORKLOAD;state.workload_serial=5;
    state.started=GetTickCount();state.stage_started=GetTickCount()-SUPPORT_STAGE_TIMEOUT_MS-1;
    support_report_poll();
    CHECK(state.phase==SUPPORT_PARTIAL && (state.gaps&SUPPORT_BAD_WORKLOAD) && workload_state()->phase==2);
    CHECK(!support_report_request());workload_state()->phase=0;
    printf("ARTIFACT %s\n",state.archive_name);

    /* Include only this run's completed intersection and population files. */
    memset(&state,0,sizeof(state));state.serial=6;state.started=state.stage_started=GetTickCount();
    state.phase=SUPPORT_VEHICLE;state.vehicle_started=1;
    state.vehicle_serial=g_shadow_engine.vehicle_diagnostics.serial;
    state.intersection_started=1;state.intersection_capture=6;
    g_shadow_engine.intersection.capture=6;
    _snwprintf(path,MAX_PATH,L"ShadowEngineIntersection-%lu-6.log",(unsigned long)GetCurrentProcessId());
    fixture_file(path,"IX_END complete=1\r\n");
    g_shadow_engine.population=(PopulationCaptureState *)calloc(1,sizeof(PopulationCaptureState));
    CHECK(g_shadow_engine.population!=NULL);
    population_state()->serial=6;population_state()->notice=4;
    state.population_started=1;state.population_serial=6;
    fixture_file(L"current-population.bin","current synthetic capture");
    CHECK(internal_tool_path(population_state()->artifact_path,L"current-population.bin"));
    support_report_poll();CHECK(state.phase==SUPPORT_SAVED && !state.gaps && !state.error);
    free(g_shadow_engine.population);g_shadow_engine.population=NULL;
    printf("ARTIFACT %s\n",state.archive_name);

    z=(SupportZip *)calloc(1,sizeof(*z));CHECK(z!=NULL);
    CHECK(internal_tool_path(path,L"zip-format-fixture.zip"));
    z->file=CreateFileW(path,GENERIC_WRITE,0,NULL,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,NULL);
    CHECK(z->file!=INVALID_HANDLE_VALUE);
    CHECK(support_zip_memory(z,"known.txt","123456789",9));
    CHECK(z->entries[0].crc==0xCBF43926U);
    CHECK(support_zip_memory(z,"empty.txt","",0));
    for(i=0;i<sizeof(binary);++i) binary[i]=(unsigned char)(i*37U);
    {
        DWORD written;HANDLE source;
        CHECK(internal_tool_path(path,L"binary-fixture.bin"));
        source=CreateFileW(path,GENERIC_WRITE,0,NULL,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,NULL);
        CHECK(source!=INVALID_HANDLE_VALUE);
        CHECK(WriteFile(source,binary,sizeof(binary),&written,NULL) && written==sizeof(binary));
        CHECK(CloseHandle(source));
        CHECK(support_zip_file(z,"binary.bin",path));
    }
    CHECK(support_zip_finish(z));CHECK(CloseHandle(z->file));
    memset(z,0,sizeof(*z));z->file=INVALID_HANDLE_VALUE;
    CHECK(!support_zip_memory(z,"denied.txt","x",1) && z->error);
    memset(z,0,sizeof(*z));
    CHECK(!support_zip_memory(z,"../escape.txt","x",1) && z->error==ERROR_INVALID_PARAMETER);
    memset(z,0,sizeof(*z));z->offset=SUPPORT_ZIP_MAX_TOTAL;
    CHECK(!support_zip_write(z,"x",1) && z->error==ERROR_BUFFER_OVERFLOW);
    free(z);
    printf("PASS support report: %u checks, elapsedMs=%lu; real archive/stage code, synthetic observations, no game or ETW session\n",checks,(unsigned long)(GetTickCount()-started));
    return 0;
}
