/* Bounded offline timing of the actual reader using the existing allocated
 * fixture. The fixture RPM wrapper adds counters/read-order hashing. Results
 * include that instrumentation, exclude module18 call-site overhead probes,
 * and are not game timings or an FPS estimate. */
#define main driver_identity_fixture_main
#include "driver_identity_harness.c"
#undef main

enum DriverCostCase {
    COST_SUCCESS, COST_SUCCESS_TRACED, COST_ON_FOOT, COST_PASSENGER,
    COST_PUBLICATION, COST_PROOF_DISABLED, COST_PAWN_CACHE_MISSING,
    COST_VEHICLE_CACHE_MISSING, COST_VEHICLE_UNREADY, COST_INVALID_REFERENCE,
    COST_CASES
};

static uint64_t cost_operation(DriverFixture *fixture,unsigned kind)
{
    DriverIdentity identity;
    DriverIdentityTrace trace;
    if(kind==COST_PUBLICATION) return copy_vehicle_entity_id(fixture->vehicle);
    if(kind==COST_SUCCESS_TRACED)
        return (uint64_t)copy_current_driver_identity_traced(&identity,&trace);
    return (uint64_t)copy_current_driver_identity(&identity);
}

int main(void)
{
    static const char *names[]={"success","success_traced","on_foot","passenger",
        "publication_id","proof_disabled","pawn_cache_missing",
        "vehicle_cache_missing","vehicle_unready","invalid_reference"};
    const unsigned warmup=400U,iterations=4000U,rounds=5U;
    DriverFixture *fixture=VirtualAlloc(NULL,sizeof(*fixture),MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    LARGE_INTEGER frequency,begin,end;
    volatile uint64_t sink=0;
    unsigned kind,round,i,j,reads_per_call;
    double times[5];
    assert(fixture && ARRAY_COUNT(names)==COST_CASES && QueryPerformanceFrequency(&frequency));
    for(kind=0;kind<ARRAY_COUNT(names);++kind) {
        DriverIdentity identity;
        DriverIdentityTrace trace;
        uint64_t expected;
        reset_fixture(fixture);
        if(kind==COST_ON_FOOT) pointer(fixture->pawn+0x90,NULL);
        if(kind==COST_PASSENGER) u64(fixture->seats+0x58,PLAYER_ID^0x100000000ULL);
        if(kind==COST_PROOF_DISABLED) g_shadow_engine.vehicle_diagnostics.driver_proof_ready=0;
        if(kind==COST_PAWN_CACHE_MISSING) pointer(fixture->player_entity+0x78,NULL);
        if(kind==COST_VEHICLE_CACHE_MISSING) pointer(fixture->vehicle_entity+0x78,NULL);
        if(kind==COST_VEHICLE_UNREADY) u32(fixture->vehicle_entity+0x60,0U);
        if(kind==COST_INVALID_REFERENCE) u64(fixture->vehicle_reference,UINT64_MAX);
        reads_per_call=reads;
        expected=cost_operation(fixture,kind);
        reads_per_call=reads-reads_per_call;
        assert(expected==(kind==COST_PUBLICATION?VEHICLE_ID:(kind<2U?1U:0U)));
        copy_current_driver_identity_traced(&identity,&trace);
        for(i=0;i<warmup;++i) sink^=cost_operation(fixture,kind);
        for(round=0;round<rounds;++round) {
            unsigned before_reads=reads;
            assert(QueryPerformanceCounter(&begin));
            for(i=0;i<iterations;++i) sink^=cost_operation(fixture,kind);
            assert(QueryPerformanceCounter(&end));
            assert(reads-before_reads==reads_per_call*iterations);
            times[round]=(end.QuadPart-begin.QuadPart)*1000000.0/(frequency.QuadPart*iterations);
        }
        for(i=1;i<rounds;++i) {
            double value=times[i];
            for(j=i;j && times[j-1U]>value;--j) times[j]=times[j-1U];
            times[j]=value;
        }
        printf("SYNTHETIC driver_cost reader=direct_reference path=%s diagnostics=%d reads_per_call=%u iterations=%u rounds=%u min_us=%.6f median_us=%.6f max_us=%.6f trace_stage=%s trace_detail=%s\n",
            names[kind],SHADOW_ENGINE_INTERNAL_DIAGNOSTICS,reads_per_call,iterations,rounds,
            times[0],times[rounds/2U],times[rounds-1U],
            driver_identity_stage_name(trace.stage),driver_identity_detail_name(trace.detail));
    }
    assert(!sink && VirtualFree(fixture,0,MEM_RELEASE));
    puts("PASS actual-reader allocated-fixture cost; instrumented real self-process RPM; no native game body, game-frame timing or FPS claim.");
    return 0;
}
