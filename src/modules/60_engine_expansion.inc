/* Module 60: early queue layout, handle/pass routing, scheduler writes, physical
 * map construction, pass registration, and read-only pass-table proof. */
static void snapshot_native_handles(void *manager)
{
    uint32_t handles[ORIGINAL_LOCAL_MAPS];
    LONG nonzero=0,distinct=0;
    uint32_t i,j;
    if(!manager) return;
    memcpy(handles,(unsigned char *)manager+0x410,sizeof(handles));
    for(i=0;i<ORIGINAL_LOCAL_MAPS;++i) {
        if(!handles[i]) continue;
        ++nonzero;
        for(j=0;j<i;++j) if(handles[j]==handles[i]) break;
        if(j==i) ++distinct;
    }
    memcpy(g_native_handles,handles,sizeof(handles));
    memcpy(g_shadow_map_ids,handles,sizeof(handles));
    memcpy(g_shadow_map_ids+ORIGINAL_LOCAL_MAPS,g_extra_handles,
           sizeof(g_extra_handles));
    g_last_resource_manager=manager;
    InterlockedExchange(&g_native_nonzero_handles,nonzero);
    InterlockedExchange(&g_native_distinct_handles,distinct);
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
    relay=(unsigned char *)VirtualAlloc(NULL,body_size+14,
        MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE);
    if(!relay) return 0;
    memcpy(relay,body,body_size);
    emit_absolute_jump(relay+body_size,target+overwrite);
    memset(patch,0x90,overwrite);
    emit_absolute_jump(patch,relay);
    if(!write_bytes(target,patch,overwrite)) {
        VirtualFree(relay,0,MEM_RELEASE);
        return 0;
    }
    return 1;
}
static int embedded_dword_offset(const unsigned char *instruction,
                                 uint32_t value)
{
    size_t i;
    int found=-1;
    for(i=0;i+4<=16;++i) {
        if(memcmp(instruction+i,&value,4)==0) {
            if(found>=0) return -1;
            found=(int)i;
        }
    }
    return found;
}

static int validate_queue_layout(void)
{
    size_t i;
    static const unsigned char count[]={0xBE,0x10,0,0,0};
    static const unsigned char clear_a[]={0x41,0xB8,0x44,0,0,0};
    static const unsigned char clear_b[]={0x44,0x8D,0x42,0x44};
    for(i=0;i<sizeof(g_tail_patches)/sizeof(g_tail_patches[0]);++i) {
        if(embedded_dword_offset(
            ENGINE_ADDRESS(g_tail_patches[i].instruction_rva),
            g_tail_patches[i].old_value)<0) return 0;
    }
    return memcmp(ENGINE_ADDRESS(QUEUE_COUNT_INIT_RVA),count,sizeof(count))==0 &&
           memcmp(ENGINE_ADDRESS(QUEUE_CLEAR_A_SIZE_RVA),clear_a,sizeof(clear_a))==0 &&
           memcmp(ENGINE_ADDRESS(QUEUE_CLEAR_B_SIZE_RVA),clear_b,sizeof(clear_b))==0;
}

static int validate_queue_layout_applied(void)
{
    size_t i;
    static const unsigned char count[]={0xBE,0x18,0,0,0};
    static const unsigned char clear_a[]={0x41,0xB8,0x64,0,0,0};
    static const unsigned char clear_b[]={0x44,0x8D,0x42,0x64};
    for(i=0;i<sizeof(g_tail_patches)/sizeof(g_tail_patches[0]);++i) {
        if(embedded_dword_offset(
            ENGINE_ADDRESS(g_tail_patches[i].instruction_rva),
            g_tail_patches[i].new_value)<0) return 0;
    }
    return memcmp(ENGINE_ADDRESS(QUEUE_COUNT_INIT_RVA),count,sizeof(count))==0 &&
           memcmp(ENGINE_ADDRESS(QUEUE_CLEAR_A_SIZE_RVA),clear_a,sizeof(clear_a))==0 &&
           memcmp(ENGINE_ADDRESS(QUEUE_CLEAR_B_SIZE_RVA),clear_b,sizeof(clear_b))==0;
}

static int apply_queue_layout(void)
{
    size_t i;
    static const unsigned char count[]={0xBE,0x18,0,0,0};
    static const unsigned char clear_a[]={0x41,0xB8,0x64,0,0,0};
    static const unsigned char clear_b[]={0x44,0x8D,0x42,0x64};
    for(i=0;i<sizeof(g_tail_patches)/sizeof(g_tail_patches[0]);++i) {
        int offset=embedded_dword_offset(
            ENGINE_ADDRESS(g_tail_patches[i].instruction_rva),
            g_tail_patches[i].old_value);
        if(offset<0 || !write_bytes(
            ENGINE_ADDRESS(g_tail_patches[i].instruction_rva)+offset,
            &g_tail_patches[i].new_value,4)) return 0;
    }
    return write_bytes(ENGINE_ADDRESS(QUEUE_COUNT_INIT_RVA),count,sizeof(count)) &&
           write_bytes(ENGINE_ADDRESS(QUEUE_CLEAR_A_SIZE_RVA),clear_a,sizeof(clear_a)) &&
           write_bytes(ENGINE_ADDRESS(QUEUE_CLEAR_B_SIZE_RVA),clear_b,sizeof(clear_b));
}

