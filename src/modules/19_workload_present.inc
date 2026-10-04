/* Internal-only presentation measurement. No swap-chain hook or game write.
 * A private in-process ETL avoids the realtime logger's privilege requirement.
 * The worker owns its session/directory; one post-stop ProcessTrace thread owns
 * statistics. No record or trace storage is recycled before that thread exits. */
#include "../internal/etw_abi.h"
#include <wchar.h>
#define WORKLOAD_PRESENT_CHAINS 4U
#define WORKLOAD_PRESENT_THREADS 16U
#define WORKLOAD_PRESENT_FILE_MB 8U
#define WORKLOAD_PRESENT_PRIVATE_MODE 0x00020801U /* PRIVATE | IN_PROC | SEQUENTIAL */
#define WORKLOAD_PRESENT_FILE_CONSUMER_MODE 0x10001000U /* EVENT_RECORD | RAW_TIMESTAMP */
typedef struct WorkloadPresentChain {
    uint64_t identity;
    int64_t first,last,sum,max;
    uint32_t presents,intervals,histogram[102]; /* 1ms buckets, last >=101ms */
} WorkloadPresentChain;
typedef struct WorkloadPresentPending {
    ULONG thread;
    uint64_t chain;
    int64_t timestamp;
} WorkloadPresentPending;
typedef struct WorkloadPresent {
    struct { SETraceProperties p; wchar_t name[96],file[MAX_PATH]; } properties;
    wchar_t directory[MAX_PATH],file[MAX_PATH]; /* Immutable owned paths. */
    SETraceLogfile logfile;
    HMODULE advapi,tdh;
    HANDLE thread;
    uint64_t session,consumer;
    ULONG error,stop_error,consume_error,pid,property_errors,failed,unpaired,overflow;
    ULONG start_error,enable_error,open_error,file_error,close_error;
    uint32_t directory_owned,read_started,consumed;
    uint64_t file_bytes;
    int64_t start,end,frequency,callback_ticks;
    uint32_t callbacks;
    WorkloadPresentChain chains[WORKLOAD_PRESENT_CHAINS];
    WorkloadPresentPending pending[WORKLOAD_PRESENT_THREADS];
    ULONG (WINAPI *start_trace)(uint64_t *,const wchar_t *,SETraceProperties *);
    ULONG (WINAPI *control)(uint64_t,const wchar_t *,SETraceProperties *,ULONG);
    ULONG (WINAPI *enable)(uint64_t,const GUID *,ULONG,UCHAR,uint64_t,uint64_t,ULONG,SEEnable *);
    uint64_t (WINAPI *open)(SETraceLogfile *);
    ULONG (WINAPI *process)(uint64_t *,ULONG,FILETIME *,FILETIME *);
    ULONG (WINAPI *close)(uint64_t);
    ULONG (WINAPI *property)(SEEventRecord *,ULONG,void *,ULONG,SEProperty *,ULONG,BYTE *);
} WorkloadPresent;
static const GUID g_workload_dxgi={0xCA11C036,0x0102,0x4A2D,{0xA6,0xAD,0xF0,0x3C,0xFE,0xD5,0xD3,0xC9}};
static const GUID g_workload_private={0x8CB14203,0x84A1,0x479D,{0x92,0xCA,0xA6,0x55,0xD1,0x83,0x01,0x41}};

static int64_t workload_qpc(void)
{
    LARGE_INTEGER now;
    if(!QueryPerformanceCounter(&now)) return 0;
    return now.QuadPart;
}

static int workload_present_property(WorkloadPresent *p,SEEventRecord *e,
    const wchar_t *name,void *value,ULONG bytes)
{
    SEProperty property;
    memset(&property,0,sizeof(property));
    property.name=(uint64_t)(uintptr_t)name;
    property.index=0xFFFFFFFFU;
    if(p->property(e,0,NULL,1,&property,bytes,(BYTE *)value)==0) return 1;
    ++p->property_errors;
    return 0;
}

