/* Actual unity source on synthetic VirtualAlloc memory. No game, DLL entry
 * point, hooks, or worker threads are started. Frozen reference bodies below
 * are from the preserved v2.0.69 module30 source, renamed only. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <assert.h>

static uintptr_t observed_queue_begin[2],observed_queue_end[2];
static unsigned queue_queries,object_queries;
static int query_override;
static MEMORY_BASIC_INFORMATION overridden_region;

static SIZE_T WINAPI renderer_test_query(LPCVOID address,
    PMEMORY_BASIC_INFORMATION info,SIZE_T length)
{
    uintptr_t value=(uintptr_t)address;
    unsigned i;
    for(i=0;i<2;++i)
        if(value>=observed_queue_begin[i] && value<observed_queue_end[i]) break;
    if(i<2) ++queue_queries;
    else ++object_queries;
    if(query_override==1) return 0;
    if(query_override==2) {
        assert(length>=sizeof(overridden_region));
        *info=overridden_region;
        return sizeof(*info);
    }
    return VirtualQuery(address,info,length);
}
#define VirtualQuery renderer_test_query
#include "../src/shadow_engine_patch.c"
#undef VirtualQuery

/* Frozen v2.0.69 behavioral oracle: do not update to mirror the candidate. */
static uint32_t reference_clamp_renderer_queue_to_face_budget(void *queue,
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

static uint32_t __fastcall reference_hooked_face_cost_reader(void *queue)
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
                InterlockedIncrement(&g_shadow_engine.renderer.null_face_cost_skips);
                continue;
            }
            total+=*(uint32_t *)((unsigned char *)object+0x20);
        }
    }
    return total;
}

typedef struct ClampResult {
    uint32_t entries,original,requested,admitted;
    unsigned queries;
} ClampResult;

static unsigned char *queues[2],*cost_objects,*invalid_object;
static SIZE_T queue_allocation;
static DWORD page_bytes;
static unsigned comparison_cases;
static uint32_t random_state=0x70451U;

static uint32_t next_random(void)
{
    random_state=random_state*1664525U+1013904223U;
    return random_state;
}

static void reset_query_counts(void)
{ queue_queries=0; object_queries=0; }

static void set_queue_count(unsigned char *queue,uint32_t count)
{ memcpy(queue+RENDER_QUEUE_COUNT_OFFSET,&count,sizeof(count)); }

static void set_face_count(unsigned char *queue,uint32_t index,uint32_t count)
{ memcpy(queue+(size_t)index*RENDER_QUEUE_ENTRY_BYTES+0x20A0,&count,sizeof(count)); }

static void reset_queues(uint32_t entries,uint32_t faces)
{
    uint32_t i,f;
    memset(queues[0],0,queue_allocation);
    for(i=0;i<TOTAL_LOCAL_MAPS;++i) {
        unsigned char *entry=queues[0]+(size_t)i*RENDER_QUEUE_ENTRY_BYTES;
        for(f=0;f<6;++f) {
            void *object=cost_objects+64U*((i*6U+f)%32U);
            memcpy(entry+0x7D8+(size_t)f*0x820U,&object,sizeof(object));
        }
    }
    for(i=0;i<PHYSICAL_QUEUE_ENTRIES;++i) set_face_count(queues[0],i,faces);
    set_queue_count(queues[0],entries);
    memcpy(queues[1],queues[0],queue_allocation);
}

static ClampResult run_clamp(unsigned char *queue,int reference,int with_totals)
{
    ClampResult r={0,UINT32_MAX,UINT32_MAX,UINT32_MAX,0};
    reset_query_counts();
    if(reference)
        r.entries=reference_clamp_renderer_queue_to_face_budget(queue,&r.original,
            with_totals?&r.requested:NULL,with_totals?&r.admitted:NULL);
    else
        r.entries=clamp_renderer_queue_to_face_budget(queue,&r.original,
            with_totals?&r.requested:NULL,with_totals?&r.admitted:NULL);
    r.queries=queue_queries;
    assert(object_queries==0);
    return r;
}

