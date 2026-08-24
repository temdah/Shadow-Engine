/* Module 70: signature preflight, phased hook installation, startup proof,
 * exported initialization entry point, and DllMain. */
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

typedef struct ShadowPatchPlan {
    const RuntimeProfile *profile;
    const unsigned char *frame_builder_prologue;
    size_t frame_builder_prologue_size;
    int prepared;
} ShadowPatchPlan;

static ShadowPatchPlan g_patch_plan;

static int prepare_early_patch_plan(HMODULE disrupt,ShadowPatchPlan *plan)
{
    unsigned char *base=(unsigned char *)disrupt;
    if(!plan) return 0;
    memset(plan,0,sizeof(*plan));
    g_disrupt_base=base;
    if(!select_runtime_profile(base)) {
        run_signature_probe(base);
        append_log("STAGE_D_ABORT reason=unknownRuntimeProfile diagnosticMapComplete=1 noEngineMutation=1");
        return 0;
    }
    if(!recover_owner_vector_reserve()) {
        append_log("RUNTIME_PROFILE_REJECTED profile=%ld reason=ownerReserveCallsiteRecoveryFailed action=readOnlyFullSiteMap noEngineMutation=1",
            (long)runtime_profile_id());
        run_signature_probe(base);
        append_log("STAGE_D_ABORT reason=ownerReserveCallsiteRecoveryFailed diagnosticMapComplete=1 noEngineMutation=1");
        return 0;
    }
    if(!validate_selected_profile_helpers()) {
        append_log("RUNTIME_PROFILE_REJECTED profile=%ld reason=helperValidationFailed action=readOnlyFullSiteMap noEngineMutation=1",
            (long)runtime_profile_id());
        run_signature_probe(base);
        append_log("STAGE_D_ABORT reason=selectedProfileHelperMismatch diagnosticMapComplete=1 noEngineMutation=1");
        return 0;
    }
    if(memcmp(ENGINE_ADDRESS(RESOURCE_CONSTRUCTOR_RVA),g_resource_prologue,
              sizeof(g_resource_prologue))!=0) {
        append_log("STAGE_D_ABORT signature=resource_constructor expectedBuildMismatch=1 noEngineMutation=1");
        return 0;
    }
    if(memcmp(ENGINE_ADDRESS(REGISTER_PASS_RVA),g_register_pass_prologue,
              sizeof(g_register_pass_prologue))!=0) {
        append_log("STAGE_D_ABORT signature=register_pass expectedBuildMismatch=1 noEngineMutation=1");
        return 0;
    }
    if(!validate_map_range_loop_extension()) {
        append_log("STAGE_D_ABORT signature=native_map_loop expectedBuildMismatch=1 noEngineMutation=1");
        return 0;
    }
    if(!validate_downstream_sites(base)) {
        append_log("STAGE_E_ABORT signature=deferred_downstream expectedBuildMismatch=1 noEngineMutation=1");
        return 0;
    }
    append_log("STAGE_M_PREFLIGHT_COMPLETE signatures=resource,pass,mapLoop,earlyQueueLayout,sliceResultStores,deferredHandleConsumers,deferredPassIndexConsumers,deferredV1216Scheduler,deferredShadowManager,rendererQueue,faceCostReader,resourceWrapperDirect,renderRecordConstructor,frameBuilder,postCallObjectRenderers,renderRecordRelease requiredPassMaxKey=0x%04X requiredPassSlots=%u passTableVerification=readOnly diagnostics=minimal tailRelocations=%u noEngineMutationBeforeThisMarker=1",
        REQUIRED_PASS_MAX_KEY,REQUIRED_PASS_TABLE_SLOTS,
        (unsigned)(sizeof(g_tail_patches)/sizeof(g_tail_patches[0])));
    plan->profile=g_runtime_state.selected;
    plan->frame_builder_prologue=runtime_frame_builder_prologue(
        &plan->frame_builder_prologue_size);
    plan->prepared=plan->profile && plan->frame_builder_prologue &&
        plan->frame_builder_prologue_size>0;
    return plan->prepared;
}

