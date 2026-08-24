/* Module 30: whole-record face admission, queue/tail validation, residency
 * sampling, F8/F9 queue snapshots, and face-cost safety. */
static void trace_renderer_capacity(void *queue)
{
    unsigned char *base=(unsigned char *)queue;
    uint32_t entries,i,faces=0,active=0;
    DWORD now=GetTickCount();
    if(now-g_last_residency_probe_tick<250U) return;
    g_last_residency_probe_tick=now;
    if(!base || !readable_memory(base+RENDER_QUEUE_COUNT_OFFSET,sizeof(uint32_t)))
        return;
    entries=*(uint32_t *)(base+RENDER_QUEUE_COUNT_OFFSET);
    if(entries>TOTAL_LOCAL_MAPS+1U) entries=TOTAL_LOCAL_MAPS+1U;
    for(i=0;i<entries;++i) {
        unsigned char *entry=base+(uint64_t)i*RENDER_QUEUE_ENTRY_BYTES;
        uint32_t entry_faces;
        if(!readable_memory(entry+0x20A0,0x10)) break;
        entry_faces=*(uint32_t *)(entry+0x20A0);
        if(entry_faces>6U) entry_faces=0;
        if(entry_faces) ++active;
        faces+=entry_faces;
    }
    if(faces>ORIGINAL_LOCAL_MAPS &&
       InterlockedCompareExchange(&g_extra_face_use_seen,1,0)==0) {
        append_log("STAGE_F_EXTRA_CAPACITY_FIRST_USE rendererCall=%ld queue=%p entries=%u activeEntries=%u faces=%u firstExtraMap=16 highestUsedMap=%u physicalMaps=24 overflow=%u",
            (long)g_renderer_calls,queue,entries,active,faces,faces-1U,
            faces>TOTAL_LOCAL_MAPS?faces-TOTAL_LOCAL_MAPS:0U);
    }
    /* Public builds keep a low-frequency capacity heartbeat for support logs.
     * Immediate first-use, overflow, clamp, and tail-integrity diagnostics are
     * reported separately, so successful play does not need a line per second. */
    if(now-g_last_residency_log_tick>=30000U) {
        append_log("STAGE_F_RESIDENCY rendererCall=%ld queue=%p entries=%u activeEntries=%u faces=%u extraMapsInUse=%u physicalMaps=24 overflow=%u B4=%ld A8=%ld ownerReserveState=%ld ownerCapacity=%ld ownerBaseChangesDuringManager=%ld",
            (long)g_renderer_calls,queue,entries,active,faces,
            faces>ORIGINAL_LOCAL_MAPS?faces-ORIGINAL_LOCAL_MAPS:0U,
            faces>TOTAL_LOCAL_MAPS?faces-TOTAL_LOCAL_MAPS:0U,
            (long)g_observed_scheduler_b4,(long)g_observed_cache_a8,
            (long)g_owner_reserve_state,(long)g_owner_capacity_after,
            (long)g_owner_base_changed_during_manager);
        g_last_residency_log_tick=now;
    }
}
/* The scheduler's B4 limit counts admitted dynamic-light records, but the
 * renderer indexes local shadow maps by the sum of every record's face count.
 * Point/cascade records can therefore make admitted positions request more
 * than the 24 real local maps.  Keep complete records in priority order and
 * temporarily expose only the largest prefix whose whole faces fit.  Restoring
 * the queue count after rendering leaves the native cleanup/ownership path in
 * control of every record that the manager built. */
