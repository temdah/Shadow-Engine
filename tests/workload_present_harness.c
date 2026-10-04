/* Actual-source private presentation capture tests. Optional real probes own an
 * unattached hardware composition chain or an unshown WARP window. They never
 * attach to a game or overlay, and report occlusion without claiming FPS. */
#include <windows.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "../src/internal/etw_abi.h"

typedef struct PresentMock {
    int active,fail_path,fail_directory,fail_thread,fail_delete,fail_remove,fail_load,fail_proc;
    int stop_failures,close_failures,run_thread,thread_ran,fail_stat;
    ULONG start_error,enable_error,open_error,consume_error,property_error;
    ULONG lost_events,lost_buffers;
    unsigned starts,stops,enables,opens,processes,closes,threads,waits;
    unsigned deletes,removes,directories,loads,frees,handle_closes;
    ULONG mode,maximum_mb,enable_filters,consumer_mode;
    LPTHREAD_START_ROUTINE entry;
    void *argument;
    uint64_t chain;
    ULONG flags,result;
    uint64_t file_size;
    int64_t clock;
} PresentMock;
static PresentMock mock;

static ULONG WINAPI mock_start(uint64_t *session,const wchar_t *name,SETraceProperties *p)
{
    const wchar_t *file=(const wchar_t *)((const BYTE *)p+p->file_offset);
    ++mock.starts; mock.mode=p->mode; mock.maximum_mb=p->max_file_mb;
    assert(name && *name && p->clock==1U && p->name_offset && p->file_offset);
    assert(file && *file);
    if(mock.start_error) return mock.start_error;
    *session=0x1234U; return 0;
}
static ULONG WINAPI mock_control(uint64_t session,const wchar_t *name,SETraceProperties *p,ULONG code)
{
    assert(session==0x1234U && !name && code==1U);
    ++mock.stops;
    if(mock.stop_failures) { --mock.stop_failures; return ERROR_ACCESS_DENIED; }
    p->events_lost=mock.lost_events; p->log_lost=mock.lost_buffers;
    /* Observed with the real private logger: STOP clears the in/out filename.
     * The capture must retain its own path independently of this API buffer. */
    if(p->file_offset) *(wchar_t *)((BYTE *)p+p->file_offset)=0;
    return 0;
}
static ULONG WINAPI mock_enable(uint64_t session,const GUID *provider,ULONG control,UCHAR level,
    uint64_t any,uint64_t all,ULONG timeout,SEEnable *parameters)
{
    (void)level; (void)any; (void)all; (void)timeout;
    assert(session==0x1234U && provider && control==1U);
    ++mock.enables; mock.enable_filters=parameters?parameters->count:0;
    return mock.enable_error;
}
static uint64_t WINAPI mock_open(SETraceLogfile *file)
{
    ++mock.opens; mock.consumer_mode=file->mode;
    assert(file->file && *file->file && !file->logger);
    assert(file->event_callback && file->context);
    if(mock.open_error) { SetLastError(mock.open_error); return (uint64_t)-1; }
    return 0x5678U;
}
static ULONG WINAPI mock_process(uint64_t *handle,ULONG count,FILETIME *start,FILETIME *end)
{
    assert(*handle==0x5678U && count==1U && !start && !end);
    ++mock.processes; return mock.consume_error;
}
static ULONG WINAPI mock_close_trace(uint64_t handle)
{
    assert(handle==0x5678U); ++mock.closes;
    if(mock.close_failures) { --mock.close_failures; return ERROR_ACCESS_DENIED; }
    return 0;
}
static ULONG WINAPI mock_property(SEEventRecord *event,ULONG contexts,void *context,ULONG count,
    SEProperty *property,ULONG size,BYTE *out)
{
    const wchar_t *name=(const wchar_t *)(uintptr_t)property->name;
    (void)event; (void)contexts; (void)context;
    assert(count==1U && property->index==0xFFFFFFFFU);
    if(mock.property_error) return mock.property_error;
    if(!lstrcmpW(name,L"pIDXGISwapChain")) {
        assert(size==8U); memcpy(out,&mock.chain,8U);
    } else if(!lstrcmpW(name,L"Flags")) {
        assert(size==4U); memcpy(out,&mock.flags,4U);
    } else {
        assert(!lstrcmpW(name,L"Result") && size==4U); memcpy(out,&mock.result,4U);
    }
    return 0;
}
static HMODULE WINAPI mock_load_library(const wchar_t *path)
{
    if(!mock.active) return LoadLibraryW(path);
    if(mock.fail_load) { SetLastError(ERROR_MOD_NOT_FOUND); return NULL; }
    ++mock.loads; return (HMODULE)(uintptr_t)0x1010U;
}
static FARPROC WINAPI mock_get_proc(HMODULE module,const char *name)
{
    if(!mock.active) return GetProcAddress(module,name);
    if(mock.fail_proc) return NULL;
    if(!strcmp(name,"StartTraceW")) return (FARPROC)mock_start;
    if(!strcmp(name,"ControlTraceW")) return (FARPROC)mock_control;
    if(!strcmp(name,"EnableTraceEx2")) return (FARPROC)mock_enable;
    if(!strcmp(name,"OpenTraceW")) return (FARPROC)mock_open;
    if(!strcmp(name,"ProcessTrace")) return (FARPROC)mock_process;
    if(!strcmp(name,"CloseTrace")) return (FARPROC)mock_close_trace;
    if(!strcmp(name,"TdhGetProperty")) return (FARPROC)mock_property;
    assert(!"unexpected runtime dependency"); return NULL;
}
static BOOL WINAPI mock_free_library(HMODULE module)
{
    if(!mock.active) return FreeLibrary(module);
    ++mock.frees; return TRUE;
}
static DWORD WINAPI mock_module_path(HMODULE module,wchar_t *path,DWORD size)
{
    if(mock.active && mock.fail_path) { SetLastError(ERROR_PATH_NOT_FOUND); return 0; }
    return GetModuleFileNameW(module,path,size);
}
static BOOL WINAPI mock_create_directory(const wchar_t *path,LPSECURITY_ATTRIBUTES attributes)
{
    if(!mock.active) return CreateDirectoryW(path,attributes);
    assert(path && *path); ++mock.directories;
    if(mock.fail_directory) { SetLastError(ERROR_ACCESS_DENIED); return FALSE; }
    return TRUE;
}
static BOOL WINAPI mock_delete_file(const wchar_t *path)
{
    if(!mock.active) return DeleteFileW(path);
    assert(path && *path); ++mock.deletes;
    if(mock.fail_delete) { SetLastError(ERROR_SHARING_VIOLATION); return FALSE; }
    return TRUE;
}
static BOOL WINAPI mock_remove_directory(const wchar_t *path)
{
    if(!mock.active) return RemoveDirectoryW(path);
    assert(path && *path); ++mock.removes;
    if(mock.fail_remove) { SetLastError(ERROR_ACCESS_DENIED); return FALSE; }
    return TRUE;
}
static HANDLE WINAPI mock_create_thread(LPSECURITY_ATTRIBUTES attributes,SIZE_T stack,
    LPTHREAD_START_ROUTINE entry,void *argument,DWORD flags,DWORD *id)
{
    if(!mock.active) return CreateThread(attributes,stack,entry,argument,flags,id);
    ++mock.threads;
    if(mock.fail_thread) { SetLastError(ERROR_NOT_ENOUGH_MEMORY); return NULL; }
    mock.entry=entry; mock.argument=argument; mock.thread_ran=0;
    return (HANDLE)(uintptr_t)0x9ABCU;
}
static DWORD WINAPI mock_wait(HANDLE handle,DWORD timeout)
{
    if(!mock.active) return WaitForSingleObject(handle,timeout);
    assert(handle==(HANDLE)(uintptr_t)0x9ABCU && timeout==0U); ++mock.waits;
    if(!mock.run_thread) return WAIT_TIMEOUT;
    if(!mock.thread_ran) { mock.thread_ran=1; mock.entry(mock.argument); }
    return WAIT_OBJECT_0;
}
static BOOL WINAPI mock_close_handle(HANDLE handle)
{
    if(!mock.active) return CloseHandle(handle);
    assert(handle==(HANDLE)(uintptr_t)0x9ABCU); ++mock.handle_closes; return TRUE;
}
static BOOL WINAPI mock_qpc(LARGE_INTEGER *now)
{
    if(!mock.active) return QueryPerformanceCounter(now);
    now->QuadPart=mock.clock; return TRUE;
}
static BOOL WINAPI mock_file_attributes(const wchar_t *path,GET_FILEEX_INFO_LEVELS level,void *out)
{
    WIN32_FILE_ATTRIBUTE_DATA *data=(WIN32_FILE_ATTRIBUTE_DATA *)out;
    if(!mock.active) return GetFileAttributesExW(path,level,out);
    assert(path && *path && level==GetFileExInfoStandard);
    if(mock.fail_stat) { SetLastError(ERROR_FILE_NOT_FOUND); return FALSE; }
    memset(data,0,sizeof(*data)); data->dwFileAttributes=FILE_ATTRIBUTE_NORMAL;
    data->nFileSizeLow=(DWORD)mock.file_size; data->nFileSizeHigh=(DWORD)(mock.file_size>>32);
    return TRUE;
}