static void compare_clamp(int with_totals,int readable_whole_queue)
{
    ClampResult before=run_clamp(queues[0],1,with_totals);
    ClampResult after=run_clamp(queues[1],0,with_totals);
    assert(before.entries==after.entries && before.original==after.original);
    assert(before.requested==after.requested && before.admitted==after.admitted);
    if(readable_whole_queue) assert(memcmp(queues[0],queues[1],queue_allocation)==0);
    ++comparison_cases;
}

static void compare_face_reader(int readable_whole_queue)
{
    uint32_t before,after;
    unsigned before_objects;
    LONG before_skips;
    g_shadow_engine.renderer.null_face_cost_skips=0;
    reset_query_counts();
    before=reference_hooked_face_cost_reader(queues[0]);
    before_objects=object_queries;
    before_skips=g_shadow_engine.renderer.null_face_cost_skips;
    g_shadow_engine.renderer.null_face_cost_skips=0;
    reset_query_counts();
    after=hooked_face_cost_reader(queues[1]);
    assert(before==after && before_objects==object_queries);
    assert(before_skips==g_shadow_engine.renderer.null_face_cost_skips);
    if(readable_whole_queue) assert(memcmp(queues[0],queues[1],queue_allocation)==0);
    ++comparison_cases;
}

static void test_normal_and_malformed_queues(void)
{
    uint32_t entries,faces,trial,i,f;
    for(entries=0;entries<=33;++entries) {
        for(faces=0;faces<=8;++faces) {
            reset_queues(entries,faces);
            compare_face_reader(1);
            compare_clamp(1,1);
            reset_queues(entries,faces);
            compare_clamp(0,1);
        }
    }
    for(trial=0;trial<600;++trial) {
        reset_queues(trial%41U,0);
        for(i=0;i<TOTAL_LOCAL_MAPS;++i) {
            unsigned char *entry=queues[0]+(size_t)i*RENDER_QUEUE_ENTRY_BYTES;
            for(f=0;f<6;++f) {
                uint32_t choice=next_random()%7U;
                void *object=choice==0?NULL:choice==1?invalid_object:
                    cost_objects+64U*(next_random()%32U);
                memcpy(entry+0x7D8+(size_t)f*0x820U,&object,sizeof(object));
            }
        }
        for(i=0;i<PHYSICAL_QUEUE_ENTRIES;++i)
            set_face_count(queues[0],i,next_random()%9U);
        if(trial%11U==0) set_queue_count(queues[0],UINT32_MAX);
        memcpy(queues[1],queues[0],queue_allocation);
        compare_face_reader(1);
        compare_clamp(trial%2U,1);
    }
    {
        uint32_t original=7,requested=7,admitted=7;
        reset_query_counts();
        assert(clamp_renderer_queue_to_face_budget(NULL,&original,&requested,&admitted)==0);
        assert(original==0 && requested==0 && admitted==0);
        assert(hooked_face_cost_reader(NULL)==0 && queue_queries==0 && object_queries==0);
    }
    puts("PASS frozen v2.0.69 equivalence: normal/malformed counts, mixed faces, null/unreadable objects, overflow totals and exact queue bytes");
}

static void protect_queue_pages(size_t offset,DWORD protection)
{
    DWORD old;
    unsigned i;
    for(i=0;i<2;++i)
        assert(VirtualProtect(queues[i]+offset,page_bytes,protection,&old));
}

