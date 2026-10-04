/* Saved requested policy, not engine readiness or GPU state. The process owns
 * one pinned event worker and two bounded path buffers. No engine pointer or
 * Lua object enters this subsystem; no token is held during file I/O. */
#define SETTINGS_FILE_BYTES 128U
#define SETTINGS_POLICY_BITS (SHADOW_POLICY_DISABLE_ALL|SHADOW_POLICY_TEN_UNLIMITED|SHADOW_POLICY_LIMIT_MASK|SHADOW_POLICY_RESOLUTION_MASK)
/* Read-only migration of the retired SESAVE3 background-quality field. */
#define SETTINGS_LEGACY_BACKGROUND_SHIFT 20U
#define SETTINGS_LEGACY_BACKGROUND_MASK (3L<<SETTINGS_LEGACY_BACKGROUND_SHIFT)
#define SETTINGS_WORLD_BITS (SHADOW_WORLD_ENABLED|SHADOW_WORLD_RESOLUTION_MASK)
static SavedSettingsRecord saved_settings_defaults(void)
{
    SavedSettingsRecord r;
    r.policy=(4L<<SHADOW_POLICY_LIMIT_SHIFT)|(3L<<SHADOW_POLICY_RESOLUTION_SHIFT);
    r.world=3L<<SHADOW_WORLD_RESOLUTION_SHIFT;
    r.revision=0;
    return r;
}
static int saved_settings_valid(const SavedSettingsRecord *r)
{
    unsigned limit=(unsigned)((r->policy&SHADOW_POLICY_LIMIT_MASK)>>SHADOW_POLICY_LIMIT_SHIFT);
    unsigned vehicle=(unsigned)((r->policy&SHADOW_POLICY_RESOLUTION_MASK)>>SHADOW_POLICY_RESOLUTION_SHIFT);
    unsigned world=(unsigned)((r->world&SHADOW_WORLD_RESOLUTION_MASK)>>SHADOW_WORLD_RESOLUTION_SHIFT);
    return !(r->policy&~SETTINGS_POLICY_BITS) && !(r->world&~SETTINGS_WORLD_BITS) &&
        limit>=1U && limit<=VEHICLE_SELECTION_MAX_STABLE_VEHICLES &&
        (!(r->policy&SHADOW_POLICY_TEN_UNLIMITED) ||
            ((r->policy&SHADOW_POLICY_DISABLE_LIMITER) && limit==10U)) &&
        ((r->policy&SHADOW_POLICY_DISABLE_LIMITER) || limit<10U) &&
        vehicle>=1U && vehicle<=4U && world>=1U && world<=4U;
}
/* Exact ASCII decimal tokens with checked arithmetic. No partial records,
 * embedded NUL, signs, unknown fields or trailing bytes are accepted. */