#define LoadLibraryW mock_load_library
#define GetProcAddress mock_get_proc
#define FreeLibrary mock_free_library
#define GetModuleFileNameW mock_module_path
#define CreateDirectoryW mock_create_directory
#define DeleteFileW mock_delete_file
#define RemoveDirectoryW mock_remove_directory
#define CreateThread mock_create_thread
#define WaitForSingleObject mock_wait
#define CloseHandle mock_close_handle
#define QueryPerformanceCounter mock_qpc
#define GetFileAttributesExW mock_file_attributes
#include "../src/modules/19_workload_present.inc"
#undef LoadLibraryW
#undef GetProcAddress
#undef FreeLibrary
#undef GetModuleFileNameW
#undef CreateDirectoryW
#undef DeleteFileW
#undef RemoveDirectoryW
#undef CreateThread
#undef WaitForSingleObject
#undef CloseHandle
#undef QueryPerformanceCounter
#undef GetFileAttributesExW

static void reset_mock(void)
{
    memset(&mock,0,sizeof(mock)); mock.active=1; mock.run_thread=1; mock.chain=0xAA55U;
    mock.file_size=65536U; mock.clock=10;
}
static void drain_mock(WorkloadPresent *capture)
{
    unsigned attempt;
    for(attempt=0;attempt<6U;++attempt) if(workload_present_cleanup(capture)) return;
    assert(!"mock cleanup did not finish");
}
static void parser_fixture(WorkloadPresent *capture,SEEventRecord *event)
{
    memset(capture,0,sizeof(*capture)); memset(event,0,sizeof(*event));
    capture->pid=GetCurrentProcessId(); capture->start=100; capture->end=1000;
    capture->frequency=1000; capture->property=mock_property;
    event->context=capture; event->header.pid=capture->pid; event->header.thread=7;
    event->header.provider=g_workload_dxgi; event->header.flags=0x40U;
}
static void emit_pair(SEEventRecord *event,int64_t timestamp)
{
    event->header.timestamp=timestamp; event->header.event.id=42U; workload_present_event(event);
    event->header.timestamp=timestamp+1; event->header.event.id=43U; workload_present_event(event);
}
static void test_parser(void)
{
    WorkloadPresent capture;
    SEEventRecord event;
    unsigned i;
    reset_mock(); parser_fixture(&capture,&event);
    emit_pair(&event,100); emit_pair(&event,110); emit_pair(&event,130);
    assert(capture.chains[0].presents==3U && capture.chains[0].intervals==2U);
    assert(capture.chains[0].sum==30 && capture.chains[0].max==20);
    assert(capture.chains[0].histogram[10]==1U && capture.chains[0].histogram[20]==1U);
    ++event.header.pid; emit_pair(&event,140); assert(capture.callbacks==6U);
    --event.header.pid; emit_pair(&event,1000); assert(capture.callbacks==6U);
    ++event.header.provider.Data1; emit_pair(&event,150); assert(capture.callbacks==6U);
    --event.header.provider.Data1;
    mock.flags=1U; emit_pair(&event,150); assert(capture.chains[0].presents==3U);
    mock.flags=0; mock.result=0x087A0001U; emit_pair(&event,170);
    assert(capture.failed==1U && capture.chains[0].presents==3U);
    mock.result=0; mock.property_error=ERROR_INVALID_DATA; emit_pair(&event,180);
    assert(capture.property_errors==1U);
    mock.property_error=0; event.header.event.version=1U; emit_pair(&event,200);
    assert(capture.property_errors==3U);
    event.header.event.version=0; event.header.flags=0; emit_pair(&event,210);
    assert(capture.property_errors==5U);
    parser_fixture(&capture,&event); event.header.event.id=42U; event.header.timestamp=100;
    workload_present_event(&event); event.header.timestamp=105; workload_present_event(&event);
    assert(capture.unpaired==1U);
    parser_fixture(&capture,&event); emit_pair(&event,100); emit_pair(&event,100);
    assert(capture.unpaired==1U && capture.chains[0].intervals==0U);
    parser_fixture(&capture,&event);
    for(i=0;i<WORKLOAD_PRESENT_CHAINS+1U;++i) { mock.chain=i+1U; emit_pair(&event,100+i*10U); }
    assert(capture.overflow==1U);
    parser_fixture(&capture,&event); mock.chain=1;
    for(i=0;i<WORKLOAD_PRESENT_THREADS+1U;++i) {
        event.header.thread=i+1U; event.header.event.id=42U; event.header.timestamp=100+i;
        workload_present_event(&event);
    }
    assert(capture.overflow==1U);
    parser_fixture(&capture,&event); emit_pair(&event,100); emit_pair(&event,300);
    assert(capture.chains[0].histogram[101]==1U && capture.chains[0].max==200);
    parser_fixture(&capture,&event); emit_pair(&event,100); emit_pair(&event,110);
    event.header.timestamp=999; event.header.event.id=42; workload_present_event(&event);
    event.header.timestamp=1001; event.header.event.id=43; workload_present_event(&event);
    assert(capture.chains[0].presents==3U && !capture.pending[0].thread);
    capture.consumed=1; assert(workload_present_valid(&capture));
    parser_fixture(&capture,&event); emit_pair(&event,100); emit_pair(&event,110); emit_pair(&event,130);
    capture.consumed=1; event.header.timestamp=999; event.header.event.id=42;
    workload_present_event(&event); assert(!workload_present_valid(&capture));
    event.header.timestamp=1001; workload_present_event(&event);
    assert(capture.unpaired==1U && !capture.pending[0].thread);
    event.header.timestamp=1002; event.header.event.id=43; workload_present_event(&event);
    assert(capture.chains[0].presents==3U);
    puts("PASS parser: successful pairs, named fields, scope/time/version, test/occluded presents, malformed properties and pairing");
}