static void workload_present_interval(WorkloadPresent *p,uint64_t chain,int64_t time)
{
    uint32_t i,bucket;
    WorkloadPresentChain *c;
    for(i=0;i<WORKLOAD_PRESENT_CHAINS;++i)
        if(!p->chains[i].identity || p->chains[i].identity==chain) break;
    if(i==WORKLOAD_PRESENT_CHAINS) { ++p->overflow; return; }
    c=&p->chains[i];
    c->identity=chain;
    if(!c->presents) c->first=time;
    else if(time>c->last) {
        int64_t delta=time-c->last;
        ++c->intervals;
        c->sum+=delta;
        if(delta>c->max) c->max=delta;
        bucket=(uint32_t)((double)delta*1000.0/(double)p->frequency);
        if(bucket>101U) bucket=101U;
        ++c->histogram[bucket];
    } else { ++p->unpaired; return; }
    c->last=time;
    ++c->presents;
}

static void WINAPI workload_present_event(SEEventRecord *e)
{
    WorkloadPresent *p=(WorkloadPresent *)e->context;
    int64_t before;
    uint32_t i;
    if(!p || e->header.pid!=p->pid ||
       memcmp(&e->header.provider,&g_workload_dxgi,sizeof(GUID)) ||
       (e->header.event.id!=42U && e->header.event.id!=43U) ||
       e->header.timestamp<p->start) return;
    if(e->header.timestamp>=p->end) {
        /* A Present begun inside the window may finish during the short stop
         * tail. Keep its result; unrelated later events are outside scope. */
        for(i=0;i<WORKLOAD_PRESENT_THREADS;++i)
            if(p->pending[i].thread==e->header.thread) break;
        if(i==WORKLOAD_PRESENT_THREADS) return;
        if(e->header.event.id==42U) {
            ++p->unpaired; p->pending[i].thread=0; return;
        }
    }
    before=workload_qpc();
    ++p->callbacks;
    if(e->header.event.version!=0 || !(e->header.flags&0x40U)) {
        ++p->property_errors; goto done;
    }
    if(e->header.event.id==42U) {
        uint64_t chain=0;
        ULONG flags=0;
        for(i=0;i<WORKLOAD_PRESENT_THREADS;++i)
            if(p->pending[i].thread==e->header.thread) break;
        if(i<WORKLOAD_PRESENT_THREADS) { ++p->unpaired; p->pending[i].thread=0; }
        if(!workload_present_property(p,e,L"Flags",&flags,sizeof(flags)) || (flags&1U)) goto done;
        if(!workload_present_property(p,e,L"pIDXGISwapChain",&chain,sizeof(chain)) || !chain) goto done;
        for(i=0;i<WORKLOAD_PRESENT_THREADS;++i) if(!p->pending[i].thread) break;
        if(i==WORKLOAD_PRESENT_THREADS) { ++p->overflow; goto done; }
        p->pending[i].thread=e->header.thread;
        p->pending[i].chain=chain;
        p->pending[i].timestamp=e->header.timestamp;
    } else {
        ULONG result=0;
        for(i=0;i<WORKLOAD_PRESENT_THREADS;++i)
            if(p->pending[i].thread==e->header.thread) break;
        if(i==WORKLOAD_PRESENT_THREADS) goto done; /* Test presents have no pending entry. */
        if(workload_present_property(p,e,L"Result",&result,sizeof(result))) {
            if(result==0) workload_present_interval(p,p->pending[i].chain,p->pending[i].timestamp);
            else ++p->failed; /* Includes occluded/not-ready, not just HRESULT failures. */
        }
        p->pending[i].thread=0;
    }
done:
    p->callback_ticks+=workload_qpc()-before;
}

static DWORD WINAPI workload_present_consumer(void *context)
{
    WorkloadPresent *p=(WorkloadPresent *)context;
    p->consume_error=p->process(&p->consumer,1,NULL,NULL);
    p->consumed=1; /* Read only after the worker observes thread termination. */
    return 0;
}

static void workload_present_stop(WorkloadPresent *p)
{
    if(p->session) {
        p->stop_error=p->control(p->session,NULL,&p->properties.p,1U);
        /* A full sequential log may have stopped itself. Keep its failure,
         * but no live session remains to own in that specific case. */
        if(!p->stop_error || p->stop_error==4201U) p->session=0;
    }
    /* No file consumer starts until STOP succeeds. Never stop by name or
     * substitute a system/realtime session when private capture fails. */
}

/* Delete only the fixed child of the directory this request created. Failed
 * cleanup retains ownership and forbids another directory on the next request. */
static int workload_present_remove_file(WorkloadPresent *p)
{
    if(!p->directory_owned) return 1;
    if(!DeleteFileW(p->file)) {
        ULONG error=GetLastError();
        if(error!=ERROR_FILE_NOT_FOUND) { p->file_error=error; return 0; }
    }
    if(!RemoveDirectoryW(p->directory)) {
        p->file_error=GetLastError(); return 0;
    }
    p->directory_owned=0;
    return 1;
}

