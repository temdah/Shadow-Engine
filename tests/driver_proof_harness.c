/* Exercise the actual production startup proof against private allocated bytes.
 * No game module is loaded, no hook is installed, and no fixture code is called.
 * External module/export discovery is counted and must remain unused.
 * readable_memory/VirtualQuery and initialization are production functions. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>

static unsigned checks,initializations,span_faults,external_queries;
static HMODULE WINAPI fixture_module(LPCSTR name)
{
    (void)name; ++external_queries; return NULL;
}
static FARPROC WINAPI fixture_export(HMODULE module,LPCSTR name)
{
    (void)module; (void)name; ++external_queries; return NULL;
}

#define GetModuleHandleA fixture_module
#define GetProcAddress fixture_export
#include "../src/shadow_engine_patch.c"
#undef GetModuleHandleA
#undef GetProcAddress

#define REQUIRE(value) do { assert(value); ++checks; } while(0)
static DriverIdentityProfile proof;
static RuntimeProfile runtime;
static DriverProofSpan spans[8];
static unsigned char *image;
static uintptr_t dummy_player_root;

static void expect_ready(int expected)
{
    /* Every rejection clears earlier success; success must also replace zero. */
    g_shadow_engine.vehicle_diagnostics.driver_proof_ready=expected?0:1;
    initialize_vehicle_driver_identity();
    ++initializations;
    REQUIRE(g_shadow_engine.vehicle_diagnostics.driver_proof_ready==expected);
}

static void setup_fixture(void)
{
    size_t i,total=0;
    proof=g_global_driver_identity_profile;
    runtime=g_runtime_profiles[0];
    REQUIRE(proof.span_count==ARRAY_COUNT(spans));
    memcpy(spans,proof.spans,sizeof(spans));
    proof.spans=spans;
    runtime.driver_identity=&proof;
    image=VirtualAlloc(NULL,runtime.image_size,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    REQUIRE(image!=NULL);
    for(i=0;i<proof.span_count;++i) {
        REQUIRE(spans[i].rva<=runtime.image_size-spans[i].size);
        memcpy(image+spans[i].rva,spans[i].bytes,spans[i].size);
        total+=spans[i].size;
    }
    REQUIRE(total==893U);
    g_runtime_state.selected=&runtime;
    g_runtime_state.image_size=runtime.image_size;
    g_shadow_engine.bootstrap.disrupt_base=image;
    /* The independently validated player-anchor proof is an input here. Its
     * current root value and all mutable metadata deliberately remain zero. */
    g_shadow_engine.vehicle_diagnostics.player_root_address=(unsigned char *)&dummy_player_root;
    g_shadow_engine.bootstrap.session_log_state=0;
    REQUIRE(*(uint32_t *)(image+proof.pawn_class_id_rva)==0U);
    REQUIRE(*(uint32_t *)(image+proof.vehicle_class_id_rva)==0U);
    expect_ready(1);
}

static void test_all_signature_bytes(void)
{
    size_t i,j;
    for(i=0;i<proof.span_count;++i) {
        for(j=0;j<spans[i].size;++j) {
            image[spans[i].rva+j]^=1U;
            expect_ready(0);
            image[spans[i].rva+j]^=1U;
            expect_ready(1);
            ++span_faults;
        }
    }
    REQUIRE(span_faults==893U);
}

static void test_bounds_and_inputs(void)
{
    uint64_t *metadata_rvas[]={&proof.pawn_class_id_rva,&proof.vehicle_class_id_rva};
    uint64_t saved;
    size_t i,saved_size;
    const unsigned char *saved_bytes;
    for(i=0;i<proof.span_count;++i) {
        saved=spans[i].rva;
        spans[i].rva=runtime.image_size-spans[i].size+1U;
        expect_ready(0);
        spans[i].rva=saved; expect_ready(1);
    }
    for(i=0;i<ARRAY_COUNT(metadata_rvas);++i) {
        saved=*metadata_rvas[i];
        *metadata_rvas[i]=runtime.image_size-3U; expect_ready(0);
        *metadata_rvas[i]=UINT64_MAX-1U; expect_ready(0);
        *metadata_rvas[i]=saved; expect_ready(1);
    }
    saved=spans[0].rva;
    spans[0].rva=UINT64_MAX-1U; expect_ready(0);
    spans[0].rva=saved; expect_ready(1);
    saved_size=spans[0].size;
    spans[0].size=0U; expect_ready(0);
    spans[0].size=(size_t)runtime.image_size+1U; expect_ready(0);
    spans[0].size=saved_size; expect_ready(1);
    saved_bytes=spans[0].bytes;
    spans[0].bytes=NULL; expect_ready(0);
    spans[0].bytes=saved_bytes; expect_ready(1);
    proof.span_count=0U; expect_ready(0);
    proof.span_count=ARRAY_COUNT(spans); expect_ready(1);
    proof.spans=NULL; expect_ready(0);
    proof.spans=spans; expect_ready(1);
    g_runtime_state.image_size=spans[0].size-1U; expect_ready(0);
    g_runtime_state.image_size=runtime.image_size; expect_ready(1);
    g_shadow_engine.bootstrap.disrupt_base=NULL; expect_ready(0);
    g_shadow_engine.bootstrap.disrupt_base=image; expect_ready(1);
    g_runtime_state.selected=NULL; expect_ready(0);
    g_runtime_state.selected=&runtime; expect_ready(1);
    runtime.driver_identity=NULL; expect_ready(0);
    runtime.driver_identity=&proof; expect_ready(1);
    g_shadow_engine.vehicle_diagnostics.player_root_address=NULL; expect_ready(0);
    g_shadow_engine.vehicle_diagnostics.player_root_address=(unsigned char *)&dummy_player_root;
    expect_ready(1);
    for(i=1U;i<ARRAY_COUNT(g_runtime_profiles);++i) {
        REQUIRE(g_runtime_profiles[i].driver_identity==NULL);
        g_runtime_state.selected=&g_runtime_profiles[i]; expect_ready(0);
    }
    g_runtime_state.selected=&runtime; expect_ready(1);
}

static void test_actual_unreadable_pages(void)
{
    DWORD old_protect,ignored;
    const uint64_t metadata_rvas[]={proof.pawn_class_id_rva,proof.vehicle_class_id_rva};
    unsigned char *span_address=image+spans[0].rva;
    size_t i;
    REQUIRE(VirtualProtect(span_address,spans[0].size,PAGE_NOACCESS,&old_protect));
    expect_ready(0);
    REQUIRE(VirtualProtect(span_address,spans[0].size,old_protect,&ignored));
    expect_ready(1);
    for(i=0;i<ARRAY_COUNT(metadata_rvas);++i) {
        REQUIRE(VirtualProtect(image+metadata_rvas[i],4U,PAGE_NOACCESS,&old_protect));
        expect_ready(0);
        REQUIRE(VirtualProtect(image+metadata_rvas[i],4U,old_protect,&ignored));
        expect_ready(1);
    }
}

int main(void)
{
    setup_fixture();
    test_all_signature_bytes();
    test_bounds_and_inputs();
    test_actual_unreadable_pages();
    REQUIRE(external_queries==0U);
    REQUIRE(g_shadow_engine.bootstrap.session_log_state==0);
    REQUIRE(VirtualFree(image,0,MEM_RELEASE));
    printf("driver startup proof: %u checks, %u initializations, %u span-byte faults with recovery, externalQueries=%u; diagnostics=%d PASS\n",
        checks,initializations,span_faults,external_queries,SHADOW_ENGINE_INTERNAL_DIAGNOSTICS);
    return 0;
}