static uint32_t clamp_renderer_queue_to_face_budget(void *queue,
    uint32_t *original_entries,uint32_t *requested_faces,
    uint32_t *admitted_faces)
{
    unsigned char *base=(unsigned char *)queue;
    uint32_t entries,index,faces=0,safe_entries=0;
    if(original_entries) *original_entries=0;
    if(requested_faces) *requested_faces=0;
    if(admitted_faces) *admitted_faces=0;
    if(!base || !readable_memory(base+RENDER_QUEUE_COUNT_OFFSET,
                                 sizeof(uint32_t))) return 0;
    entries=*(uint32_t *)(base+RENDER_QUEUE_COUNT_OFFSET);
    if(original_entries) *original_entries=entries;
    if(entries>TOTAL_LOCAL_MAPS+1U) entries=TOTAL_LOCAL_MAPS+1U;
    for(index=0;index<entries;++index) {
        unsigned char *entry=base+(uint64_t)index*RENDER_QUEUE_ENTRY_BYTES;
        uint32_t count;
        if(!readable_memory(entry+0x20A0,sizeof(uint32_t))) break;
        count=*(uint32_t *)(entry+0x20A0);
        if(count>6U) break;
        if(faces+count>TOTAL_LOCAL_MAPS) break;
        faces+=count;
        safe_entries=index+1U;
    }
    if(requested_faces) {
        uint32_t total=faces;
        for(;index<entries;++index) {
            unsigned char *entry=base+(uint64_t)index*RENDER_QUEUE_ENTRY_BYTES;
            uint32_t count;
            if(!readable_memory(entry+0x20A0,sizeof(uint32_t))) break;
            count=*(uint32_t *)(entry+0x20A0);
            if(count>6U) break;
            total+=count;
        }
        *requested_faces=total;
    }
    if(admitted_faces) *admitted_faces=faces;
    if(safe_entries<entries) {
        *(uint32_t *)(base+RENDER_QUEUE_COUNT_OFFSET)=safe_entries;
        return safe_entries;
    }
    return entries;
}
static void trace_queue_tail_integrity(void *queue,const char *phase,
    volatile LONG *reported,uint32_t entries,uint32_t faces)
{
    unsigned char *base=(unsigned char *)queue;
    void *tail;
    if(!base || !readable_memory(base+RENDER_QUEUE_TAIL_OFFSET,
                                 sizeof(void *))) return;
    tail=*(void **)(base+RENDER_QUEUE_TAIL_OFFSET);
    if(tail && !readable_memory(tail,0x38U) &&
       InterlockedCompareExchange(reported,1,0)==0) {
        append_log("STAGE_G_QUEUE_TAIL_INVALID phase=%s queue=%p tail=%p entries=%u faces=%u tailOffset=0x397A0 builderReadRva=0x30D1BD directWriterRva=0x3CA76F mutation=0",
            phase,queue,tail,entries,faces);
    }
}