static int commit_early_patch_plan(const ShadowPatchPlan *plan)
{
    PatchTransaction transaction;
    PatchRollbackResult rollback;
    const char *failed_step=NULL;
    if(!plan || !plan->prepared || plan->profile!=g_runtime_state.selected) {
        append_log("STAGE_E_ABORT mutation=early_commit invalidPreparedPlan=1 noEngineMutation=1");
        return 0;
    }
    if(!patch_transaction_begin(&transaction,"early",-1)) {
        append_log("STAGE_E_ABORT mutation=early_commit transactionBusy=1 noEngineMutation=1");
        return 0;
    }
    if(!apply_queue_layout()) {
        failed_step="early_queue_layout";
        goto rollback_early;
    }
    append_log("STAGE_E_EARLY_QUEUE_LAYOUT_APPLIED beforeQueueConstruction=1 physicalQueueEntries=25 allocationBytes=0x397C0 relocatedTailReferences=%u tailInitializedByNativeConstructor=1",
        (unsigned)(sizeof(g_tail_patches)/sizeof(g_tail_patches[0])));
    if(!install_map_range_loop_extension()) {
        failed_step="native_map_loop_extension";
        goto rollback_early;
    }
    if(!install_detour(ENGINE_ADDRESS(RENDER_RECORD_CONSTRUCTOR_RVA),
        g_render_record_constructor_prologue,
        sizeof(g_render_record_constructor_prologue),
        hooked_external_render_record_constructor,
        (void **)&g_original_render_record_constructor)) {
        failed_step="render_record_constructor";
        goto rollback_early;
    }
    if(!install_detour(ENGINE_ADDRESS(RESOURCE_CONSTRUCTOR_RVA),g_resource_prologue,
                       sizeof(g_resource_prologue),hooked_resource_constructor,
                       (void **)&g_original_resource_constructor)) {
        failed_step="resource_constructor";
        goto rollback_early;
    }
    if(!install_detour(ENGINE_ADDRESS(REGISTER_PASS_RVA),g_register_pass_prologue,
                       sizeof(g_register_pass_prologue),hooked_register_pass,
                       (void **)&g_original_register_pass)) {
        failed_step="register_pass";
        goto rollback_early;
    }
    if(!patch_transaction_commit(&transaction)) {
        failed_step="transaction_commit";
        goto rollback_early;
    }
    InterlockedExchange(&g_hooks_installed,1);
    append_log("STAGE_M_EARLY_HOOKS_INSTALLED nativeLoopMaps=24 nativeManagerMaps=16 externalMaps=8 pairedExtraPasses=16 passTableAllocatorHook=0 readOnlyPassTableVerification=1 requiredPassSlots=50 earlyQueueLayout=25 renderRecordConstructorExternalState=1 diagnostics=minimal downstreamHandleRouting=0 downstreamPassRouting=0 schedulerChanged=0 managerHook=0 transactionWrites=%u transactionAllocations=%u rollbackAvailableUntilCommit=1",
        (unsigned)transaction.write_count,
        (unsigned)transaction.allocation_count);
    return 1;

rollback_early:
    rollback=patch_transaction_rollback(&transaction);
    InterlockedExchange(&g_hooks_installed,
        rollback.failed_restores==0 ? 0:-1);
    append_log("STAGE_E_TRANSACTION_ROLLBACK phase=early failedStep=%s appliedWrites=%u restoredWrites=%u failedRestores=%u releasedAllocations=%u rollbackComplete=%u beforeQueueConstruction=1",
        failed_step ? failed_step:"unknown",
        (unsigned)transaction.write_count,
        (unsigned)rollback.restored_writes,
        (unsigned)rollback.failed_restores,
        (unsigned)rollback.released_allocations,
        rollback.failed_restores==0 ? 1U:0U);
    return 0;
}

static int install_early_construction_hooks(HMODULE disrupt)
{
    if(!prepare_early_patch_plan(disrupt,&g_patch_plan)) return 0;
    return commit_early_patch_plan(&g_patch_plan);
}

