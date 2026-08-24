/* Module 65: PE/profile selection, unknown-build mapping, signature preflight,
 * helper recovery, and complete read-only mutation-site validation. */
typedef struct SignatureProbeSite {
    const char *label;
    uint64_t rva;
    const unsigned char *expected;
    size_t expected_size;
} SignatureProbeSite;

static void signature_probe_hex(const unsigned char *bytes,size_t count,
    char *output,size_t output_size)
{
    static const char digits[]="0123456789ABCDEF";
    size_t i;
    if(!output_size) return;
    if(count>(output_size-1)/2) count=(output_size-1)/2;
    for(i=0;i<count;i++) {
        output[i*2]=digits[(bytes[i]>>4)&0x0F];
        output[i*2+1]=digits[bytes[i]&0x0F];
    }
    output[count*2]='\0';
}

static uint32_t signature_probe_search(const unsigned char *text,
    uint32_t text_rva,uint32_t text_size,const unsigned char *pattern,
    const unsigned char *mask,size_t pattern_size,uint32_t first[4])
{
    uint32_t matches=0;
    size_t offset,i;
    if(!text || !pattern_size || pattern_size>text_size) return 0;
    for(offset=0;offset<=text_size-pattern_size;offset++) {
        int equal=1;
        for(i=0;i<pattern_size;i++) {
            if((!mask || mask[i]) && text[offset+i]!=pattern[i]) {
                equal=0;
                break;
            }
        }
        if(equal) {
            if(matches<4) first[matches]=text_rva+(uint32_t)offset;
            matches++;
        }
    }
    return matches;
}

static void signature_probe_candidate_list(const uint32_t first[4],
    uint32_t matches,char *output,size_t output_size)
{
    uint32_t i;
    size_t used=0;
    if(!output_size) return;
    output[0]='\0';
    for(i=0;i<4 && i<matches;i++) {
        int wrote=_snprintf(output+used,output_size-used,
            "%s0x%08X",used ? ",":"",(unsigned)first[i]);
        if(wrote<=0 || (size_t)wrote>=output_size-used) break;
        used+=(size_t)wrote;
    }
}

static uint64_t probe_cluster_delta(uint64_t supported_rva,
    uint64_t cluster_a,uint64_t cluster_b,uint64_t cluster_c)
{
    if(supported_rva>=0x00420000ULL) return cluster_c;
    if(supported_rva>=0x003C0000ULL) return cluster_b;
    if(supported_rva>=0x002B0000ULL) return cluster_a;
    return 0;
}

