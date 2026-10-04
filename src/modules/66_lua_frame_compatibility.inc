/* Internal frame-format experiment. No detours, callbacks, side table, GC
 * policy changes or per-call logging. Existing prototypes are NOT migrated.
 * The readiness checks reject observed late startup; they are not a lock or
 * proof that every native Lua state is absent. NexusTools 1.1.13-ad643fa passed the user runtime test on 2026-10-03.
 * Exact format changes live in immutable, independently mapped profile data. */

static void lua_frame_compatibility_notice(LONG state,const char *reason,
                                         const char *version)
{
    if(InterlockedExchange(&g_shadow_engine.bootstrap.host_compatibility,state)==state) return;
    append_log("NEXUS_COMPATIBILITY state=%ld detected=%s lastRuntimeTested=1.1.13-ad643fa reason=%s shadowPreflight=required luaRepair=%s",
        (long)state,version,reason,state<=2?"independentlyValidated":"skipped");
    if(state>=2)
        append_log("NEXUS_COMPATIBILITY_WARNING This NexusTools build or optional Lua repair is unverified. Attempting compatibility with normal shadow safety checks. Last runtime-tested NexusTools: 1.1.13-ad643fa. If issues occur, revert to that complete NexusTools version. Unverified optional Lua writes are skipped; future compatibility is not guaranteed.");
}

static const LuaFrameHostProfile *lua_frame_host_profile(const unsigned char *base)
{
    const IMAGE_DOS_HEADER *dos;
    const IMAGE_NT_HEADERS64 *nt;
    size_t i;
    if(!base || !readable_memory(base,sizeof(*dos))) return NULL;
    dos=(const IMAGE_DOS_HEADER *)base;
    if(dos->e_magic!=IMAGE_DOS_SIGNATURE || dos->e_lfanew<0 || dos->e_lfanew>4096) return NULL;
    nt=(const IMAGE_NT_HEADERS64 *)(base+dos->e_lfanew);
    if(!readable_memory(nt,sizeof(*nt)) || nt->Signature!=IMAGE_NT_SIGNATURE ||
       nt->FileHeader.Machine!=IMAGE_FILE_MACHINE_AMD64 ||
       nt->FileHeader.SizeOfOptionalHeader<sizeof(IMAGE_OPTIONAL_HEADER64) ||
       nt->OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR64_MAGIC) return NULL;
    for(i=0;i<sizeof(g_lua_frame_host_profiles)/sizeof(g_lua_frame_host_profiles[0]);++i) {
        const LuaFrameHostProfile *p=&g_lua_frame_host_profiles[i];
        if(nt->FileHeader.TimeDateStamp==p->timestamp && nt->OptionalHeader.SizeOfImage==p->image_size)
            return p;
    }
    return NULL;
}

static int lua_frame_startup_window(void)
{
    HMODULE host=GetModuleHandleW(L"TroploNexusTools.ipe");
    HMODULE helper=g_shadow_engine.bootstrap.snapshot.troplo;
    const unsigned char *base=(const unsigned char *)host;
    const LuaFrameHostProfile *p=lua_frame_host_profile(base);
    volatile unsigned char *ready,*intercepted,*manager_started;
    void *volatile *original_run_game;
    if(!p) {
        lua_frame_compatibility_notice(3,"unknownHostIdentity","unknown");
        return 0;
    }
    /* Validate both the complete entry guard and the caller's exact static
     * instance argument before reading it. This never patches the host. */
    if(!helper || !readable_memory(base+p->initialize_rva,39) ||
       memcmp(base+p->initialize_rva,g_lua_host_initialize_prologue,27)!=0 ||
       memcmp(base+p->initialize_rva+27,p->guard_bytes,12)!=0 ||
       !readable_memory(base+p->caller_rva,12) ||
       memcmp(base+p->caller_rva,p->caller_bytes,12)!=0) {
        lua_frame_compatibility_notice(4,"hostLayoutOrHelperUnavailable",p->version);
        return 0;
    }
    manager_started=(volatile unsigned char *)(base+p->init_guard_data_rva);
    ready=(volatile unsigned char *)GetProcAddress(helper,"g_NexusReady");
    intercepted=(volatile unsigned char *)GetProcAddress(helper,"g_RunGameIntercepted");
    original_run_game=(void *volatile *)GetProcAddress(helper,"OriginalRunGame");
    if(!readable_memory((const void *)manager_started,1) ||
       !readable_memory((const void *)ready,1) ||
       !readable_memory((const void *)intercepted,1) ||
       !readable_memory((const void *)original_run_game,sizeof(void *)) ||
       *manager_started || *ready || !*intercepted || !*original_run_game) {
        lua_frame_compatibility_notice(4,"startupWindowUnavailable",p->version);
        return 0;
    }
    lua_frame_compatibility_notice(1,p->runtime_tested?"mappedStartupWindow":"mappedStartupWindowRuntimeTestPending",p->version);
    return 1;
}