static int validate_slice_result_store_sites(uint32_t result_pointer_offset)
{
    unsigned char nonnull_site[15]={
        0x49,0x8B,0x8D,0,0,0,0,
        0x48,0x8B,0x94,0x24,0xD8,0,0,0
    };
    unsigned char null_site[15]={
        0x49,0x8B,0x85,0,0,0,0,
        0x48,0x8B,0x94,0x24,0xD8,0,0,0
    };
    memcpy(nonnull_site+3,&result_pointer_offset,4);
    memcpy(null_site+3,&result_pointer_offset,4);
    return memcmp(ENGINE_ADDRESS(SLICE_RESULT_NONNULL_RVA),
                  nonnull_site,sizeof(nonnull_site))==0 &&
           memcmp(ENGINE_ADDRESS(SLICE_RESULT_NULL_RVA),
                  null_site,sizeof(null_site))==0;
}

static void emit_atomic_increment(unsigned char *body,size_t *size,
                                  volatile LONG *counter)
{
    uint64_t address=(uint64_t)(uintptr_t)counter;
    size_t n=*size;
    body[n++]=0x49; body[n++]=0xBB; /* mov r11,imm64 */
    memcpy(body+n,&address,8); n+=8;
    body[n++]=0xF0; body[n++]=0x41; body[n++]=0xFF; body[n++]=0x03;
                                         /* lock inc dword ptr [r11] */
    *size=n;
}

static void emit_relay_jump(unsigned char *body,size_t *size,
                            const void *destination)
{
    emit_absolute_jump(body+*size,destination);
    *size+=14;
}

static int install_slice_result_bounds_relays(void)
{
    unsigned char body[128];
    size_t n,external_branch,external_label;
    uint32_t result_offset=RENDER_QUEUE_RESULT_OFFSET;
    uint64_t external_store=(uint64_t)(uintptr_t)store_external_slice_result;
    static const unsigned char expected_nonnull[15]={
        0x49,0x8B,0x8D,0xB0,0x97,0x03,0x00,
        0x48,0x8B,0x94,0x24,0xD8,0x00,0x00,0x00
    };
    static const unsigned char expected_null[15]={
        0x49,0x8B,0x85,0xB0,0x97,0x03,0x00,
        0x48,0x8B,0x94,0x24,0xD8,0x00,0x00,0x00
    };
    unsigned char *epilogue=ENGINE_ADDRESS(SLICE_RESULT_EPILOGUE_RVA);

    if(result_offset!=0x397B0U ||
       !validate_slice_result_store_sites(result_offset)) return 0;

    n=0;
    body[n++]=0x49; body[n++]=0x8B; body[n++]=0x8D;
    memcpy(body+n,&result_offset,4); n+=4; /* mov rcx,[r13+result_offset] */
    body[n++]=0x48; body[n++]=0x8B; body[n++]=0x94; body[n++]=0x24;
    body[n++]=0xD8; body[n++]=0; body[n++]=0; body[n++]=0;
                                           /* mov rdx,[rsp+d8] */
    body[n++]=0x83; body[n++]=0xFA; body[n++]=0x10; /* cmp edx,16 */
    body[n++]=0x77; external_branch=n++;            /* ja external */
    body[n++]=0x48; body[n++]=0x8B; body[n++]=0x85;
    body[n++]=0xA0; body[n++]=0x20; body[n++]=0; body[n++]=0;
                                           /* mov rax,[rbp+20a0] */
    body[n++]=0x48; body[n++]=0x89; body[n++]=0x04; body[n++]=0xD1;
                                           /* mov [rcx+rdx*8],rax */
    emit_atomic_increment(body,&n,&g_slice_result_native_nonnull);
    emit_relay_jump(body,&n,epilogue);
    external_label=n;
    if(external_label-(external_branch+1)>127U) return 0;
    body[external_branch]=(unsigned char)
        (external_label-(external_branch+1));
    body[n++]=0x4C; body[n++]=0x8B; body[n++]=0x85;
    body[n++]=0xA0; body[n++]=0x20; body[n++]=0; body[n++]=0;
                                           /* mov r8,[rbp+20a0] */
    body[n++]=0x48; body[n++]=0xB8;
    memcpy(body+n,&external_store,8); n+=8; /* mov rax,helper */
    body[n++]=0xFF; body[n++]=0xD0;         /* call rax */
    emit_relay_jump(body,&n,epilogue);
    if(!install_inline_relay(SLICE_RESULT_NONNULL_RVA,expected_nonnull,
                             sizeof(expected_nonnull),body,n)) return 0;

    n=0;
    body[n++]=0x49; body[n++]=0x8B; body[n++]=0x85;
    memcpy(body+n,&result_offset,4); n+=4; /* mov rax,[r13+result_offset] */
    body[n++]=0x48; body[n++]=0x8B; body[n++]=0x94; body[n++]=0x24;
    body[n++]=0xD8; body[n++]=0; body[n++]=0; body[n++]=0;
                                           /* mov rdx,[rsp+d8] */
    body[n++]=0x83; body[n++]=0xFA; body[n++]=0x10; /* cmp edx,16 */
    body[n++]=0x77; external_branch=n++;            /* ja external */
    body[n++]=0x48; body[n++]=0xC7; body[n++]=0x04; body[n++]=0xD0;
    body[n++]=0; body[n++]=0; body[n++]=0; body[n++]=0;
                                           /* mov qword [rax+rdx*8],0 */
    emit_atomic_increment(body,&n,&g_slice_result_native_null);
    emit_relay_jump(body,&n,epilogue);
    external_label=n;
    if(external_label-(external_branch+1)>127U) return 0;
    body[external_branch]=(unsigned char)
        (external_label-(external_branch+1));
    body[n++]=0x48; body[n++]=0x8B; body[n++]=0xC8; /* mov rcx,rax */
    body[n++]=0x45; body[n++]=0x33; body[n++]=0xC0; /* xor r8d,r8d */
    body[n++]=0x48; body[n++]=0xB8;
    memcpy(body+n,&external_store,8); n+=8; /* mov rax,helper */
    body[n++]=0xFF; body[n++]=0xD0;         /* call rax */
    emit_relay_jump(body,&n,epilogue);
    if(!install_inline_relay(SLICE_RESULT_NULL_RVA,expected_null,
                             sizeof(expected_null),body,n)) return 0;

    InterlockedExchange(&g_slice_result_guard_state,2);
    return 1;
}