static void test_unreadable_queue_fields(void)
{
    const size_t offsets[]={0U,0x2000U,0x24000U,0x47000U};
    const DWORD protections[]={PAGE_NOACCESS,PAGE_READWRITE|PAGE_GUARD};
    unsigned i,j;
    for(i=0;i<ARRAY_COUNT(offsets);++i) {
        for(j=0;j<ARRAY_COUNT(protections);++j) {
            reset_queues(31,1);
            protect_queue_pages(offsets[i],protections[j]);
            compare_face_reader(0);
            compare_clamp(1,0);
            protect_queue_pages(offsets[i],PAGE_READWRITE);
            assert(memcmp(queues[0],queues[1],queue_allocation)==0);
        }
    }
    /* An entry-face field and a face-pointer slot can individually straddle a
     * region boundary. Keep the readable count at its shifted native offset. */
    for(i=0;i<2;++i) {
        unsigned char *saved[2]={queues[0],queues[1]};
        size_t field=i?0x7D8U:0x20A0U;
        size_t shift=(page_bytes-(field%page_bytes)-2U)%page_bytes;
        size_t boundary=((shift+field)/page_bytes+1U)*page_bytes;
        reset_queues(0,0);
        for(j=0;j<2;++j) {
            queues[j]=saved[j]+shift;
            set_queue_count(queues[j],1);
            set_face_count(queues[j],0,1);
            if(i) {
                void *object=cost_objects;
                memcpy(queues[j]+field,&object,sizeof(object));
            }
        }
        /* Both sides readable, but one complete read still must fit inside
         * one VirtualQuery region, exactly as in the original validator. */
        {
            DWORD old;
            for(j=0;j<2;++j)
                assert(VirtualProtect(saved[j]+boundary,page_bytes,PAGE_READONLY,&old));
        }
        compare_face_reader(0);
        compare_clamp(1,0);
        for(j=0;j<2;++j) queues[j]=saved[j];
        protect_queue_pages(boundary,PAGE_READWRITE);
        assert(memcmp(queues[0],queues[1],queue_allocation)==0);
    }
    puts("PASS queue guard/noaccess and straddling fields preserve rejection, skips, prefix mutation and totals");
}

static void test_scope_boundaries(void)
{
    unsigned char *region=VirtualAlloc(NULL,4U*page_bytes,MEM_RESERVE,PAGE_NOACCESS);
    RendererQueueReadScope scope={0};
    DWORD old;
    assert(region && VirtualAlloc(region,2U*page_bytes,MEM_COMMIT,PAGE_READWRITE));
    reset_query_counts();
    assert(!renderer_queue_readable(&scope,NULL,4));
    assert(!renderer_queue_readable(&scope,region,0));
    assert(!renderer_queue_readable(&scope,(void *)(UINTPTR_MAX-1U),4));
    assert(object_queries==0 && queue_queries==0);
    assert(renderer_queue_readable(&scope,region+16,4));
    assert(renderer_queue_readable(&scope,region+32,8));
    assert(object_queries==1);
    assert(!renderer_queue_readable(&scope,region+2U*page_bytes,4));
    assert(!renderer_queue_readable(&scope,region+2U*page_bytes-2U,4));
    assert(scope.begin<scope.end);
    assert(VirtualProtect(region+page_bytes,page_bytes,PAGE_READWRITE|PAGE_GUARD,&old));
    memset(&scope,0,sizeof(scope));
    assert(renderer_queue_readable(&scope,region+16,4));
    assert(!renderer_queue_readable(&scope,region+page_bytes,4));
    assert(!renderer_queue_readable(&scope,region+page_bytes-2U,4));
    assert(VirtualProtect(region+page_bytes,page_bytes,PAGE_NOACCESS,&old));
    memset(&scope,0,sizeof(scope));
    assert(!renderer_queue_readable(&scope,region+page_bytes,4));
    assert(VirtualProtect(region+page_bytes,page_bytes,PAGE_READONLY,&old));
    memset(&scope,0,sizeof(scope));
    assert(renderer_queue_readable(&scope,region+page_bytes,4));
    assert(!renderer_queue_readable(&scope,region+page_bytes-2U,4));
    /* VirtualQuery metadata is an API trust boundary: synthetic malformed
     * output must never seed a reusable successful range. */
    query_override=1;
    memset(&scope,0,sizeof(scope));
    assert(!renderer_queue_readable(&scope,(void *)0x1000U,4));
    query_override=2;
    memset(&overridden_region,0,sizeof(overridden_region));
    overridden_region.State=MEM_COMMIT; overridden_region.Protect=PAGE_READWRITE;
    overridden_region.BaseAddress=(void *)(UINTPTR_MAX-10U);
    overridden_region.RegionSize=20;
    assert(!renderer_queue_readable(&scope,(void *)(UINTPTR_MAX-5U),4));
    overridden_region.BaseAddress=(void *)0x2000U; overridden_region.RegionSize=0x1000;
    assert(!renderer_queue_readable(&scope,(void *)0x1000U,4));
    overridden_region.BaseAddress=(void *)0x1000U; overridden_region.RegionSize=2;
    assert(!renderer_queue_readable(&scope,(void *)0x1000U,4));
    assert(scope.begin==0 && scope.end==0);
    query_override=0;
    assert(VirtualFree(region,0,MEM_RELEASE));
    puts("PASS scoped validation: null/zero/wrap, uncommitted pages, exact regions, guard/noaccess, failed query and malformed metadata");
}