static void run_signature_probe(unsigned char *base)
{
    static const SignatureProbeSite sites[]={
        {"resource_constructor",RESOURCE_CONSTRUCTOR_RVA,
            g_resource_prologue,sizeof(g_resource_prologue)},
        {"register_pass",REGISTER_PASS_RVA,
            g_register_pass_prologue,sizeof(g_register_pass_prologue)},
        {"shadow_manager",SHADOW_MANAGER_RVA,
            g_manager_prologue,sizeof(g_manager_prologue)},
        {"renderer_queue",RENDER_QUEUE_PROCESS_RVA,
            g_renderer_queue_prologue,sizeof(g_renderer_queue_prologue)},
        {"face_cost_reader",FACE_COST_READER_RVA,
            g_face_cost_reader_prologue,sizeof(g_face_cost_reader_prologue)},
        {"resource_wrapper",RESOURCE_WRAPPER_RVA,
            g_resource_wrapper_prologue,sizeof(g_resource_wrapper_prologue)},
        {"render_record_constructor",RENDER_RECORD_CONSTRUCTOR_RVA,
            g_render_record_constructor_prologue,
            sizeof(g_render_record_constructor_prologue)},
        {"frame_builder",FRAME_BUILDER_RVA,
            g_frame_builder_prologue,sizeof(g_frame_builder_prologue)},
        {"post_call_object_renderers",POST_CALL_OBJECT_RENDERERS_RVA,
            g_post_call_object_renderers_prologue,
            sizeof(g_post_call_object_renderers_prologue)},
        {"render_record_release",RENDER_RECORD_RELEASE_RVA,
            g_render_record_release_prologue,
            sizeof(g_render_record_release_prologue)}
    };
    IMAGE_DOS_HEADER *dos;
    IMAGE_NT_HEADERS64 *nt;
    IMAGE_SECTION_HEADER *section;
    unsigned char *text=NULL;
    uint32_t text_rva=0,text_size=0,image_size=0;
    uint32_t text_hash_low=0,text_hash_high=0;
    uint64_t text_hash=1469598103934665603ULL;
    uint64_t probe_delta_a=0,probe_delta_b=0,probe_delta_c=0;
    int probe_a_valid=0,probe_b_valid=0,probe_c_valid=0;
    size_t site_index,byte_index;
    uint32_t translated_exact=0,translated_failed=0;
    uint32_t inline_mapped=0,inline_failed=0;
    uint32_t tail_mapped=0,tail_failed=0;

    append_log("UNKNOWN_BUILD_MAP_BEGIN diagnosticOnly=1 engineWrites=0 hookInstalls=0 knownProfileActivationDisabled=1 inference=uniqueAnchorsThenFullValidation");
    if(!readable_memory(base,sizeof(IMAGE_DOS_HEADER))) {
        append_log("UNKNOWN_BUILD_MAP_ABORT reason=dosHeaderUnreadable noEngineMutation=1");
        return;
    }
    dos=(IMAGE_DOS_HEADER *)base;
    if(dos->e_magic!=IMAGE_DOS_SIGNATURE || dos->e_lfanew<=0 ||
       dos->e_lfanew>0x100000 ||
       !readable_memory(base+dos->e_lfanew,sizeof(IMAGE_NT_HEADERS64))) {
        append_log("UNKNOWN_BUILD_MAP_ABORT reason=invalidPeHeader noEngineMutation=1");
        return;
    }
    nt=(IMAGE_NT_HEADERS64 *)(base+dos->e_lfanew);
    if(nt->Signature!=IMAGE_NT_SIGNATURE ||
       nt->OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR64_MAGIC) {
        append_log("UNKNOWN_BUILD_MAP_ABORT reason=unsupportedPeFormat noEngineMutation=1");
        return;
    }
    image_size=nt->OptionalHeader.SizeOfImage;
    section=IMAGE_FIRST_SECTION(nt);
    for(site_index=0;site_index<nt->FileHeader.NumberOfSections;site_index++) {
        if(memcmp(section[site_index].Name,".text",5)==0) {
            text_rva=section[site_index].VirtualAddress;
            text_size=section[site_index].Misc.VirtualSize;
            if(text_rva<image_size && text_size<=image_size-text_rva)
                text=base+text_rva;
            break;
        }
    }
    if(text && readable_memory(text,text_size)) {
        for(byte_index=0;byte_index<text_size;byte_index++) {
            text_hash^=(uint64_t)text[byte_index];
            text_hash*=1099511628211ULL;
        }
        text_hash_low=(uint32_t)text_hash;
        text_hash_high=(uint32_t)(text_hash>>32);
    } else {
        text=NULL;
        text_rva=0;
        text_size=0;
    }
    append_log("UNKNOWN_BUILD_MAP_PE machine=0x%04X timestamp=0x%08X sections=%u imageSize=0x%08X checksum=0x%08X entryRva=0x%08X textRva=0x%08X textSize=0x%08X textFnv1a64=%08X%08X",
        (unsigned)nt->FileHeader.Machine,
        (unsigned)nt->FileHeader.TimeDateStamp,
        (unsigned)nt->FileHeader.NumberOfSections,
        (unsigned)image_size,(unsigned)nt->OptionalHeader.CheckSum,
        (unsigned)nt->OptionalHeader.AddressOfEntryPoint,
        (unsigned)text_rva,(unsigned)text_size,
        (unsigned)text_hash_high,(unsigned)text_hash_low);

    if(text) {
        uint32_t manager_first[4]={0,0,0,0};
        uint32_t face_first[4]={0,0,0,0};
        uint32_t constructor_first[4]={0,0,0,0};
        uint32_t release_first[4]={0,0,0,0};
        uint32_t wrapper_first[4]={0,0,0,0};
        uint32_t manager_matches=signature_probe_search(text,text_rva,text_size,
            g_manager_prologue,NULL,sizeof(g_manager_prologue),manager_first);
        uint32_t face_matches=signature_probe_search(text,text_rva,text_size,
            g_face_cost_reader_prologue,NULL,sizeof(g_face_cost_reader_prologue),face_first);
        uint32_t constructor_matches=signature_probe_search(text,text_rva,text_size,
            g_render_record_constructor_prologue,NULL,
            sizeof(g_render_record_constructor_prologue),constructor_first);
        uint32_t release_matches=signature_probe_search(text,text_rva,text_size,
            g_render_record_release_prologue,NULL,
            sizeof(g_render_record_release_prologue),release_first);
        uint32_t wrapper_matches=signature_probe_search(text,text_rva,text_size,
            g_resource_wrapper_prologue,NULL,sizeof(g_resource_wrapper_prologue),wrapper_first);
        uint64_t manager_delta=manager_matches==1 && manager_first[0]>=SHADOW_MANAGER_RVA ?
            manager_first[0]-SHADOW_MANAGER_RVA : 0;
        uint64_t face_delta=face_matches==1 && face_first[0]>=FACE_COST_READER_RVA ?
            face_first[0]-FACE_COST_READER_RVA : 0;
        uint64_t constructor_delta=constructor_matches==1 &&
            constructor_first[0]>=RENDER_RECORD_CONSTRUCTOR_RVA ?
            constructor_first[0]-RENDER_RECORD_CONSTRUCTOR_RVA : 0;
        uint64_t release_delta=release_matches==1 &&
            release_first[0]>=RENDER_RECORD_RELEASE_RVA ?
            release_first[0]-RENDER_RECORD_RELEASE_RVA : 0;
        if(manager_matches==1 && face_matches==1 &&
           manager_first[0]>=SHADOW_MANAGER_RVA &&
           face_first[0]>=FACE_COST_READER_RVA &&
           manager_delta==face_delta) {
            probe_delta_a=manager_delta;
            probe_a_valid=1;
        }
        if(constructor_matches==1 && release_matches==1 &&
           constructor_first[0]>=RENDER_RECORD_CONSTRUCTOR_RVA &&
           release_first[0]>=RENDER_RECORD_RELEASE_RVA &&
           constructor_delta==release_delta) {
            probe_delta_b=constructor_delta;
            probe_b_valid=1;
        }
        if(wrapper_matches==1 && wrapper_first[0]>=RESOURCE_WRAPPER_RVA) {
            probe_delta_c=wrapper_first[0]-RESOURCE_WRAPPER_RVA;
            probe_c_valid=1;
        }
        append_log("UNKNOWN_BUILD_MAP_CLUSTERS clusterA=0x%08X anchorManagerMatches=%u anchorFaceMatches=%u anchorsAAgree=%u clusterB=0x%08X anchorConstructorMatches=%u anchorReleaseMatches=%u anchorsBAgree=%u clusterC=0x%08X anchorWrapperMatches=%u anchorCValid=%u inferredReadOnly=1",
            (unsigned)probe_delta_a,(unsigned)manager_matches,
            (unsigned)face_matches,probe_a_valid ? 1U:0U,
            (unsigned)probe_delta_b,(unsigned)constructor_matches,
            (unsigned)release_matches,probe_b_valid ? 1U:0U,
            (unsigned)probe_delta_c,(unsigned)wrapper_matches,
            probe_c_valid ? 1U:0U);
    }

    for(site_index=0;site_index<sizeof(sites)/sizeof(sites[0]);site_index++) {
        const SignatureProbeSite *site=&sites[site_index];
        char expected_hex[65],actual_hex[65],candidates[128];
        uint32_t matches=0,first[4]={0,0,0,0};
        size_t actual_size=32,used=0;
        int exact=0;
        expected_hex[0]=actual_hex[0]=candidates[0]='\0';
        signature_probe_hex(site->expected,site->expected_size,
            expected_hex,sizeof(expected_hex));
        if(site->rva<image_size && actual_size<=image_size-site->rva &&
           readable_memory(base+site->rva,actual_size)) {
            signature_probe_hex(base+site->rva,actual_size,
                actual_hex,sizeof(actual_hex));
            exact=memcmp(base+site->rva,site->expected,
                site->expected_size)==0;
        } else {
            lstrcpynA(actual_hex,"UNREADABLE",sizeof(actual_hex));
        }
        if(text && site->expected_size<=text_size) {
            for(byte_index=0;byte_index<=text_size-site->expected_size;
                byte_index++) {
                if(memcmp(text+byte_index,site->expected,
                          site->expected_size)==0) {
                    if(matches<4) first[matches]=text_rva+(uint32_t)byte_index;
                    matches++;
                }
            }
        }
        for(byte_index=0;byte_index<4 && byte_index<matches;byte_index++) {
            int wrote=_snprintf(candidates+used,sizeof(candidates)-used,
                "%s0x%08X",used ? ",":"",(unsigned)first[byte_index]);
            if(wrote<=0 || (size_t)wrote>=sizeof(candidates)-used) break;
            used+=(size_t)wrote;
        }
        append_log("UNKNOWN_BUILD_MAP_SIGNATURE label=%s supportedRva=0x%08X signatureBytes=%u supportedRvaMatch=%u expected=%s actual32AtSupported=%s textMatches=%u firstRvas=%s",
            site->label,(unsigned)site->rva,
            (unsigned)site->expected_size,exact ? 1U:0U,
            expected_hex,actual_hex,(unsigned)matches,
            candidates[0] ? candidates:"none");
    }
    /* Validate the three relocation clusters recovered from the v1.0.3 log.
     * These checks are intentionally read-only and use the exact hook
     * signatures already required by the functional build. */
    for(site_index=0;site_index<sizeof(sites)/sizeof(sites[0]);site_index++) {
        const SignatureProbeSite *site=&sites[site_index];
        uint64_t delta=probe_cluster_delta(site->rva,probe_delta_a,
            probe_delta_b,probe_delta_c);
        uint64_t candidate=site->rva+delta;
        int cluster_valid=site->rva>=0x00420000ULL ? probe_c_valid :
            (site->rva>=0x003C0000ULL ? probe_b_valid : probe_a_valid);
        char actual_hex[65];
        int exact=0;
        actual_hex[0]='\0';
        if(cluster_valid && candidate<image_size &&
           site->expected_size<=image_size-candidate &&
           readable_memory(base+candidate,32)) {
            signature_probe_hex(base+candidate,32,actual_hex,
                sizeof(actual_hex));
            exact=memcmp(base+candidate,site->expected,
                site->expected_size)==0;
        } else {
            lstrcpynA(actual_hex,"UNREADABLE",sizeof(actual_hex));
        }
        if(exact) translated_exact++; else translated_failed++;
        append_log("UNKNOWN_BUILD_MAP_TRANSLATED label=%s supportedRva=0x%08X delta=0x%08X candidateRva=0x%08X exactMatch=%u actual32=%s",
            site->label,(unsigned)site->rva,(unsigned)delta,
            (unsigned)candidate,exact ? 1U:0U,actual_hex);
    }

    {
        static const unsigned char queue_count[]={0xBE,0x10,0,0,0};
        static const unsigned char clear_a[]={0x41,0xB8,0x44,0,0,0};
        static const unsigned char clear_b[]={0x44,0x8D,0x42,0x44};
        static const unsigned char slice_nonnull[]={0x49,0x8B,0x8D,0x70,0x71,2,0,0x48,0x8B,0x94,0x24,0xD8,0,0,0};
        static const unsigned char slice_null[]={0x49,0x8B,0x85,0x70,0x71,2,0,0x48,0x8B,0x94,0x24,0xD8,0,0,0};
        static const unsigned char handle_edx[]={0x8B,0x94,0x87,0x10,4,0,0,0x44,0x89,0x55,0x54,0xC7,0x45,0x58,1,0,0,0};
        static const unsigned char handle_ecx[]={0x42,0x8B,0x8C,0xB7,0x10,4,0,0,0x48,0x8B,0x53,0x58,0x89,0x4A,0x48};
        static const unsigned char handle_address[]={0x48,0x8D,0x94,0x8F,0x10,4,0,0,0x48,0x8B,0x4F,8,0x4C,0x89,0xAD,0,4,0,0};
        static const unsigned char pass_lookup[]={0x41,0x8D,4,0x0E,0x41,0xC7,0x87,0x4C,5,0,0,0xFF,0xFF,0x7F,0xFF};
        static const unsigned char pass_store[]={0x41,0x8D,4,0x0E,0x49,0x89,0x9C,0x34,0xF0,7,0,0,0xB9,4,0,0,0};
        static const unsigned char scheduler_init[]={0xC7,0x85,0xB4,0,0,0,4,0,0,0};
        static const unsigned char scheduler_floor[]={0xBA,4,0,0,0};
        static const unsigned char scheduler_upper[]={0x41,0xB8,8,0,0,0};
        static const unsigned char map_bound[]={0x83,0xFF,0x10,0x0F,0x82,0xEE,0xFE,0xFF,0xFF};
        static const unsigned char source_lookup[]={0x48,0x8B,0x4C,0xCA,8,0x48,0x85,0xC9,0x74,0x29,0x48,0x8B,9,0x48,0x8D,0x94};
        static const SignatureProbeSite inline_sites[]={
            {"queue_count",QUEUE_COUNT_INIT_RVA,queue_count,sizeof(queue_count)},
            {"queue_clear_a",QUEUE_CLEAR_A_SIZE_RVA,clear_a,sizeof(clear_a)},
            {"queue_clear_b",QUEUE_CLEAR_B_SIZE_RVA,clear_b,sizeof(clear_b)},
            {"slice_nonnull",SLICE_RESULT_NONNULL_RVA,slice_nonnull,sizeof(slice_nonnull)},
            {"slice_null",SLICE_RESULT_NULL_RVA,slice_null,sizeof(slice_null)},
            {"handle_edx",HANDLE_LOAD_EDX_RVA,handle_edx,sizeof(handle_edx)},
            {"handle_ecx",HANDLE_LOAD_ECX_RVA,handle_ecx,sizeof(handle_ecx)},
            {"handle_address",HANDLE_ADDRESS_RDX_RVA,handle_address,sizeof(handle_address)},
            {"pass_lookup",PASS_INDEX_LOOKUP_RVA,pass_lookup,sizeof(pass_lookup)},
            {"pass_store",PASS_INDEX_STORE_RVA,pass_store,sizeof(pass_store)},
            {"scheduler_init",SCHEDULER_INIT_RVA,scheduler_init,sizeof(scheduler_init)},
            {"scheduler_floor",SCHEDULER_FLOOR_RVA,scheduler_floor,sizeof(scheduler_floor)},
            {"scheduler_upper",SCHEDULER_UPPER_RVA,scheduler_upper,sizeof(scheduler_upper)},
            {"map_bound",MAP_LOOP_BOUND_RVA,map_bound,sizeof(map_bound)},
            {"source_lookup",SOURCE_CONFIG_LOOKUP_RVA,source_lookup,sizeof(source_lookup)}
        };
        for(site_index=0;site_index<sizeof(inline_sites)/sizeof(inline_sites[0]);site_index++) {
            const SignatureProbeSite *site=&inline_sites[site_index];
            uint64_t candidate=site->rva+probe_delta_a;
            int exact=candidate<image_size &&
                site->expected_size<=image_size-candidate &&
                readable_memory(base+candidate,site->expected_size) &&
                memcmp(base+candidate,site->expected,site->expected_size)==0;
            if(exact) inline_mapped++; else inline_failed++;
            append_log("UNKNOWN_BUILD_MAP_INLINE label=%s supportedRva=0x%08X candidateRva=0x%08X exactMatch=%u",
                site->label,(unsigned)site->rva,(unsigned)candidate,
                exact ? 1U:0U);
        }
    }

    {
        uint64_t candidate=MAP_LOOP_STORE_RVA+probe_delta_a;
        int structure=0;
        uint64_t call1=0,call2=0;
        char actual_hex[65];
        actual_hex[0]='\0';
        if(candidate<image_size && 32<=image_size-candidate &&
           readable_memory(base+candidate,32)) {
            const unsigned char *p=base+candidate;
            static const unsigned char prefix[]={0x44,0x8B,0x9D,0x30,8,0,0,0x45,0x89,0x1C,0x24};
            static const unsigned char xmm1[]={0x0F,0x28,0xF0};
            static const unsigned char xmm2[]={0x0F,0x28,0xF8};
            signature_probe_hex(p,32,actual_hex,sizeof(actual_hex));
            structure=memcmp(p,prefix,sizeof(prefix))==0 && p[11]==0xE8 &&
                memcmp(p+16,xmm1,sizeof(xmm1))==0 && p[19]==0xE8 &&
                memcmp(p+24,xmm2,sizeof(xmm2))==0;
            if(structure) {
                int32_t rel1,rel2;
                memcpy(&rel1,p+12,4); memcpy(&rel2,p+20,4);
                call1=candidate+16+(int64_t)rel1;
                call2=candidate+24+(int64_t)rel2;
            }
        } else lstrcpynA(actual_hex,"UNREADABLE",sizeof(actual_hex));
        append_log("UNKNOWN_BUILD_MAP_MAP_LOOP supportedRva=0x%08X candidateRva=0x%08X structureMatch=%u callTarget1=0x%08X callTarget2=0x%08X targetsEqual=%u actual32=%s",
            (unsigned)MAP_LOOP_STORE_RVA,(unsigned)candidate,
            structure ? 1U:0U,(unsigned)call1,(unsigned)call2,
            call1 && call1==call2 ? 1U:0U,actual_hex);
    }

    {
        static const unsigned char before_call[7]={
            0x48,0x8D,0x8F,0x98,0x00,0x00,0x00};
        static const unsigned char after_call[16]={
            0x48,0x8B,0x5C,0x24,0x30,0x48,0x8B,0x74,
            0x24,0x38,0x48,0x83,0xC4,0x20,0x5F,0xC3};
        static const unsigned char reserve_prologue[16]={
            0x4C,0x8B,0xDC,0x48,0x83,0xEC,0x68,0x44,
            0x8B,0x41,0x0C,0x4C,0x8D,0x49,0x08,0x4C};
        uint64_t call_rva=OWNER_VECTOR_RESERVE_CALL_RVA+probe_delta_a;
        uint64_t target_rva=0;
        int context_ok=0,target_ok=0;
        char call_hex[57],target_hex[33];
        lstrcpynA(call_hex,"UNREADABLE",sizeof(call_hex));
        lstrcpynA(target_hex,"UNREADABLE",sizeof(target_hex));
        if(probe_a_valid && call_rva>=7 && call_rva<image_size &&
           21<=image_size-call_rva && readable_memory(base+call_rva-7,28)) {
            const unsigned char *call=base+call_rva;
            signature_probe_hex(call-7,28,call_hex,sizeof(call_hex));
            context_ok=memcmp(call-7,before_call,sizeof(before_call))==0 &&
                call[0]==0xE8 &&
                memcmp(call+5,after_call,sizeof(after_call))==0;
            if(context_ok) {
                int32_t rel;
                memcpy(&rel,call+1,4);
                target_rva=call_rva+5+(int64_t)rel;
                if(target_rva<image_size &&
                   sizeof(reserve_prologue)<=image_size-target_rva &&
                   readable_memory(base+target_rva,sizeof(reserve_prologue))) {
                    signature_probe_hex(base+target_rva,
                        sizeof(reserve_prologue),target_hex,
                        sizeof(target_hex));
                    target_ok=memcmp(base+target_rva,reserve_prologue,
                        sizeof(reserve_prologue))==0;
                }
            }
        }
        append_log("UNKNOWN_BUILD_MAP_OWNER_RESERVE_CALL supportedCallRva=0x%08X candidateCallRva=0x%08X contextMatch=%u targetRva=0x%08X targetMatch=%u actualCall28=%s actualTarget16=%s diagnosticOnly=1",
            (unsigned)OWNER_VECTOR_RESERVE_CALL_RVA,(unsigned)call_rva,
            context_ok ? 1U:0U,(unsigned)target_rva,
            target_ok ? 1U:0U,call_hex,target_hex);
    }

    for(site_index=0;site_index<sizeof(g_tail_patches)/sizeof(g_tail_patches[0]);site_index++) {
        const TailPatch *tail=&g_tail_patches[site_index];
        uint64_t delta=probe_cluster_delta(tail->instruction_rva,
            probe_delta_a,probe_delta_b,probe_delta_c);
        uint64_t candidate=tail->instruction_rva+delta;
        int cluster_valid=tail->instruction_rva>=0x00420000ULL ?
            probe_c_valid : (tail->instruction_rva>=0x003C0000ULL ?
            probe_b_valid : probe_a_valid);
        int offset=-1;
        char actual_hex[33];
        actual_hex[0]='\0';
        if(cluster_valid && candidate<image_size && 16<=image_size-candidate &&
           readable_memory(base+candidate,16)) {
            offset=embedded_dword_offset(base+candidate,tail->old_value);
            signature_probe_hex(base+candidate,16,actual_hex,sizeof(actual_hex));
        } else lstrcpynA(actual_hex,"UNREADABLE",sizeof(actual_hex));
        if(offset>=0) tail_mapped++; else tail_failed++;
        append_log("UNKNOWN_BUILD_MAP_TAIL index=%u supportedRva=0x%08X delta=0x%08X candidateRva=0x%08X oldValue=0x%05X embeddedOffset=%d actual16=%s",
            (unsigned)site_index,(unsigned)tail->instruction_rva,
            (unsigned)delta,(unsigned)candidate,(unsigned)tail->old_value,
            offset,actual_hex);
    }

    {
        static const unsigned char string_assign[]={0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x8B,0xD9,0x48,0x85,0xD2,0x74,0x65,0x4C,0x8B};
        static const unsigned char string_value[]={0x48,0x8B,0x01,0x48,0x85,0xC0,0x75,0x08,0x48,0x8D,0x05,0,0,0,0,0xC3,0x48,0x83,0xC0,0x18,0xC3};
        static const unsigned char string_value_mask[]={1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,1,1,1,1,1,1};
        static const unsigned char random_fn[]={0x8B,0x05,0,0,0,0,0x66,0x0F,0xEF,0xC0,0x69,0xC0,0xFD,0x43,0x03,0x00,0x05,0xC3,0x9E,0x26,0x00,0x89,0x05,0,0,0,0,0xC1,0xE8,0x10,0x25};
        static const unsigned char random_mask[]={1,1,0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,1,1,1,1};
        static const unsigned char profile_fn[]={0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xEC,0x20,0x48,0x8B,0xF9,0xE8,0,0,0,0,0x48,0x8B,0xCF,0xE8,0,0,0,0,0x4C,0x8B,0x5F,0x18,0xBA,0x08};
        static const unsigned char profile_mask[]={1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,1,1,1,1,0,0,0,0,1,1,1,1,1,1};
        static const unsigned char owner_fn[]={0x4C,0x8B,0xDC,0x48,0x83,0xEC,0x68,0x44,0x8B,0x41,0x0C,0x4C,0x8D,0x49,0x08,0x4C};
        static const unsigned char submit_fn[]={0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57};
        static const unsigned char epilogue_fn[]={0x48,0x81,0xC4,0x88,0,0,0,0x41,0x5F,0x41,0x5E,0x41,0x5D,0x41,0x5C,0x5F};
        static const SignatureProbeSite helpers[]={
            {"string_assign",STRING_ASSIGN_RVA,string_assign,sizeof(string_assign)},
            {"string_value",STRING_VALUE_RVA,string_value,sizeof(string_value)},
            {"native_random",0x000B9780ULL,random_fn,sizeof(random_fn)},
            {"profile_callback",PROFILE_CALLBACK_RVA,profile_fn,sizeof(profile_fn)},
            {"owner_vector_reserve",OWNER_VECTOR_RESERVE_RVA,owner_fn,sizeof(owner_fn)},
            {"resource_submit",RESOURCE_SUBMIT_RVA,submit_fn,sizeof(submit_fn)},
            {"slice_epilogue",SLICE_RESULT_EPILOGUE_RVA,epilogue_fn,sizeof(epilogue_fn)}
        };
        static const unsigned char *masks[]={NULL,string_value_mask,random_mask,profile_mask,NULL,NULL,NULL};
        for(site_index=0;site_index<sizeof(helpers)/sizeof(helpers[0]);site_index++) {
            const SignatureProbeSite *site=&helpers[site_index];
            uint32_t first[4]={0,0,0,0};
            uint32_t matches=signature_probe_search(text,text_rva,text_size,
                site->expected,masks[site_index],site->expected_size,first);
            char candidates[128],expected_hex[129];
            signature_probe_candidate_list(first,matches,candidates,
                sizeof(candidates));
            signature_probe_hex(site->expected,site->expected_size,
                expected_hex,sizeof(expected_hex));
            append_log("UNKNOWN_BUILD_MAP_HELPER label=%s supportedRva=0x%08X signatureBytes=%u masked=%u textMatches=%u firstRvas=%s supportedExpected=%s",
                site->label,(unsigned)site->rva,
                (unsigned)site->expected_size,masks[site_index] ? 1U:0U,
                (unsigned)matches,candidates[0] ? candidates:"none",
                expected_hex);
        }
    }

    append_log("UNKNOWN_BUILD_MAP_COMPLETE hookSignatures=%u translatedExact=%u translatedFailed=%u inlineMapped=%u inlineFailed=%u tailMapped=%u tailFailed=%u helpers=7 diagnosticOnly=1 engineWrites=0 hookInstalls=0 sendCompleteLog=1",
        (unsigned)(sizeof(sites)/sizeof(sites[0])),
        (unsigned)translated_exact,(unsigned)translated_failed,
        (unsigned)inline_mapped,(unsigned)inline_failed,
        (unsigned)tail_mapped,(unsigned)tail_failed);
}