static int validate_handle_relays(void)
{
    static const unsigned char edx[18]={
        0x8B,0x94,0x87,0x10,0x04,0,0,0x44,0x89,0x55,0x54,
        0xC7,0x45,0x58,1,0,0,0};
    static const unsigned char ecx[15]={
        0x42,0x8B,0x8C,0xB7,0x10,0x04,0,0,0x48,0x8B,0x53,0x58,
        0x89,0x4A,0x48};
    static const unsigned char address[19]={
        0x48,0x8D,0x94,0x8F,0x10,0x04,0,0,0x48,0x8B,0x4F,8,
        0x4C,0x89,0xAD,0,4,0,0};
    return memcmp(ENGINE_ADDRESS(HANDLE_LOAD_EDX_RVA),edx,sizeof(edx))==0 &&
           memcmp(ENGINE_ADDRESS(HANDLE_LOAD_ECX_RVA),ecx,sizeof(ecx))==0 &&
           memcmp(ENGINE_ADDRESS(HANDLE_ADDRESS_RDX_RVA),address,sizeof(address))==0;
}

static int install_handle_relays(void)
{
    unsigned char body[64];
    size_t n;
    const uint64_t table=(uint64_t)(uintptr_t)g_shadow_map_ids;
    static const unsigned char exp_edx[18]={
        0x8B,0x94,0x87,0x10,0x04,0,0,0x44,0x89,0x55,0x54,
        0xC7,0x45,0x58,1,0,0,0};
    static const unsigned char tail_edx[11]={
        0x44,0x89,0x55,0x54,0xC7,0x45,0x58,1,0,0,0};
    static const unsigned char exp_ecx[15]={
        0x42,0x8B,0x8C,0xB7,0x10,0x04,0,0,0x48,0x8B,0x53,0x58,
        0x89,0x4A,0x48};
    static const unsigned char tail_ecx[7]={
        0x48,0x8B,0x53,0x58,0x89,0x4A,0x48};
    static const unsigned char exp_address[19]={
        0x48,0x8D,0x94,0x8F,0x10,0x04,0,0,0x48,0x8B,0x4F,8,
        0x4C,0x89,0xAD,0,4,0,0};
    static const unsigned char tail_address[11]={
        0x48,0x8B,0x4F,8,0x4C,0x89,0xAD,0,4,0,0};

    n=0; body[n++]=0x48; body[n++]=0xBA; memcpy(body+n,&table,8); n+=8;
    body[n++]=0x8B; body[n++]=0x14; body[n++]=0x82;
    memcpy(body+n,tail_edx,sizeof(tail_edx)); n+=sizeof(tail_edx);
    if(!install_inline_relay(HANDLE_LOAD_EDX_RVA,exp_edx,sizeof(exp_edx),body,n))
        return 0;

    n=0; body[n++]=0x48; body[n++]=0xB9; memcpy(body+n,&table,8); n+=8;
    body[n++]=0x42; body[n++]=0x8B; body[n++]=0x0C; body[n++]=0xB1;
    memcpy(body+n,tail_ecx,sizeof(tail_ecx)); n+=sizeof(tail_ecx);
    if(!install_inline_relay(HANDLE_LOAD_ECX_RVA,exp_ecx,sizeof(exp_ecx),body,n))
        return 0;

    n=0; body[n++]=0x48; body[n++]=0xBA; memcpy(body+n,&table,8); n+=8;
    body[n++]=0x48; body[n++]=0x8D; body[n++]=0x14; body[n++]=0x8A;
    memcpy(body+n,tail_address,sizeof(tail_address)); n+=sizeof(tail_address);
    return install_inline_relay(HANDLE_ADDRESS_RDX_RVA,exp_address,
                                sizeof(exp_address),body,n);
}

static int validate_pass_index_relays(void)
{
    static const unsigned char lookup[15]={
        0x41,0x8D,4,0x0E,0x41,0xC7,0x87,0x4C,5,0,0,
        0xFF,0xFF,0x7F,0xFF};
    static const unsigned char store[17]={
        0x41,0x8D,4,0x0E,0x49,0x89,0x9C,0x34,0xF0,7,0,0,
        0xB9,4,0,0,0};
    return memcmp(ENGINE_ADDRESS(PASS_INDEX_LOOKUP_RVA),lookup,sizeof(lookup))==0 &&
           memcmp(ENGINE_ADDRESS(PASS_INDEX_STORE_RVA),store,sizeof(store))==0;
}