static void test_query_reduction_and_fresh_calls(void)
{
    ClampResult before,after;
    unsigned old_queries,new_queries,old_objects;
    uint32_t old_total,new_total;
    DWORD old;
    reset_queues(30,1);
    before=run_clamp(queues[0],1,1); after=run_clamp(queues[1],0,1);
    assert(before.queries>after.queries && after.queries<=2U);
    printf("PASS clamp queue VirtualQuery calls: %u -> %u; same 30 entries / 30 faces\n",before.queries,after.queries);
    reset_query_counts(); old_total=reference_hooked_face_cost_reader(queues[0]);
    old_queries=queue_queries; old_objects=object_queries;
    reset_query_counts(); new_total=hooked_face_cost_reader(queues[1]);
    new_queries=queue_queries;
    assert(old_total==new_total && old_queries>new_queries && new_queries<=3U);
    assert(old_objects==object_queries && old_objects==30U);
    printf("PASS face-reader queue VirtualQuery calls: %u -> %u; object checks retained: %u -> %u\n",
        old_queries,new_queries,old_objects,object_queries);
    /* Each actual callback must rebuild its local scope. Its previous success
     * must not survive protection changes between independent invocations. */
    assert(VirtualProtect(queues[1]+0x2000U,page_bytes,PAGE_NOACCESS,&old));
    after=run_clamp(queues[1],0,1);
    assert(after.entries==0 && after.requested==0 && after.admitted==0);
    assert(VirtualProtect(queues[1]+0x2000U,page_bytes,PAGE_READWRITE,&old));
    reset_queues(1,1);
    assert(hooked_face_cost_reader(queues[1])>0);
    assert(VirtualProtect(queues[1],page_bytes,PAGE_NOACCESS,&old));
    reset_query_counts();
    assert(hooked_face_cost_reader(queues[1])==0 && queue_queries>0 && object_queries==0);
    assert(VirtualProtect(queues[1],page_bytes,PAGE_READWRITE,&old));
    /* Cost-object protection is independently rechecked even while the queue
     * itself remains unchanged/readable. */
    assert(VirtualProtect(cost_objects,page_bytes,PAGE_NOACCESS,&old));
    g_shadow_engine.renderer.null_face_cost_skips=0;
    reset_query_counts();
    assert(hooked_face_cost_reader(queues[1])==0 && object_queries==1);
    assert(g_shadow_engine.renderer.null_face_cost_skips==1);
    assert(VirtualProtect(cost_objects,page_bytes,PAGE_READWRITE,&old));
    puts("PASS fresh callback validation and independent cost-object protection checks");
}