static int resolver_masked_match(const unsigned char *actual,
    const unsigned char *pattern,const unsigned char *mask,size_t count)
{
    size_t i;
    for(i=0;i<count;i++)
        if(mask[i] && actual[i]!=pattern[i]) return 0;
    return 1;
}

static int validate_runtime_profile_map(const RuntimeProfile *profile)
{
    static const uint64_t required[]={
        RESOURCE_CONSTRUCTOR_RVA,REGISTER_PASS_RVA,MAP_LOOP_STORE_RVA,
        MAP_LOOP_BOUND_RVA,QUEUE_COUNT_INIT_RVA,QUEUE_CLEAR_A_SIZE_RVA,
        QUEUE_CLEAR_B_SIZE_RVA,PASS_INDEX_LOOKUP_RVA,PASS_INDEX_STORE_RVA,
        HANDLE_LOAD_EDX_RVA,HANDLE_LOAD_ECX_RVA,HANDLE_ADDRESS_RDX_RVA,
        SCHEDULER_INIT_RVA,SCHEDULER_FLOOR_RVA,SCHEDULER_UPPER_RVA,
        SHADOW_MANAGER_RVA,PROFILE_CALLBACK_RVA,
        OWNER_VECTOR_RESERVE_CALL_RVA,RENDER_QUEUE_PROCESS_RVA,
        FACE_COST_READER_RVA,RESOURCE_WRAPPER_RVA,FRAME_BUILDER_RVA,
        FRAME_BUILDER_END_RVA,SLICE_RESULT_NONNULL_RVA,
        SLICE_RESULT_NULL_RVA,SLICE_RESULT_EPILOGUE_RVA,
        POST_CALL_OBJECT_RENDERERS_RVA,RENDER_RECORD_RELEASE_RVA,
        RENDER_RECORD_CONSTRUCTOR_RVA,FIELD_100_LOOKUP_RETURN_RVA,
        FIELD_F8_LOOKUP_RETURN_RVA,FIELD_110_LOOKUP_RETURN_RVA,
        SOURCE_CONFIG_LOOKUP_RVA
    };
    size_t i;
    const size_t map_count=profile->rva_map_count;
    for(i=0;i<map_count;i++) {
        if(profile->rva_map[i].runtime_rva>=g_runtime_state.image_size ||
           (i && profile->rva_map[i-1].supported_rva>=
                 profile->rva_map[i].supported_rva)) {
            append_log("RUNTIME_RVA_MAP_REJECTED profile=%s reason=orderingOrBounds index=%u entries=%u noEngineMutation=1",
                profile->name,(unsigned)i,(unsigned)map_count);
            return 0;
        }
    }
    for(i=0;i<ARRAY_COUNT(required);i++) {
        if(!resolve_engine_rva(required[i])) {
            append_log("RUNTIME_RVA_MAP_REJECTED profile=%s reason=missingRequired supportedRva=0x%08X noEngineMutation=1",
                profile->name,(unsigned)required[i]);
            return 0;
        }
    }
    for(i=0;i<ARRAY_COUNT(g_tail_patches);i++) {
        if(!resolve_engine_rva(g_tail_patches[i].instruction_rva)) {
            append_log("RUNTIME_RVA_MAP_REJECTED profile=%s reason=missingTail index=%u supportedRva=0x%08X noEngineMutation=1",
                profile->name,(unsigned)i,
                (unsigned)g_tail_patches[i].instruction_rva);
            return 0;
        }
    }
    append_log("RUNTIME_RVA_MAP_VALIDATED profile=%s entries=%u requiredSites=%u tailSites=%u mapping=independentPerSite noEngineMutation=1",
        profile->name,(unsigned)map_count,(unsigned)ARRAY_COUNT(required),
        (unsigned)ARRAY_COUNT(g_tail_patches));
    return 1;
}

