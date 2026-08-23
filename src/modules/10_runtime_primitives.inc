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
    wchar_t path[MAX_PATH];
    char line[1024];
    DWORD written;
    HANDLE file;
    SYSTEMTIME local_time,utc_time;
    LONG state=InterlockedCompareExchange(&g_session_log_state,1,0);
    if(state==2) return 1;
    if(state!=0) return 0;
    lstrcpynW(path,g_snapshot.bin_directory,MAX_PATH);
    if(lstrlenW(path)+22>=MAX_PATH) {
        InterlockedExchange(&g_session_log_state,-1);
        return 0;
    }
    lstrcatW(path,L"ShadowEnginePatch.log");
    file=CreateFileW(path,GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,
                     NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);
    if(file==INVALID_HANDLE_VALUE) {
        InterlockedExchange(&g_session_log_state,-1);
        return 0;
    }
    GetLocalTime(&local_time);
    GetSystemTime(&utc_time);
    _snprintf(line,sizeof(line)-1,
        "SESSION_BEGIN schema=1 product=ShadowEnginePatch version=%s pid=%lu role=real-game cleanLog=1 localTime=%04u-%02u-%02uT%02u:%02u:%02u.%03u utcTime=%04u-%02u-%02uT%02u:%02u:%02u.%03uZ\r\n",
        PATCH_VERSION,(unsigned long)GetCurrentProcessId(),
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
    InterlockedExchange(&g_session_log_state,2);
    return 1;
}

static void append_log(const char *format,...)
{
    wchar_t path[MAX_PATH];
    char line[1024];
    DWORD written;
    HANDLE file;
    va_list args;
    if(InterlockedCompareExchange(&g_session_log_state,2,2)!=2) return;
    lstrcpynW(path,g_snapshot.bin_directory,MAX_PATH);
    if(lstrlenW(path)+22>=MAX_PATH) return;
    lstrcatW(path,L"ShadowEnginePatch.log");
    va_start(args,format);
    _vsnprintf(line,sizeof(line)-3,format,args);
    va_end(args);
    line[sizeof(line)-3]='\0';
    lstrcatA(line,"\r\n");
    file=CreateFileW(path,FILE_APPEND_DATA,FILE_SHARE_READ|FILE_SHARE_WRITE,
                     NULL,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);
    if(file==INVALID_HANDLE_VALUE) return;
    WriteFile(file,line,(DWORD)lstrlenA(line),&written,NULL);
    CloseHandle(file);
}

static double elapsed_ms(LARGE_INTEGER value)
{
    if(!g_snapshot.frequency.QuadPart) return 0.0;
    return (double)(value.QuadPart-g_snapshot.initialize_time.QuadPart)*1000.0/
           (double)g_snapshot.frequency.QuadPart;
}

static int write_bytes(unsigned char *target,const void *data,size_t size)
{
    DWORD old_protect,ignored;
    if(!VirtualProtect(target,size,PAGE_EXECUTE_READWRITE,&old_protect)) return 0;
    memcpy(target,data,size);
    VirtualProtect(target,size,old_protect,&ignored);
    FlushInstructionCache(GetCurrentProcess(),target,size);
    return 1;
}

static int readable_memory(const void *address,size_t size)
{
    MEMORY_BASIC_INFORMATION info;
    uintptr_t start=(uintptr_t)address;
    uintptr_t end=start+size;
    if(!address || end<start || !VirtualQuery(address,&info,sizeof(info))) return 0;
    if(info.State!=MEM_COMMIT || (info.Protect&(PAGE_NOACCESS|PAGE_GUARD))) return 0;
    return end<=(uintptr_t)info.BaseAddress+info.RegionSize;
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
    trampoline=(unsigned char *)VirtualAlloc(NULL,patch_size+14,
        MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE);
    if(!trampoline) return 0;
    memcpy(trampoline,target,patch_size);
    emit_absolute_jump(trampoline+patch_size,target+patch_size);
    memset(patch,0x90,patch_size);
    emit_absolute_jump(patch,hook);
    if(!write_bytes(target,patch,patch_size)) {
        VirtualFree(trampoline,0,MEM_RELEASE);
        return 0;
    }
    *original=trampoline;
    return 1;
}

/* The manager begins with a relative null-candidate JZ. The wrapper preserves
 * that behavior; its trampoline starts after the branch and copies the first
 * two complete prologue instructions, matching the validated v1.2.16 hook. */
