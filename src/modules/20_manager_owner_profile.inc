/* Module 20: shadow-manager detour, B4/A8 profile application, and owner-vector
 * pre-reservation/invariant checks. */
static int install_manager_detour(void)
{
    unsigned char *target=ENGINE_ADDRESS(SHADOW_MANAGER_RVA);
    unsigned char *trampoline;
    unsigned char patch[14];
    if(memcmp(target,g_manager_prologue,sizeof(g_manager_prologue))!=0) return 0;
    trampoline=(unsigned char *)patch_allocate(32,(void **)&g_shadow_engine.hooks.original_manager);
    if(!trampoline) return 0;
    memcpy(trampoline,target+9,5);
    emit_absolute_jump(trampoline+5,target+14);
    memset(patch,0x90,sizeof(patch));
    emit_absolute_jump(patch,hooked_shadow_manager);
    if(!patch_write_bytes(target,patch,sizeof(patch))) return 0;
    g_shadow_engine.hooks.original_manager=(ShadowManagerFn)trampoline;
    return 1;
}
/* Disrupt packs vector capacity into the allocator metadata dword at +0x0C.
 * Both the native reserve routine (+0x319160) and append routine (+0x30F670)
 * decode it as: (encoded >> 6) << ((encoded >> 1) & 31). */
static uint32_t decode_owner_vector_capacity(uint32_t encoded)
{
    uint32_t shift=(encoded>>1U)&31U;
    uint64_t base=(uint64_t)(encoded>>6U);
    uint64_t capacity=base<<shift;
    return capacity>UINT32_MAX ? UINT32_MAX : (uint32_t)capacity;
}
/* v0.7.6 raised the logical A8 ceiling after the profile callback had reserved
 * only four 0x700-byte owner records. Owner #5 then moved the vector while the
 * same admission pass still held raw pointers to earlier records. The reserve
 * helper preserves existing records, so it is safe here even when A0 is nonzero:
 * this hook runs before the current call enters admission and publishes its raw
 * owner pointers. Keep the validated eight-record physical reserve at that
 * boundary, require the native record count to remain unchanged, then advertise
 * the narrower logical A8 profile selected by this build. */