static int install_deferred_downstream(void)
{
    HMODULE user32;
    PatchTransaction transaction;
    PatchRollbackResult rollback;
    const char *failed_step=NULL;
    const unsigned char *frame_builder_prologue=
        g_patch_plan.frame_builder_prologue;
    size_t frame_builder_prologue_size=
        g_patch_plan.frame_builder_prologue_size;
    if(InterlockedCompareExchange(&g_downstream_state,1,0)!=0) return 0;
    if(!g_patch_plan.prepared ||
       g_patch_plan.profile!=g_runtime_state.selected ||
       !frame_builder_prologue || !frame_builder_prologue_size) {
        append_log("STAGE_E_DOWNSTREAM_ABORT invalidPreparedPlan=1 noDeferredMutation=1");
        InterlockedExchange(&g_downstream_state,-1);
        return 0;
    }
    if(!validate_deferred_sites_after_early_queue(g_disrupt_base)) {
        append_log("STAGE_E_DOWNSTREAM_ABORT signaturesChangedDuringDeferral=1");
        InterlockedExchange(&g_downstream_state,-1);
        return 0;
    }
    if(!patch_transaction_begin(&transaction,"deferred",-1)) {
        append_log("STAGE_E_DOWNSTREAM_ABORT transactionBusy=1 noDeferredMutation=1");
        InterlockedExchange(&g_downstream_state,-1);
        return 0;
    }
    if(!install_slice_result_bounds_relays()) {
        failed_step="slice_result_bounds_relays";
        goto rollback_deferred;
    }
    if(!install_handle_relays()) {
        failed_step="handle_relays";
        goto rollback_deferred;
    }
    if(!install_pass_index_relays()) {
        failed_step="pass_index_relays";
        goto rollback_deferred;
    }
    if(!apply_v1216_scheduler()) {
        failed_step="scheduler";
        goto rollback_deferred;
    }
    g_profile_callback=(ProfileCallbackFn)ENGINE_ADDRESS(PROFILE_CALLBACK_RVA);
    g_owner_vector_reserve=(OwnerVectorReserveFn)
        (g_disrupt_base+g_runtime_state.owner_vector_reserve_rva);
    if(!install_manager_detour()) {
        failed_step="manager_detour";
        goto rollback_deferred;
    }
    if(!install_detour(ENGINE_ADDRESS(RENDER_QUEUE_PROCESS_RVA),
        g_renderer_queue_prologue,sizeof(g_renderer_queue_prologue),
        hooked_renderer_queue,(void **)&g_original_renderer_queue)) {
        failed_step="renderer_queue";
        goto rollback_deferred;
    }
    if(!install_detour(ENGINE_ADDRESS(FACE_COST_READER_RVA),
        g_face_cost_reader_prologue,sizeof(g_face_cost_reader_prologue),
        hooked_face_cost_reader,(void **)&g_original_face_cost_reader)) {
        failed_step="face_cost_reader";
        goto rollback_deferred;
    }
    g_original_resource_wrapper=(ResourceWrapperFn)
        ENGINE_ADDRESS(RESOURCE_WRAPPER_RVA);
    if(!install_detour(ENGINE_ADDRESS(FRAME_BUILDER_RVA),
        frame_builder_prologue,frame_builder_prologue_size,
        hooked_external_frame_builder,(void **)&g_original_frame_builder)) {
        failed_step="frame_builder";
        goto rollback_deferred;
    }
    if(!install_detour(ENGINE_ADDRESS(POST_CALL_OBJECT_RENDERERS_RVA),
        g_post_call_object_renderers_prologue,
        sizeof(g_post_call_object_renderers_prologue),
        hooked_external_post_call_object_renderers,
        (void **)&g_original_post_call_object_renderers)) {
        failed_step="post_call_object_renderers";
        goto rollback_deferred;
    }
    if(!install_detour(ENGINE_ADDRESS(RENDER_RECORD_RELEASE_RVA),
        g_render_record_release_prologue,
        sizeof(g_render_record_release_prologue),
        hooked_external_render_record_release,
        (void **)&g_original_render_record_release)) {
        failed_step="render_record_release";
        goto rollback_deferred;
    }
    if(!patch_transaction_commit(&transaction)) {
        failed_step="transaction_commit";
        goto rollback_deferred;
    }
    InterlockedExchange(&g_lifecycle_hooks_state,2);
    user32=GetModuleHandleW(L"user32.dll");
    if(!user32) user32=LoadLibraryW(L"user32.dll");
    if(user32) g_get_async_key_state=(GetAsyncKeyStateFn)
        GetProcAddress(user32,"GetAsyncKeyState");
    InterlockedExchange(&g_renderer_hook_state,2);
    InterlockedExchange(&g_routing_state,2);
    InterlockedExchange(&g_downstream_state,2);
    append_log("STAGE_M_DOWNSTREAM_INSTALLED afterConstructors=1 stabilizationMs=%u physicalQueueEntries=25 queueLayoutAppliedBeforeConstruction=1 physicalFaceBudget=24 wholeRecordPriorityPrefix=1 queueCountRestoredAfterRender=1 relocatedTailReferences=%u handleConsumers=3 passIndexConsumers=2 sliceResultBoundsRelays=2 nativeSliceResults=%u externalSliceResults=7 routedSliceIndices=17-23 externalConsumer=postCallObjectRenderers releaseFallback=renderRecordRelease nonNullStoreRva=0x2CC316 nullStoreRva=0x2CC32B schedulerInit=16 schedulerFloor=16 schedulerUpper=16 schedulerUpperRva=0x2E9C3B cacheOwnerCeiling=4 ownerVectorPhysicalReserve=8 ownerRecordStride=0x700 reserveRva=0x319160 rendererCapacityProbe=1 queueTailIntegrityProbe=1 nullFaceCostGuard=1 nativeWrapperDirectCall=1 resourceWrapperDetour=0 resourceSubmitDetour=0 renderRecordLifecycleHooks=constructor,builder,postCallObjectRenderers,release lifecycleRecordSlots=%u manualQueueCapture=%u blackKey=F8 recoveredKey=F9 repair=0 sidecars=0 taskProvenance=0 resourceTrace=0 sharedTextureTrace=0 frameGraphTrace=0 acquireHook=0 diagnostics=minimal firstManagerProfileCallback=1 fullCandidatePassthrough=1 filtering=0 limiter=0 transactionWrites=%u transactionAllocations=%u rollbackAvailableUntilCommit=1",
        DOWNSTREAM_STABILIZE_MS,
        (unsigned)(sizeof(g_tail_patches)/sizeof(g_tail_patches[0])),
        NATIVE_SLICE_RESULT_COUNT,
        LIFECYCLE_RECORD_SLOTS,
        g_get_async_key_state ? 1U : 0U,
        (unsigned)transaction.write_count,
        (unsigned)transaction.allocation_count);
    return 1;

rollback_deferred:
    rollback=patch_transaction_rollback(&transaction);
    if(rollback.failed_restores==0) {
        g_profile_callback=NULL;
        g_owner_vector_reserve=NULL;
        g_original_resource_wrapper=NULL;
        InterlockedExchange(&g_slice_result_guard_state,-1);
        InterlockedExchange(&g_lifecycle_hooks_state,-1);
        InterlockedExchange(&g_renderer_hook_state,-1);
        InterlockedExchange(&g_routing_state,-1);
        InterlockedExchange(&g_downstream_state,-1);
    } else {
        /* Preserve helper/original pointers for any hook whose executable
         * bytes could not be restored. Mark the phase unrecoverable rather
         * than creating a use-after-free/null-call path. */
        InterlockedExchange(&g_slice_result_guard_state,-2);
        InterlockedExchange(&g_lifecycle_hooks_state,-2);
        InterlockedExchange(&g_renderer_hook_state,-2);
        InterlockedExchange(&g_routing_state,-2);
        InterlockedExchange(&g_downstream_state,-2);
    }
    append_log("STAGE_E_TRANSACTION_ROLLBACK phase=deferred failedStep=%s appliedWrites=%u restoredWrites=%u failedRestores=%u releasedAllocations=%u rollbackComplete=%u earlyPhaseRetained=1",
        failed_step ? failed_step:"unknown",
        (unsigned)transaction.write_count,
        (unsigned)rollback.restored_writes,
        (unsigned)rollback.failed_restores,
        (unsigned)rollback.released_allocations,
        rollback.failed_restores==0 ? 1U:0U);
    return 0;
}