static int select_runtime_profile(unsigned char *base)
{
    IMAGE_DOS_HEADER *dos;
    IMAGE_NT_HEADERS64 *nt;
    const RuntimeProfile *profile;
    if(!readable_memory(base,sizeof(IMAGE_DOS_HEADER))) return 0;
    dos=(IMAGE_DOS_HEADER *)base;
    if(dos->e_magic!=IMAGE_DOS_SIGNATURE || dos->e_lfanew<=0 ||
       dos->e_lfanew>0x100000 ||
       !readable_memory(base+dos->e_lfanew,sizeof(IMAGE_NT_HEADERS64)))
        return 0;
    nt=(IMAGE_NT_HEADERS64 *)(base+dos->e_lfanew);
    if(nt->Signature!=IMAGE_NT_SIGNATURE ||
       nt->OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR64_MAGIC)
        return 0;

    clear_runtime_profile();
    profile=find_runtime_profile(nt->OptionalHeader.SizeOfImage,
        nt->FileHeader.TimeDateStamp,nt->OptionalHeader.CheckSum,
        nt->OptionalHeader.AddressOfEntryPoint);
    if(profile) {
        select_runtime_profile_state(profile);
        if(profile->rva_strategy==RUNTIME_RVA_EXPLICIT &&
           !validate_runtime_profile_map(profile)) {
            clear_runtime_profile();
            return 0;
        }
        append_log("RUNTIME_PROFILE_SELECTED profile=%s profileId=%ld timestamp=0x%08X checksum=0x%08X entryRva=0x%08X imageSize=0x%08X rvaStrategy=%s clusterA=0x%08X clusterB=0x%08X clusterC=0x%08X independentRvaSites=%u stringAssignRva=0x%08X nativeRandomRva=0x%08X selection=peIdentityThenProfileValidationThenFullPreflight",
            profile->name,(long)profile->id,(unsigned)profile->timestamp,
            (unsigned)profile->checksum,(unsigned)profile->entry_rva,
            (unsigned)profile->image_size,
            profile->rva_strategy==RUNTIME_RVA_EXPLICIT ? "explicit":"clustered",
            (unsigned)profile->cluster_a_delta,
            (unsigned)profile->cluster_b_delta,
            (unsigned)profile->cluster_c_delta,
            (unsigned)profile->rva_map_count,
            (unsigned)profile->string_assign_rva,
            (unsigned)profile->native_random_rva);
        return 1;
    }
    append_log("RUNTIME_PROFILE_UNKNOWN timestamp=0x%08X checksum=0x%08X entryRva=0x%08X imageSize=0x%08X action=readOnlyFullSiteMap noEngineMutation=1",
        (unsigned)nt->FileHeader.TimeDateStamp,
        (unsigned)nt->OptionalHeader.CheckSum,
        (unsigned)nt->OptionalHeader.AddressOfEntryPoint,
        (unsigned)nt->OptionalHeader.SizeOfImage);
    return 0;
}