static int validate_v1216_scheduler(void)
{
    static const unsigned char scheduler_init[]={
        0xC7,0x85,0xB4,0,0,0,4,0,0,0};
    static const unsigned char scheduler_floor[]={0xBA,4,0,0,0};
    static const unsigned char scheduler_upper[]={0x41,0xB8,8,0,0,0};
    return memcmp(ENGINE_ADDRESS(SCHEDULER_INIT_RVA),scheduler_init,
                  sizeof(scheduler_init))==0 &&
           memcmp(ENGINE_ADDRESS(SCHEDULER_FLOOR_RVA),scheduler_floor,
                  sizeof(scheduler_floor))==0 &&
           memcmp(ENGINE_ADDRESS(SCHEDULER_UPPER_RVA),scheduler_upper,
                  sizeof(scheduler_upper))==0;
}

static int apply_v1216_scheduler(void)
{
    static const unsigned char scheduler_init[]={
        0xC7,0x85,0xB4,0,0,0,0x10,0,0,0};
    static const unsigned char scheduler_floor[]={0xBA,0x10,0,0,0};
    static const unsigned char scheduler_upper[]={0x41,0xB8,0x10,0,0,0};
    return write_bytes(ENGINE_ADDRESS(SCHEDULER_INIT_RVA),scheduler_init,
                       sizeof(scheduler_init)) &&
           write_bytes(ENGINE_ADDRESS(SCHEDULER_FLOOR_RVA),scheduler_floor,
                       sizeof(scheduler_floor)) &&
           write_bytes(ENGINE_ADDRESS(SCHEDULER_UPPER_RVA),scheduler_upper,
                       sizeof(scheduler_upper));
}

static int install_pass_index_relays(void)
{
    unsigned char body[64];
    size_t n;
    static const unsigned char exp_lookup[15]={
        0x41,0x8D,4,0x0E,0x41,0xC7,0x87,0x4C,5,0,0,
        0xFF,0xFF,0x7F,0xFF};
    static const unsigned char tail_lookup[11]={
        0x41,0xC7,0x87,0x4C,5,0,0,0xFF,0xFF,0x7F,0xFF};
    static const unsigned char exp_store[17]={
        0x41,0x8D,4,0x0E,0x49,0x89,0x9C,0x34,0xF0,7,0,0,
        0xB9,4,0,0,0};
    static const unsigned char tail_store[13]={
        0x49,0x89,0x9C,0x34,0xF0,7,0,0,0xB9,4,0,0,0};
    static const unsigned char map_index[]={
        0x41,0x8D,4,0x0E,0x83,0xF8,0x10,0x72,2,0xFF,0xC0};

    n=0; memcpy(body+n,map_index,sizeof(map_index)); n+=sizeof(map_index);
    memcpy(body+n,tail_lookup,sizeof(tail_lookup)); n+=sizeof(tail_lookup);
    if(!install_inline_relay(PASS_INDEX_LOOKUP_RVA,exp_lookup,
                             sizeof(exp_lookup),body,n)) return 0;
    n=0; memcpy(body+n,map_index,sizeof(map_index)); n+=sizeof(map_index);
    memcpy(body+n,tail_store,sizeof(tail_store)); n+=sizeof(tail_store);
    return install_inline_relay(PASS_INDEX_STORE_RVA,exp_store,
                                sizeof(exp_store),body,n);
}

static int validate_map_range_loop_extension(void);