static void log_handle_snapshot(void)
{
    append_log("RESOURCE_CONSTRUCTOR_HIT count=%ld manager=%p nativeNonZero=%ld nativeDistinct=%ld handles=%08X,%08X,%08X,%08X,%08X,%08X,%08X,%08X,%08X,%08X,%08X,%08X,%08X,%08X,%08X,%08X extraCreation=8 externalNonZero=%ld externalDistinct=%ld",
        (long)g_resource_hits,g_last_resource_manager,
        (long)g_native_nonzero_handles,(long)g_native_distinct_handles,
        g_native_handles[0],g_native_handles[1],g_native_handles[2],g_native_handles[3],
        g_native_handles[4],g_native_handles[5],g_native_handles[6],g_native_handles[7],
        g_native_handles[8],g_native_handles[9],g_native_handles[10],g_native_handles[11],
        g_native_handles[12],g_native_handles[13],g_native_handles[14],g_native_handles[15],
        (long)g_range_nonzero_handles,(long)g_range_distinct_handles);
}

static DWORD WINAPI stage_e_worker(void *unused)
{
    HMODULE disrupt=NULL;
    volatile unsigned char *intercepted=NULL;
    volatile unsigned char *ready=NULL;
    void **original_run_game=NULL;
    unsigned char last_intercepted=0xFF,last_ready=0xFF;
    void *last_original=(void *)(uintptr_t)~0ULL;
    LONG last_resource=0,last_register=0;
    DWORD elapsed=0;
    int install_attempted=0;
    int session_started=0;
    DWORD constructors_ready_elapsed=0;
    (void)unused;
    if(g_snapshot.troplo) {
        intercepted=(volatile unsigned char *)GetProcAddress(g_snapshot.troplo,"g_RunGameIntercepted");
        ready=(volatile unsigned char *)GetProcAddress(g_snapshot.troplo,"g_NexusReady");
        original_run_game=(void **)GetProcAddress(g_snapshot.troplo,"OriginalRunGame");
    }

    while(elapsed<=POLL_TIMEOUT_MS) {
        LARGE_INTEGER now;
        unsigned char current_intercepted=intercepted ? *intercepted : 0;
        unsigned char current_ready=ready ? *ready : 0;
        void *current_original=original_run_game ? *original_run_game : NULL;
        LONG current_resource=g_resource_hits;
        LONG current_register=g_register_hits;
        if(!disrupt) {
            disrupt=GetModuleHandleW(L"Disrupt_b64.dll");
            if(!disrupt) disrupt=GetModuleHandleW(L"Disrupt_64.dll");
        }
        if(!session_started && disrupt && current_intercepted &&
           current_original && !current_ready) {
            session_started=begin_session_log();
            if(session_started) {
                append_log("BOOTSTRAP_BEGIN product=ShadowEnginePatch version=%s pid=%lu initThread=%lu proxy=%p self=%p",
                    PATCH_VERSION,(unsigned long)GetCurrentProcessId(),
                    (unsigned long)g_snapshot.initialize_thread,g_snapshot.proxy,g_self);
                LONG nexus_entry_kind=InterlockedCompareExchange(&g_nexus_entry_kind,0,0);
                const char *nexus_entry=nexus_entry_kind==2 ? "OnBeforeEngineInit" :
                    (nexus_entry_kind==1 ? "Init" :
                    (nexus_entry_kind==3 ? "DllMainDeferredFallback" : "unknown"));
                append_log("NEXUSTOOLS_EARLY_CALLBACK_COMPAT entry=%s callbackCalls=%ld interface=%p interfaceUsed=0 dllMainInit=0 dllMainDeferredFallback=%u fallbackAttempts=%ld manualHelperLoad=0 helperAlreadyLoaded=%u exports=Init,OnBeforeEngineInit shippedNexusTools112Entry=OnBeforeEngineInit",
                    nexus_entry,
                    (long)g_nexus_init_calls,(void *)g_nexus_interface,
                    nexus_entry_kind==3 ? 1U:0U,
                    (long)g_dllmain_fallback_attempts,
                    g_snapshot.troplo ? 1U:0U);
                append_log("SESSION_ROLE_CONFIRMED runGameIntercepted=1 originalRunGame=%p nexusReady=0 helperWritesSuppressed=1 logMode=create-always",
                    current_original);
                append_log("DESIGN role=engine-only stage=Z-post-fix-performance-control earlyNativeMaps=24 earlyPasses17to24=1 passTableAllocatorHook=0 passTableVerification=readOnly requiredPassMaxKey=0x310C requiredPassSlots=50 directAddedSlotVerification=1 failClosedBeforeExtraPassRegistration=1 queueAllocationAndTailRelocationsBeforeConstruction=1 remainingDownstreamRoutingDeferredUntilAfterConstructors=1 stabilizationMs=%u schedulerInitThen=16 schedulerFloorThen=16 schedulerUpperThen=16 schedulerUpperRva=0x2E9C3B cacheOwnerCeilingA8=4 ownerVectorPhysicalReserve=8 ownerRecordStride=0x700 reserveRva=0x319160 failClosedOnReserveMismatch=1 physicalFaceBudget=24 wholeRecordPriorityPrefix=1 queueCountRestoredAfterRender=1 targetOrdinaryDynamicPositions=15 rendererProbeActive=1 queueTailIntegrityProbe=1 nativeSliceResultCount=17 externalSliceResultCount=7 sliceResultBoundsRelays=2 routedSliceIndices=17-23 externalConsumer=postCallObjectRenderers releaseFallback=renderRecordRelease nonNullStoreRva=0x2CC316 nullStoreRva=0x2CC32B rejectedIndexedWriterDetourRemoved=1 nullFaceCostGuard=1 resourceProducerWrapperRva=0x427860 nativeWrapperDirectCall=1 resourceWrapperDetour=0 resourceSubmitDetour=0 renderRecordLifecycleHooks=constructor,builder,postCallObjectRenderers,release lifecycleRecordSlots=%u repair=0 sidecars=0 taskProvenance=0 resourceTrace=0 sharedTextureTrace=0 frameGraphTrace=0 acquireHook=0 diagnostics=minimal stageYSummaryPeriodMs=%u manualQueueAndStageYCapture=F8,F9 fieldMutation=externalSliceResultRouting fullCandidatePassthrough=1 filtering=0 vehicleLimiter=0",
                    DOWNSTREAM_STABILIZE_MS,LIFECYCLE_RECORD_SLOTS,
                    EXTERNAL_SLICE_SUMMARY_PERIOD_MS);
                append_log("TROPLO_BIND module=%p error=%lu boundMs=%.3f",
                    g_snapshot.troplo,(unsigned long)g_snapshot.troplo_error,
                    elapsed_ms(g_snapshot.troplo_return_time));
                append_log("TROPLO_EXPORTS interceptedVar=%p readyVar=%p originalRunGameVar=%p",
                    (void *)intercepted,(void *)ready,(void *)original_run_game);
                QueryPerformanceCounter(&now);
                append_log("DISRUPT_MAPPED module=%p elapsedMs=%.3f",disrupt,elapsed_ms(now));
            }
        }
        if(current_intercepted!=last_intercepted) {
            QueryPerformanceCounter(&now);
            append_log("RUN_GAME_INTERCEPTED value=%u elapsedMs=%.3f",
                (unsigned)current_intercepted,elapsed_ms(now));
            last_intercepted=current_intercepted;
        }
        if(current_ready!=last_ready) {
            QueryPerformanceCounter(&now);
            append_log("NEXUS_READY value=%u elapsedMs=%.3f",
                (unsigned)current_ready,elapsed_ms(now));
            last_ready=current_ready;
        }
        if(current_original!=last_original) {
            QueryPerformanceCounter(&now);
            append_log("ORIGINAL_RUN_GAME value=%p elapsedMs=%.3f",
                current_original,elapsed_ms(now));
            last_original=current_original;
        }
        if(!install_attempted && disrupt && current_intercepted && current_original) {
            install_attempted=1;
            if(current_ready) {
                append_log("STAGE_E_ABORT_LATE nexusReady=1 noEngineMutation=1");
            } else {
                install_early_construction_hooks(disrupt);
            }
        }
        if(current_resource!=last_resource) {
            log_handle_snapshot();
            last_resource=current_resource;
        }
        if(current_register!=last_register) {
            append_log("REGISTER_PASS_ACTIVITY nativeHookCalls=%ld nativeShadow=%ld nativeShadowAlpha=%ld nativeLastShadow=%ld nativeLastAlpha=%ld stageCPassState=%ld extraRegisteredByPatch=%u",
                (long)current_register,(long)g_shadow_hits,(long)g_shadow_alpha_hits,
                (long)g_last_native_shadow_seen,(long)g_last_native_alpha_seen,
                (long)g_range_pass_state,g_range_pass_state==2 ? 16U : 0U);
            last_register=current_register;
        }
        if(!constructors_ready_elapsed && g_range_resource_state==2 &&
           g_range_pass_state==2) {
            constructors_ready_elapsed=elapsed ? elapsed : 1;
            append_log("STAGE_E_CONSTRUCTORS_COMPLETE downstreamStillDeferred=1 stabilizeForMs=%u",
                DOWNSTREAM_STABILIZE_MS);
        }
        if(constructors_ready_elapsed && g_downstream_state==0 &&
           elapsed-constructors_ready_elapsed>=DOWNSTREAM_STABILIZE_MS) {
            install_deferred_downstream();
        }
        if(g_hooks_installed && g_resource_hits>0 && g_register_hits>0 &&
           g_last_native_shadow_seen && g_last_native_alpha_seen &&
           g_pass_table_state==2 &&
           g_pass_table_slots>=(LONG)REQUIRED_PASS_TABLE_SLOTS &&
           g_pass_table_stored_max_key>=(LONG)REQUIRED_PASS_MAX_KEY &&
           g_pass_table_added_nonzero==(LONG)EXTRA_PASS_SLOT_COUNT &&
           g_pass_table_added_distinct==(LONG)EXTRA_PASS_SLOT_COUNT &&
           g_range_resource_state==2 && g_range_nonzero_handles==8 &&
           g_range_distinct_handles==8 && g_range_pass_state==2 &&
           g_routing_state==2 && g_downstream_state==2 && g_profile_state==2 &&
           g_lifecycle_hooks_state==2 &&
           g_renderer_hook_state==2 && g_observed_scheduler_b4==TARGET_DYNAMIC_B4 &&
           g_observed_cache_a8==TARGET_CACHE_A8 && g_owner_reserve_state==2 &&
           g_owner_capacity_after>=(LONG)OWNER_VECTOR_RESERVE_CAPACITY &&
           g_owner_reserve_fail_closed==0 &&
           g_owner_base_changed_during_manager==0) {
            append_log("STAGE_M_COMPLETE resourceConstructorReached=1 passConstructorReached=1 passTableAllocatorHook=0 passTableStoredMaxKey=0x%04lX physicalPassSlots=%ld requiredPassSlots=%u addedPassSlotsNonZero=%ld addedPassSlotsDistinct=%ld directPassStorageVerified=1 extraMaps16to23NonZero=8 pairedPasses17to24RegistrationReturned=16 downstreamInstalledAfterConstructors=1 stabilizationMs=%u physicalQueueEntries=25 physicalFaceBudget=24 wholeRecordPriorityPrefix=1 observedB4=%ld observedA8=%ld ownerReserveState=%ld ownerCapacityBefore=%ld ownerCapacityAfter=%ld ownerReserveFailClosed=%ld ownerBaseChangesDuringManager=%ld targetOrdinaryDynamicPositions=15 targetCachedOwners=4 ownerPhysicalReserve=8 rendererProbeActive=1 queueTailIntegrityProbe=1 tailInvalidBeforeRenderer=%ld tailInvalidAfterRenderer=%ld extraFaceUseSeen=%ld faceBudgetClamps=%ld faceBudgetDroppedEntries=%ld faceBudgetPeakRequested=%ld nullFaceCostSkips=%ld lifecycleConstructorCalls=%ld lifecycleBuilderCalls=%ld postCallConsumerCalls=%ld lifecycleReleaseCalls=%ld externalResultRouting=1 nativeWrapperDirectCall=1 resourceWrapperDetour=0 resourceSubmitDetour=0 repair=0 sidecars=0 taskProvenance=0 resourceTrace=0 sharedTextureTrace=0 frameGraphTrace=0 acquireHook=0 diagnostics=minimal fullCandidatePassthrough=1 filtering=0 limiter=0",
                (long)g_pass_table_stored_max_key,(long)g_pass_table_slots,
                REQUIRED_PASS_TABLE_SLOTS,
                (long)g_pass_table_added_nonzero,
                (long)g_pass_table_added_distinct,
                DOWNSTREAM_STABILIZE_MS,
                (long)g_observed_scheduler_b4,(long)g_observed_cache_a8,
                (long)g_owner_reserve_state,(long)g_owner_capacity_before,
                (long)g_owner_capacity_after,(long)g_owner_reserve_fail_closed,
                (long)g_owner_base_changed_during_manager,
                (long)g_tail_invalid_before_renderer,
                (long)g_tail_invalid_after_renderer,
                (long)g_extra_face_use_seen,(long)g_face_budget_clamps,
                (long)g_face_budget_dropped_entries,
                (long)g_face_budget_peak_requested,(long)g_null_face_cost_skips,
                (long)g_lifecycle_constructor_calls,
                (long)g_lifecycle_builder_calls,
                (long)g_postcall_calls,
                (long)g_lifecycle_release_calls);
            return 0;
        }
        Sleep(POLL_INTERVAL_MS);
        elapsed+=POLL_INTERVAL_MS;
    }
    append_log("STAGE_M_TIMEOUT hooksInstalled=%ld passTableState=%ld passTableStoredMaxKey=0x%04lX passTableSlots=%ld addedPassSlotsNonZero=%ld addedPassSlotsDistinct=%ld resourceHits=%ld registerHits=%ld nativeLastShadow=%ld nativeLastAlpha=%ld resourceState=%ld extraNonZero=%ld extraDistinct=%ld passState=%ld routingState=%ld downstreamState=%ld profileState=%ld rendererHookState=%ld lifecycleHooksState=%ld rendererCalls=%ld tailInvalidBeforeRenderer=%ld tailInvalidAfterRenderer=%ld extraFaceUseSeen=%ld nullFaceCostSkips=%ld ownerReserveState=%ld ownerCapacityBefore=%ld ownerCapacityAfter=%ld ownerReserveFailClosed=%ld ownerBaseChangesDuringManager=%ld observedB4=%ld observedA8=%ld lifecycleConstructorCalls=%ld lifecycleBuilderCalls=%ld postCallConsumerCalls=%ld lifecycleReleaseCalls=%ld lifecycleRecordSlotMisses=%ld resourceWrapperDetour=0 resourceSubmitDetour=0 diagnostics=minimal limiter=0",
        (long)g_hooks_installed,
        (long)g_pass_table_state,
        (long)g_pass_table_stored_max_key,(long)g_pass_table_slots,
        (long)g_pass_table_added_nonzero,(long)g_pass_table_added_distinct,
        (long)g_resource_hits,(long)g_register_hits,
        (long)g_last_native_shadow_seen,(long)g_last_native_alpha_seen,
        (long)g_range_resource_state,(long)g_range_nonzero_handles,
        (long)g_range_distinct_handles,(long)g_range_pass_state,
        (long)g_routing_state,(long)g_downstream_state,(long)g_profile_state,
        (long)g_renderer_hook_state,(long)g_lifecycle_hooks_state,
        (long)g_renderer_calls,
        (long)g_tail_invalid_before_renderer,
        (long)g_tail_invalid_after_renderer,
        (long)g_extra_face_use_seen,(long)g_null_face_cost_skips,
        (long)g_owner_reserve_state,
        (long)g_owner_capacity_before,(long)g_owner_capacity_after,
        (long)g_owner_reserve_fail_closed,
        (long)g_owner_base_changed_during_manager,
        (long)g_observed_scheduler_b4,(long)g_observed_cache_a8,
        (long)g_lifecycle_constructor_calls,
        (long)g_lifecycle_builder_calls,(long)g_postcall_calls,
        (long)g_lifecycle_release_calls,
        (long)g_lifecycle_record_slot_misses);
    return 0;
}