static int recover_owner_vector_reserve(void)
{
    static const unsigned char before_call[7]={
        0x48,0x8D,0x8F,0x98,0x00,0x00,0x00};
    static const unsigned char after_call[16]={
        0x48,0x8B,0x5C,0x24,0x30,0x48,0x8B,0x74,
        0x24,0x38,0x48,0x83,0xC4,0x20,0x5F,0xC3};
    static const unsigned char reserve_prologue[16]={
        0x4C,0x8B,0xDC,0x48,0x83,0xEC,0x68,0x44,
        0x8B,0x41,0x0C,0x4C,0x8D,0x49,0x08,0x4C};
    unsigned char *call=ENGINE_ADDRESS(OWNER_VECTOR_RESERVE_CALL_RVA);
    int32_t rel;
    uint64_t target_rva;
    char call_hex[57],target_hex[33];
    lstrcpynA(call_hex,"UNREADABLE",sizeof(call_hex));
    lstrcpynA(target_hex,"UNREADABLE",sizeof(target_hex));
    int context_ok=readable_memory(call-7,28) &&
        memcmp(call-7,before_call,sizeof(before_call))==0 &&
        call[0]==0xE8 &&
        memcmp(call+5,after_call,sizeof(after_call))==0;
    if(readable_memory(call-7,28))
        signature_probe_hex(call-7,28,call_hex,sizeof(call_hex));
    if(!context_ok) {
        append_log("OWNER_RESERVE_RECOVERY profile=%ld callsiteRva=0x%08X contextValid=0 targetRva=0x00000000 targetValid=0 actualCall28=%s noEngineMutation=1",
            (long)runtime_profile_id(),
            (unsigned)RESOLVED_ENGINE_RVA(OWNER_VECTOR_RESERVE_CALL_RVA),
            call_hex);
        return 0;
    }
    memcpy(&rel,call+1,4);
    target_rva=RESOLVED_ENGINE_RVA(OWNER_VECTOR_RESERVE_CALL_RVA)+5+
        (int64_t)rel;
    if(target_rva<g_runtime_state.image_size &&
       readable_memory(g_disrupt_base+target_rva,sizeof(reserve_prologue)))
        signature_probe_hex(g_disrupt_base+target_rva,
            sizeof(reserve_prologue),target_hex,sizeof(target_hex));
    if(target_rva>=g_runtime_state.image_size ||
       !readable_memory(g_disrupt_base+target_rva,sizeof(reserve_prologue)) ||
       memcmp(g_disrupt_base+target_rva,reserve_prologue,
              sizeof(reserve_prologue))!=0) {
        append_log("OWNER_RESERVE_RECOVERY profile=%ld callsiteRva=0x%08X contextValid=1 targetRva=0x%08X targetValid=0 actualCall28=%s actualTarget16=%s noEngineMutation=1",
            (long)runtime_profile_id(),
            (unsigned)RESOLVED_ENGINE_RVA(OWNER_VECTOR_RESERVE_CALL_RVA),
            (unsigned)target_rva,call_hex,target_hex);
        return 0;
    }
    g_runtime_state.owner_vector_reserve_rva=target_rva;
    append_log("OWNER_RESERVE_RECOVERY profile=%ld callsiteRva=0x%08X contextValid=1 targetRva=0x%08X targetValid=1 method=translatedCallsiteRel32",
        (long)runtime_profile_id(),
        (unsigned)RESOLVED_ENGINE_RVA(OWNER_VECTOR_RESERVE_CALL_RVA),
        (unsigned)g_runtime_state.owner_vector_reserve_rva);
    return 1;
}