static void test_lifecycle(void)
{
    WorkloadPresent capture;
    unsigned directories;
#define FRESH() do { reset_mock(); memset(&capture,0,sizeof(capture)); } while(0)
#define START() workload_present_start(&capture,100,1100,1000,7U,NULL)
    FRESH(); assert(START());
    assert(mock.mode==0x20801U && mock.maximum_mb==8U && mock.enable_filters==0U);
    assert(mock.opens==0U && mock.threads==0U && capture.directory_owned);
    assert(!START() && mock.starts==1U); /* Busy never resets owned state. */
    workload_present_stop(&capture); assert(mock.stops==1U);
    drain_mock(&capture);
    assert(mock.opens==1U && mock.consumer_mode==0x10001000U && mock.processes==1U);
    assert(mock.closes==1U && mock.deletes==1U && mock.removes==1U && mock.frees==2U);
    assert(!capture.directory_owned && capture.consumed && !capture.error);
    drain_mock(&capture); assert(mock.opens==1U && mock.deletes==1U && mock.closes==1U);

    FRESH(); mock.start_error=ERROR_ACCESS_DENIED; assert(!START());
    assert(capture.start_error==ERROR_ACCESS_DENIED && !capture.session);
    drain_mock(&capture); assert(!mock.enables && !mock.stops && !mock.opens && mock.removes==1U);
    FRESH(); mock.enable_error=ERROR_INVALID_PARAMETER; assert(!START());
    assert(capture.enable_error==ERROR_INVALID_PARAMETER && mock.stops==1U);
    drain_mock(&capture); assert(!mock.opens && mock.removes==1U);
    FRESH(); mock.fail_load=1; assert(!START()); drain_mock(&capture);
    assert(capture.error==ERROR_MOD_NOT_FOUND && !mock.starts && !mock.directories);
    FRESH(); mock.fail_proc=1; assert(!START()); drain_mock(&capture);
    assert(capture.error==ERROR_PROC_NOT_FOUND && !mock.starts && mock.frees==2U);
    FRESH(); mock.fail_path=1; assert(!START()); drain_mock(&capture);
    assert(capture.file_error && !mock.starts && !mock.directories);
    FRESH(); mock.fail_directory=1; assert(!START()); drain_mock(&capture);
    assert(capture.file_error==ERROR_ACCESS_DENIED && !mock.starts && !mock.deletes && !mock.removes);
    FRESH(); assert(!workload_present_start(&capture,100,1100,0,7U,NULL));
    assert(capture.error==ERROR_INVALID_PARAMETER && !mock.loads);
    FRESH(); mock.clock=101; assert(!START()); drain_mock(&capture);
    assert(capture.error==ERROR_TIMEOUT && mock.stops==1U && !mock.opens);

    FRESH(); assert(START()); mock.stop_failures=2; workload_present_stop(&capture);
    assert(capture.session && capture.stop_error==ERROR_ACCESS_DENIED);
    assert(!workload_present_cleanup(&capture));
    assert(capture.session && !mock.opens && !mock.deletes && !mock.frees);
    drain_mock(&capture); assert(mock.stops==3U && mock.opens==1U && !capture.session);
    FRESH(); assert(START()); mock.run_thread=0;
    assert(!workload_present_cleanup(&capture));
    assert(capture.thread && mock.opens==1U && !mock.deletes && !mock.frees);
    assert(!START() && mock.starts==1U);
    mock.run_thread=1; drain_mock(&capture); assert(mock.processes==1U && mock.deletes==1U);
    FRESH(); assert(START()); mock.close_failures=1;
    assert(!workload_present_cleanup(&capture));
    assert(capture.close_error && !mock.deletes && !mock.frees);
    assert(!START() && mock.starts==1U && !mock.deletes);
    drain_mock(&capture); assert(mock.closes==2U && mock.opens==1U && mock.deletes==1U);

    FRESH(); assert(START()); mock.fail_stat=1; drain_mock(&capture);
    assert(capture.file_error==ERROR_FILE_NOT_FOUND && !mock.opens);
    FRESH(); assert(START()); mock.file_size=0; drain_mock(&capture);
    assert(capture.file_error==ERROR_INSUFFICIENT_BUFFER && !mock.opens);
    FRESH(); assert(START()); mock.file_size=8U*1024U*1024U; drain_mock(&capture);
    assert(capture.file_error==ERROR_INSUFFICIENT_BUFFER && !mock.opens);
    FRESH(); assert(START()); mock.open_error=ERROR_BAD_FORMAT; drain_mock(&capture);
    assert(capture.open_error==ERROR_BAD_FORMAT && !mock.threads && !mock.closes);
    FRESH(); assert(START()); mock.fail_thread=1; drain_mock(&capture);
    assert(capture.open_error==ERROR_NOT_ENOUGH_MEMORY && mock.closes==1U && !capture.consumed);
    FRESH(); assert(START()); mock.consume_error=ERROR_INVALID_DATA; drain_mock(&capture);
    assert(capture.consume_error==ERROR_INVALID_DATA && capture.consumed);

    FRESH(); assert(START()); mock.fail_delete=1; drain_mock(&capture);
    assert(capture.directory_owned && capture.file_error==ERROR_SHARING_VIOLATION && !mock.removes);
    directories=mock.directories; assert(!START()); assert(mock.directories==directories);
    mock.fail_delete=0; assert(START()); assert(mock.directories==directories+1U);
    drain_mock(&capture);
    FRESH(); assert(START()); mock.fail_remove=1; drain_mock(&capture);
    assert(capture.directory_owned && capture.file_error==ERROR_ACCESS_DENIED);
    directories=mock.directories; assert(!START() && mock.directories==directories);
    mock.fail_remove=0; assert(START()); drain_mock(&capture);
    puts("PASS lifecycle: private/file flags, no PID filter, no live consumer, setup failures, owned stop/close retry, drain exclusion, file cap/read errors and bounded cleanup retry");
#undef START
#undef FRESH
}