static BOOL start_shadow_engine_patch(void)
{
    wchar_t path[MAX_PATH];
    wchar_t *slash;
    HANDLE thread;
    if(InterlockedCompareExchange(&g_initialized,1,0)!=0) return TRUE;
    QueryPerformanceFrequency(&g_snapshot.frequency);
    QueryPerformanceCounter(&g_snapshot.initialize_time);
    g_snapshot.initialize_thread=GetCurrentThreadId();
    g_snapshot.proxy=GetModuleHandleW(L"dinput8.dll");
    g_snapshot.disrupt_before=GetModuleHandleW(L"Disrupt_b64.dll");
    if(!g_snapshot.disrupt_before) g_snapshot.disrupt_before=GetModuleHandleW(L"Disrupt_64.dll");
    if(!GetModuleFileNameW(g_self,path,MAX_PATH)) return FALSE;
    slash=path+lstrlenW(path);
    while(slash>path && slash[-1]!=L'\\' && slash[-1]!=L'/') --slash;
    *slash=L'\0';
    lstrcpynW(g_snapshot.bin_directory,path,MAX_PATH);
    SetLastError(ERROR_SUCCESS);
    g_snapshot.troplo=GetModuleHandleW(L"TroploAsiInjectionHelper.asi");
    g_snapshot.troplo_error=g_snapshot.troplo ? ERROR_SUCCESS : GetLastError();
    QueryPerformanceCounter(&g_snapshot.troplo_return_time);
    if(!g_snapshot.troplo) {
        LONG nexus_entry_kind=InterlockedCompareExchange(&g_nexus_entry_kind,0,0);
        const char *nexus_entry=nexus_entry_kind==2 ? "OnBeforeEngineInit" :
            (nexus_entry_kind==1 ? "Init" :
            (nexus_entry_kind==3 ? "DllMainDeferredFallback" : "unknown"));
        if(begin_session_log())
            append_log("NEXUSTOOLS_EARLY_CALLBACK_ABORT entry=%s helperAlreadyLoaded=0 manualHelperLoad=0 error=%lu noEngineMutation=1",
                nexus_entry,
                (unsigned long)g_snapshot.troplo_error);
        return FALSE;
    }
    thread=CreateThread(NULL,0,stage_e_worker,NULL,0,NULL);
    if(!thread) {
        LONG nexus_entry_kind=InterlockedCompareExchange(&g_nexus_entry_kind,0,0);
        const char *nexus_entry=nexus_entry_kind==2 ? "OnBeforeEngineInit" :
            (nexus_entry_kind==1 ? "Init" :
            (nexus_entry_kind==3 ? "DllMainDeferredFallback" : "unknown"));
        if(begin_session_log())
            append_log("NEXUSTOOLS_EARLY_CALLBACK_ABORT entry=%s helperAlreadyLoaded=1 workerThreadCreated=0 error=%lu noEngineMutation=1",
                nexus_entry,
                (unsigned long)GetLastError());
        return FALSE;
    }
    CloseHandle(thread);
    return TRUE;
}