static int saved_settings_number(const char **cursor,DWORD *value,char delimiter)
{
    const char *p=*cursor;
    DWORD n=0;
    if(*p<'0' || *p>'9') return 0;
    do {
        unsigned digit=(unsigned)(*p-'0');
        if(n>(0xffffffffUL-digit)/10U) return 0;
        n=n*10U+digit; ++p;
    } while(*p>='0' && *p<='9');
    if(*p!=delimiter) return 0;
    *value=n; *cursor=p+1; return 1;
}
static int saved_settings_parse(const char *text,DWORD bytes,SavedSettingsRecord *out)
{
    char copy[SETTINGS_FILE_BYTES];
    const char *p;
    DWORD revision,policy,world;
    int legacy;
    SavedSettingsRecord r;
    if(bytes<8U || bytes>=SETTINGS_FILE_BYTES || memchr(text,0,bytes) ||
       (memcmp(text,"SESAVE1 ",8U) && memcmp(text,"SESAVE2 ",8U) && memcmp(text,"SESAVE3 ",8U))) return 0;
    legacy=!memcmp(text,"SESAVE1 ",8U);
    memcpy(copy,text,bytes); copy[bytes]=0; p=copy;
    p+=8;
    if(!saved_settings_number(&p,&revision,' ') ||
       !saved_settings_number(&p,&policy,' ') ||
       !saved_settings_number(&p,&world,'\n') || p!=copy+bytes || !revision) return 0;
    r.policy=(LONG)policy; r.world=(LONG)world; r.revision=revision;
    if(!memcmp(text,"SESAVE3 ",8U)) {
        if(((r.policy&SETTINGS_LEGACY_BACKGROUND_MASK)>>SETTINGS_LEGACY_BACKGROUND_SHIFT)>2L) return 0;
        r.policy&=~SETTINGS_LEGACY_BACKGROUND_MASK;
    } else if(r.policy&SETTINGS_LEGACY_BACKGROUND_MASK) return 0;
    if(legacy) {
        /* Version1 did not own this bit. Migrate only an active saved10;
         * selected0 may remember10 and must remain0. Reading never rewrites
         * the source file or increments its revision. */
        if(r.policy&SHADOW_POLICY_TEN_UNLIMITED) return 0;
        if(!(r.policy&SHADOW_POLICY_DISABLE_LIMITER) &&
           ((r.policy&SHADOW_POLICY_LIMIT_MASK)>>SHADOW_POLICY_LIMIT_SHIFT)==10L)
            r.policy|=SHADOW_POLICY_TEN_UNLIMITED|SHADOW_POLICY_DISABLE_LIMITER;
    }
    if(!saved_settings_valid(&r)) return 0;
    *out=r; return 1;
}
static int saved_settings_merge(const SavedSettingsRecord *old,
    const SavedSettingsRequest *request,SavedSettingsRecord *next)
{
    unsigned mask=request->mask;
    *next=*old;
    if(!mask || (mask&~31U)) return 0;
    if(mask&SETTINGS_CHANGE_LIMIT) {
        if(request->limit>VEHICLE_SELECTION_MAX_STABLE_VEHICLES) return 0;
        next->policy&=~SHADOW_POLICY_TEN_UNLIMITED;
        if(!request->limit) next->policy|=SHADOW_POLICY_DISABLE_LIMITER;
        else {
            next->policy=(next->policy&~(SHADOW_POLICY_LIMIT_MASK|SHADOW_POLICY_DISABLE_LIMITER))|
                ((LONG)request->limit<<SHADOW_POLICY_LIMIT_SHIFT);
            if(request->limit==10U)
                next->policy|=SHADOW_POLICY_TEN_UNLIMITED|SHADOW_POLICY_DISABLE_LIMITER;
        }
    }
    if(mask&SETTINGS_CHANGE_VEHICLE) {
        if(request->vehicle_quality>4U) return 0;
        if(!request->vehicle_quality) next->policy|=SHADOW_POLICY_DISABLE_QUALITY;
        else next->policy=(next->policy&~(SHADOW_POLICY_RESOLUTION_MASK|SHADOW_POLICY_DISABLE_QUALITY))|
            ((LONG)request->vehicle_quality<<SHADOW_POLICY_RESOLUTION_SHIFT);
    }
    if(mask&SETTINGS_CHANGE_WORLD) {
        if(request->world_quality>4U) return 0;
        if(!request->world_quality) next->world&=~SHADOW_WORLD_ENABLED;
        else next->world=((LONG)request->world_quality<<SHADOW_WORLD_RESOLUTION_SHIFT)|SHADOW_WORLD_ENABLED;
    }
    if(mask&SETTINGS_CHANGE_POLICY) {
        if(request->policy_mask&~SETTINGS_POLICY_BITS) return 0;
        /* Legacy Boolean controls express0 or enable the retained bounded
         * count. They cannot silently turn remembered10 into a finite limit;
         * complete validation rejects that enable until a1..9 choice is made. */
        if((request->policy_mask&(SHADOW_POLICY_DISABLE_LIMITER|SHADOW_POLICY_LIMIT_MASK)) &&
           !(request->policy_mask&SHADOW_POLICY_TEN_UNLIMITED))
            next->policy&=~SHADOW_POLICY_TEN_UNLIMITED;
        next->policy=(next->policy&~request->policy_mask)|(request->policy_value&request->policy_mask);
        /* Legacy zero encodings denote four and2048, not invalid choices. */
        if(!(next->policy&SHADOW_POLICY_LIMIT_MASK)) next->policy|=4L<<SHADOW_POLICY_LIMIT_SHIFT;
        if(!(next->policy&SHADOW_POLICY_RESOLUTION_MASK)) next->policy|=3L<<SHADOW_POLICY_RESOLUTION_SHIFT;
    }
    if(mask&SETTINGS_CHANGE_WORLD_WORD) next->world=request->world_value;
    return saved_settings_valid(next);
}
static void saved_settings_publish(const SavedSettingsRecord *next)
{
    LONG previous=InterlockedExchange(&g_shadow_engine.policy_control.disabled,next->policy);
    InterlockedExchange(&g_shadow_engine.policy_control.world_configuration,next->world);
    if((previous^next->policy)&(SHADOW_POLICY_DISABLE_LIMITER|SHADOW_POLICY_LIMIT_MASK))
        InterlockedExchange(&g_shadow_engine.vehicle_diagnostics.residency_reset_requested,1);
}
static int saved_settings_lock(void)
{ return InterlockedCompareExchange(&g_shadow_engine.saved_settings.lock,1,0)==0; }
static void saved_settings_unlock(void)
{ InterlockedExchange(&g_shadow_engine.saved_settings.lock,0); }
/* Only control workers wait for this short scalar-copy token. */
static void saved_settings_worker_lock(void)
{ while(!saved_settings_lock()) Sleep(0); }
static int saved_settings_status(SavedSettingsStatus *out)
{
    SavedSettingsState *s=&g_shadow_engine.saved_settings;
    if(!saved_settings_lock()) return 0;
    out->current=s->current; out->processed_sequence=s->processed_sequence;
    out->pending_sequence=s->busy?s->pending.sequence:0U;
    out->result=s->result; out->error=s->error; out->load_reason=s->load_reason;
    saved_settings_unlock(); return 1;
}
/* Zero means accepted, negative is a terminal rejection. Callers with a
 * transport sequence must publish a rejected result as well as an ACK. */