static void log_manual_queue_capture(const char *state,void *manager,void *queue,
    uint32_t original_entries,uint32_t requested_faces,
    uint32_t admitted_entries,uint32_t admitted_faces)
{
    unsigned char *base=(unsigned char *)queue;
    uint32_t scan_entries=original_entries;
    uint32_t entry_index,map_cursor=0;
    void *tail=NULL;
    LONG serial=InterlockedCompareExchange(&g_renderer_calls,0,0);
    if(scan_entries>TOTAL_LOCAL_MAPS+1U) scan_entries=TOTAL_LOCAL_MAPS+1U;
    if(base && readable_memory(base+RENDER_QUEUE_TAIL_OFFSET,sizeof(void *)))
        tail=*(void **)(base+RENDER_QUEUE_TAIL_OFFSET);
    append_log("STAGE_J_MANUAL_CAPTURE_BEGIN serial=%ld state=%s rendererCall=%ld manager=%p queue=%p tail=%p originalEntries=%u requestedFaces=%u admittedEntries=%u admittedFaces=%u B4=%ld A8=%ld ownerReserveState=%ld ownerCapacity=%ld ownerBaseChangesDuringManager=%ld",
        (long)serial,state,(long)g_renderer_calls,manager,queue,tail,
        original_entries,requested_faces,admitted_entries,admitted_faces,
        (long)g_observed_scheduler_b4,(long)g_observed_cache_a8,
        (long)g_owner_reserve_state,(long)g_owner_capacity_after,
        (long)g_owner_base_changed_during_manager);
    append_log("STAGE_J_MANUAL_CAPTURE_HANDLES serial=%ld maps0to23=%08X,%08X,%08X,%08X,%08X,%08X,%08X,%08X,%08X,%08X,%08X,%08X,%08X,%08X,%08X,%08X,%08X,%08X,%08X,%08X,%08X,%08X,%08X,%08X",
        (long)serial,
        g_shadow_map_ids[0],g_shadow_map_ids[1],g_shadow_map_ids[2],
        g_shadow_map_ids[3],g_shadow_map_ids[4],g_shadow_map_ids[5],
        g_shadow_map_ids[6],g_shadow_map_ids[7],g_shadow_map_ids[8],
        g_shadow_map_ids[9],g_shadow_map_ids[10],g_shadow_map_ids[11],
        g_shadow_map_ids[12],g_shadow_map_ids[13],g_shadow_map_ids[14],
        g_shadow_map_ids[15],g_shadow_map_ids[16],g_shadow_map_ids[17],
        g_shadow_map_ids[18],g_shadow_map_ids[19],g_shadow_map_ids[20],
        g_shadow_map_ids[21],g_shadow_map_ids[22],g_shadow_map_ids[23]);
    if(!base) {
        append_log("STAGE_J_MANUAL_CAPTURE_END serial=%ld state=%s scannedEntries=0 scannedFaces=0 queueReadable=0",
            (long)serial,state);
        log_manual_external_slice_snapshot(state,serial);
        return;
    }
    for(entry_index=0;entry_index<scan_entries;++entry_index) {
        unsigned char *entry=base+(uint64_t)entry_index*RENDER_QUEUE_ENTRY_BYTES;
        void *objects[6]={0};
        uint32_t costs[6]={0};
        uint32_t face_index,face_count=0,raw_faces=0,entry_readable=0;
        if(readable_memory(entry+0x20A0,sizeof(uint32_t))) {
            entry_readable=1;
            raw_faces=*(uint32_t *)(entry+0x20A0);
            face_count=raw_faces<=6U ? raw_faces : 0U;
        }
        for(face_index=0;face_index<face_count;++face_index) {
            unsigned char *slot=entry+0x7D8+(uint64_t)face_index*0x820U;
            if(!readable_memory(slot,sizeof(void *))) break;
            objects[face_index]=*(void **)slot;
            if(objects[face_index] && readable_memory(
               (unsigned char *)objects[face_index]+0x20,sizeof(uint32_t)))
                costs[face_index]=*(uint32_t *)((unsigned char *)objects[face_index]+0x20);
        }
        append_log("STAGE_J_MANUAL_CAPTURE_ENTRY serial=%ld entry=%u entryReadable=%u rawFaces=%u mappedFaces=%u mapStart=%u mapEndExclusive=%u f0=%p/%u f1=%p/%u f2=%p/%u f3=%p/%u f4=%p/%u f5=%p/%u",
            (long)serial,entry_index,entry_readable,raw_faces,face_count,
            map_cursor,map_cursor+face_count,
            objects[0],costs[0],objects[1],costs[1],
            objects[2],costs[2],objects[3],costs[3],
            objects[4],costs[4],objects[5],costs[5]);
        map_cursor+=face_count;
    }
    append_log("STAGE_J_MANUAL_CAPTURE_END serial=%ld state=%s scannedEntries=%u scannedFaces=%u queueReadable=1",
        (long)serial,state,scan_entries,map_cursor);
    log_manual_external_slice_snapshot(state,serial);
}

static void poll_manual_capture(void *manager,void *queue,
    uint32_t original_entries,uint32_t requested_faces,
    uint32_t admitted_entries,uint32_t admitted_faces)
{
    LONG f8_down,f9_down;
    if(!g_get_async_key_state) return;
    f8_down=(g_get_async_key_state(VK_F8)&0x8000)!=0;
    f9_down=(g_get_async_key_state(VK_F9)&0x8000)!=0;
    if(f8_down && !InterlockedCompareExchange(&g_f8_was_down,0,0))
        log_manual_queue_capture("BLACK_F8",manager,queue,original_entries,
            requested_faces,admitted_entries,admitted_faces);
    if(f9_down && !InterlockedCompareExchange(&g_f9_was_down,0,0))
        log_manual_queue_capture("RECOVERED_F9",manager,queue,original_entries,
            requested_faces,admitted_entries,admitted_faces);
    InterlockedExchange(&g_f8_was_down,f8_down);
    InterlockedExchange(&g_f9_was_down,f9_down);
}