static int validate_selected_profile_helpers(void)
{
    static const unsigned char string_assign[16]={
        0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x8B,
        0xD9,0x48,0x85,0xD2,0x74,0x65,0x4C,0x8B};
    static const unsigned char owner_reserve[7]={
        0x4C,0x8B,0xDC,0x48,0x83,0xEC,0x68};
    static const unsigned char slice_epilogue[16]={
        0x48,0x81,0xC4,0x88,0,0,0,0x41,
        0x5F,0x41,0x5E,0x41,0x5D,0x41,0x5C,0x5F};
    static const unsigned char profile_callback[32]={
        0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,
        0xEC,0x20,0x48,0x8B,0xF9,0xE8,0,0,0,0,
        0x48,0x8B,0xCF,0xE8,0,0,0,0,0x4C,0x8B,
        0x5F,0x18,0xBA,0x08};
    static const unsigned char profile_mask[32]={
        1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,
        1,1,1,1,0,0,0,0,1,1,1,1,1,1};
    static const unsigned char random_pattern[31]={
        0x8B,0x05,0,0,0,0,0x66,0x0F,0xEF,0xC0,
        0x69,0xC0,0xFD,0x43,0x03,0x00,0x05,0xC3,
        0x9E,0x26,0x00,0x89,0x05,0,0,0,0,0xC1,
        0xE8,0x10,0x25};
    static const unsigned char random_mask[31]={
        1,1,0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,
        1,1,1,1,1,0,0,0,0,1,1,1,1};
    int string_ok=memcmp(g_disrupt_base+runtime_string_assign_rva(),string_assign,
        sizeof(string_assign))==0;
    int owner_ok=memcmp(g_disrupt_base+g_runtime_state.owner_vector_reserve_rva,
        owner_reserve,
        sizeof(owner_reserve))==0;
    int epilogue_ok=memcmp(ENGINE_ADDRESS(SLICE_RESULT_EPILOGUE_RVA),
        slice_epilogue,sizeof(slice_epilogue))==0;
    int profile_ok=resolver_masked_match(ENGINE_ADDRESS(PROFILE_CALLBACK_RVA),
        profile_callback,profile_mask,sizeof(profile_callback));
    int random_ok=resolver_masked_match(
        g_disrupt_base+runtime_native_random_rva(),
        random_pattern,random_mask,sizeof(random_pattern));
    append_log("RUNTIME_PROFILE_HELPERS profile=%ld stringAssign=%u ownerReserve=%u sliceEpilogue=%u profileCallback=%u nativeRandom=%u stringValueDependency=localNonNullPayloadPlus0x18",
        (long)runtime_profile_id(),string_ok ? 1U:0U,owner_ok ? 1U:0U,
        epilogue_ok ? 1U:0U,profile_ok ? 1U:0U,random_ok ? 1U:0U);
    return string_ok && owner_ok && epilogue_ok && profile_ok && random_ok;
}