static unsigned fake_renderer_calls;
static uint32_t fake_expected_entries,fake_expected_cost;
static void __fastcall fake_renderer(void *manager,void *queue,uint64_t a3,
    void *a4,void *a5,uint64_t a6,uint64_t a7,uint64_t a8,uint64_t a9)
{
    (void)manager; (void)a3; (void)a4; (void)a5; (void)a6;
    (void)a7; (void)a8; (void)a9;
    assert(*(uint32_t *)((unsigned char *)queue+RENDER_QUEUE_COUNT_OFFSET)==fake_expected_entries);
    assert(hooked_face_cost_reader(queue)==fake_expected_cost);
    ++fake_renderer_calls;
}

static void test_renderer_count_restoration(void)
{
    ClampResult before;
    uint32_t modes[]={1U,6U,7U};
    unsigned i;
    for(i=0;i<ARRAY_COUNT(modes);++i) {
        uint32_t original;
        reset_queues(31,modes[i]);
        original=*(uint32_t *)(queues[0]+RENDER_QUEUE_COUNT_OFFSET);
        before=run_clamp(queues[0],1,1);
        fake_expected_entries=before.entries;
        fake_expected_cost=reference_hooked_face_cost_reader(queues[0]);
        if(original>before.entries) set_queue_count(queues[0],original);
        memset(&g_shadow_engine,0,sizeof(g_shadow_engine));
        g_shadow_engine.hooks.original_renderer_queue=fake_renderer;
        g_shadow_engine.renderer.last_residency_probe_tick=GetTickCount();
        hooked_renderer_queue(NULL,queues[1],0,NULL,NULL,0,0,0,0);
        assert(*(uint32_t *)(queues[1]+RENDER_QUEUE_COUNT_OFFSET)==original);
        assert(memcmp(queues[0],queues[1],queue_allocation)==0);
        assert(g_shadow_engine.renderer.renderer_calls==1);
#if SHADOW_ENGINE_INTERNAL_DIAGNOSTICS
        assert(g_shadow_engine.renderer.face_budget_clamps==1);
        assert(g_shadow_engine.renderer.face_budget_dropped_entries==(LONG)(original-before.entries));
#endif
    }
    assert(fake_renderer_calls==ARRAY_COUNT(modes));
    puts("PASS actual renderer wrapper: admitted native prefix, nested face reader, full count restoration and exact final queue bytes");
}

int main(void)
{
    SYSTEM_INFO info;
    unsigned i;
    GetSystemInfo(&info); page_bytes=info.dwPageSize;
    assert(page_bytes==4096U);
    queue_allocation=(RENDER_QUEUE_ALLOCATION_BYTES+2U*page_bytes-1U)&~(SIZE_T)(page_bytes-1U);
    for(i=0;i<2;++i) {
        queues[i]=VirtualAlloc(NULL,queue_allocation,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
        assert(queues[i]);
        observed_queue_begin[i]=(uintptr_t)queues[i];
        observed_queue_end[i]=(uintptr_t)queues[i]+queue_allocation;
    }
    cost_objects=VirtualAlloc(NULL,page_bytes,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    invalid_object=VirtualAlloc(NULL,page_bytes,MEM_RESERVE|MEM_COMMIT,PAGE_NOACCESS);
    assert(cost_objects && invalid_object);
    for(i=0;i<32;++i) *(uint32_t *)(cost_objects+64U*i+0x20)=i%7U==0?UINT32_MAX:17U+i;
    test_normal_and_malformed_queues();
    test_unreadable_queue_fields();
    test_scope_boundaries();
    test_query_reduction_and_fresh_calls();
    test_renderer_count_restoration();
    printf("PASS %u frozen-reference comparisons; synthetic validation only, runtime/FPS acceptance remains manual\n",comparison_cases);
    for(i=0;i<2;++i) assert(VirtualFree(queues[i],0,MEM_RELEASE));
    assert(VirtualFree(cost_objects,0,MEM_RELEASE));
    assert(VirtualFree(invalid_object,0,MEM_RELEASE));
    return 0;
}