static void test_validity(void)
{
    WorkloadPresent capture;
    SEEventRecord event;
    reset_mock(); parser_fixture(&capture,&event);
    emit_pair(&event,100); emit_pair(&event,110); emit_pair(&event,130);
    assert(!workload_present_valid(&capture));
    capture.consumed=1; assert(workload_present_valid(&capture));
#define REJECT(field) do { capture.field=1; assert(!workload_present_valid(&capture)); capture.field=0; } while(0)
    REJECT(error); REJECT(stop_error); REJECT(consume_error); REJECT(open_error);
    REJECT(file_error); REJECT(close_error); REJECT(properties.p.events_lost);
    REJECT(properties.p.log_lost); REJECT(logfile.events_lost); REJECT(logfile.header.lost);
    REJECT(property_errors); REJECT(failed); REJECT(unpaired); REJECT(overflow);
    capture.chains[1].presents=1; assert(!workload_present_valid(&capture));
    capture.chains[1].presents=0; capture.chains[0].intervals=1;
    assert(!workload_present_valid(&capture));
    puts("PASS validity: unconsumed/incomplete/error/lost/malformed/multiple-chain/insufficient records never certify FPS");
#undef REJECT
}

/* Minimal documented x64 DXGI/D3D11 ABI: only the swapchain descriptor and
 * inherited COM Release/Present methods needed by this isolated probe. */