static int prepare_lua_frame_repair(const RuntimeProfile *profile,int *enabled)
{
    size_t i,j;
    unsigned char *base=g_shadow_engine.bootstrap.disrupt_base;
    if(!enabled) return 0;
    *enabled=0;
    /* Optional compatibility must not reject an independently valid shadow
     * plan. Unknown/late hosts leave the native Lua format untouched. */
    if(!lua_frame_startup_window()) {
        append_log("LUA_FRAME_REPAIR_SKIPPED reason=hostOrStartupWindow mutation=0 shadows=independent");
        return 1;
    }
    if(!profile->lua_frame_site_count) {
        append_log("LUA_FRAME_REPAIR_UNSUPPORTED profile=%s mutation=0",profile->name);
        return 1;
    }
    if(!profile->lua_frame_sites) {
        lua_frame_compatibility_notice(5,"missingLuaSiteTable","seeHostLog");
        return 1;
    }
    for(i=0;i<profile->lua_frame_site_count;i++) {
        const LuaFramePatchSite *site=&profile->lua_frame_sites[i];
        if(!site->size || site->size>PATCH_TRANSACTION_MAX_WRITE_SIZE ||
           site->rva>=profile->image_size || site->size>profile->image_size-site->rva ||
           !readable_memory(base+site->rva,site->size) ||
           memcmp(base+site->rva,site->before,site->size)!=0) {
            append_log("LUA_FRAME_REPAIR_SKIPPED reason=siteMismatch site=%s mutation=0 shadows=independent",site->role);
            lua_frame_compatibility_notice(5,"gameLuaSitesChanged","seeHostLog");
            return 1;
        }
        for(j=0;j<i;j++) {
            const LuaFramePatchSite *prior=&profile->lua_frame_sites[j];
            if(site->rva<prior->rva+prior->size && prior->rva<site->rva+site->size) {
                lua_frame_compatibility_notice(5,"overlappingLuaSites","seeHostLog");
                return 1;
            }
        }
    }
    append_log("LUA_FRAME_REPAIR_PREFLIGHT sites=%u metadataByte=12 sourceLimitExclusive=250 experimental=1 synchronousGateProven=0",
        (unsigned)profile->lua_frame_site_count);
    *enabled=1;
    return 1;
}

static int apply_lua_frame_repair(const RuntimeProfile *profile,int *enabled)
{
    size_t i;
    unsigned char *base=g_shadow_engine.bootstrap.disrupt_base;
    if(!enabled) return 0;
    if(!*enabled) return 1;
    /* Revalidate the entire set immediately before any format write. Caller
     * owns the early transaction; a failure restores every applied site. */
    if(!prepare_lua_frame_repair(profile,enabled)) return 0;
    if(!*enabled) return 1;
    for(i=0;i<profile->lua_frame_site_count;i++) {
        const LuaFramePatchSite *site=&profile->lua_frame_sites[i];
        if(!patch_write_bytes(base+site->rva,site->after,site->size)) return 0;
    }
    for(i=0;i<profile->lua_frame_site_count;i++) {
        const LuaFramePatchSite *site=&profile->lua_frame_sites[i];
        if(memcmp(base+site->rva,site->after,site->size)!=0) return 0;
    }
    return 1;
}
