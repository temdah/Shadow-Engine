#include "../internal/overhead_probe.h"
/* Module 10: session logging, memory validation, atomic helpers, and generic
 * x64 detour installation. Depends only on module 00. */
static LONG atomic_add_long(volatile LONG *target,LONG value)
{
    LONG observed,replaced;
    do {
        observed=*target;
        replaced=InterlockedCompareExchange(target,observed+value,observed);
    } while(replaced!=observed);
    return observed;
}

static LONG atomic_or_long(volatile LONG *target,LONG value)
{
    LONG observed,replaced;
    do {
        observed=*target;
        replaced=InterlockedCompareExchange(target,observed|value,observed);
    } while(replaced!=observed);
    return observed;
}

static int begin_session_log(void)
{
    wchar_t path[MAX_PATH],previous[MAX_PATH];
    char line[1024];
    DWORD written;
    DWORD rotation_error=0;
    HANDLE file;
    SYSTEMTIME local_time,utc_time;
    LONG state=InterlockedCompareExchange(&g_shadow_engine.bootstrap.session_log_state,1,0);
    if(state==2) return 1;
    if(state!=0) return 0;
    lstrcpynW(path,g_shadow_engine.bootstrap.snapshot.bin_directory,MAX_PATH);
    if(lstrlenW(path)+36>=MAX_PATH) {
        InterlockedExchange(&g_shadow_engine.bootstrap.session_log_state,-1);
        return 0;
    }
    lstrcpyW(previous,path);
    lstrcatW(previous,L"ShadowEnginePatch.previous.log");
    lstrcatW(path,L"ShadowEnginePatch.log");
    /* One bounded prior session. Never truncate if preservation fails. */
    if(!MoveFileExW(path,previous,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)) {
        rotation_error=GetLastError();
        if(rotation_error==ERROR_FILE_NOT_FOUND) rotation_error=0;
    }
    file=CreateFileW(path,rotation_error?FILE_APPEND_DATA:GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,
                     NULL,rotation_error?OPEN_ALWAYS:CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);
    if(file==INVALID_HANDLE_VALUE) {
        InterlockedExchange(&g_shadow_engine.bootstrap.session_log_state,-1);
        return 0;
    }
    GetLocalTime(&local_time);
    GetSystemTime(&utc_time);
    _snprintf(line,sizeof(line)-1,
        "SESSION_BEGIN schema=1 product=ShadowEnginePatch version=%s pid=%lu role=real-game cleanLog=%u rotationError=%lu localTime=%04u-%02u-%02uT%02u:%02u:%02u.%03u utcTime=%04u-%02u-%02uT%02u:%02u:%02u.%03uZ\r\n",
        PATCH_VERSION,(unsigned long)GetCurrentProcessId(),rotation_error?0U:1U,(unsigned long)rotation_error,
        (unsigned)local_time.wYear,(unsigned)local_time.wMonth,
        (unsigned)local_time.wDay,(unsigned)local_time.wHour,
        (unsigned)local_time.wMinute,(unsigned)local_time.wSecond,
        (unsigned)local_time.wMilliseconds,
        (unsigned)utc_time.wYear,(unsigned)utc_time.wMonth,
        (unsigned)utc_time.wDay,(unsigned)utc_time.wHour,
        (unsigned)utc_time.wMinute,(unsigned)utc_time.wSecond,
        (unsigned)utc_time.wMilliseconds);
    line[sizeof(line)-1]='\0';
    WriteFile(file,line,(DWORD)lstrlenA(line),&written,NULL);
    FlushFileBuffers(file);
    CloseHandle(file);
    InterlockedExchange(&g_shadow_engine.bootstrap.session_log_state,2);
    return 1;
}

static int __attribute__((format(printf,1,2))) append_log(const char *format,...)
{
    wchar_t path[MAX_PATH];
    char line[1024];
    DWORD written;
    HANDLE file;
    va_list args;
    int ok;
    if(InterlockedCompareExchange(&g_shadow_engine.bootstrap.session_log_state,2,2)!=2) return 0;
    OVERHEAD_BEGIN(log_timing,OH_LOG_WRITE);
    lstrcpynW(path,g_shadow_engine.bootstrap.snapshot.bin_directory,MAX_PATH);
    if(lstrlenW(path)+22>=MAX_PATH) { OVERHEAD_END(log_timing); return 0; }
    lstrcatW(path,L"ShadowEnginePatch.log");
    va_start(args,format);
    _vsnprintf(line,sizeof(line)-3,format,args);
    va_end(args);
    line[sizeof(line)-3]='\0';
    lstrcatA(line,"\r\n");
    file=CreateFileW(path,FILE_APPEND_DATA,FILE_SHARE_READ|FILE_SHARE_WRITE,
                     NULL,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);
    if(file==INVALID_HANDLE_VALUE) {
        InterlockedIncrement(&g_shadow_engine.bootstrap.log_write_failures);
        OVERHEAD_END(log_timing); return 0;
    }
    ok=WriteFile(file,line,(DWORD)lstrlenA(line),&written,NULL) && written==(DWORD)lstrlenA(line);
    if(!ok)
        InterlockedIncrement(&g_shadow_engine.bootstrap.log_write_failures);
    CloseHandle(file);
    OVERHEAD_END(log_timing);
    return ok;
}