static int prepare_owner_vector_before_admission(void *manager,LONG call)
{
    unsigned char *base=(unsigned char *)manager;
    void **data=(void **)(base+0x98);
    uint32_t *size=(uint32_t *)(base+0xA0);
    uint32_t *encoded=(uint32_t *)(base+0xA4);
    uint32_t before_size=*size;
    uint32_t before_encoded=*encoded;
    uint32_t before_capacity=decode_owner_vector_capacity(before_encoded);
    void *before_data=*data;
    uint32_t after_size,after_encoded,after_capacity;
    void *after_data;

    InterlockedExchange(&g_shadow_engine.renderer.owner_capacity_before,(LONG)before_capacity);
    if((before_size!=0U && !before_data) || before_size>before_capacity) {
        *(uint32_t *)(base+0xA8)=0U;
        InterlockedExchange(&g_shadow_engine.renderer.owner_reserve_fail_closed,1);
        InterlockedExchange(&g_shadow_engine.renderer.owner_reserve_state,-1);
        if(InterlockedCompareExchange(&g_shadow_engine.renderer.owner_reserve_failure_logged,1,0)==0)
            append_log("STAGE_F_OWNER_RESERVE_FAIL_CLOSED call=%ld reason=invalidVectorBeforeReserve beforeData=%p beforeSize=%u beforeEncoded=0x%08X beforeCapacity=%u requested=%u safeA8=0 originalManagerSkipped=1",
                (long)call,before_data,before_size,before_encoded,before_capacity,
                OWNER_VECTOR_RESERVE_CAPACITY);
        return 0;
    }
    g_shadow_engine.hooks.owner_vector_reserve(data,OWNER_VECTOR_RESERVE_CAPACITY);
    after_data=*data;
    after_size=*size;
    after_encoded=*encoded;
    after_capacity=decode_owner_vector_capacity(after_encoded);
    InterlockedExchange(&g_shadow_engine.renderer.owner_capacity_after,(LONG)after_capacity);
    if(after_size!=before_size || after_capacity<OWNER_VECTOR_RESERVE_CAPACITY ||
       !after_data || after_size>after_capacity) {
        uint32_t safe_ceiling=!after_data ? 0U :
            (after_capacity<TARGET_CACHE_A8 ? after_capacity:TARGET_CACHE_A8);
        *(uint32_t *)(base+0xA8)=safe_ceiling;
        InterlockedExchange(&g_shadow_engine.renderer.owner_reserve_fail_closed,1);
        InterlockedExchange(&g_shadow_engine.renderer.owner_reserve_state,-1);
        if(InterlockedCompareExchange(&g_shadow_engine.renderer.owner_reserve_failure_logged,1,0)==0)
            append_log("STAGE_F_OWNER_RESERVE_FAIL_CLOSED call=%ld reason=reserveInvariantFailed beforeData=%p beforeSize=%u beforeEncoded=0x%08X beforeCapacity=%u afterData=%p afterSize=%u afterEncoded=0x%08X afterCapacity=%u requested=%u safeA8=%u admissionWillUseReservedCapacityOnly=1",
                (long)call,before_data,before_size,before_encoded,before_capacity,
                after_data,after_size,after_encoded,after_capacity,
                OWNER_VECTOR_RESERVE_CAPACITY,
                safe_ceiling);
        return 0;
    }

    *(uint32_t *)(base+0xA8)=TARGET_CACHE_A8;
    InterlockedExchange(&g_shadow_engine.renderer.owner_reserve_fail_closed,0);
    InterlockedExchange(&g_shadow_engine.renderer.owner_reserve_state,2);
    append_log("STAGE_F_OWNER_VECTOR_PRE_RESERVED call=%ld beforeData=%p beforeSize=%u beforeEncoded=0x%08X beforeCapacity=%u afterData=%p afterSize=%u afterEncoded=0x%08X afterCapacity=%u physicalReserve=%u A8=%u recordStride=0x700 reserveRva=0x319160 beforeAdmission=1",
        (long)call,before_data,before_size,before_encoded,before_capacity,
        after_data,after_size,after_encoded,after_capacity,
        OWNER_VECTOR_RESERVE_CAPACITY,TARGET_CACHE_A8);
    return 1;
}