static int install_map_range_loop_extension(void)
{
    unsigned char body[128];
    unsigned char expected_store[27];
    size_t n=0;
    uint64_t handle_address=(uint64_t)(uintptr_t)&g_extra_handles[0];
    uint64_t companion_address=(uint64_t)(uintptr_t)&g_extra_companions[0][0];
    uint64_t random_function=(uint64_t)(uintptr_t)
        (g_disrupt_base+g_native_random_rva);
    static const unsigned char expected_bound[9]={
        0x83,0xFF,0x10,0x0F,0x82,0xEE,0xFE,0xFF,0xFF
    };
    static const unsigned char replacement_bound[9]={
        0x83,0xFF,0x18,0x0F,0x82,0xEE,0xFE,0xFF,0xFF
    };

    if(!validate_map_range_loop_extension()) return 0;
    memcpy(expected_store,ENGINE_ADDRESS(MAP_LOOP_STORE_RVA),
           sizeof(expected_store));
    if(memcmp(ENGINE_ADDRESS(MAP_LOOP_BOUND_RVA),expected_bound,
              sizeof(expected_bound))!=0) return 0;

    /* Preserve the native result load. */
    memcpy(body+n,expected_store,7); n+=7;
    /* For EDI>=16, redirect R12/R13 from overlapping manager-tail addresses
     * into external handle[8] and aligned companion[8][16] arrays. */
    body[n++]=0x83; body[n++]=0xFF; body[n++]=0x10; /* cmp edi,16 */
    body[n++]=0x72; body[n++]=0x24;                 /* jb +36 */
    body[n++]=0x8B; body[n++]=0xC7;                /* mov eax,edi */
    body[n++]=0x83; body[n++]=0xE8; body[n++]=0x10;/* sub eax,16 */
    body[n++]=0x49; body[n++]=0xBC;                /* mov r12,imm64 */
    memcpy(body+n,&handle_address,8); n+=8;
    body[n++]=0x4D; body[n++]=0x8D; body[n++]=0x24; body[n++]=0x84;
                                                       /* lea r12,[r12+rax*4] */
    body[n++]=0x49; body[n++]=0xBD;                /* mov r13,imm64 */
    memcpy(body+n,&companion_address,8); n+=8;
    body[n++]=0x48; body[n++]=0xC1; body[n++]=0xE0; body[n++]=0x04;
                                                       /* shl rax,4 */
    body[n++]=0x49; body[n++]=0x01; body[n++]=0xC5;/* add r13,rax */
    /* Reproduce the native handle store. Re-encode both relative CALLs as
     * absolute calls because the relay may not be within rel32 range. */
    body[n++]=0x45; body[n++]=0x89; body[n++]=0x1C; body[n++]=0x24;
    body[n++]=0x48; body[n++]=0xB8;
    memcpy(body+n,&random_function,8); n+=8;
    body[n++]=0xFF; body[n++]=0xD0;
    body[n++]=0x0F; body[n++]=0x28; body[n++]=0xF0;
    body[n++]=0x48; body[n++]=0xB8;
    memcpy(body+n,&random_function,8); n+=8;
    body[n++]=0xFF; body[n++]=0xD0;
    body[n++]=0x0F; body[n++]=0x28; body[n++]=0xF8;

    if(!install_inline_relay(MAP_LOOP_STORE_RVA,expected_store,
                             sizeof(expected_store),body,n)) return 0;
    if(!write_bytes(ENGINE_ADDRESS(MAP_LOOP_BOUND_RVA),replacement_bound,
                    sizeof(replacement_bound))) return 0;
    return 1;
}

static int validate_map_range_loop_extension(void)
{
    static const unsigned char bound[9]={
        0x83,0xFF,0x10,0x0F,0x82,0xEE,0xFE,0xFF,0xFF
    };
    static const unsigned char prefix[11]={
        0x44,0x8B,0x9D,0x30,0x08,0x00,0x00,0x45,0x89,0x1C,0x24
    };
    static const unsigned char xmm1[3]={0x0F,0x28,0xF0};
    static const unsigned char xmm2[3]={0x0F,0x28,0xF8};
    const unsigned char *store=ENGINE_ADDRESS(MAP_LOOP_STORE_RVA);
    int32_t rel1,rel2;
    uint64_t call1,call2;
    if(memcmp(store,prefix,sizeof(prefix))!=0 || store[11]!=0xE8 ||
       memcmp(store+16,xmm1,sizeof(xmm1))!=0 || store[19]!=0xE8 ||
       memcmp(store+24,xmm2,sizeof(xmm2))!=0 ||
       memcmp(ENGINE_ADDRESS(MAP_LOOP_BOUND_RVA),bound,sizeof(bound))!=0)
        return 0;
    memcpy(&rel1,store+12,4);
    memcpy(&rel2,store+20,4);
    call1=RESOLVED_ENGINE_RVA(MAP_LOOP_STORE_RVA)+16+(int64_t)rel1;
    call2=RESOLVED_ENGINE_RVA(MAP_LOOP_STORE_RVA)+24+(int64_t)rel2;
    return call1==g_native_random_rva && call2==g_native_random_rva;
}

/* v0.8.4 proved this registry already owns 64 physical slots (maximum 0x3F00).
 * Keep direct, read-only capacity verification before adding pass pointers, but
 * do not hook or mutate its allocator. */
static int get_shadow_pass_table(void *registry,void ***slot_array,
    uint32_t *slot_count,uint32_t *maximum_key)
{
    unsigned char *table,*record;
    void **slots;
    uint32_t count,maximum;
    if(!registry || !readable_memory((unsigned char *)registry+8U,
                                     sizeof(void *))) return 0;
    table=*(unsigned char **)((unsigned char *)registry+8U);
    if(!table) return 0;
    g_pass_table_base=table;
    record=table+SHADOW_PASS_CATEGORY*16U;
    if(!readable_memory(record,16U)) return 0;
    slots=*(void ***)record;
    count=*(uint32_t *)(record+8U);
    maximum=*(uint32_t *)(record+12U);
    if(!slots || count<REQUIRED_PASS_TABLE_SLOTS || count>4096U ||
       maximum<REQUIRED_PASS_MAX_KEY ||
       !readable_memory(slots,(size_t)count*sizeof(void *))) return 0;
    if(slot_array) *slot_array=slots;
    if(slot_count) *slot_count=count;
    if(maximum_key) *maximum_key=maximum;
    return 1;
}

static int verify_added_pass_slots(void *registry,uint32_t *nonzero_out,
    uint32_t *distinct_out)
{
    void **slots;
    uint32_t count,maximum,index,j,nonzero=0,distinct=0;
    if(!get_shadow_pass_table(registry,&slots,&count,&maximum)) {
        if(nonzero_out) *nonzero_out=0;
        if(distinct_out) *distinct_out=0;
        return 0;
    }
    for(index=0;index<EXTRA_PASS_SLOT_COUNT;++index) {
        void *value=slots[FIRST_EXTRA_PASS_SLOT+index];
        int unique=value!=NULL;
        if(value) ++nonzero;
        for(j=0;unique && j<index;++j)
            if(value==slots[FIRST_EXTRA_PASS_SLOT+j]) unique=0;
        if(unique) ++distinct;
    }
    if(nonzero_out) *nonzero_out=nonzero;
    if(distinct_out) *distinct_out=distinct;
    return nonzero==EXTRA_PASS_SLOT_COUNT &&
           distinct==EXTRA_PASS_SLOT_COUNT;
}