/* Worker-only durability boundary for completed diagnostic reports. */
static int flush_session_log(void)
{
    wchar_t path[MAX_PATH];
    HANDLE file;
    int ok;
    if(InterlockedCompareExchange(&g_shadow_engine.bootstrap.session_log_state,2,2)!=2) return 0;
    OVERHEAD_BEGIN(flush_timing,OH_LOG_FLUSH);
    lstrcpynW(path,g_shadow_engine.bootstrap.snapshot.bin_directory,MAX_PATH);
    if(lstrlenW(path)+22>=MAX_PATH) { OVERHEAD_END(flush_timing); return 0; }
    lstrcatW(path,L"ShadowEnginePatch.log");
    file=CreateFileW(path,GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
    if(file==INVALID_HANDLE_VALUE) { OVERHEAD_END(flush_timing); return 0; }
    ok=FlushFileBuffers(file)!=0;
    CloseHandle(file);
    OVERHEAD_END(flush_timing);
    return ok;
}

static double elapsed_ms(LARGE_INTEGER value)
{
    if(!g_shadow_engine.bootstrap.snapshot.frequency.QuadPart) return 0.0;
    return (double)(value.QuadPart-g_shadow_engine.bootstrap.snapshot.initialize_time.QuadPart)*1000.0/
           (double)g_shadow_engine.bootstrap.snapshot.frequency.QuadPart;
}

static int raw_write_bytes(unsigned char *target,const void *data,size_t size)
{
    DWORD old_protect,ignored;
    if(!VirtualProtect(target,size,PAGE_EXECUTE_READWRITE,&old_protect)) return 0;
    memcpy(target,data,size);
    VirtualProtect(target,size,old_protect,&ignored);
    FlushInstructionCache(GetCurrentProcess(),target,size);
    return 1;
}

static void *patch_allocate(size_t size,void **published_pointer);
static int patch_write_bytes(unsigned char *target,const void *data,size_t size);

static int readable_memory(const void *address,size_t size)
{
    MEMORY_BASIC_INFORMATION info;
    uintptr_t start=(uintptr_t)address;
    uintptr_t end=start+size;
    if(!address || end<start || !VirtualQuery(address,&info,sizeof(info))) return 0;
    if(info.State!=MEM_COMMIT || (info.Protect&(PAGE_NOACCESS|PAGE_GUARD))) return 0;
    return end<=(uintptr_t)info.BaseAddress+info.RegionSize;
}

/* Read-only lifecycle lookup shared by diagnostics and the lifecycle owner.
 * Creation and mutation remain exclusively in module 40. */
static LifecycleRecordSnapshot *find_existing_lifecycle_record_fast(
    void *record)
{
    uint32_t start,index;
    if(!record) return NULL;
    start=(uint32_t)(((uintptr_t)record>>4U)&(LIFECYCLE_RECORD_SLOTS-1U));
    for(index=0;index<LIFECYCLE_RECORD_SLOTS;++index) {
        LifecycleRecordSnapshot *slot=
            &g_shadow_engine.external_results.lifecycle_records[
            (start+index)&(LIFECYCLE_RECORD_SLOTS-1U)];
        void *current=(void *)slot->record;
        if(current==record) return slot;
        if(!current) return NULL;
    }
    return NULL;
}

static void emit_absolute_jump(unsigned char *at,const void *destination)
{
    at[0]=0xFF; at[1]=0x25; at[2]=0; at[3]=0; at[4]=0; at[5]=0;
    *(const void **)(at+6)=destination;
}

static int install_detour(unsigned char *target,const unsigned char *expected,
                          size_t patch_size,const void *hook,void **original)
{
    unsigned char *trampoline;
    unsigned char patch[32];
    if(patch_size<14 || patch_size>sizeof(patch)) return 0;
    if(memcmp(target,expected,patch_size)!=0) return 0;
    trampoline=(unsigned char *)patch_allocate(patch_size+14,original);
    if(!trampoline) return 0;
    memcpy(trampoline,target,patch_size);
    emit_absolute_jump(trampoline+patch_size,target+patch_size);
    memset(patch,0x90,patch_size);
    emit_absolute_jump(patch,hook);
    /* A native thread may enter as soon as the executable write lands. Publish
     * the callable original first; the transaction clears it on full rollback. */
    InterlockedExchangePointer((PVOID volatile *)original,trampoline);
    if(!patch_write_bytes(target,patch,patch_size)) return 0;
    return 1;
}

static int install_inline_relay(uint64_t rva,const unsigned char *expected,
                                size_t overwrite,const unsigned char *body,
                                size_t body_size)
{
    unsigned char *relay;
    unsigned char patch[32];
    unsigned char *target=ENGINE_ADDRESS(rva);
    if(overwrite<14 || overwrite>sizeof(patch)) return 0;
    if(memcmp(target,expected,overwrite)!=0) return 0;
    relay=(unsigned char *)patch_allocate(body_size+14,NULL);
    if(!relay) return 0;
    memcpy(relay,body,body_size);
    emit_absolute_jump(relay+body_size,target+overwrite);
    memset(patch,0x90,overwrite);
    emit_absolute_jump(patch,relay);
    return patch_write_bytes(target,patch,overwrite);
}

/* The manager begins with a relative null-candidate JZ. The wrapper preserves
 * that behavior; its trampoline starts after the branch and copies the first
 * two complete prologue instructions, matching the validated v1.2.16 hook. */