typedef struct ProbeMode {
    UINT width,height,refresh_numerator,refresh_denominator;
    int format,scanline,scaling;
} ProbeMode;
typedef struct ProbeSwapDesc {
    ProbeMode mode;
    UINT sample_count,sample_quality,usage,buffer_count;
    HWND window;
    BOOL windowed;
    int swap_effect;
    UINT flags;
} ProbeSwapDesc;
typedef char ProbeDescSize[(sizeof(ProbeSwapDesc)==72)?1:-1];
typedef char ProbeWindowOffset[(offsetof(ProbeSwapDesc,window)==48)?1:-1];
typedef HRESULT (WINAPI *ProbeCreateDevice)(void *,int,HMODULE,UINT,const int *,UINT,
    UINT,const ProbeSwapDesc *,void **,void **,int *,void **);
typedef HRESULT (WINAPI *ProbeCreateDeviceOnly)(void *,int,HMODULE,UINT,const int *,UINT,
    UINT,void **,int *,void **);
typedef struct ProbeCompositionDesc {
    UINT width,height;
    int format;
    BOOL stereo;
    UINT sample_count,sample_quality,usage,buffer_count;
    int scaling,swap_effect,alpha;
    UINT flags;
} ProbeCompositionDesc;
typedef char ProbeCompositionSize[(sizeof(ProbeCompositionDesc)==48)?1:-1];
static const GUID probe_factory2={0x50C83A1C,0xE072,0x4C48,{0x87,0xB0,0x36,0x30,0xFA,0x36,0xA6,0xD0}};
static ULONG probe_release(void *object)
{
    void **vtable;
    if(!object) return 0;
    vtable=*(void ***)object;
    return ((ULONG (WINAPI *)(void *))vtable[2])(object);
}
static HRESULT probe_present(void *chain)
{
    void **vtable=*(void ***)chain;
    return ((HRESULT (WINAPI *)(void *,UINT,UINT))vtable[8])(chain,0U,0U);
}
static uint64_t (WINAPI *probe_original_open)(SETraceLogfile *);
static unsigned probe_events,probe_dxgi,probe_present_events,probe_private_events;
static const GUID probe_provider={0xEA8D0D52,0xC203,0x4F29,{0xB5,0x97,0x7C,0x22,0x56,0xA5,0x81,0x0D}};
static void WINAPI probe_event(SEEventRecord *event)
{
    ++probe_events;
    if(!memcmp(&event->header.provider,&probe_provider,sizeof(GUID))) ++probe_private_events;
    if(!memcmp(&event->header.provider,&g_workload_dxgi,sizeof(GUID))) {
        ++probe_dxgi;
        if(event->header.event.id==42U || event->header.event.id==43U) {
            ++probe_present_events;
            if(probe_present_events<=2U) printf("REAL_DXGI_EVENT pid=%lu id=%u version=%u flags=%u timestamp=%lld\n",
                event->header.pid,event->header.event.id,event->header.event.version,
                event->header.flags,event->header.timestamp);
        }
    }
    workload_present_event(event);
}
static uint64_t WINAPI probe_open(SETraceLogfile *file)
{
    file->event_callback=probe_event;
    return probe_original_open(file);
}
static int ordinary_token(void)
{
    HANDLE token=NULL;
    DWORD elevated=0,returned=0;
    if(!OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token) ||
       !GetTokenInformation(token,(TOKEN_INFORMATION_CLASS)20,&elevated,sizeof(elevated),&returned)) {
        printf("FAIL private probe: cannot verify unelevated token error=%lu\n",GetLastError());
        if(token) CloseHandle(token);
        return 0;
    }
    CloseHandle(token);
    if(elevated) { puts("FAIL private probe: ordinary-user probe requires an unelevated process"); return 0; }
    return 1;
}
static int real_private_transport(void)
{
    WorkloadPresent capture;
    LARGE_INTEGER frequency;
    SEEventDescriptor descriptor;
    SEEnable enable;
    ULONG (WINAPI *event_register)(const GUID *,void *,void *,uint64_t *);
    ULONG (WINAPI *event_write)(uint64_t,const SEEventDescriptor *,ULONG,const void *);
    ULONG (WINAPI *event_unregister)(uint64_t);
    BOOLEAN (WINAPI *provider_enabled)(uint64_t,UCHAR,uint64_t);
    uint64_t registration=0;
    ULONG provider_error=0,write_error=0;
    unsigned i,enabled=0,drained=0,emitted=0;
    memset(&mock,0,sizeof(mock)); memset(&capture,0,sizeof(capture));
    capture.consumer=(uint64_t)-1;
    if(!ordinary_token() || !QueryPerformanceFrequency(&frequency)) return 1;
    if(!workload_present_start(&capture,workload_qpc()+frequency.QuadPart/2,
        workload_qpc()+frequency.QuadPart*3,frequency.QuadPart,2U,NULL)) goto cleanup;
    event_register=(void *)GetProcAddress(capture.advapi,"EventRegister");
    event_write=(void *)GetProcAddress(capture.advapi,"EventWrite");
    event_unregister=(void *)GetProcAddress(capture.advapi,"EventUnregister");
    provider_enabled=(void *)GetProcAddress(capture.advapi,"EventProviderEnabled");
    if(!event_register || !event_write || !event_unregister || !provider_enabled) {
        provider_error=ERROR_PROC_NOT_FOUND; goto cleanup;
    }
    provider_error=event_register(&probe_provider,NULL,NULL,&registration);
    if(provider_error) goto cleanup;
    memset(&enable,0,sizeof(enable)); enable.version=2;
    provider_error=capture.enable(capture.session,&probe_provider,1U,5U,2U,0,0,&enable);
    if(provider_error) goto unregister;
    enabled=provider_enabled(registration,4U,2U)?1U:0U;
    memset(&descriptor,0,sizeof(descriptor)); descriptor.id=123; descriptor.level=4; descriptor.keyword=2;
    probe_original_open=capture.open; capture.open=probe_open;
    while(workload_qpc()<capture.start) Sleep(1);
    for(i=0;i<12U;++i) {
        ULONG status=event_write(registration,&descriptor,0,NULL);
        if(status) write_error=status;
        else ++emitted;
        Sleep(1);
    }
unregister:
    event_unregister(registration);
cleanup:
    workload_present_stop(&capture);
    for(i=0;i<400U;++i) {
        if(workload_present_cleanup(&capture)) { drained=1; break; }
        Sleep(5);
    }
    printf("REAL_PRIVATE tokenElevated=0 syntheticProvider=1 enabled=%u emitted=%u received=%u allEvents=%u providerError=%lu writeError=%lu error=%lu stopError=%lu consumeError=%lu fileError=%lu drained=%u\n",
        enabled,emitted,probe_private_events,probe_events,provider_error,write_error,capture.error,
        capture.stop_error,capture.consume_error,capture.file_error,drained);
    if(drained && enabled && !provider_error && !write_error && !capture.error &&
       !capture.stop_error && !capture.consume_error && !capture.file_error && probe_private_events==12U) {
        puts("PASS actual private transport with own synthetic provider; DXGI and game FPS remain unproven"); return 0;
    }
    puts("FAIL actual private transport with own synthetic provider"); return 1;
}
static int real_dxgi_probe(int composition)
{
    WorkloadPresent capture;
    ProbeSwapDesc desc;
    ProbeCreateDevice create;
    ProbeCreateDeviceOnly create_device;
    ProbeCompositionDesc composition_desc;
    HRESULT (WINAPI *create_factory)(const GUID *,void **);
    HMODULE d3d11=NULL;
    HMODULE dxgi=NULL;
    HWND window=NULL;
    void *chain=NULL,*device=NULL,*context=NULL,*factory=NULL;
    wchar_t path[MAX_PATH];
    LARGE_INTEGER frequency;
    UINT length;
    HRESULT result=0;
    unsigned i,ok=0,occluded=0,other=0,drained=0;
    int verdict=1;
    memset(&mock,0,sizeof(mock)); memset(&capture,0,sizeof(capture));
    capture.consumer=(uint64_t)-1;
    if(!ordinary_token()) return 1;
    printf("REAL_DXGI tokenElevated=0 ownedWindowVisible=0 driver=%s composition=%u\n",
        composition?"hardware":"WARP",composition?1U:0U);
    if(!QueryPerformanceFrequency(&frequency)) return 1;
    length=GetSystemDirectoryW(path,MAX_PATH);
    if(!length || length+12U>=MAX_PATH) return 1;
    lstrcatW(path,L"\\d3d11.dll"); d3d11=LoadLibraryW(path);
    if(!d3d11) { printf("FAIL real DXGI: system D3D11 unavailable error=%lu\n",GetLastError()); goto done; }
    create=(ProbeCreateDevice)GetProcAddress(d3d11,"D3D11CreateDeviceAndSwapChain");
    if(!create) { puts("FAIL real DXGI: create function unavailable"); goto done; }
    if(composition) {
        void **vtable;
        create_device=(ProbeCreateDeviceOnly)GetProcAddress(d3d11,"D3D11CreateDevice");
        if(!create_device) { puts("FAIL real DXGI: device function unavailable"); goto done; }
        result=create_device(NULL,1,NULL,0,NULL,0,7,&device,NULL,&context); /* Hardware, SDK7 */
        if(result<0) { printf("FAIL real DXGI: hardware device HRESULT=0x%08lX\n",(ULONG)result); goto done; }
        path[length]=0; lstrcatW(path,L"\\dxgi.dll"); dxgi=LoadLibraryW(path);
        if(!dxgi) { puts("FAIL real DXGI: system DXGI unavailable"); goto done; }
        create_factory=(void *)GetProcAddress(dxgi,"CreateDXGIFactory1");
        if(!create_factory) { puts("FAIL real DXGI: factory function unavailable"); goto done; }
        result=create_factory(&probe_factory2,&factory);
        if(result<0) { printf("FAIL real DXGI: factory2 HRESULT=0x%08lX\n",(ULONG)result); goto done; }
        memset(&composition_desc,0,sizeof(composition_desc));
        composition_desc.width=32; composition_desc.height=32; composition_desc.format=28;
        composition_desc.sample_count=1; composition_desc.usage=0x20U;
        composition_desc.buffer_count=2; composition_desc.swap_effect=3; /* FLIP_SEQUENTIAL */
        composition_desc.alpha=3; /* IGNORE, STRETCH scaling0 */
        vtable=*(void ***)factory;
        result=((HRESULT (WINAPI *)(void *,void *,const ProbeCompositionDesc *,void *,void **))vtable[24])(
            factory,device,&composition_desc,NULL,&chain);
        if(result<0) { printf("FAIL real DXGI: composition chain HRESULT=0x%08lX\n",(ULONG)result); goto done; }
    } else {
    window=CreateWindowExW(0,L"STATIC",L"ShadowEngine private capture test",WS_POPUP,
        0,0,32,32,NULL,NULL,GetModuleHandleW(NULL),NULL);
    if(!window) { printf("FAIL real DXGI: hidden owned window unavailable error=%lu\n",GetLastError()); goto done; }
    memset(&desc,0,sizeof(desc));
    desc.mode.width=32; desc.mode.height=32; desc.mode.format=28; /* R8G8B8A8_UNORM */
    desc.sample_count=1; desc.usage=0x20U; desc.buffer_count=1;
    desc.window=window; desc.windowed=TRUE; /* DISCARD swap effect */
    result=create(NULL,5,NULL,0,NULL,0,7,&desc,&chain,&device,NULL,&context); /* WARP, SDK7 */
    if(result<0) { printf("FAIL real DXGI: WARP creation HRESULT=0x%08lX\n",(ULONG)result); goto done; }
    }
    if(!workload_present_start(&capture,workload_qpc()+frequency.QuadPart/2,workload_qpc()+frequency.QuadPart*3,
        frequency.QuadPart,1U,NULL)) {
        printf("FAIL real DXGI: private capture start error=%lu\n",capture.error); goto cleanup;
    }
    probe_original_open=capture.open; capture.open=probe_open;
    while(workload_qpc()<capture.start) Sleep(1);
    for(i=0;i<32U;++i) {
        result=probe_present(chain);
        if(result==0) ++ok;
        else if((ULONG)result==0x087A0001U) ++occluded;
        else ++other;
        Sleep(5);
    }
cleanup:
    workload_present_stop(&capture);
    for(i=0;i<400U;++i) {
        if(workload_present_cleanup(&capture)) { drained=1; break; }
        Sleep(5);
    }
    printf("REAL_DXGI ok=%u occluded=%u other=%u callbacks=%u presents=%u intervals=%u failedOrOccluded=%lu propertyErrors=%lu error=%lu stopError=%lu consumeError=%lu fileError=%lu openError=%lu consumed=%u eventsLost=%lu logBuffersLost=%lu drained=%u\n",
        ok,occluded,other,capture.callbacks,capture.chains[0].presents,capture.chains[0].intervals,
        capture.failed,capture.property_errors,capture.error,capture.stop_error,capture.consume_error,
        capture.file_error,capture.open_error,capture.consumed,
        capture.properties.p.events_lost,capture.properties.p.log_lost,drained);
    printf("REAL_DXGI_RAW allEvents=%u dxgiEvents=%u presentEvents=%u start=%lld end=%lld\n",
        probe_events,probe_dxgi,probe_present_events,capture.start,capture.end);
    if(drained && !capture.error && !capture.stop_error && !capture.consume_error &&
       !capture.property_errors && !capture.file_error && !capture.open_error && capture.consumed &&
       !capture.properties.p.events_lost && !capture.properties.p.log_lost &&
       capture.callbacks>=4U) {
        puts(capture.chains[0].intervals>1U ?
            "PASS real DXGI private delivery and successful Present intervals; isolated probe process only" :
            "PASS real DXGI private delivery only; occluded probe supplied no valid FPS claim");
        verdict=0;
    } else puts("FAIL real DXGI private transport; synthetic success is not a substitute");
done:
    probe_release(context); probe_release(chain); probe_release(device);
    probe_release(factory);
    if(window) DestroyWindow(window);
    if(d3d11) FreeLibrary(d3d11);
    if(dxgi) FreeLibrary(dxgi);
    return verdict;
}

int main(int argc,char **argv)
{
    if(argc==2 && !strcmp(argv[1],"--real-dxgi")) return real_dxgi_probe(1);
    if(argc==2 && !strcmp(argv[1],"--real-dxgi-hidden-window")) return real_dxgi_probe(0);
    if(argc==2 && !strcmp(argv[1],"--real-private-transport")) return real_private_transport();
    test_parser();
    test_lifecycle();
    test_validity();
    return 0;
}