static void __fastcall hooked_resource_constructor(void *manager)
{
    uint32_t i,j;
    LONG nonzero=0,distinct=0;
    g_original_resource_constructor(manager);
    snapshot_native_handles(manager);
    if(manager) {
        InterlockedExchange(&g_observed_scheduler_b4,
            (LONG)*(uint32_t *)((unsigned char *)manager+0xB4));
        append_log("STAGE_E_CONSTRUCTOR_NATIVE_STATE manager=%p B4=%ld A8=%u downstreamRoutingDeferred=1 schedulerPatchDeferred=1 managerHookDeferred=1",
            manager,(long)g_observed_scheduler_b4,
            *(uint32_t *)((unsigned char *)manager+0xA8));
    }
    for(i=0;i<EXTRA_LOCAL_MAPS;++i) {
        int unique=g_extra_handles[i]!=0;
        if(g_extra_handles[i]) ++nonzero;
        for(j=0;unique && j<ORIGINAL_LOCAL_MAPS;++j)
            if(g_extra_handles[i]==g_native_handles[j]) unique=0;
        for(j=0;unique && j<i;++j)
            if(g_extra_handles[i]==g_extra_handles[j]) unique=0;
        if(unique) ++distinct;
    }
    InterlockedExchange(&g_range_nonzero_handles,nonzero);
    InterlockedExchange(&g_range_distinct_handles,distinct);
    InterlockedExchange(&g_range_resource_state,
        (nonzero==EXTRA_LOCAL_MAPS && distinct==EXTRA_LOCAL_MAPS) ? 2 : -1);
    if(nonzero==EXTRA_LOCAL_MAPS && distinct==EXTRA_LOCAL_MAPS) {
        append_log("STAGE_E_HANDLE_TABLE_READY table=%p nativeEntries=16 externalEntries=8 handle16=%08X handle23=%08X downstreamRoutingDeferred=1",
            (void *)g_shadow_map_ids,g_shadow_map_ids[16],g_shadow_map_ids[23]);
    }
    append_log("STAGE_E_RESOURCE_RESULT handles=%08X,%08X,%08X,%08X,%08X,%08X,%08X,%08X nonZero=%ld distinctFromNativeAndEachOther=%ld externalCompanions=%p storedExternally=8 nativeLoopExecuted24=1 nativeManagerLayoutChanged=0 downstreamRoutingDeferred=1",
        g_extra_handles[0],g_extra_handles[1],g_extra_handles[2],g_extra_handles[3],
        g_extra_handles[4],g_extra_handles[5],g_extra_handles[6],g_extra_handles[7],
        (long)nonzero,(long)distinct,(void *)&g_extra_companions[0][0]);
    for(i=0;i<EXTRA_LOCAL_MAPS;++i) {
        append_log("STAGE_E_MAP map=%u pass=%u handle=%08X nonZero=%u companion=%p downstreamRoutingDeferred=1",
            ORIGINAL_LOCAL_MAPS+i,ORIGINAL_LOCAL_MAPS+i+1,g_extra_handles[i],
            g_extra_handles[i] ? 1U : 0U,(void *)&g_extra_companions[i][0]);
        }
    InterlockedIncrement(&g_resource_hits);
}