static void __fastcall hooked_shadow_manager(void *manager,void *candidates,
    uint32_t candidate_count,void *bindings,void *context)
{
    LONG call=InterlockedIncrement(&g_shadow_engine.renderer.manager_calls);
    if(!candidates) return;
    if(InterlockedCompareExchange(&g_shadow_engine.renderer.profile_state,1,0)==0) {
        uint32_t before_a8=manager ?
            *(uint32_t *)((unsigned char *)manager+0xA8) : 0;
        uint32_t before_b4=manager ?
            *(uint32_t *)((unsigned char *)manager+0xB4) : 0;
        g_shadow_engine.hooks.profile_callback(manager);
        InterlockedExchange(&g_shadow_engine.renderer.observed_cache_a8,
            manager ? (LONG)*(uint32_t *)((unsigned char *)manager+0xA8) : -1);
        InterlockedExchange(&g_shadow_engine.renderer.observed_scheduler_b4,
            manager ? (LONG)*(uint32_t *)((unsigned char *)manager+0xB4) : -1);
        append_log("STAGE_E_PROFILE_INITIALIZED manager=%p call=%ld beforeA8=%u beforeB4=%u afterA8=%ld afterB4=%ld callbackRva=0x308A90 fullCandidatePassthrough=1 filtering=0 tracing=0",
            manager,(long)call,before_a8,before_b4,
            (long)g_shadow_engine.renderer.observed_cache_a8,(long)g_shadow_engine.renderer.observed_scheduler_b4);
        if(manager && !prepare_owner_vector_before_admission(manager,call)) {
            InterlockedExchange(&g_shadow_engine.renderer.profile_state,2);
            return;
        }
        InterlockedExchange(&g_shadow_engine.renderer.profile_state,2);
    }
    if(manager) {
        unsigned char *manager_bytes=(unsigned char *)manager;
        void *live_data=*(void **)(manager_bytes+0x98);
        uint32_t live_capacity=decode_owner_vector_capacity(
            *(uint32_t *)(manager_bytes+0xA4));
        uint32_t live_size=*(uint32_t *)(manager_bytes+0xA0);
        if(live_capacity>=OWNER_VECTOR_RESERVE_CAPACITY && live_data!=NULL &&
           live_size<=live_capacity) {
            *(uint32_t *)(manager_bytes+0xA8)=TARGET_CACHE_A8;
            InterlockedExchange(&g_shadow_engine.renderer.owner_capacity_after,(LONG)live_capacity);
            InterlockedExchange(&g_shadow_engine.renderer.owner_reserve_fail_closed,0);
            InterlockedExchange(&g_shadow_engine.renderer.owner_reserve_state,2);
        } else if((live_size!=0U && !live_data) ||
                  live_size>live_capacity) {
            *(uint32_t *)(manager_bytes+0xA8)=0U;
            InterlockedExchange(&g_shadow_engine.renderer.owner_reserve_fail_closed,1);
            InterlockedExchange(&g_shadow_engine.renderer.owner_reserve_state,-1);
            if(InterlockedCompareExchange(&g_shadow_engine.renderer.owner_reserve_failure_logged,1,0)==0)
                append_log("STAGE_F_OWNER_RESERVE_FAIL_CLOSED call=%ld reason=invalidLiveVector data=%p size=%u encoded=0x%08X decodedCapacity=%u requested=%u safeA8=0 originalManagerSkipped=1",
                    (long)call,live_data,live_size,
                    *(uint32_t *)(manager_bytes+0xA4),live_capacity,
                    TARGET_CACHE_A8);
            return;
        } else {
            if(!prepare_owner_vector_before_admission(manager,call)) return;
        }
    }
    {
        void *owner_base_before=manager ?
            *(void **)((unsigned char *)manager+0x98) : NULL;
    g_shadow_engine.hooks.original_manager(manager,candidates,candidate_count,bindings,context);
        if(manager && owner_base_before!=*(void **)((unsigned char *)manager+0x98)) {
            InterlockedIncrement(&g_shadow_engine.renderer.owner_base_changed_during_manager);
            InterlockedExchange(&g_shadow_engine.renderer.owner_reserve_fail_closed,1);
            InterlockedExchange(&g_shadow_engine.renderer.owner_reserve_state,-1);
            append_log("STAGE_F_OWNER_VECTOR_INVARIANT_BROKEN call=%ld beforeData=%p afterData=%p size=%u encoded=0x%08X decodedCapacity=%u A8=%u",
                (long)call,owner_base_before,
                *(void **)((unsigned char *)manager+0x98),
                *(uint32_t *)((unsigned char *)manager+0xA0),
                *(uint32_t *)((unsigned char *)manager+0xA4),
                decode_owner_vector_capacity(
                    *(uint32_t *)((unsigned char *)manager+0xA4)),
                *(uint32_t *)((unsigned char *)manager+0xA8));
        }
    }
    if(manager) {
        LONG live_a8=(LONG)*(uint32_t *)((unsigned char *)manager+0xA8);
        LONG live_b4=(LONG)*(uint32_t *)((unsigned char *)manager+0xB4);
        InterlockedExchange(&g_shadow_engine.renderer.observed_cache_a8,live_a8);
        InterlockedExchange(&g_shadow_engine.renderer.observed_scheduler_b4,live_b4);
        if(live_a8==TARGET_CACHE_A8 && live_b4==TARGET_DYNAMIC_B4 &&
           InterlockedCompareExchange(&g_shadow_engine.renderer.live_a8_ready_logged,1,0)==0) {
            append_log("STAGE_F_LIVE_MANAGER_READY call=%ld B4=%ld A8=%ld targetOrdinaryDynamicPositions=15 targetCachedOwners=4 physicalOwnerReserve=8 afterOriginalManager=1",
                (long)call,(long)live_b4,(long)live_a8);
        }
    }
}