static int saved_settings_submit(const SavedSettingsRequest *request)
{
    SavedSettingsState *s=&g_shadow_engine.saved_settings;
    SavedSettingsRecord next;
    if(InterlockedCompareExchange(&s->initialized,0,0)!=2 ||
       !InterlockedCompareExchange(&s->worker_ready,0,0)) return -4;
    if(!saved_settings_lock()) return -2;
    if(s->busy) { saved_settings_unlock(); return -2; }
    if(!saved_settings_merge(&s->current,request,&next)) { saved_settings_unlock(); return -1; }
    s->pending=*request; s->busy=1;
    if(!SetEvent(s->event)) {
        s->busy=0; InterlockedExchange(&s->worker_ready,0);
        saved_settings_unlock(); return -4;
    }
    saved_settings_unlock(); return 0;
}
static int saved_settings_queue_policy(LONG mask,LONG value,DWORD sequence)
{
    SavedSettingsRequest request={0};
    request.mask=SETTINGS_CHANGE_POLICY; request.policy_mask=mask;
    request.policy_value=value; request.sequence=sequence;
    return saved_settings_submit(&request);
}
static int saved_settings_write(const SavedSettingsRecord *record,DWORD *error)
{
    SavedSettingsState *s=&g_shadow_engine.saved_settings;
    char text[SETTINGS_FILE_BYTES];
    DWORD written=0;
    int length=_snprintf(text,sizeof(text),"SESAVE2 %lu %lu %lu\n",
        (unsigned long)record->revision,(unsigned long)record->policy,(unsigned long)record->world);
    HANDLE file;
    int ok;
    *error=0;
    if(length<=0 || length>=(int)sizeof(text)) { *error=ERROR_INVALID_DATA; return 0; }
    file=CreateFileW(s->temporary,GENERIC_WRITE,0,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);
    if(file==INVALID_HANDLE_VALUE) { *error=GetLastError(); return 0; }
    ok=WriteFile(file,text,(DWORD)length,&written,NULL)!=0;
    if(!ok) *error=GetLastError();
    else if(written!=(DWORD)length) { ok=0; *error=ERROR_WRITE_FAULT; }
    if(ok && !FlushFileBuffers(file)) { ok=0; *error=GetLastError(); }
    if(!CloseHandle(file)) { if(ok) *error=GetLastError(); ok=0; }
    if(ok && !MoveFileExW(s->temporary,s->path,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)) {
        ok=0; *error=GetLastError();
    }
    if(!ok) { if(!*error) *error=ERROR_WRITE_FAULT; DeleteFileW(s->temporary); }
    return ok;
}
/* One pending request; the worker claims its copy while busy remains set.
 * New requests cannot supersede a save in progress. Lua retains/coalesces edits. */