static void __fastcall hooked_register_pass(void *registry,uint32_t key,
    uint64_t name_value,const char *category,uint32_t mode,uint32_t enabled)
{
    g_original_register_pass(registry,key,name_value,category,mode,enabled);
    InterlockedIncrement(&g_register_hits);
    if(!category) return;
    if(strcmp(category,"Shadow")==0) {
        InterlockedIncrement(&g_shadow_hits);
        if(key==0x200CU) {
            StringAssignFn assign=(StringAssignFn)
                (g_disrupt_base+g_string_assign_rva);
            InterlockedExchange(&g_last_native_shadow_seen,1);
            if(InterlockedCompareExchange(&g_range_pass_state,1,0)==0) {
                uint32_t i;
                uint32_t slot_count=0,maximum_key=0;
                uint32_t added_nonzero=0,added_distinct=0;
                void **slot_array=NULL;
                char name[32];
                if(!get_shadow_pass_table(registry,&slot_array,&slot_count,
                                          &maximum_key)) {
                    InterlockedExchange(&g_range_pass_state,-1);
                    InterlockedExchange(&g_pass_table_state,-1);
                    append_log("STAGE_L_PASS_REGISTER_ABORT reason=physicalPassTableTooSmall requiredMaxKey=0x%04X requiredSlots=%u extraRegistrationAttempted=0 overflowPrevented=1 allocatorMutation=0",
                        REQUIRED_PASS_MAX_KEY,
                        REQUIRED_PASS_TABLE_SLOTS);
                    return;
                }
                InterlockedExchange(&g_pass_table_slots,(LONG)slot_count);
                InterlockedExchange(&g_pass_table_stored_max_key,
                                    (LONG)maximum_key);
                InterlockedExchange(&g_pass_table_state,1);
                append_log("STAGE_L_PASS_TABLE_CAPACITY table=%p slotArray=%p physicalSlots=%u storedMaxKey=0x%04X requiredSlots=%u requiredMaxKey=0x%04X sufficient=1 allocatorMutation=0",
                    g_pass_table_base,slot_array,slot_count,maximum_key,
                    REQUIRED_PASS_TABLE_SLOTS,REQUIRED_PASS_MAX_KEY);
                /* The native accessor used by the supported build has two
                 * structurally identical matches on A4EE.  Avoid guessing:
                 * assign all non-empty names first, validate their objects,
                 * then use the accessor's proven non-null contract directly
                 * (payload is object+0x18).  No pass is registered if any
                 * assignment fails. */
                for(i=0;i<EXTRA_LOCAL_MAPS;++i) {
                    uint32_t map=ORIGINAL_LOCAL_MAPS+i;
                    sprintf(name,"ShadowAlpha%u",map);
                    assign(&g_extra_alpha_names[i],name);
                    sprintf(name,"Shadow%u",map);
                    assign(&g_extra_shadow_names[i],name);
                    if(!g_extra_alpha_names[i] || !g_extra_shadow_names[i]) {
                        InterlockedExchange(&g_range_pass_state,-1);
                        append_log("STAGE_E_PASS_REGISTER_ABORT reason=nameAssignmentReturnedNull map=%u extraRegistrationAttempted=0 failClosed=1",
                            map);
                        return;
                    }
                }
                append_log("STAGE_E_PASS_REGISTER_BEGIN maps=16-23 passes=17-24 insideNativeConstruction=1 downstreamRoutingDeferred=1");
                for(i=0;i<EXTRA_LOCAL_MAPS;++i) {
                    uint32_t map=ORIGINAL_LOCAL_MAPS+i;
                    uint32_t pass=map+1;
                    uint32_t shadow_key=(pass<<9)|0x0CU;
                    uint32_t alpha_key=(pass<<9)|0x10CU;
                    g_original_register_pass(registry,alpha_key,
                        g_extra_alpha_names[i]+0x18ULL,"ShadowAlpha",5,1);
                    g_original_register_pass(registry,shadow_key,
                        g_extra_shadow_names[i]+0x18ULL,"Shadow",5,1);
                    append_log("STAGE_E_PASS map=%u pass=%u shadowKey=0x%04X alphaKey=0x%04X pairedCallsReturned=1 downstreamRoutingDeferred=1",
                        map,pass,shadow_key,alpha_key);
                }
                if(verify_added_pass_slots(registry,&added_nonzero,
                                           &added_distinct)) {
                    InterlockedExchange(&g_pass_table_added_nonzero,
                                        (LONG)added_nonzero);
                    InterlockedExchange(&g_pass_table_added_distinct,
                                        (LONG)added_distinct);
                    InterlockedExchange(&g_pass_table_state,2);
                    InterlockedExchange(&g_range_pass_state,2);
                    append_log("STAGE_L_PASS_TABLE_RESULT table=%p slotArray=%p physicalSlots=%u storedMaxKey=0x%04X addedSlotFirst=%u addedSlotLast=%u addedNonZero=%u addedDistinct=%u directStorageVerified=1 allocatorMutation=0",
                        g_pass_table_base,slot_array,slot_count,maximum_key,
                        FIRST_EXTRA_PASS_SLOT,
                        FIRST_EXTRA_PASS_SLOT+EXTRA_PASS_SLOT_COUNT-1U,
                        added_nonzero,added_distinct);
                    append_log("STAGE_E_PASS_REGISTER_RETURN maps=16-23 passes=17-24 pairedCallsReturned=16 extraPassesRegistered=16 physicalPassTableVerified=1 downstreamRoutingDeferred=1");
                } else {
                    InterlockedExchange(&g_pass_table_added_nonzero,
                                        (LONG)added_nonzero);
                    InterlockedExchange(&g_pass_table_added_distinct,
                                        (LONG)added_distinct);
                    InterlockedExchange(&g_pass_table_state,-1);
                    InterlockedExchange(&g_range_pass_state,-1);
                    append_log("STAGE_L_PASS_TABLE_RESULT table=%p slotArray=%p physicalSlots=%u storedMaxKey=0x%04X addedSlotFirst=%u addedSlotLast=%u addedNonZero=%u addedDistinct=%u directStorageVerified=0 downstreamInstallBlocked=1 allocatorMutation=0",
                        g_pass_table_base,slot_array,slot_count,maximum_key,
                        FIRST_EXTRA_PASS_SLOT,
                        FIRST_EXTRA_PASS_SLOT+EXTRA_PASS_SLOT_COUNT-1U,
                        added_nonzero,added_distinct);
                }
            }
        }
    } else if(strcmp(category,"ShadowAlpha")==0) {
        InterlockedIncrement(&g_shadow_alpha_hits);
        if(key==0x210CU) InterlockedExchange(&g_last_native_alpha_seen,1);
    }
}

