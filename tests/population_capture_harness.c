/* Actual-source P-A fixtures. Synthetic native data and an in-memory worker log
 * only: no hooks installed, game access, DLL loading, ETW or external process. */
#define WIN32_LEAN_AND_MEAN
#define SHADOW_ENGINE_OVERHEAD_MEASUREMENT 0
#include <windows.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <assert.h>

static unsigned fixture_checks,fixture_callback_depth,fixture_allocations,fixture_io;
static unsigned fixture_libraries,fixture_writes,fixture_flushes,fixture_native_calls;
static unsigned fixture_queries;
static DWORD fixture_tick=1000;
static int64_t fixture_counter=1000000;
static int64_t fixture_counter_step=1;
static int fixture_reject_allocation,fixture_reject_open,fixture_reject_write,fixture_reject_flush;
static int fixture_reject_close,fixture_short_write,fixture_zero_write;
static int fixture_reject_qpc;
static unsigned fixture_binary_opens,fixture_binary_writes,fixture_binary_flushes,fixture_binary_closes;
static unsigned fixture_max_binary_write;
static unsigned char *fixture_binary;
static size_t fixture_binary_used;
static const char *fixture_artifact_directory;
#define FIXTURE_BINARY_CAPACITY (128U*1024U*1024U)
static volatile LONG *fixture_contended_lock;
static char fixture_log[262144];
static size_t fixture_log_used;
#define CHECK(value) do { ++fixture_checks; if(!(value)) { \
    fprintf(stderr,"FAIL %s:%u: %s\n",__FILE__,(unsigned)__LINE__,#value); exit(1); } } while(0)

static void *fixture_calloc(size_t count,size_t bytes)
{
    CHECK(!fixture_callback_depth); ++fixture_allocations;
    if(fixture_reject_allocation) return NULL;
    return calloc(count,bytes);
}
static void *fixture_malloc(size_t bytes)
{
    CHECK(!fixture_callback_depth); ++fixture_allocations;
    if(fixture_reject_allocation) return NULL;
    return malloc(bytes);
}
static void *fixture_realloc(void *pointer,size_t bytes)
{
    CHECK(!fixture_callback_depth); ++fixture_allocations;
    if(fixture_reject_allocation) return NULL;
    return realloc(pointer,bytes);
}
static LPVOID WINAPI fixture_virtual_alloc(LPVOID address,SIZE_T size,DWORD kind,DWORD protection)
{
    CHECK(!fixture_callback_depth); ++fixture_allocations;
    if(fixture_reject_allocation) return NULL;
    return VirtualAlloc(address,size,kind,protection);
}
static DWORD WINAPI fixture_get_tick(void) { return fixture_tick; }
static BOOL WINAPI fixture_qpc(LARGE_INTEGER *counter)
{
    if(fixture_reject_qpc) return FALSE;
    counter->QuadPart=fixture_counter; fixture_counter+=fixture_counter_step; return TRUE;
}
static BOOL WINAPI fixture_qpf(LARGE_INTEGER *frequency)
{
    frequency->QuadPart=1000000; return TRUE;
}
static SIZE_T WINAPI fixture_virtual_query(LPCVOID pointer,PMEMORY_BASIC_INFORMATION info,SIZE_T size)
{
    ++fixture_queries;
    return VirtualQuery(pointer,info,size);
}
static LONG WINAPI fixture_compare_exchange(volatile LONG *target,LONG value,LONG expected)
{
    if(target==fixture_contended_lock && value==1 && expected==0) return 1;
    return InterlockedCompareExchange(target,value,expected);
}
static HMODULE WINAPI fixture_load_library(LPCWSTR path)
{
    (void)path; ++fixture_libraries;
    CHECK(!"population fixture must never load ETW or other libraries"); return NULL;
}
static HANDLE WINAPI fixture_create_file(LPCWSTR path,DWORD access,DWORD sharing,
    LPSECURITY_ATTRIBUTES security,DWORD creation,DWORD flags,HANDLE template_file)
{
    (void)access; (void)sharing; (void)security;
    (void)flags; (void)template_file;
    CHECK(!fixture_callback_depth); ++fixture_io;
    if(fixture_reject_open) { SetLastError(ERROR_ACCESS_DENIED); return INVALID_HANDLE_VALUE; }
    if(wcsstr(path,L"ShadowEnginePopulation-")) {
        CHECK(creation==CREATE_NEW);
        ++fixture_binary_opens; fixture_binary_used=0;
        return (HANDLE)(uintptr_t)0x8888U;
    }
    return (HANDLE)(uintptr_t)0x7777U;
}
static BOOL WINAPI fixture_write_file(HANDLE file,LPCVOID data,DWORD bytes,
    LPDWORD written,LPOVERLAPPED overlapped)
{
    size_t available=sizeof(fixture_log)-1U-fixture_log_used;
    (void)overlapped; CHECK(file==(HANDLE)(uintptr_t)0x7777U || file==(HANDLE)(uintptr_t)0x8888U);
    CHECK(!fixture_callback_depth); ++fixture_io; ++fixture_writes;
    if(fixture_reject_write) { *written=0; SetLastError(ERROR_WRITE_FAULT); return FALSE; }
    if(file==(HANDLE)(uintptr_t)0x8888U) {
        ++fixture_binary_writes;
        if(bytes>fixture_max_binary_write) fixture_max_binary_write=bytes;
        if(fixture_zero_write) { *written=0; return TRUE; }
        if(fixture_short_write && bytes) --bytes;
        CHECK(fixture_binary && bytes<=FIXTURE_BINARY_CAPACITY-fixture_binary_used);
        memcpy(fixture_binary+fixture_binary_used,data,bytes); fixture_binary_used+=bytes;
        *written=bytes; return TRUE;
    }
    if(bytes<available) {
        memcpy(fixture_log+fixture_log_used,data,bytes); fixture_log_used+=bytes;
        fixture_log[fixture_log_used]=0;
    }
    *written=bytes; return TRUE;
}
static BOOL WINAPI fixture_flush_file(HANDLE file)
{
    CHECK((file==(HANDLE)(uintptr_t)0x7777U || file==(HANDLE)(uintptr_t)0x8888U) && !fixture_callback_depth);
    if(file==(HANDLE)(uintptr_t)0x8888U) ++fixture_binary_flushes;
    ++fixture_io; ++fixture_flushes;
    if(fixture_reject_flush) { SetLastError(ERROR_WRITE_FAULT); return FALSE; }
    return TRUE;
}
static BOOL WINAPI fixture_close_handle(HANDLE file)
{
    if(file==(HANDLE)(uintptr_t)0x8888U) {
        CHECK(!fixture_callback_depth); ++fixture_binary_closes;
        if(fixture_reject_close) { SetLastError(ERROR_WRITE_FAULT); return FALSE; }
        return TRUE;
    }
    if(file==(HANDLE)(uintptr_t)0x7777U) return TRUE;
    return CloseHandle(file);
}
#define calloc fixture_calloc
#define malloc fixture_malloc
#define realloc fixture_realloc
#define VirtualAlloc fixture_virtual_alloc
#define GetTickCount fixture_get_tick
#define QueryPerformanceCounter fixture_qpc
#define QueryPerformanceFrequency fixture_qpf
#define VirtualQuery fixture_virtual_query
#undef InterlockedCompareExchange
#define InterlockedCompareExchange fixture_compare_exchange
#define LoadLibraryW fixture_load_library
#define CreateFileW fixture_create_file
#define WriteFile fixture_write_file
#define FlushFileBuffers fixture_flush_file
#define CloseHandle fixture_close_handle
#include "../src/shadow_engine_patch.c"
#undef calloc
#undef malloc
#undef realloc
#undef VirtualAlloc
#undef GetTickCount
#undef QueryPerformanceCounter
#undef QueryPerformanceFrequency
#undef VirtualQuery
#undef InterlockedCompareExchange
#undef LoadLibraryW
#undef CreateFileW
#undef WriteFile
#undef FlushFileBuffers
#undef CloseHandle

/* Bytes below are native fixture inputs, not copied capture implementation
 * layouts. The mapped Global evidence independently owns these offsets. */
static unsigned char fixture_manager[0xC0],fixture_configuration[0xF30];
static unsigned char fixture_candidates[4][0x80],fixture_descriptors[4][0x48];
static unsigned char fixture_spatial[4][0x58],fixture_owners[8][0x700];
static unsigned char fixture_binding_vector[16],fixture_bindings[4][0x18];
static unsigned char fixture_context_object[0x20];
static void *fixture_context_table[2],*fixture_context;
static unsigned char *fixture_image;
static size_t fixture_image_bytes;
static unsigned char fixture_queue[0x47470],fixture_backing[0x48];
static unsigned char fixture_resource_before[0x48],fixture_resource_after[0x48];

static void put_u32(void *where,uint32_t value) { memcpy(where,&value,4U); }
static void put_u64(void *where,uint64_t value) { memcpy(where,&value,8U); }
static void put_pointer(void *where,void *value) { memcpy(where,&value,sizeof(value)); }

static PopulationGateInputs gate_input(void)
{
    PopulationGateInputs input;
    memset(&input,0,sizeof(input));
    input.valid=PG_VALID_DESCRIPTOR|PG_VALID_CANDIDATE|PG_VALID_CONFIG|
        PG_VALID_CONTEXT|PG_VALID_CONSTANTS|PG_VALID_MXCSR;
    input.raw_type=3; input.enabled=1; input.type3_enabled=1;
    input.descriptor_factor_bits=0x3F800000U; input.candidate_factor_bits=0x3F800000U;
    input.epsilon_bits=0x3A83126FU; input.numerator_bits=0x3F800000U;
    input.width=2; input.height=2; input.coverage=1;
    input.threshold_bits=0x3E800000U; input.mxcsr=0x1F80U;
    return input;
}
static void test_gate_decisions(void)
{
    PopulationGateInputs input;
    uint32_t mxcsr;
    unsigned i;
    static const uint32_t unsupported_float[]={0x7F800000U,0xFF800000U,
        0x7FC00000U,0x7F800001U,0x00000001U,0x80000001U};
    CHECK(population_gate_evaluate(NULL)==PG_UNKNOWN);
    input=gate_input(); CHECK(population_gate_evaluate(&input)==PG_ORDINARY_ELIGIBLE);
    input.valid=0; CHECK(population_gate_evaluate(&input)==PG_UNKNOWN);
    input=gate_input(); input.enabled=0; input.valid=PG_VALID_DESCRIPTOR;
    input.descriptor_factor_bits=0x7F800001U;
    CHECK(population_gate_evaluate(&input)==PG_DISABLED);
    input=gate_input(); input.descriptor_factor_bits=0x3A83126EU;
    CHECK(population_gate_evaluate(&input)==PG_DESCRIPTOR_FACTOR);
    input.descriptor_factor_bits=0x3A83126FU;
    CHECK(population_gate_evaluate(&input)==PG_ORDINARY_ELIGIBLE);
    input.candidate_factor_bits=0x3A83126EU;
    CHECK(population_gate_evaluate(&input)==PG_CANDIDATE_FACTOR);
    input.candidate_factor_bits=0x3A83126FU;
    CHECK(population_gate_evaluate(&input)==PG_ORDINARY_ELIGIBLE);
    input=gate_input(); input.descriptor_factor_bits=0;
    input.valid&=~PG_VALID_CANDIDATE;
    CHECK(population_gate_evaluate(&input)==PG_DESCRIPTOR_FACTOR);
    input=gate_input(); input.raw_type=0; input.valid&=~(PG_VALID_CONFIG|PG_VALID_CONTEXT|PG_VALID_MXCSR);
    CHECK(population_gate_evaluate(&input)==PG_SPECIAL_ELIGIBLE);
    input.raw_type=2; input.descriptor_flags=0;
    CHECK(population_gate_evaluate(&input)==PG_TYPE2_FLAGS);
    input.descriptor_flags=0x80000000U; CHECK(population_gate_evaluate(&input)==PG_SPECIAL_ELIGIBLE);
    input.descriptor_flags=0x04000000U; CHECK(population_gate_evaluate(&input)==PG_SPECIAL_ELIGIBLE);
    input.descriptor_flags=0x02000000U; CHECK(population_gate_evaluate(&input)==PG_TYPE2_FLAGS);
    input.raw_type=4; CHECK(population_gate_evaluate(&input)==PG_TYPE4);
    input=gate_input(); input.type3_enabled=0; input.threshold_bits=0x7FC00000U;
    CHECK(population_gate_evaluate(&input)==PG_TYPE3_CONFIG);
    input=gate_input(); input.type3_enabled=2;
    CHECK(population_gate_evaluate(&input)==PG_ORDINARY_ELIGIBLE);
    input.raw_type=0xFFFFFFFFU; CHECK(population_gate_evaluate(&input)==PG_ORDINARY_ELIGIBLE);
    input.raw_type=5; CHECK(population_gate_evaluate(&input)==PG_ORDINARY_ELIGIBLE);
    input=gate_input(); input.threshold_bits=0x3E800001U;
    CHECK(population_gate_evaluate(&input)==PG_COVERAGE);
    input.threshold_bits=0x3E800000U; CHECK(population_gate_evaluate(&input)==PG_ORDINARY_ELIGIBLE);
    input.coverage=0; CHECK(population_gate_evaluate(&input)==PG_COVERAGE);
    input.reduced=1; input.valid&=~(PG_VALID_CONTEXT|PG_VALID_MXCSR);
    CHECK(population_gate_evaluate(&input)==PG_ORDINARY_ELIGIBLE);
    input.reduced=0; input.threshold_bits=0;
    CHECK(population_gate_evaluate(&input)==PG_ORDINARY_ELIGIBLE);
    input.threshold_bits=0xBF800000U; CHECK(population_gate_evaluate(&input)==PG_ORDINARY_ELIGIBLE);
    input=gate_input(); input.mxcsr|=0x2000U;
    CHECK(population_gate_evaluate(&input)==PG_UNKNOWN);
    input=gate_input(); input.valid&=~PG_VALID_CONTEXT;
    CHECK(population_gate_evaluate(&input)==PG_UNKNOWN);
    input=gate_input(); input.valid&=~PG_VALID_MXCSR;
    CHECK(population_gate_evaluate(&input)==PG_UNKNOWN);
    for(i=0;i<sizeof(unsupported_float)/sizeof(unsupported_float[0]);++i) {
        input=gate_input(); input.descriptor_factor_bits=unsupported_float[i];
        mxcsr=population_capture_mxcsr(); CHECK(population_gate_evaluate(&input)==PG_UNKNOWN);
        CHECK(population_capture_mxcsr()==mxcsr);
        input=gate_input(); input.candidate_factor_bits=unsupported_float[i];
        CHECK(population_gate_evaluate(&input)==PG_UNKNOWN);
        input=gate_input(); input.threshold_bits=unsupported_float[i];
        CHECK(population_gate_evaluate(&input)==PG_UNKNOWN);
    }
    /* Independent arithmetic oracles: low32 products 0,negative,2 must use
     * denominators1,1,2, and unsigned coverage must retain its high bit. */
    input=gate_input(); input.width=65536U; input.height=65536U;
    input.threshold_bits=0x3F800000U;
    CHECK(population_gate_evaluate(&input)==PG_ORDINARY_ELIGIBLE);
    input.width=0x80000000U; input.height=1;
    CHECK(population_gate_evaluate(&input)==PG_ORDINARY_ELIGIBLE);
    input.width=0x80000001U; input.height=2; input.threshold_bits=0x3F000000U;
    CHECK(population_gate_evaluate(&input)==PG_ORDINARY_ELIGIBLE);
    input=gate_input(); input.width=0x7FFFFFFFU; input.height=1;
    input.coverage=0xFFFFFFFFU; input.threshold_bits=0x40000000U;
    CHECK(population_gate_evaluate(&input)==PG_ORDINARY_ELIGIBLE);
    input.threshold_bits=0x40000001U; CHECK(population_gate_evaluate(&input)==PG_COVERAGE);
    CHECK(!strcmp(population_gate_name(PG_ORDINARY_ELIGIBLE),"ordinaryEligible"));
    CHECK(!strcmp(population_gate_name(PG_SPECIAL_ELIGIBLE),"specialEligible"));
    puts("PASS copied eligibility: predicate order, exact boundaries/types/reduced mode, low32 area arithmetic, uint32 coverage and nonfinite/MXCSR unknown");
}

/* Load a preserved PE by its section table, independently of candidate proof
 * arrays. The wrapper script verifies the immutable evidence SHA256 first. */
static void load_global_evidence(const char *path)
{
    FILE *file;
    unsigned char *raw;
    long length;
    IMAGE_DOS_HEADER *dos;
    IMAGE_NT_HEADERS64 *nt;
    IMAGE_SECTION_HEADER *sections;
    unsigned i;
    file=fopen(path,"rb"); CHECK(file!=NULL);
    CHECK(!fseek(file,0,SEEK_END)); length=ftell(file); CHECK(length>0);
    CHECK(!fseek(file,0,SEEK_SET)); raw=malloc((size_t)length); CHECK(raw!=NULL);
    CHECK(fread(raw,1,(size_t)length,file)==(size_t)length); fclose(file);
    CHECK((size_t)length>=sizeof(*dos)); dos=(IMAGE_DOS_HEADER *)raw;
    CHECK(dos->e_magic==IMAGE_DOS_SIGNATURE && dos->e_lfanew>=0);
    CHECK((size_t)dos->e_lfanew+sizeof(*nt)<=(size_t)length);
    nt=(IMAGE_NT_HEADERS64 *)(raw+dos->e_lfanew);
    CHECK(nt->Signature==IMAGE_NT_SIGNATURE && nt->OptionalHeader.Magic==0x20BU);
    fixture_image_bytes=nt->OptionalHeader.SizeOfImage;
    CHECK(fixture_image_bytes>=0x0361EE84U && fixture_image_bytes<=256U*1024U*1024U);
    fixture_image=VirtualAlloc(NULL,fixture_image_bytes,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    CHECK(fixture_image!=NULL);
    sections=(IMAGE_SECTION_HEADER *)((unsigned char *)&nt->OptionalHeader+nt->FileHeader.SizeOfOptionalHeader);
    CHECK((unsigned char *)(sections+nt->FileHeader.NumberOfSections)<=raw+length);
    for(i=0;i<nt->FileHeader.NumberOfSections;++i) {
        IMAGE_SECTION_HEADER *section=&sections[i];
        CHECK((uint64_t)section->PointerToRawData+section->SizeOfRawData<=(uint64_t)length);
        CHECK((uint64_t)section->VirtualAddress+section->SizeOfRawData<=fixture_image_bytes);
        if(section->SizeOfRawData) memcpy(fixture_image+section->VirtualAddress,
            raw+section->PointerToRawData,section->SizeOfRawData);
    }
    free(raw);
}

static void reset_fixture(void)
{
    int32_t displacement;
    PopulationCaptureState *old=population_state();
    free(old);
    memset(&g_shadow_engine,0,sizeof(g_shadow_engine));
    select_runtime_profile_state(&g_runtime_profiles[0]);
    g_shadow_engine.bootstrap.disrupt_base=fixture_image;
    memcpy(&displacement,fixture_image+0x306E60U+0x33AU,4);
    g_shadow_engine.intersection.lookup_target=fixture_image+0x306E60U+0x33EU+displacement;
    g_shadow_engine.bootstrap.session_log_state=2;
    g_shadow_engine.renderer.profile_state=2;
    g_shadow_engine.renderer.live_a8_ready_logged=1;
    g_shadow_engine.policy_control.disabled=SHADOW_POLICY_DISABLE_LIMITER;
    fixture_log_used=0; fixture_log[0]=0;
    fixture_reject_allocation=fixture_reject_open=fixture_reject_write=fixture_reject_flush=0;
    fixture_reject_close=fixture_short_write=fixture_zero_write=0;
    fixture_reject_qpc=0;
    fixture_binary_used=0;
    fixture_binary_opens=fixture_binary_writes=fixture_binary_flushes=fixture_binary_closes=0;
    fixture_max_binary_write=0;
    fixture_contended_lock=NULL; fixture_tick=1000;
    fixture_counter=1000000; fixture_counter_step=1;
    fixture_callback_depth=fixture_native_calls=0;
}
static void setup_native(unsigned count)
{
    unsigned i;
    CHECK(count<=4);
    memset(fixture_manager,0,sizeof(fixture_manager));
    memset(fixture_configuration,0,sizeof(fixture_configuration));
    memset(fixture_candidates,0,sizeof(fixture_candidates));
    memset(fixture_descriptors,0,sizeof(fixture_descriptors));
    memset(fixture_spatial,0,sizeof(fixture_spatial));
    memset(fixture_owners,0,sizeof(fixture_owners));
    memset(fixture_binding_vector,0,sizeof(fixture_binding_vector));
    memset(fixture_bindings,0,sizeof(fixture_bindings));
    put_pointer(fixture_manager+0x18,fixture_configuration);
    put_pointer(fixture_manager+0x98,fixture_owners);
    put_u32(fixture_manager+0xA0,2); put_u32(fixture_manager+0xA4,8U<<6U);
    put_u32(fixture_manager+0xA8,4); put_u32(fixture_manager+0xAC,4096);
    fixture_manager[0xB0]=1; put_u32(fixture_manager+0xB4,31);
    fixture_configuration[0xA48]=1; put_u32(fixture_configuration+0xA6C,0x3E800000U);
    put_u32(fixture_image+0x0361EE80U,0);
    fixture_context_table[0]=NULL; fixture_context_table[1]=fixture_context_object;
    fixture_context=fixture_context_table;
    put_u32(fixture_context_object+0x10,2); put_u32(fixture_context_object+0x14,2);
    for(i=0;i<count;++i) {
        put_pointer(fixture_candidates[i],i+1U<count?fixture_candidates[i+1U]:NULL);
        put_pointer(fixture_candidates[i]+8,fixture_descriptors[i]);
        put_pointer(fixture_candidates[i]+0x10,fixture_spatial[i]);
        put_u32(fixture_candidates[i]+0x68,0x3F800000U);
        put_u32(fixture_candidates[i]+0x78,1);
        put_u32(fixture_descriptors[i]+8,3); fixture_descriptors[i][0xD]=1;
        put_u32(fixture_descriptors[i]+0x34,0x3F800000U);
        put_u64(fixture_spatial[i]+0x50,((uint64_t)(i+1U)<<32)|0xAAU);
    }
    for(i=0;i<2;++i) {
        put_u32(fixture_owners[i]+0x78,0xAAU); put_u32(fixture_owners[i]+0x7C,i+8U);
        fixture_owners[i][0x80]=1; put_u64(fixture_owners[i]+0x90,0xABCD0000ULL+i);
    }
    put_pointer(fixture_binding_vector,fixture_bindings);
}
static void arm_fixture(void)
{
    CHECK(population_toggle()==1);
    CHECK(population_state() && population_state()->active && population_state()->phase==1);
}
static void next_fixture_sample(void)
{
    fixture_tick+=50U;
    fixture_counter+=50000;
}
static PopulationTicket begin_manager(unsigned count)
{
    PopulationTicket ticket;
    PopulationCaptureState *s=population_state();
    /* Independent native cases below occur in successive sample intervals.
     * Sampling boundary tests call the underlying observer directly. */
    if(s && s->manager_sequence && !s->manager_busy) next_fixture_sample();
    ++fixture_callback_depth;
    ticket=population_manager_begin(fixture_manager,count?fixture_candidates[0]:NULL,
        count,&fixture_context,NULL,10);
    --fixture_callback_depth;
    return ticket;
}
static PopulationTicket begin_queue_fixture(void *queue,uint32_t original,uint32_t admitted,
    uint32_t requested_faces,uint32_t admitted_faces,LONG call)
{
    PopulationCaptureState *s=population_state();
    if(s && s->queue_sequence && !s->queue_busy) next_fixture_sample();
    return population_queue_begin(queue,original,admitted,requested_faces,admitted_faces,call);
}
#define population_queue_begin(...) begin_queue_fixture(__VA_ARGS__)
static void prepared_manager(PopulationTicket ticket,void *head,unsigned count)
{
    VehicleLimiterChain chain;
    memset(&chain,0,sizeof(chain)); chain.head=head; chain.count=count;
    ++fixture_callback_depth; population_manager_prepared(ticket,&chain); --fixture_callback_depth;
}
static void end_manager(PopulationTicket ticket)
{
    ++fixture_callback_depth;
    population_manager_end(ticket,fixture_manager,fixture_binding_vector);
    --fixture_callback_depth;
}
static PopulationManagerPacket *manager_packet(PopulationTicket ticket)
{
    PopulationCaptureState *s=population_state();
    return &s->managers[population_packet_slot(s,(uint32_t)ticket,0)];
}
static void drain_fixture(void)
{
    unsigned polls=0;
    while(population_state()->phase==2 && polls++<10000U) population_poll();
    CHECK(population_state()->phase==0 && polls<=10000U);
}
static void set_binding(unsigned index,unsigned candidate,void *owner,unsigned update)
{
    CHECK(index<4 && candidate<4);
    put_pointer(fixture_bindings[index],fixture_candidates[candidate]);
    put_pointer(fixture_bindings[index]+8,owner); fixture_bindings[index][0x10]=(unsigned char)update;
    put_u32(fixture_binding_vector+8,index+1U);
}
static void verify_fixture_artifact(const PopulationCaptureState *s);
static void save_fixture_artifact(const char *name);
static void test_optional_proof(void)
{
    size_t i;
    unsigned char saved[14],before;
    LONG policy;
    reset_fixture(); policy=g_shadow_engine.policy_control.disabled;
    CHECK(population_profile_valid(&g_global_population_capture_profile));
    CHECK(!g_shadow_engine.vehicle_diagnostics.handle_link_enabled);
    CHECK(!g_shadow_engine.vehicle_diagnostics.resolution_enabled);
    CHECK(!population_profile_valid(NULL));
    g_shadow_engine.bootstrap.disrupt_base=NULL;
    CHECK(!population_toggle()); CHECK(!population_state());
    g_shadow_engine.bootstrap.disrupt_base=fixture_image;
    for(i=1;i<sizeof(g_runtime_profiles)/sizeof(g_runtime_profiles[0]);++i) {
        select_runtime_profile_state(&g_runtime_profiles[i]);
        CHECK(!population_toggle() && !population_state());
    }
    select_runtime_profile_state(&g_runtime_profiles[0]);
    g_runtime_state.image_size=0x2000;
    CHECK(!population_toggle()); CHECK(!population_state());
    g_runtime_state.image_size=(uint32_t)fixture_image_bytes;
    for(i=0;i<g_global_population_capture_profile.span_count;++i) {
        uint64_t rva=g_global_population_capture_profile.spans[i].rva;
        fixture_image[rva]^=0x01U;
        CHECK(!population_profile_valid(&g_global_population_capture_profile));
        fixture_image[rva]^=0x01U;
    }
    memcpy(saved,fixture_image+0x2E9B60,14);
    fixture_image[0x2E9B60]=0xFF; fixture_image[0x2E9B61]=0x25;
    memset(fixture_image+0x2E9B62,0,4); put_u64(fixture_image+0x2E9B66,0x1234567812345678ULL);
    before=fixture_image[0x2E9C21]; fixture_image[0x2E9C21]=31;
    CHECK(population_profile_valid(&g_global_population_capture_profile));
    fixture_image[0x2E9C21]=before;
    before=fixture_image[0x2E9C3B]; fixture_image[0x2E9C3B]=31;
    CHECK(population_profile_valid(&g_global_population_capture_profile));
    fixture_image[0x2E9C3B]=before; memcpy(fixture_image+0x2E9B60,saved,14);
    CHECK(g_shadow_engine.policy_control.disabled==policy);
    fixture_reject_allocation=1; CHECK(!population_toggle()); CHECK(!population_state());
    fixture_reject_allocation=0; arm_fixture();
    CHECK(g_shadow_engine.policy_control.disabled==policy);
    population_finish(); population_poll(); CHECK(population_state()->notice==4);
    puts("PASS independent Global PE proof: all spans, absent/short/corrupt rejection, existing manager detour/B4 patches, F10-independent arm and allocation failure preserve policy");
}
static void test_manager_copies_and_bindings(void)
{
    PopulationTicket ticket;
    PopulationManagerPacket *m;
    unsigned queries,io,alloc;
    uint32_t mxcsr;
    unsigned char native_candidates[sizeof(fixture_candidates)];
    reset_fixture(); setup_native(3); arm_fixture();
    memcpy(native_candidates,fixture_candidates,sizeof(native_candidates));
    mxcsr=population_capture_mxcsr(); io=fixture_io; alloc=fixture_allocations;
    ticket=begin_manager(3); CHECK(ticket!=0); m=manager_packet(ticket);
    CHECK(m->copied==3 && m->chain_complete && m->before.valid==7);
    CHECK(m->before.count==2 && m->before.capacity==8 && m->before.a8==4 && m->before.dimension==4096);
    CHECK(m->rows[0].handle==0x1000000AAULL && m->rows[1].handle==0x2000000AAULL);
    CHECK(m->rows[0].handle!=m->rows[1].handle && m->before.owners[0].identity==m->before.owners[1].identity);
    CHECK(m->rows[0].gate_result==PG_UNKNOWN && m->rows[0].gate.width==2 && m->rows[0].gate.height==2);
    prepared_manager(ticket,fixture_candidates[0],3);
    CHECK(m->keep_complete && m->rows[0].keep_known && m->rows[2].kept);
    /* Eligible rows do not imply admission: native bindings select 0 and 2,
     * update=0 is retained separately from both decisions. */
    set_binding(0,0,fixture_owners[1],0); set_binding(1,2,NULL,1);
    put_u32(fixture_owners[1]+0x7C,55); end_manager(ticket);
    CHECK(m->complete==1 && m->admission_valid && m->admitted==2);
    CHECK(m->rows[0].admitted && !m->rows[1].admitted && m->rows[2].admitted);
    CHECK(m->rows[0].cache_slot==1 && m->rows[2].cache_slot==-1 && !m->rows[0].update);
    CHECK(m->after.owners[1].age==55 && m->before.owners[1].age==9);
    CHECK(population_capture_mxcsr()==mxcsr && fixture_io==io && fixture_allocations==alloc);
    CHECK(!memcmp(native_candidates,fixture_candidates,sizeof(native_candidates)));
    queries=fixture_queries;
    memset(fixture_candidates,0xDD,sizeof(fixture_candidates));
    memset(fixture_descriptors,0xDD,sizeof(fixture_descriptors));
    memset(fixture_spatial,0xDD,sizeof(fixture_spatial)); memset(fixture_owners,0xDD,sizeof(fixture_owners));
    population_finish(); drain_fixture();
    CHECK(fixture_queries==queries); CHECK(m->rows[0].handle==0x1000000AAULL);
    CHECK(strstr(fixture_log,"sampledOnly=1")!=NULL);
    CHECK(strstr(fixture_log,"causalManagerQueueJoin=0")!=NULL);
    verify_fixture_artifact(population_state()); save_fixture_artifact("population-native-copies.bin");
    puts("PASS manager copies: exact native input/output separation, owner slots/update/age, full64 handles versus cache32 keys, no mutation/MXCSR change/allocation/I/O, worker never revisits native addresses");
}
static void test_malformed_inputs(void)
{
    PopulationCacheCopy cache;
    PopulationTicket ticket;
    PopulationManagerPacket *m;
    unsigned i;
    reset_fixture(); setup_native(2); arm_fixture();
    CHECK(!population_address((void *)(UINTPTR_MAX-3U),8,8));
    CHECK(!population_address((void *)(UINTPTR_MAX-3U),0,8));
    for(i=0;i<5;++i) {
        memset(&cache,0,sizeof(cache)); setup_native(2);
        if(i==0) put_u32(fixture_manager+0xA0,9);
        if(i==1) put_u32(fixture_manager+0xA4,1U<<6U);
        if(i==2) put_pointer(fixture_manager+0x98,NULL);
        if(i==3) put_pointer(fixture_manager+0x98,fixture_owners[0]+1);
        if(i==4) put_pointer(fixture_manager+0x98,(void *)(UINTPTR_MAX-7U));
        population_cache(&cache,fixture_manager,&g_global_population_capture_profile.layout);
        CHECK(!(cache.valid&4U));
    }
    setup_native(2); put_pointer(fixture_candidates[1],fixture_candidates[0]);
    ticket=begin_manager(3); m=manager_packet(ticket);
    CHECK(m->copied==2 && m->duplicate_candidates==1 && !m->chain_complete);
    prepared_manager(ticket,fixture_candidates[0],2); end_manager(ticket); CHECK(!m->admission_valid);
    setup_native(2); put_pointer(fixture_candidates[0],fixture_candidates[1]+1);
    ticket=begin_manager(2); m=manager_packet(ticket); CHECK(m->copied==1 && !m->chain_complete); end_manager(ticket);
    setup_native(2); put_pointer(fixture_candidates[0]+8,fixture_descriptors[0]+1);
    put_pointer(fixture_candidates[0]+0x10,fixture_spatial[0]+1);
    ticket=begin_manager(2); m=manager_packet(ticket);
    CHECK(!(m->rows[0].gate.valid&PG_VALID_DESCRIPTOR) && !m->rows[0].handle_valid); end_manager(ticket);
    for(i=0;i<6;++i) {
        setup_native(2); ticket=begin_manager(2); m=manager_packet(ticket);
        prepared_manager(ticket,fixture_candidates[0],2);
        set_binding(0,0,fixture_owners[0],1);
        if(i==0) set_binding(1,0,fixture_owners[1],1);
        if(i==1) put_pointer(fixture_bindings[0],fixture_candidates[3]);
        if(i==2) put_u32(fixture_binding_vector+8,513);
        if(i==3) put_pointer(fixture_binding_vector,fixture_bindings[0]+1);
        if(i==4) put_pointer(fixture_binding_vector,(void *)(UINTPTR_MAX-7U));
        if(i==5) { put_pointer(fixture_bindings[0]+8,fixture_owners[0]+1); }
        end_manager(ticket);
        if(i==5) CHECK(m->admission_valid && m->rows[0].cache_slot==-2);
        else CHECK(!m->admission_valid);
    }
    puts("PASS malformed copies: pointer overflow/alignment, cache bounds/capacity, cycles/short chains, unknown/duplicate/oversized bindings, separate unknown owner slot");
}
static void test_lifecycle_and_sampling(void)
{
    PopulationCaptureState *s;
    PopulationTicket first,ticket,queue_ticket;
    unsigned i,millisecond,queries,io,alloc;
    uint32_t serial;
    DWORD started;
    reset_fixture(); setup_native(0); arm_fixture(); s=population_state();
    first=begin_manager(0); CHECK(first && s->manager_busy && !s->manager_lock);
    CHECK(!begin_manager(0) && s->manager_rate_skipped==1);
    fixture_contended_lock=&s->queue_lock;
    CHECK(!population_queue_begin(NULL,0,0,0,0,1) && s->queue_skipped==1);
    fixture_contended_lock=NULL;
    population_finish(); CHECK(s->phase==2 && !s->active);
    serial=s->serial; population_poll(); CHECK(s->phase==2);
    CHECK(!population_toggle() && s->serial==serial && s->manager_busy);
    end_manager(first);
    drain_fixture(); CHECK(s->phase==0 && s->notice==4);
    arm_fixture(); CHECK(s->serial==serial+1U);
    ticket=begin_manager(0); CHECK(ticket!=first && s->manager_busy);
    end_manager(first); CHECK(s->manager_busy && !manager_packet(ticket)->complete);
    end_manager(ticket); end_manager(ticket); CHECK(!s->manager_busy);
    population_finish(); drain_fixture();

    reset_fixture(); setup_native(0); arm_fixture(); s=population_state(); started=fixture_tick;
    CHECK(sizeof(*s)<128U*1024U*1024U);
    for(i=0;i<1200U;++i) {
        fixture_tick=started+i*50U;
        fixture_counter=s->started+(int64_t)i*50000;
        ++fixture_callback_depth;
        ticket=population_manager_begin(fixture_manager,NULL,0,&fixture_context,NULL,(LONG)i+1);
        queue_ticket=(population_queue_begin)(NULL,0,0,0,0,(LONG)i+1);
        --fixture_callback_depth;
        CHECK(ticket && queue_ticket);
        CHECK(s->managers[i].sequence==i+1U && s->queues[i].sequence==i+1U);
        CHECK((uint32_t)ticket==i+1U && (uint32_t)queue_ticket==(0x80000000U|(i+1U)));
        end_manager(ticket); population_queue_end(queue_ticket,NULL);
        queries=fixture_queries; io=fixture_io; alloc=fixture_allocations;
        for(millisecond=1;millisecond<50U;++millisecond) {
            fixture_tick=started+i*50U+millisecond;
            fixture_counter=s->started+(int64_t)i*50000+(int64_t)millisecond*1000;
            ++fixture_callback_depth;
            CHECK(!population_manager_begin(fixture_manager,NULL,0,&fixture_context,NULL,0));
            CHECK(!(population_queue_begin)(NULL,0,0,0,0,0));
            --fixture_callback_depth;
        }
        CHECK(fixture_queries==queries && fixture_io==io && fixture_allocations==alloc);
        CHECK(!s->reservation_inflight);
        CHECK(s->managers[0].sequence==1U && s->managers[0].complete==1);
        CHECK(s->queues[0].sequence==1U && s->queues[0].complete==1);
    }
    CHECK(s->manager_sequence==1200U && s->queue_sequence==1200U);
    CHECK(s->manager_bucket==1199U && s->queue_bucket==1199U);
    CHECK(s->manager_rate_skipped==58800U && s->queue_rate_skipped==58800U);
    CHECK(!s->manager_skipped && !s->queue_skipped && !s->marked);
    fixture_tick=started+60000U;
    fixture_counter=s->started+60000000;
    CHECK(!population_manager_begin(fixture_manager,NULL,0,&fixture_context,NULL,0));
    CHECK(!(population_queue_begin)(NULL,0,0,0,0,0));
    population_poll(); CHECK(s->phase==2 && !s->active); drain_fixture();
    CHECK(s->notice==4U && !s->marked && population_elapsed_ms()==60000U);
    CHECK(s->managers[1199].complete==1 && s->queues[1199].complete==1);
    verify_fixture_artifact(s); save_fixture_artifact("population-full-minute.bin");
    puts("PASS whole-minute lifecycle: 1200 first/last manager and queue samples retained without Mark or overwrite, 50ms boundaries, no catch-up, duplicate-rate accounting, automatic timeout/save and bounded storage");
}
static void test_queue_copies(void)
{
    PopulationTicket ticket;
    PopulationQueuePacket *q;
    unsigned i,io,alloc;
    reset_fixture(); setup_native(1); arm_fixture();
    memset(fixture_queue,0,sizeof(fixture_queue));
    put_pointer(fixture_candidates[0]+0x58,fixture_backing);
    put_pointer(fixture_backing+0x40,fixture_resource_before);
    put_u32(fixture_resource_before+0x40,1024); put_u32(fixture_resource_before+0x44,512);
    put_u32(fixture_resource_after+0x40,4096); put_u32(fixture_resource_after+0x44,2048);
    for(i=0;i<31;++i) {
        unsigned char *entry=fixture_queue+i*0x24C0U;
        put_pointer(entry+0x10,fixture_candidates[0]);
        put_u32(entry+0x20A0,1); put_u32(entry+0x20A4,4096); put_u32(entry+0x20A8,2048);
        put_pointer(entry+0x20B0,fixture_resource_after);
    }
    io=fixture_io; alloc=fixture_allocations; ++fixture_callback_depth;
    ticket=population_queue_begin(fixture_queue,31,30,31,30,77);
    CHECK(ticket && !population_state()->queue_lock && population_state()->queue_busy);
    q=population_queue_ticket(ticket); CHECK(q && q->copied==31);
    CHECK(q->rows[0].before_width==1024 && q->rows[0].before_height==512);
    CHECK(q->rows[0].submitted && !q->rows[30].submitted);
    put_u32(fixture_queue+0x20A4,2048);
    population_queue_prepared(ticket,fixture_queue);
    CHECK(q->rows[0].native_width==4096 && q->rows[0].width==2048 && q->rows[0].policy_sampled);
    population_queue_end(ticket,fixture_queue); --fixture_callback_depth;
    CHECK(q->complete && q->rows[0].resource_width==4096 && q->rows[0].resource_height==2048);
    CHECK(!q->rows[30].resource_after && fixture_io==io && fixture_allocations==alloc);
    ticket=population_queue_begin(fixture_queue,32,32,32,32,78);
    q=population_queue_ticket(ticket); CHECK(q->copied==31 && q->original==32); population_queue_end(ticket,fixture_queue);
    put_u32(fixture_queue+0x20A0,7);
    ticket=population_queue_begin(fixture_queue,1,1,7,7,79);
    q=population_queue_ticket(ticket); CHECK(!q->rows[0].valid); population_queue_end(ticket,fixture_queue);
    puts("PASS queue copies: bounded 31 entries, admitted/suppressed distinction, before/after resource dimensions and malformed face coverage");
}
static void check_before_resource_equivalence(const void *candidate,uint32_t valid)
{
    VehicleCopyRegionCache scope={0};
    ShadowResourceSnapshot original,scoped;
    ++fixture_callback_depth;
    copy_shadow_resource_before(&original,candidate);
    population_copy_resource_before_scoped(&scoped,candidate,&scope);
    --fixture_callback_depth;
    CHECK(original.valid==valid && scoped.valid==valid);
    CHECK(!memcmp(&original,&scoped,sizeof(original)));
}
static void test_queue_copy_scopes(void)
{
    unsigned char *queue,*candidate,*spatial,*backing,*resource,*pages;
    PopulationTicket ticket;
    PopulationQueuePacket *q;
    DWORD old;
    unsigned i,queries,begin_queries,prepared_queries,end_queries,io,alloc;
    reset_fixture(); setup_native(0); arm_fixture();
    queue=VirtualAlloc(NULL,0x48000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    candidate=VirtualAlloc(NULL,4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    spatial=VirtualAlloc(NULL,4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    backing=VirtualAlloc(NULL,4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    resource=VirtualAlloc(NULL,4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    pages=VirtualAlloc(NULL,8192,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    CHECK(queue && candidate && spatial && backing && resource && pages);
    put_pointer(candidate+8,fixture_descriptors[0]); put_pointer(candidate+0x10,spatial);
    put_pointer(candidate+0x58,backing); put_pointer(backing+0x40,resource);
    put_u64(spatial+0x50,0x12345678000000AAULL);
    put_u32(resource+0x40,4096); put_u32(resource+0x44,2048);
    for(i=0;i<31;++i) {
        unsigned char *entry=queue+i*0x24C0U;
        put_pointer(entry+0x10,candidate);
        put_u32(entry+0x20A0,1); put_u32(entry+0x20A4,4096); put_u32(entry+0x20A8,2048);
        put_pointer(entry+0x20B0,resource);
    }
    io=fixture_io; alloc=fixture_allocations; queries=fixture_queries;
    ++fixture_callback_depth;
    ticket=population_queue_begin(queue,31,31,31,31,80);
    begin_queries=fixture_queries-queries; queries=fixture_queries;
    q=population_queue_ticket(ticket); CHECK(q && q->copied==31);
    for(i=0;i<31;++i) CHECK(q->rows[i].before_valid==7 && q->rows[i].handle_valid);
    population_queue_prepared(ticket,queue);
    prepared_queries=fixture_queries-queries; queries=fixture_queries;
    population_queue_end(ticket,queue);
    end_queries=fixture_queries-queries;
    --fixture_callback_depth;
    /* Five independent allocations bound six readable queries: the queue
     * dimensions are on a later page than its candidate field, and Windows
     * may return only the region beginning at the queried page. This bound
     * uses allocation facts, not the copy cache's implementation or counters. */
    CHECK(begin_queries>0 && begin_queries<=6);
    CHECK(prepared_queries==1 && end_queries==2);
    CHECK(q->complete && q->rows[30].valid==7 && q->rows[30].policy_sampled);
    CHECK(fixture_io==io && fixture_allocations==alloc);
    printf("PASS 31-entry queue query bound: begin=%u prepared=%u end=%u, no callback allocation/I/O\n",
        begin_queries,prepared_queries,end_queries);

    /* A protection change between each synchronous phase must be observed.
     * A cache retained across the native boundary would read inaccessible pages. */
    ++fixture_callback_depth; ticket=population_queue_begin(queue,1,1,1,1,81); --fixture_callback_depth;
    q=population_queue_ticket(ticket); CHECK(q && q->rows[0].before_valid==7);
    CHECK(VirtualProtect(queue,0x48000,PAGE_NOACCESS,&old)); queries=fixture_queries;
    ++fixture_callback_depth; population_queue_prepared(ticket,queue); --fixture_callback_depth;
    CHECK(fixture_queries>queries && !q->rows[0].policy_sampled);
    CHECK(VirtualProtect(queue,0x48000,PAGE_READWRITE,&old));
    CHECK(VirtualProtect(resource,4096,PAGE_NOACCESS,&old)); queries=fixture_queries;
    ++fixture_callback_depth; population_queue_end(ticket,queue); --fixture_callback_depth;
    CHECK(fixture_queries>queries && q->complete && q->rows[0].valid==3);
    CHECK(q->rows[0].resource_after==(uint64_t)(uintptr_t)resource && !q->rows[0].resource_width);

    /* The next begin must also revalidate the resource used by the previous one. */
    queries=fixture_queries;
    ++fixture_callback_depth; ticket=population_queue_begin(queue,1,1,1,1,82); --fixture_callback_depth;
    q=population_queue_ticket(ticket);
    CHECK(fixture_queries>queries && q && q->rows[0].before_valid==3 && !q->rows[0].before_width);
    CHECK(VirtualProtect(resource,4096,PAGE_READWRITE,&old));
    ++fixture_callback_depth; population_queue_prepared(ticket,queue); --fixture_callback_depth;
    CHECK(q->rows[0].policy_sampled);
    CHECK(VirtualProtect(queue,0x48000,PAGE_NOACCESS,&old)); queries=fixture_queries;
    ++fixture_callback_depth; population_queue_end(ticket,queue); --fixture_callback_depth;
    CHECK(fixture_queries>queries && q->complete && q->rows[0].valid==3 && !q->rows[0].resource_after);
    CHECK(VirtualProtect(queue,0x48000,PAGE_READWRITE,&old));

    check_before_resource_equivalence(NULL,0);
    put_pointer(candidate+0x58,NULL); check_before_resource_equivalence(candidate,1);
    put_pointer(candidate+0x58,backing); put_pointer(backing+0x40,NULL);
    check_before_resource_equivalence(candidate,3);
    put_pointer(backing+0x40,resource); check_before_resource_equivalence(candidate,7);
    /* Only the final field bytes are readable. Full prefix spans must still
     * be rejected, exactly as the original before-resource observer does. */
    put_pointer(pages+4096,backing);
    CHECK(VirtualProtect(pages,4096,PAGE_NOACCESS,&old));
    check_before_resource_equivalence(pages+4096-0x58,0);
    put_pointer(pages+4096,resource); put_pointer(candidate+0x58,pages+4096-0x40);
    check_before_resource_equivalence(candidate,1);
    put_pointer(candidate+0x58,backing); put_pointer(backing+0x40,pages+4096-0x40);
    put_u32(pages+4096,4096); put_u32(pages+4100,2048);
    check_before_resource_equivalence(candidate,3);
    CHECK(fixture_io==io && fixture_allocations==alloc);
    VirtualFree(queue,0,MEM_RELEASE); VirtualFree(candidate,0,MEM_RELEASE);
    VirtualFree(spatial,0,MEM_RELEASE); VirtualFree(backing,0,MEM_RELEASE);
    VirtualFree(resource,0,MEM_RELEASE); VirtualFree(pages,0,MEM_RELEASE);
    puts("PASS queue phase scopes: fresh begin/prepared/end permission validation, complete before-resource span equivalence, known-null and inaccessible memory remain distinct");
}
static void __fastcall fixture_original_manager(void *manager,void *candidates,uint32_t count,
    void *bindings,void *context,void *view,void *call_context)
{
    PopulationCaptureState *s=population_state();
    (void)view; (void)call_context;
    CHECK(manager==fixture_manager && candidates==fixture_candidates[0] && count==2);
    CHECK(bindings==fixture_binding_vector && context==&fixture_context);
    CHECK(!s || (!s->manager_lock && !s->queue_lock));
    CHECK(!g_shadow_engine.vehicle_diagnostics.selection_writer_active);
    ++fixture_native_calls;
    set_binding(0,1,fixture_owners[1],1); put_u32(fixture_owners[1]+0x7C,99);
}
static void test_native_hook_equivalence(void)
{
    unsigned char before_candidates[sizeof(fixture_candidates)],off_manager[sizeof(fixture_manager)];
    unsigned char off_owners[sizeof(fixture_owners)],off_bindings[sizeof(fixture_bindings)];
    unsigned pass;
    for(pass=0;pass<2;++pass) {
        reset_fixture(); setup_native(2); g_shadow_engine.hooks.original_manager=fixture_original_manager;
        memcpy(before_candidates,fixture_candidates,sizeof(before_candidates));
        if(pass) arm_fixture();
        ++fixture_callback_depth;
        hooked_shadow_manager(fixture_manager,fixture_candidates[0],2,fixture_binding_vector,&fixture_context,NULL,NULL);
        --fixture_callback_depth;
        CHECK(fixture_native_calls==1 && !memcmp(before_candidates,fixture_candidates,sizeof(before_candidates)));
        if(!pass) {
            memcpy(off_manager,fixture_manager,sizeof(off_manager));
            memcpy(off_owners,fixture_owners,sizeof(off_owners)); memcpy(off_bindings,fixture_bindings,sizeof(off_bindings));
        } else {
            CHECK(!memcmp(off_manager,fixture_manager,sizeof(off_manager)));
            CHECK(!memcmp(off_owners,fixture_owners,sizeof(off_owners)));
            CHECK(!memcmp(off_bindings,fixture_bindings,sizeof(off_bindings)));
            CHECK(population_state()->managers[0].complete && population_state()->managers[0].admission_valid);
        }
    }
    puts("PASS actual manager hook: observation off/on calls native once with identical arguments and preserves native outputs/list links");
}
static void test_identity_and_contention(void)
{
    VehicleBatchScope scope;
    VehicleLightDiagnosticState *d;
    PopulationTicket ticket;
    PopulationManagerPacket *m;
    LONG gaps,overflow,rotations;
    reset_fixture(); setup_native(1); arm_fixture(); d=&g_shadow_engine.vehicle_diagnostics;
    d->selection_enabled=1; g_shadow_engine.renderer.renderer_calls=5;
    g_shadow_engine.renderer.manager_calls=9;
    memset(&scope,0,sizeof(scope)); scope.pending_count=1;
    scope.pending[0].descriptor_handle=0x1000000AAULL;
    scope.pending[0].vehicle=0xDEAD0000ULL; scope.pending[0].vehicle_entity_id=0xFEDCBA9812345678ULL;
    scope.pending[0].observations=1; scope.pending[0].tick=fixture_tick;
    publish_vehicle_selection_batch(&scope);
    ticket=begin_manager(1); m=manager_packet(ticket);
    CHECK(m->identity_health==1 && m->rows[0].identity_valid);
    CHECK(m->rows[0].vehicle==0xDEAD0000ULL && m->rows[0].entity_id==0xFEDCBA9812345678ULL);
    end_manager(ticket);
    gaps=d->selection_gaps; overflow=d->selection_overflow; rotations=d->selection_rotations;
    fixture_contended_lock=&d->selection_writer_active;
    ticket=begin_manager(1); m=manager_packet(ticket);
    CHECK(!m->identity_health && !m->rows[0].identity_valid); end_manager(ticket);
    fixture_contended_lock=NULL;
    CHECK(d->selection_gaps==gaps && d->selection_overflow==overflow && d->selection_rotations==rotations);
    /* Identical native addresses in a later copy with another full handle do
     * not acquire the old record merely because cache32 and pointers match. */
    put_u64(fixture_spatial[0]+0x50,0x2000000AAULL);
    ticket=begin_manager(1); m=manager_packet(ticket); CHECK(!m->rows[0].identity_valid && !m->rows[0].identity_matches); end_manager(ticket);
    put_u64(fixture_spatial[0]+0x50,0x1000000AAULL);
    scope.pending[0].vehicle=0xBEEF0000ULL; publish_vehicle_selection_batch(&scope);
    ticket=begin_manager(1); m=manager_packet(ticket);
    CHECK(m->rows[0].identity_matches==2 && !m->rows[0].identity_valid); end_manager(ticket);
    puts("PASS copied identity: full entity/handle match, pointer reuse/high-bit distinction, ambiguous owners, registry contention never changes native selection counters");
}
static void fixture_set_mxcsr(uint32_t value)
{
    /* Test-only controlled FPU environment. LDMXCSR [RAX], independently
     * exercising the actual STMXCSR reader; production never writes MXCSR. */
    __asm__ volatile(".byte 0x0f,0xae,0x10" : : "a"(&value) : "memory");
}
static void test_callback_bounds_and_restoration(void)
{
    unsigned char *nodes;
    PopulationTicket ticket;
    PopulationManagerPacket *m;
    PopulationCaptureState *s;
    VehicleLimiterChain chain;
    unsigned i,queries,io,alloc;
    uint32_t saved_mxcsr;
    void *next;
    reset_fixture(); setup_native(3); arm_fixture(); s=population_state();
    saved_mxcsr=population_capture_mxcsr(); CHECK(!(saved_mxcsr&0xFFFF0000U));
    fixture_set_mxcsr(0x1F80U); CHECK(population_capture_mxcsr()==0x1F80U);
    put_u32(fixture_descriptors[0]+0x34,0x7F800001U);
    ticket=begin_manager(3); prepared_manager(ticket,fixture_candidates[0],3); end_manager(ticket);
    CHECK(population_capture_mxcsr()==0x1F80U);
    fixture_set_mxcsr(saved_mxcsr);
    setup_native(3); ticket=begin_manager(3); m=manager_packet(ticket);
    memset(&chain,0,sizeof(chain)); chain.head=fixture_candidates[0]; chain.count=2;
    chain.active=1; chain.captured=3;
    for(i=0;i<3;++i) {
        chain.nodes[i]=fixture_candidates[i];
        memcpy(&chain.original_next[i],fixture_candidates[i],8);
    }
    put_pointer(fixture_candidates[0],fixture_candidates[2]);
    ++fixture_callback_depth; population_manager_prepared(ticket,&chain);
    CHECK(m->keep_complete && m->rows[0].kept && !m->rows[1].kept && m->rows[2].kept);
    CHECK(m->rows[1].keep_known && m->limiter_active);
    restore_vehicle_limiter_chain(&chain); --fixture_callback_depth;
    for(i=0;i<3;++i) {
        memcpy(&next,fixture_candidates[i],8); CHECK(next==chain.original_next[i]);
    }
    set_binding(0,2,fixture_owners[0],1); end_manager(ticket);
    CHECK(m->admission_valid && m->rows[2].admitted && !m->rows[1].admitted);
    setup_native(3); ticket=begin_manager(3); m=manager_packet(ticket);
    put_pointer(fixture_candidates[0],fixture_candidates[2]);
    prepared_manager(ticket,fixture_candidates[0],2);
    put_pointer(fixture_candidates[0],fixture_candidates[1]);
    set_binding(0,1,fixture_owners[0],1); end_manager(ticket);
    CHECK(!m->admission_valid && m->binding_unknown==1);
    nodes=calloc(513U,0x80U); CHECK(nodes!=NULL);
    for(i=0;i<513U;++i) {
        put_pointer(nodes+i*0x80U,i<512U?nodes+(i+1U)*0x80U:NULL);
        put_pointer(nodes+i*0x80U+8U,fixture_descriptors[0]);
        put_pointer(nodes+i*0x80U+0x10U,fixture_spatial[0]);
        put_u32(nodes+i*0x80U+0x68U,0x3F800000U);
    }
    queries=fixture_queries; io=fixture_io; alloc=fixture_allocations;
    next_fixture_sample();
    ++fixture_callback_depth;
    ticket=population_manager_begin(fixture_manager,nodes,UINT32_MAX,&fixture_context,NULL,99);
    --fixture_callback_depth; m=manager_packet(ticket);
    CHECK(m->copied==512 && !m->chain_complete && m->count==UINT32_MAX);
    CHECK(fixture_queries-queries<100U && fixture_allocations==alloc && fixture_io==io);
    end_manager(ticket); free(nodes);
    /* Optional annotation changes neither sample ownership nor retention. */
    fixture_contended_lock=&s->manager_lock; population_mark(); CHECK(s->marked && s->notice==2);
    fixture_contended_lock=NULL; CHECK(s->end_tick==s->started_tick+60000U);
    population_finish(); fixture_reject_open=1; drain_fixture(); CHECK(s->notice==5);
    fixture_reject_open=0;
    puts("PASS callback bounds: 512 copied maximum with bounded region queries and zero allocation/I/O, signaling-NaN MXCSR untouched, actual filtered-chain restoration, optional annotation and log-open failure");
}
static unsigned count_log(const char *token)
{
    unsigned count=0;
    const char *at=fixture_log;
    size_t size=strlen(token);
    while((at=strstr(at,token))!=NULL) { ++count; at+=size; }
    return count;
}
static uint32_t fixture_u32(const unsigned char *p)
{
    return p[0]|((uint32_t)p[1]<<8U)|((uint32_t)p[2]<<16U)|((uint32_t)p[3]<<24U);
}
static uint64_t fixture_u64(const unsigned char *p)
{
    return fixture_u32(p)|((uint64_t)fixture_u32(p+4)<<32U);
}
static uint32_t fixture_crc32(const unsigned char *bytes,size_t size)
{
    uint32_t table[256],crc=0xFFFFFFFFU;
    for(unsigned i=0;i<256U;++i) {
        uint32_t value=i;
        for(unsigned bit=0;bit<8U;++bit) value=(value>>1U)^((value&1U)?0xEDB88320U:0U);
        table[i]=value;
    }
    for(size_t i=0;i<size;++i) crc=(crc>>8U)^table[(crc^bytes[i])&255U];
    return crc^0xFFFFFFFFU;
}
static void verify_fixture_artifact(const PopulationCaptureState *s)
{
    const unsigned char *header=fixture_binary,*footer;
    size_t cursor=192U;
    uint32_t managers=0,queues=0,last_manager=UINT32_MAX,last_queue=UINT32_MAX;
    CHECK(fixture_binary_used>=256U);
    CHECK(!memcmp(header,"SEPOP01\0",8));
    CHECK(fixture_u32(header+8)==1U && fixture_u32(header+12)==192U);
    CHECK(fixture_u32(header+16)==0x01020304U && fixture_u32(header+20)==24U);
    CHECK(fixture_u32(header+24)==576U && fixture_u32(header+28)==168U);
    CHECK(fixture_u32(header+32)==72U && fixture_u32(header+36)==112U);
    CHECK(fixture_u32(header+40)==512U && fixture_u32(header+44)==31U);
    CHECK(fixture_u32(header+48)==8U && fixture_u32(header+52)==1200U);
    CHECK(fixture_u32(header+56)==50U && fixture_u32(header+60)==60000U);
    CHECK(fixture_u32(header+68)==s->serial);
    CHECK(fixture_u32(header+112)==s->elapsed_ms);
    CHECK(fixture_u32(header+120)==s->manager_sequence && fixture_u32(header+124)==s->queue_sequence);
    while(cursor+64U<fixture_binary_used) {
        const unsigned char *record=fixture_binary+cursor,*payload=record+24U;
        uint32_t type=fixture_u32(record),bucket=fixture_u32(record+8);
        uint32_t bytes=fixture_u32(record+12),rows;
        CHECK(fixture_u32(record+4)==24U && bucket<1200U);
        CHECK(!fixture_u32(record+20));
        CHECK(bytes<=fixture_binary_used-cursor-24U-64U);
        if(type==1U) {
            const PopulationManagerPacket *m=&s->managers[bucket];
            CHECK(bytes>=576U); rows=fixture_u32(payload+28U);
            CHECK(rows<=512U && bytes==576U+rows*168U && rows==m->copied);
            CHECK(fixture_u32(record+16)==managers+1U && fixture_u32(payload+16U)==managers+1U);
            CHECK(last_manager==UINT32_MAX || bucket>last_manager);
            CHECK(!memcmp(payload,m,bytes)); last_manager=bucket; ++managers;
        } else {
            const PopulationQueuePacket *q=&s->queues[bucket];
            CHECK(type==2U && bytes>=72U); rows=fixture_u32(payload+40U);
            CHECK(rows<=31U && bytes==72U+rows*112U && rows==q->copied);
            CHECK(fixture_u32(record+16)==queues+1U && fixture_u32(payload+16U)==queues+1U);
            CHECK(last_queue==UINT32_MAX || bucket>last_queue);
            CHECK(!memcmp(payload,q,bytes)); last_queue=bucket; ++queues;
        }
        cursor+=24U+bytes;
    }
    CHECK(cursor+64U==fixture_binary_used); footer=fixture_binary+cursor;
    CHECK(!memcmp(footer,"SEPOEND\0",8));
    CHECK(fixture_u32(footer+8)==1U && fixture_u32(footer+12)==64U);
    CHECK(fixture_u32(footer+16)==managers+queues && fixture_u32(footer+20)==managers && fixture_u32(footer+24)==queues);
    CHECK(managers==s->manager_sequence && queues==s->queue_sequence);
    CHECK(fixture_u64(footer+32)==cursor && fixture_u64(footer+40)==fixture_binary_used);
    CHECK(fixture_u32(footer+48)==fixture_u32(header+76) && !fixture_u64(footer+56));
    CHECK(fixture_u32(footer+28)==fixture_crc32(fixture_binary,cursor));
}
static void save_fixture_artifact(const char *name)
{
    char path[1024]; FILE *file;
    int length;
    if(!fixture_artifact_directory) return;
    length=_snprintf(path,sizeof(path),"%s/%s",fixture_artifact_directory,name);
    CHECK(length>0 && (size_t)length<sizeof(path));
    file=fopen(path,"wb"); CHECK(file);
    CHECK(fwrite(fixture_binary,1,fixture_binary_used,file)==fixture_binary_used);
    CHECK(!fclose(file));
}
static void test_artifact_capacity_and_faults(void)
{
    PopulationCaptureState *s;
    unsigned i,j,before;
    size_t before_bytes;
    CHECK(fixture_crc32((const unsigned char *)"123456789",9U)==0xCBF43926U);
    reset_fixture(); setup_native(0); arm_fixture(); s=population_state();
    /* Worst-case frozen observation payload, independently constructed from
     * the public binary contract. Native copying is exercised separately. */
    for(i=0;i<1200U;++i) {
        PopulationManagerPacket *m=&s->managers[i]; PopulationQueuePacket *q=&s->queues[i];
        m->complete=1; m->ticket=((uint64_t)s->serial<<32)|(i+1U);
        m->sequence=i+1U; m->count=m->copied=512U; m->chain_complete=1;
        m->prepared=m->keep_complete=m->admission_valid=1; m->kept_count=512U;
        m->begin=s->started+(int64_t)i*50000; m->end=m->begin+10;
        for(j=0;j<512U;++j) {
            PopulationCandidateCopy *r=&m->rows[j];
            r->handle=((uint64_t)i<<32)|j; r->entity_id=UINT64_MAX-j;
            r->handle_valid=1; r->gate=gate_input(); r->gate.coverage=i+j;
            r->keep_known=r->kept=1;
            r->gate.candidate_factor_bits=0x7F800001U+j; /* Raw NaN payloads survive unchanged. */
            r->cache_slot=-2; r->gate_result=PG_UNKNOWN;
        }
        q->complete=1; q->ticket=((uint64_t)s->serial<<32)|0x80000000ULL|(i+1U);
        q->sequence=i+1U; q->original=q->admitted=q->copied=31U;
        q->requested_faces=q->admitted_faces=31U; q->begin=m->begin+20; q->end=q->begin+10;
        for(j=0;j<31U;++j) {
            q->rows[j].handle=((uint64_t)i<<32)|j; q->rows[j].handle_valid=1;
            q->rows[j].valid=7; q->rows[j].submitted=1; q->rows[j].faces=1;
            q->rows[j].policy_sampled=1;
            q->rows[j].native_width=4096; q->rows[j].width=512;
        }
    }
    s->manager_sequence=s->queue_sequence=1200U;
    s->manager_bucket=s->queue_bucket=1199U;
    s->manager_attempted=s->queue_attempted=1200U;
    fixture_counter=s->started+60000000; fixture_counter_step=25000; population_poll();
    CHECK(s->phase==2 && !s->active && s->export_records==1U);
    before=s->export_records; before_bytes=fixture_binary_used; fixture_counter_step=1;
    population_poll(); CHECK(s->phase==2 && s->export_records-before<=32U);
    CHECK(fixture_binary_used-before_bytes<=1024U*1024U);
    drain_fixture(); CHECK(s->notice==4U && !s->report_partial);
    CHECK(fixture_binary_used==108220800U+256U);
    CHECK(fixture_max_binary_write<=86592U && fixture_binary_opens==1U);
    CHECK(fixture_binary_flushes==1U && fixture_binary_closes==1U);
    verify_fixture_artifact(s); save_fixture_artifact("population-maximum.bin");
    CHECK(!count_log("STAGE_POPULATION_CANDIDATE serial="));
    for(i=0;i<6U;++i) {
        reset_fixture(); setup_native(0); arm_fixture(); s=population_state();
        end_manager(begin_manager(0)); population_finish();
        if(i==0) fixture_reject_open=1;
        if(i==1) fixture_reject_write=1;
        if(i==2) fixture_short_write=1;
        if(i==3) fixture_zero_write=1;
        if(i==4) fixture_reject_flush=1;
        if(i==5) fixture_reject_close=1;
        drain_fixture(); CHECK(s->notice==5U);
        if(i==5) {
            unsigned close_attempts=fixture_binary_closes;
            CHECK(!population_toggle() && s->notice==5U);
            CHECK(fixture_binary_closes>close_attempts);
            fixture_reject_close=0; CHECK(population_toggle()==1);
            population_finish(); drain_fixture(); CHECK(s->notice==4U);
        }
    }
    puts("PASS binary artifact: 1200x512 manager plus1200x31 queue rows, >16MiB exact raw bits/validity, independent CRC32, bounded worker writes, open/false/short/zero-write/flush/close failure and owned-handle retry");
}
static void test_sampling_gaps_and_page_boundary(void)
{
    PopulationCaptureState *s;
    PopulationTicket ticket,stale;
    unsigned char *pages;
    DWORD old;
    DWORD deadline;
    reset_fixture(); setup_native(0); fixture_tick=0xFFFFFFF0U; arm_fixture(); s=population_state();
    deadline=s->end_tick;
    ticket=(population_queue_begin)(NULL,0,0,0,0,1); CHECK(ticket!=0);
    population_queue_end(ticket,NULL); stale=ticket;
    fixture_counter=s->started+49000; fixture_tick+=49U;
    CHECK(!(population_queue_begin)(NULL,0,0,0,0,1));
    ticket=population_manager_begin(fixture_manager,NULL,0,&fixture_context,NULL,1);
    CHECK(ticket && s->manager_sequence==1U); end_manager(ticket);
    fixture_counter=s->started+50000; fixture_contended_lock=&s->queue_lock;
    CHECK(!(population_queue_begin)(NULL,0,0,0,0,1)); CHECK(s->queue_skipped==1);
    fixture_contended_lock=NULL;
    ticket=(population_queue_begin)(NULL,0,0,0,0,1);
    CHECK(ticket && s->queue_sequence==2U && s->queue_bucket==1U);
    population_queue_end(stale,NULL); CHECK(s->queue_busy);
    fixture_counter=s->started+100000; fixture_tick+=51U;
    CHECK(!(population_queue_begin)(NULL,0,0,0,0,1)); CHECK(s->queue_skipped==2);
    population_queue_end(ticket,NULL);
    ticket=(population_queue_begin)(NULL,0,0,0,0,1);
    CHECK(ticket && s->queue_sequence==3U && s->queue_bucket==2U);
    population_queue_end(ticket,NULL);
    population_mark();
    CHECK(s->marked && s->active && s->end_tick==deadline);
    fixture_counter=s->started+500000;
    ticket=(population_queue_begin)(NULL,0,0,0,0,1);
    CHECK(ticket && s->queue_sequence==4U && s->queue_bucket==10U);
    population_queue_end(ticket,NULL);
    CHECK(!s->queues[3].complete && s->queues[0].sequence==1U);
    fixture_counter=s->started+550000;
    population_finish(); drain_fixture();
    CHECK(population_elapsed_ms()==550U && s->expected_bins==11U);
    CHECK(fixture_u32(fixture_binary+152)==10U && fixture_u32(fixture_binary+156)==7U);
    verify_fixture_artifact(s);

    reset_fixture(); setup_native(0); arm_fixture(); s=population_state();
    /* Fractional milliseconds still cover the newly entered sample bucket. */
    fixture_counter=s->started+50001;
    population_finish(); drain_fixture();
    CHECK(population_elapsed_ms()==50U && s->expected_bins==2U);
    CHECK(fixture_u32(fixture_binary+152)==2U && fixture_u32(fixture_binary+156)==2U);
    pages=VirtualAlloc(NULL,8192,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE); CHECK(pages!=NULL);
    CHECK(VirtualProtect(pages+4096,4096,PAGE_NOACCESS,&old));
    setup_native(0); arm_fixture();
    /* A node with a readable link/descriptor but inaccessible spatial pointer
     * cannot become a complete candidate row. */
    put_pointer(pages+4096-16,NULL); put_pointer(pages+4096-8,fixture_descriptors[0]);
    ++fixture_callback_depth;
    ticket=population_manager_begin(fixture_manager,pages+4096-16,1,&fixture_context,NULL,1);
    --fixture_callback_depth;
    CHECK(!manager_packet(ticket)->copied && !manager_packet(ticket)->chain_complete);
    end_manager(ticket); VirtualFree(pages,0,MEM_RELEASE);
    puts("PASS sample gaps and memory boundary: independent streams, exact50ms buckets, QPC timing across tick wrap, contention/busy retry, stale queue tickets, optional annotation, sparse/partial-bin coverage and unreadable node exclusion");
}
static void check_population_controls_isolated(LONG policy)
{
    CHECK(!g_shadow_engine.internal_tools.capture_requests && !g_shadow_engine.detail_window);
    CHECK(!g_shadow_engine.workload && !g_shadow_engine.overhead);
    CHECK(!g_shadow_engine.vehicle_diagnostics.state && !g_shadow_engine.intersection.state);
    CHECK(!g_shadow_engine.intersection.requests && !g_shadow_engine.intersection.capture);
    CHECK(g_shadow_engine.policy_control.disabled==policy);
}
static void test_deadline_drain_and_clock_errors(void)
{
    PopulationCaptureState *s;
    PopulationTicket manager,queue;
    unsigned writes;
    reset_fixture(); setup_native(0); fixture_reject_qpc=1;
    CHECK(!population_toggle() && !population_state()); fixture_reject_qpc=0;
    arm_fixture(); s=population_state();
    manager=begin_manager(0); queue=(population_queue_begin)(NULL,0,0,0,0,1);
    CHECK(manager && queue); writes=fixture_binary_writes;
    fixture_counter=s->started+60000000; fixture_tick=s->end_tick;
    population_poll(); CHECK(s->phase==2 && !s->active);
    CHECK(!fixture_binary_opens && fixture_binary_writes==writes);
    CHECK(!population_toggle() && s->manager_busy && s->queue_busy);
    end_manager(manager); population_poll();
    CHECK(!fixture_binary_opens && s->queue_busy && s->phase==2);
    population_queue_end(queue,NULL);
    CHECK(!s->manager_busy && !s->queue_busy);
    s->reservation_inflight=1; population_poll();
    CHECK(s->phase==2 && !fixture_binary_opens && s->reservation_inflight==1);
    s->reservation_inflight=0; drain_fixture();
    CHECK(s->notice==4U && population_elapsed_ms()==60000U && s->expected_bins==1200U);
    fixture_counter+=90000000; CHECK(population_elapsed_ms()==60000U);
    verify_fixture_artifact(s);
    {
        uint32_t serial=s->serial;
        s->reservation_inflight=1;
        CHECK(!population_toggle() && s->serial==serial && s->reservation_inflight==1);
        s->reservation_inflight=0;
        CHECK(population_toggle()==1 && s->serial==serial+1U);
        population_finish(); drain_fixture();
    }

    reset_fixture(); setup_native(0); arm_fixture(); s=population_state();
    fixture_counter=s->started+500001;
    manager=population_manager_begin(fixture_manager,NULL,0,&fixture_context,NULL,1);
    CHECK(manager && s->manager_bucket==10U); end_manager(manager);
    fixture_counter=s->started+250001;
    CHECK(!population_manager_begin(fixture_manager,NULL,0,&fixture_context,NULL,1));
    CHECK(s->clock_failures && s->report_partial);
    fixture_reject_qpc=1;
    CHECK(!(population_queue_begin)(NULL,0,0,0,0,1));
    fixture_tick=s->end_tick; population_poll(); drain_fixture();
    CHECK(s->notice==4U && s->stop_reason==1U && s->elapsed_ms==60000U);
    CHECK(s->expected_bins==1200U && s->report_partial && (fixture_u32(fixture_binary+76U)&4U));
    CHECK(fixture_u32(fixture_binary+152U)==1199U && fixture_u32(fixture_binary+156U)==1200U);
    fixture_reject_qpc=0; verify_fixture_artifact(s);
    save_fixture_artifact("population-clock-anomaly.bin");
    puts("PASS deadline/clock safety: timeout waits for both native tickets and reservation pins, pinned rearm rejects, QPC start failure rejects, regressing/failed clocks disclose anomalies and tick fallback still saves a bounded pass");
}
static void test_population_controls(void)
{
    PopulationCaptureState *s;
    DetailWindowState *detail;
    uint32_t serial;
    DWORD marked;
    LONG policy;
    unsigned action;
    reset_fixture(); setup_native(0);
    /* Legacy black-world actions retain their own capture owners. */
    for(action=7;action<=9;++action) {
        CHECK(internal_tool_dispatch(action,action)==1);
        CHECK(!population_state());
    }
    CHECK(g_shadow_engine.internal_tools.capture_requests==
        (SHADOW_CAPTURE_ARM|SHADOW_CAPTURE_MARK|SHADOW_CAPTURE_FINISH));
    detail=detail_state(); CHECK(detail!=NULL);
    TlsFree(detail->producer_tls); free(detail); g_shadow_engine.detail_window=NULL;
    reset_fixture(); setup_native(0); policy=g_shadow_engine.policy_control.disabled;
    CHECK(internal_tool_dispatch(15,10)==-1 && internal_tool_dispatch(16,11)==-1);
    CHECK(!population_state()); check_population_controls_isolated(policy);
    CHECK(internal_tool_dispatch(14,12)==1);
    s=population_state(); CHECK(s && s->active && s->phase==1 && !s->marked);
    serial=s->serial; check_population_controls_isolated(policy);
    CHECK(internal_tool_dispatch(14,13)==-1);
    CHECK(s->active && s->phase==1 && s->serial==serial && !s->marked);
    CHECK(internal_tool_dispatch(15,14)==1);
    CHECK(s->marked && s->active); marked=s->marked_tick;
    fixture_tick+=100;
    CHECK(internal_tool_dispatch(15,15)==-1 && s->marked_tick==marked);
    check_population_controls_isolated(policy);
    CHECK(internal_tool_dispatch(16,16)==1);
    CHECK(s->phase==2 && !s->active && s->serial==serial);
    for(action=14;action<=16;++action) CHECK(internal_tool_dispatch(action,20+action)==-1);
    CHECK(s->phase==2 && !s->active && s->serial==serial);
    check_population_controls_isolated(policy);
    drain_fixture(); CHECK(s->notice==4);
    CHECK(internal_tool_dispatch(15,40)==-1 && internal_tool_dispatch(16,41)==-1);
    CHECK(internal_tool_dispatch(14,42)==1 && s->active && s->serial==serial+1U && !s->marked);
    check_population_controls_isolated(policy);
    /* Old actions must also leave an already armed onset capture intact. */
    serial=s->serial;
    for(action=7;action<=9;++action) {
        CHECK(internal_tool_dispatch(action,50+action)==1);
        CHECK(s->phase==1 && s->active && !s->marked && s->serial==serial);
    }
    detail=detail_state(); CHECK(detail!=NULL);
    TlsFree(detail->producer_tls); free(detail); g_shadow_engine.detail_window=NULL;
    population_finish(); drain_fixture();
    puts("PASS independent onset controls: legacy 7/8/9 leave population untouched; 14 start, 15 single mark and 16 stop enforce active/saving states without arming black-world, H3, intersection or workload captures");
}

int main(int argc,char **argv)
{
    CHECK(argc==2 || argc==3);
    fixture_artifact_directory=argc==3?argv[2]:NULL;
    fixture_binary=malloc(FIXTURE_BINARY_CAPACITY); CHECK(fixture_binary);
    load_global_evidence(argv[1]);
    test_gate_decisions();
    test_optional_proof();
    test_manager_copies_and_bindings();
    test_malformed_inputs();
    test_lifecycle_and_sampling();
    test_queue_copies();
    test_queue_copy_scopes();
    test_native_hook_equivalence();
    test_identity_and_contention();
    test_callback_bounds_and_restoration();
    test_artifact_capacity_and_faults();
    test_sampling_gaps_and_page_boundary();
    test_deadline_drain_and_clock_errors();
    test_population_controls();
    CHECK(!fixture_libraries);
    free(population_state()); g_shadow_engine.population=NULL;
    VirtualFree(fixture_image,0,MEM_RELEASE);
    free(fixture_binary);
    printf("LAYOUT populationStateBytes=%llu managerPrefix=%u candidateBytes=%u queuePrefix=%u queueRowBytes=%u packetsPerStream=%u candidatesPerPacket=%u\n",
        (unsigned long long)sizeof(PopulationCaptureState),(unsigned)offsetof(PopulationManagerPacket,rows),
        (unsigned)sizeof(PopulationCandidateCopy),(unsigned)offsetof(PopulationQueuePacket,rows),
        (unsigned)sizeof(PopulationQueueCopy),POPULATION_PACKETS,POPULATION_CANDIDATES);
    printf("PASS population capture checks=%u\n",fixture_checks);
    return 0;
}