static int saved_settings_process_pending(void)
{
    SavedSettingsState *s=&g_shadow_engine.saved_settings;
    SavedSettingsRequest request;
    SavedSettingsRecord before,next;
    DWORD error=0;
    int result;
    saved_settings_worker_lock();
    if(!s->busy) { saved_settings_unlock(); return 0; }
    request=s->pending; before=s->current;
    saved_settings_unlock();
    if(!saved_settings_merge(&before,&request,&next)) result=-1;
    else if(next.policy==before.policy && next.world==before.world) result=2;
    else if(before.revision==0xffffffffUL) { result=-3; error=ERROR_ARITHMETIC_OVERFLOW; }
    else {
        next.revision=before.revision+1U;
        result=saved_settings_write(&next,&error)?1:-3;
    }
    saved_settings_worker_lock();
    if(result==1) { saved_settings_publish(&next); s->current=next; }
    if(request.sequence) {
        s->processed_sequence=request.sequence; s->result=result; s->error=error;
    }
    s->busy=0;
    saved_settings_unlock();
    append_log("STAGE_SAVED_SETTINGS sequence=%lu result=%d revision=%lu error=%lu requestedPolicy=0x%lX requestedWorld=0x%lX readinessUnchanged=1",
        (unsigned long)request.sequence,result,(unsigned long)(result==1?next.revision:before.revision),
        (unsigned long)error,(unsigned long)(result==1?next.policy:before.policy),
        (unsigned long)(result==1?next.world:before.world));
    return 1;
}
static DWORD WINAPI saved_settings_worker(void *unused)
{
    SavedSettingsState *s=&g_shadow_engine.saved_settings;
    (void)unused;
    for(;;) {
        if(WaitForSingleObject(s->event,INFINITE)!=WAIT_OBJECT_0) {
            DWORD error=GetLastError();
            saved_settings_worker_lock();
            s->worker_ready=0;
            if(s->busy && s->pending.sequence) {
                s->processed_sequence=s->pending.sequence; s->result=-4; s->error=error;
            }
            s->busy=0; saved_settings_unlock(); return 0;
        }
        saved_settings_process_pending();
    }
}
static void saved_settings_load(const wchar_t *directory)
{
    SavedSettingsState *s=&g_shadow_engine.saved_settings;
    char text[SETTINGS_FILE_BYTES];
    HANDLE file;
    DWORD bytes=0,error;
    int ok;
    s->current=saved_settings_defaults(); s->load_reason=4U;
    if(!directory || !directory[0] || lstrlenW(directory)+40>=MAX_PATH) return;
    if(directory[lstrlenW(directory)-1]!=L'\\' && directory[lstrlenW(directory)-1]!=L'/') return;
    lstrcpyW(s->path,directory); lstrcatW(s->path,L"ShadowEngineSettings.cfg");
    lstrcpyW(s->temporary,directory); lstrcatW(s->temporary,L"ShadowEngineSettings.cfg.tmp");
    file=CreateFileW(s->path,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
    if(file==INVALID_HANDLE_VALUE) {
        error=GetLastError(); s->load_reason=error==ERROR_FILE_NOT_FOUND?1U:3U; s->error=error; return;
    }
    ok=ReadFile(file,text,sizeof(text),&bytes,NULL)!=0;
    error=ok?0:GetLastError();
    if(!CloseHandle(file)) { ok=0; error=GetLastError(); }
    if(!ok) { s->load_reason=3U; s->error=error; return; }
    s->load_reason=saved_settings_parse(text,bytes,&s->current)?0U:2U;
}
static void saved_settings_initialize(const wchar_t *directory)
{
    SavedSettingsState *s=&g_shadow_engine.saved_settings;
    HMODULE pinned=NULL;
    HANDLE thread;
    if(InterlockedCompareExchange(&s->initialized,1,0)) return;
    saved_settings_load(directory);
    saved_settings_publish(&s->current);
    s->event=CreateEventW(NULL,FALSE,FALSE,NULL);
    if(s->path[0] && s->event && GetModuleHandleExW(
       GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
       (LPCWSTR)(uintptr_t)saved_settings_worker,&pinned)) {
        s->worker_ready=1;
        thread=CreateThread(NULL,0,saved_settings_worker,NULL,0,NULL);
        if(thread) CloseHandle(thread);
        else s->worker_ready=0;
    }
    if(!s->worker_ready && s->event) { CloseHandle(s->event); s->event=NULL; }
    InterlockedExchange(&s->initialized,2);
}