static int validate_downstream_sites(unsigned char *base)
{
    const unsigned char *frame_builder_prologue;
    size_t frame_builder_prologue_size;
    static const unsigned char owner_reserve[7]={
        0x4C,0x8B,0xDC,0x48,0x83,0xEC,0x68
    };
    static const unsigned char source_config_lookup[16]={
        0x48,0x8B,0x4C,0xCA,0x08,0x48,0x85,0xC9,0x74,0x29,
        0x48,0x8B,0x09,0x48,0x8D,0x94
    };
    frame_builder_prologue=g_runtime_profile==5 ?
        g_frame_builder_asia_prologue:g_frame_builder_prologue;
    frame_builder_prologue_size=g_runtime_profile==5 ?
        sizeof(g_frame_builder_asia_prologue):sizeof(g_frame_builder_prologue);
    return validate_queue_layout() &&
           validate_slice_result_store_sites(0x27170U) &&
           validate_handle_relays() &&
           validate_pass_index_relays() && validate_v1216_scheduler() &&
           memcmp(ENGINE_ADDRESS(SHADOW_MANAGER_RVA),g_manager_prologue,
                  sizeof(g_manager_prologue))==0 &&
           memcmp(ENGINE_ADDRESS(RENDER_QUEUE_PROCESS_RVA),g_renderer_queue_prologue,
                  sizeof(g_renderer_queue_prologue))==0 &&
           memcmp(ENGINE_ADDRESS(FACE_COST_READER_RVA),g_face_cost_reader_prologue,
                  sizeof(g_face_cost_reader_prologue))==0 &&
           memcmp(ENGINE_ADDRESS(RESOURCE_WRAPPER_RVA),g_resource_wrapper_prologue,
                  sizeof(g_resource_wrapper_prologue))==0 &&
           memcmp(ENGINE_ADDRESS(FRAME_BUILDER_RVA),frame_builder_prologue,
                  frame_builder_prologue_size)==0 &&
           memcmp(ENGINE_ADDRESS(POST_CALL_OBJECT_RENDERERS_RVA),
                  g_post_call_object_renderers_prologue,
                  sizeof(g_post_call_object_renderers_prologue))==0 &&
           memcmp(ENGINE_ADDRESS(RENDER_RECORD_RELEASE_RVA),
                  g_render_record_release_prologue,
                  sizeof(g_render_record_release_prologue))==0 &&
           memcmp(ENGINE_ADDRESS(RENDER_RECORD_CONSTRUCTOR_RVA),
                  g_render_record_constructor_prologue,
                  sizeof(g_render_record_constructor_prologue))==0 &&
           memcmp(ENGINE_ADDRESS(SOURCE_CONFIG_LOOKUP_RVA),source_config_lookup,
                  sizeof(source_config_lookup))==0 &&
           memcmp(g_disrupt_base+g_owner_vector_reserve_rva,owner_reserve,
                  sizeof(owner_reserve))==0;
}
static int validate_deferred_sites_after_early_queue(unsigned char *base)
{
    const unsigned char *frame_builder_prologue;
    size_t frame_builder_prologue_size;
    static const unsigned char owner_reserve[7]={
        0x4C,0x8B,0xDC,0x48,0x83,0xEC,0x68
    };
    static const unsigned char source_config_lookup[16]={
        0x48,0x8B,0x4C,0xCA,0x08,0x48,0x85,0xC9,0x74,0x29,
        0x48,0x8B,0x09,0x48,0x8D,0x94
    };
    frame_builder_prologue=g_runtime_profile==5 ?
        g_frame_builder_asia_prologue:g_frame_builder_prologue;
    frame_builder_prologue_size=g_runtime_profile==5 ?
        sizeof(g_frame_builder_asia_prologue):sizeof(g_frame_builder_prologue);
    return validate_queue_layout_applied() &&
           validate_slice_result_store_sites(RENDER_QUEUE_RESULT_OFFSET) &&
           validate_handle_relays() &&
           validate_pass_index_relays() && validate_v1216_scheduler() &&
           memcmp(ENGINE_ADDRESS(SHADOW_MANAGER_RVA),g_manager_prologue,
                  sizeof(g_manager_prologue))==0 &&
           memcmp(ENGINE_ADDRESS(RENDER_QUEUE_PROCESS_RVA),g_renderer_queue_prologue,
                  sizeof(g_renderer_queue_prologue))==0 &&
           memcmp(ENGINE_ADDRESS(FACE_COST_READER_RVA),g_face_cost_reader_prologue,
                  sizeof(g_face_cost_reader_prologue))==0 &&
           memcmp(ENGINE_ADDRESS(RESOURCE_WRAPPER_RVA),g_resource_wrapper_prologue,
                  sizeof(g_resource_wrapper_prologue))==0 &&
           memcmp(ENGINE_ADDRESS(FRAME_BUILDER_RVA),frame_builder_prologue,
                  frame_builder_prologue_size)==0 &&
           memcmp(ENGINE_ADDRESS(POST_CALL_OBJECT_RENDERERS_RVA),
                  g_post_call_object_renderers_prologue,
                  sizeof(g_post_call_object_renderers_prologue))==0 &&
           memcmp(ENGINE_ADDRESS(RENDER_RECORD_RELEASE_RVA),
                  g_render_record_release_prologue,
                  sizeof(g_render_record_release_prologue))==0 &&
           memcmp(ENGINE_ADDRESS(SOURCE_CONFIG_LOOKUP_RVA),source_config_lookup,
                  sizeof(source_config_lookup))==0 &&
           memcmp(g_disrupt_base+g_owner_vector_reserve_rva,owner_reserve,
                  sizeof(owner_reserve))==0;
}