static int workload_present_path(WorkloadPresent *p,HMODULE owner,unsigned serial)
{
    wchar_t module[MAX_PATH],*tail;
    DWORD length=GetModuleFileNameW(owner,module,MAX_PATH);
    int written;
    if(!length || length>=MAX_PATH) { p->file_error=ERROR_BAD_PATHNAME; return 0; }
    tail=wcsrchr(module,L'\\');
    if(!tail) { p->file_error=ERROR_BAD_PATHNAME; return 0; }
    *tail=0;
    written=_snwprintf(p->directory,MAX_PATH,L"%ls\\ShadowEnginePresent-%lu-%u-%lu",
        module,(unsigned long)p->pid,serial,(unsigned long)GetTickCount());
    if(written<=0 || written>=MAX_PATH-13) {
        p->file_error=ERROR_BUFFER_OVERFLOW; return 0;
    }
    lstrcpyW(p->file,p->directory);
    lstrcatW(p->file,L"\\capture.etl");
    lstrcpyW(p->properties.file,p->file); /* ETW may overwrite this in/out buffer. */
    if(!CreateDirectoryW(p->directory,NULL)) { p->file_error=GetLastError(); return 0; }
    p->directory_owned=1;
    return 1;
}

static int workload_present_start(WorkloadPresent *p,int64_t start,int64_t end,
    int64_t frequency,unsigned serial,HMODULE owner)
{
    SEEnable enable;
    /* The normal caller drains all handles before reuse. Retain an undeleted
     * file on failure, but still permit the separate CPU workload report. */
    if(p->session || p->thread || (p->consumer && p->consumer!=(uint64_t)-1)) return 0;
    if(!workload_present_remove_file(p)) {
        memset(p->chains,0,sizeof(p->chains));
        p->error=p->file_error; return 0;
    }
    memset(p,0,sizeof(*p));
    p->consumer=(uint64_t)-1;
    p->start=start; p->end=end; p->frequency=frequency;
    p->pid=GetCurrentProcessId();
    if(frequency<=0 || end<=start) { p->error=ERROR_INVALID_PARAMETER; return 0; }
    /* Resolve from the system directory, never a mod-supplied DLL search path. */
    {
        wchar_t path[MAX_PATH];
        UINT n=GetSystemDirectoryW(path,MAX_PATH);
        if(!n || n+14U>=MAX_PATH) { p->error=ERROR_PATH_NOT_FOUND; return 0; }
        lstrcatW(path,L"\\advapi32.dll"); p->advapi=LoadLibraryW(path);
        path[n]=0; lstrcatW(path,L"\\tdh.dll"); p->tdh=LoadLibraryW(path);
    }
    if(!p->advapi || !p->tdh) { p->error=ERROR_MOD_NOT_FOUND; return 0; }
#define WORKLOAD_API(field,name) p->field=(void *)GetProcAddress(p->advapi,name)
    WORKLOAD_API(start_trace,"StartTraceW"); WORKLOAD_API(control,"ControlTraceW");
    WORKLOAD_API(enable,"EnableTraceEx2"); WORKLOAD_API(open,"OpenTraceW");
    WORKLOAD_API(process,"ProcessTrace"); WORKLOAD_API(close,"CloseTrace");
#undef WORKLOAD_API
    p->property=(void *)GetProcAddress(p->tdh,"TdhGetProperty");
    if(!p->start_trace || !p->control || !p->enable || !p->open ||
       !p->process || !p->close || !p->property) { p->error=ERROR_PROC_NOT_FOUND; return 0; }
    if(!workload_present_path(p,owner,serial)) { p->error=p->file_error; return 0; }
    _snwprintf(p->properties.name,96,L"ShadowEngineWorkload-%lu-%u-%lu",
        (unsigned long)p->pid,serial,(unsigned long)GetTickCount());
    p->properties.p.size=sizeof(p->properties);
    p->properties.p.guid=g_workload_private;
    p->properties.p.guid.Data1^=p->pid;
    p->properties.p.guid.Data2^=(USHORT)serial;
    p->properties.p.guid.Data3^=(USHORT)GetTickCount();
    p->properties.p.clock=1U; /* QPC timestamps, not system time. */
    p->properties.p.flags=0x20000U; /* WNODE_FLAG_TRACED_GUID */
    p->properties.p.buffer_kb=64U;
    p->properties.p.min_buffers=2U; p->properties.p.max_buffers=8U;
    p->properties.p.mode=WORKLOAD_PRESENT_PRIVATE_MODE;
    p->properties.p.max_file_mb=WORKLOAD_PRESENT_FILE_MB;
    p->properties.p.flush_seconds=1U;
    p->properties.p.name_offset=offsetof(WorkloadPresent,properties.name);
    p->properties.p.file_offset=offsetof(WorkloadPresent,properties.file);
    p->start_error=p->start_trace(&p->session,p->properties.name,&p->properties.p);
    p->error=p->start_error;
    if(p->error) { p->session=0; return 0; }
    memset(&enable,0,sizeof(enable));
    enable.version=2U; /* Private IN_PROC provides scope; no kernel PID filter. */
    p->enable_error=p->enable(p->session,&g_workload_dxgi,1U,0U,
        0x8000000000000002ULL,0,0,&enable);
    p->error=p->enable_error;
    if(p->error) { workload_present_stop(p); return 0; }
    if(workload_qpc()>start) { p->error=ERROR_TIMEOUT; workload_present_stop(p); return 0; }
    return 1;
}