static void __fastcall hooked_renderer_queue(void *manager,void *queue,
    uint64_t argument3,void *argument4,void *argument5,uint64_t argument6,
    uint64_t argument7,uint64_t argument8,uint64_t argument9)
{
    uint32_t original_entries=0,requested_faces=0,admitted_faces=0;
    uint32_t admitted_entries=clamp_renderer_queue_to_face_budget(queue,
        &original_entries,&requested_faces,&admitted_faces);
    trace_queue_tail_integrity(queue,"beforeOriginal",
        &g_tail_invalid_before_renderer,original_entries,requested_faces);
    if(requested_faces>(uint32_t)InterlockedCompareExchange(
           &g_face_budget_peak_requested,0,0))
        InterlockedExchange(&g_face_budget_peak_requested,(LONG)requested_faces);
    if(original_entries>admitted_entries) {
        LONG clamp=InterlockedIncrement(&g_face_budget_clamps);
        atomic_add_long(&g_face_budget_dropped_entries,
            (LONG)(original_entries-admitted_entries));
        if(clamp<=8 || (clamp&0xFF)==0)
            append_log("STAGE_F_FACE_BUDGET_CLAMP clamp=%ld queue=%p requestedEntries=%u requestedFaces=%u admittedEntries=%u admittedFaces=%u droppedEntries=%u physicalMaps=24 schedulerB4=%ld cacheA8=%ld",
                (long)clamp,queue,original_entries,requested_faces,
                admitted_entries,admitted_faces,
                original_entries-admitted_entries,
                (long)g_observed_scheduler_b4,(long)g_observed_cache_a8);
    }
    poll_manual_capture(manager,queue,original_entries,requested_faces,
        admitted_entries,admitted_faces);
    g_original_renderer_queue(manager,queue,argument3,argument4,argument5,
        argument6,argument7,argument8,argument9);
    trace_queue_tail_integrity(queue,"afterOriginalBeforeCountRestore",
        &g_tail_invalid_after_renderer,original_entries,requested_faces);
    InterlockedIncrement(&g_renderer_calls);
    trace_renderer_capacity(queue);
    if(queue && original_entries>admitted_entries)
        *(uint32_t *)((unsigned char *)queue+RENDER_QUEUE_COUNT_OFFSET)=
            original_entries;
}

static uint32_t __fastcall hooked_face_cost_reader(void *queue)
{
    unsigned char *base=(unsigned char *)queue;
    uint32_t entries,entry_index,total=0;
    if(!base || !readable_memory(base+RENDER_QUEUE_COUNT_OFFSET,sizeof(uint32_t)))
        return 0;
    entries=*(uint32_t *)(base+RENDER_QUEUE_COUNT_OFFSET);
    if(entries>TOTAL_LOCAL_MAPS) entries=TOTAL_LOCAL_MAPS;
    for(entry_index=0;entry_index<entries;++entry_index) {
        unsigned char *entry=base+(uint64_t)entry_index*RENDER_QUEUE_ENTRY_BYTES;
        uint32_t face_count,face_index;
        if(!readable_memory(entry+0x20A0,sizeof(uint32_t))) break;
        face_count=*(uint32_t *)(entry+0x20A0);
        if(face_count>6U) face_count=6U;
        for(face_index=0;face_index<face_count;++face_index) {
            void *object;
            unsigned char *slot=entry+0x7D8+(uint64_t)face_index*0x820U;
            if(!readable_memory(slot,sizeof(void *))) break;
            object=*(void **)slot;
            if(!object || !readable_memory((unsigned char *)object+0x20,
                                           sizeof(uint32_t))) {
                InterlockedIncrement(&g_null_face_cost_skips);
                continue;
            }
            total+=*(uint32_t *)((unsigned char *)object+0x20);
        }
    }
    return total;
}

static uint32_t count_nonnull_objects(void **objects,uint32_t count)
{
    uint32_t index,nonzero=0;
    if(!objects || !count) return 0;
    if(!readable_memory(objects,(size_t)count*sizeof(void *))) return count;
    for(index=0;index<count;++index) if(objects[index]) ++nonzero;
    return nonzero;
}

static uint64_t build_null_mask(void **objects,uint32_t count)
{
    uint64_t mask=0;
    uint32_t index,limit=count<64U ? count:64U;
    if(!count) return 0;
    if(!objects) return limit==64U ? ~(uint64_t)0 :
        (((uint64_t)1U<<limit)-1U);
    if(!readable_memory(objects,(size_t)count*sizeof(void *))) return 0;
    for(index=0;index<limit;++index)
        if(!objects[index]) mask|=(uint64_t)1U<<index;
    return mask;
}