static void nexus_early_entry(void *interface,LONG entry_kind)
{
    InterlockedIncrement(&g_nexus_init_calls);
    InterlockedCompareExchange(&g_nexus_entry_kind,entry_kind,0);
    InterlockedExchangePointer((PVOID volatile *)&g_nexus_interface,interface);
    start_shadow_engine_patch();
}

__declspec(dllexport) void Init(void *interface)
{
    nexus_early_entry(interface,1);
}

__declspec(dllexport) void OnBeforeEngineInit(void *interface)
{
    nexus_early_entry(interface,2);
}

/* NexusTools 1.1.12 can load a native ASI without dispatching its early
 * callback during Watch Dogs' duplicated/subprocess startup.  Keep the
 * supported callback as the primary route: this worker yields briefly after
 * DllMain returns, then starts the same guarded bootstrap only if the callback
 * has not already claimed it.  start_shadow_engine_patch merely launches the
 * existing polling worker; all engine mutation still waits for intercepted
 * RunGame, a non-null OriginalRunGame, and g_NexusReady==0. */
static DWORD WINAPI dllmain_deferred_fallback(void *unused)
{
    (void)unused;
    Sleep(1U);
    if(InterlockedCompareExchange(&g_initialized,0,0)!=0) return 0;
    InterlockedIncrement(&g_dllmain_fallback_attempts);
    InterlockedCompareExchange(&g_nexus_entry_kind,3,0);
    start_shadow_engine_patch();
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE instance,DWORD reason,LPVOID reserved)
{
    (void)reserved;
    if(reason==DLL_PROCESS_ATTACH) {
        HANDLE fallback_thread;
        g_self=instance;
        DisableThreadLibraryCalls(instance);
        fallback_thread=CreateThread(NULL,0,dllmain_deferred_fallback,
            NULL,0,NULL);
        if(fallback_thread) CloseHandle(fallback_thread);
    }
    return TRUE;
}