/* Nonblocking cleanup. Never recycle memory still owned by ProcessTrace. */
static int workload_present_cleanup(WorkloadPresent *p)
{
    if(p->session) { workload_present_stop(p); if(p->session) return 0; }
    if(p->directory_owned && !p->read_started && !p->error && !p->stop_error) {
        WIN32_FILE_ATTRIBUTE_DATA data;
        p->read_started=1;
        if(!GetFileAttributesExW(p->file,GetFileExInfoStandard,&data))
            p->file_error=GetLastError();
        else {
            p->file_bytes=((uint64_t)data.nFileSizeHigh<<32)|data.nFileSizeLow;
            /* Sequential saturation can stop recording without an event-loss
             * counter. A full or empty file cannot certify the entire window. */
            if(!p->file_bytes || p->file_bytes>=(uint64_t)WORKLOAD_PRESENT_FILE_MB*1024U*1024U)
                p->file_error=ERROR_INSUFFICIENT_BUFFER;
        }
        if(!p->file_error) {
            memset(&p->logfile,0,sizeof(p->logfile));
            p->logfile.file=p->file;
            p->logfile.mode=WORKLOAD_PRESENT_FILE_CONSUMER_MODE;
            p->logfile.event_callback=workload_present_event; p->logfile.context=p;
            p->consumer=p->open(&p->logfile);
            if(p->consumer==(uint64_t)-1) p->open_error=GetLastError();
            else {
                p->thread=CreateThread(NULL,0,workload_present_consumer,p,0,NULL);
                if(!p->thread) p->open_error=GetLastError();
            }
        }
    }
    if(p->thread && WaitForSingleObject(p->thread,0)!=WAIT_OBJECT_0) return 0;
    if(p->thread) { CloseHandle(p->thread); p->thread=NULL; }
    if(p->consumer!=(uint64_t)-1 && p->close) {
        p->close_error=p->close(p->consumer);
        if(p->close_error) return 0;
        p->consumer=(uint64_t)-1;
    }
    workload_present_remove_file(p); /* Report a failure; do not accumulate files. */
    if(p->tdh) { FreeLibrary(p->tdh); p->tdh=NULL; }
    if(p->advapi) { FreeLibrary(p->advapi); p->advapi=NULL; }
    return 1;
}

static int workload_present_valid(const WorkloadPresent *p)
{
    uint32_t i,chains=0,intervals=0;
    for(i=0;i<WORKLOAD_PRESENT_THREADS;++i) if(p->pending[i].thread) return 0;
    for(i=0;i<WORKLOAD_PRESENT_CHAINS;++i) if(p->chains[i].presents) {
        ++chains; intervals=p->chains[i].intervals;
    }
    return p->consumed && !p->error && !p->stop_error && !p->consume_error &&
        !p->open_error && !p->file_error && !p->close_error &&
        !p->properties.p.events_lost && !p->properties.p.log_lost &&
        !p->properties.p.realtime_lost &&
        !p->logfile.events_lost && !p->logfile.header.lost &&
        !p->property_errors && !p->failed && !p->unpaired && !p->overflow &&
        chains==1U && intervals>1U;
}
