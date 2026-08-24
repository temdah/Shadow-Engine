/* Module 05: immutable runtime-profile definitions and address resolution. */

typedef struct RuntimeRvaMap {
    uint64_t supported_rva;
    uint64_t runtime_rva;
} RuntimeRvaMap;

/* Asia/Miru does not preserve the three large relocation clusters used by
 * the other protected layouts.  Every active address is therefore mapped
 * independently from the captured unpacked image and the complete table is
 * validated before the first write.  Keep this array sorted by supported RVA
 * so the resolver can use a bounded binary search. */
static const RuntimeRvaMap g_asia_rva_map[] = {
    { 0x002BFF60ULL, 0x008D9410ULL },
    { 0x002C0490ULL, 0x008D9940ULL },
    { 0x002C049EULL, 0x008D994EULL },
    { 0x002C04E2ULL, 0x008D9992ULL },
    { 0x002CBF18ULL, 0x008E2CB8ULL },
    { 0x002CC150ULL, 0x008E2EF0ULL },
    { 0x002CC1B9ULL, 0x008E2F59ULL },
    { 0x002CC1C4ULL, 0x008E2F64ULL },
    { 0x002CC1D6ULL, 0x008E2F76ULL },
    { 0x002CC1E8ULL, 0x008E2F88ULL },
    { 0x002CC1FAULL, 0x008E2F9AULL },
    { 0x002CC27FULL, 0x008E301FULL },
    { 0x002CC291ULL, 0x008E3031ULL },
    { 0x002CC2A3ULL, 0x008E3043ULL },
    { 0x002CC2C3ULL, 0x008E3063ULL },
    { 0x002CC300ULL, 0x008E30A0ULL },
    { 0x002CC31CULL, 0x008E30BCULL },
    { 0x002CC333ULL, 0x008E30D3ULL },
    { 0x002D608DULL, 0x008F0EADULL },
    { 0x002D60B2ULL, 0x008F0ED2ULL },
    { 0x002D61D1ULL, 0x008F0FF1ULL },
    { 0x002D6269ULL, 0x008F1089ULL },
    { 0x002E2236ULL, 0x008FD8C6ULL },
    { 0x002E225CULL, 0x008FD8ECULL },
    { 0x002E2262ULL, 0x008FD8F2ULL },
    { 0x002E2269ULL, 0x008FD8F9ULL },
    { 0x002E2270ULL, 0x008FD900ULL },
    { 0x002E2277ULL, 0x008FD907ULL },
    { 0x002E8E89ULL, 0x00907D59ULL },
    { 0x002E9B60ULL, 0x00908C00ULL },
    { 0x002E9C21ULL, 0x00908CC1ULL },
    { 0x002E9C3BULL, 0x00908CDBULL },
    { 0x00304F7FULL, 0x0094018FULL },
    { 0x00304FC3ULL, 0x009401D3ULL },
    { 0x00305018ULL, 0x00940228ULL },
    { 0x003050C8ULL, 0x009402D8ULL },
    { 0x00306E60ULL, 0x00940510ULL },
    { 0x00306EAEULL, 0x00940564ULL },
    { 0x00306EB9ULL, 0x0094056FULL },
    { 0x00306ECDULL, 0x00940583ULL },
    { 0x00306EEFULL, 0x009405A5ULL },
    { 0x00306EF3ULL, 0x009405A9ULL },
    { 0x00306F64ULL, 0x0094061AULL },
    { 0x00307168ULL, 0x00940818ULL },
    { 0x003071ABULL, 0x0094085BULL },
    { 0x00307285ULL, 0x00940935ULL },
    { 0x0030728CULL, 0x0094093CULL },
    { 0x003073E2ULL, 0x00940A92ULL },
    { 0x00308389ULL, 0x00941B00ULL },
    { 0x00308441ULL, 0x00941BB8ULL },
    { 0x00308A01ULL, 0x00942178ULL },
    { 0x00308A90ULL, 0x0092F4B0ULL },
    { 0x00308B53ULL, 0x0092F573ULL },
    { 0x00308DB0ULL, 0x00933780ULL },
    { 0x00309050ULL, 0x00933A20ULL },
    { 0x003090B9ULL, 0x00933A89ULL },
    { 0x0030A5F8ULL, 0x00934F98ULL },
    { 0x0030ABC0ULL, 0x00935560ULL },
    { 0x0030ACC9ULL, 0x00935669ULL },
    { 0x0030D1BDULL, 0x00937B56ULL },
    { 0x0030D222ULL, 0x00937BBBULL },
    { 0x003C1350ULL, 0x00A1D5B0ULL },
    { 0x003C6D1DULL, 0x00A2346DULL },
    { 0x003CA3E2ULL, 0x00A29E62ULL },
    { 0x003CA441ULL, 0x00A29EC1ULL },
    { 0x003CA76FULL, 0x00A2A1EFULL },
    { 0x003CA77AULL, 0x00A2A1FAULL },
    { 0x003CA980ULL, 0x00A2A400ULL },
    { 0x003DC780ULL, 0x00A53740ULL },
    { 0x003DDD9CULL, 0x00A54EF2ULL },
    { 0x003DDFC0ULL, 0x00A55138ULL },
    { 0x003DE059ULL, 0x00A551DAULL },
    { 0x003DF833ULL, 0x00A56A1BULL },
    { 0x003E072FULL, 0x00A571EFULL },
    { 0x003E07E7ULL, 0x00A572A7ULL },
    { 0x003E4420ULL, 0x00A5B330ULL },
    { 0x00427860ULL, 0x009947B0ULL }
};

typedef enum RuntimeRvaStrategy {
    RUNTIME_RVA_CLUSTERED = 0,
    RUNTIME_RVA_EXPLICIT = 1
} RuntimeRvaStrategy;

typedef struct RuntimeProfile {
    LONG id;
    const char *name;
    uint32_t image_size;
    uint32_t timestamp;
    uint32_t checksum;
    uint32_t entry_rva;
    RuntimeRvaStrategy rva_strategy;
    uint64_t cluster_a_delta;
    uint64_t cluster_b_delta;
    uint64_t cluster_c_delta;
    uint64_t string_assign_rva;
    uint64_t native_random_rva;
    const RuntimeRvaMap *rva_map;
    size_t rva_map_count;
    const unsigned char *frame_builder_prologue;
    size_t frame_builder_prologue_size;
} RuntimeProfile;

typedef struct RuntimeProfileState {
    const RuntimeProfile *selected;
    uint64_t image_size;
    uint64_t owner_vector_reserve_rva;
} RuntimeProfileState;

#define ARRAY_COUNT(value) (sizeof(value)/sizeof((value)[0]))

static const RuntimeProfile g_runtime_profiles[] = {
    { 1, "supported-04DF", 0x06595000U, 0x5CD045B4U, 0x01E5793CU,
      0x04760FA6U, RUNTIME_RVA_CLUSTERED, 0, 0, 0, STRING_ASSIGN_RVA,
      0x000B9780ULL, NULL, 0, g_frame_builder_prologue,
      sizeof(g_frame_builder_prologue) },
    { 2, "a4ee-signed-variant", 0x06595000U, 0x5CD13864U, 0x01E4FD51U,
      0x05824EE5U, RUNTIME_RVA_CLUSTERED, 0x00597690ULL, 0x005FDC60ULL,
      0x00524650ULL, 0x00136CD0ULL, 0x0011A380ULL, NULL, 0,
      g_frame_builder_prologue, sizeof(g_frame_builder_prologue) },
    { 3, "vmpless-1.06", 0x04ED6000U, 0x5BE5AA1AU, 0x048AF60CU,
      0x02F58AC8U, RUNTIME_RVA_CLUSTERED, 0x005B83F0ULL, 0x0061E710ULL,
      0x00545D80ULL, 0x0013A080ULL, 0x0011D660ULL, NULL, 0,
      g_frame_builder_prologue, sizeof(g_frame_builder_prologue) },
    { 4, "complete-edition-1.06.329", 0x041AB000U, 0x55DC1ECDU,
      0x03B84DA7U, 0x021FD388U, RUNTIME_RVA_CLUSTERED, 0x019FFDE0ULL,
      0x01A66680ULL, 0x0198C410ULL, 0x00136A50ULL, 0x0011A190ULL,
      NULL, 0, g_frame_builder_prologue, sizeof(g_frame_builder_prologue) },
    { 5, "asia-miru-1.06.329", 0x07EE1000U, 0x543589C5U, 0x02B88696U,
      0x07C88AFEU, RUNTIME_RVA_EXPLICIT, 0, 0, 0, 0x00127F30ULL,
      0x00170020ULL, g_asia_rva_map, ARRAY_COUNT(g_asia_rva_map),
      g_frame_builder_asia_prologue, sizeof(g_frame_builder_asia_prologue) }
};

static RuntimeProfileState g_runtime_state;

static const RuntimeProfile *find_runtime_profile(uint32_t image_size,
    uint32_t timestamp,uint32_t checksum,uint32_t entry_rva)
{
    size_t i;
    for(i=0;i<ARRAY_COUNT(g_runtime_profiles);i++) {
        const RuntimeProfile *profile=&g_runtime_profiles[i];
        if(profile->image_size==image_size && profile->timestamp==timestamp &&
           profile->checksum==checksum && profile->entry_rva==entry_rva)
            return profile;
    }
    return NULL;
}

static void clear_runtime_profile(void)
{
    memset(&g_runtime_state,0,sizeof(g_runtime_state));
    g_runtime_state.owner_vector_reserve_rva=OWNER_VECTOR_RESERVE_RVA;
}

static void select_runtime_profile_state(const RuntimeProfile *profile)
{
    clear_runtime_profile();
    g_runtime_state.selected=profile;
    if(profile) g_runtime_state.image_size=profile->image_size;
}

static LONG runtime_profile_id(void)
{
    return g_runtime_state.selected ? g_runtime_state.selected->id:0;
}

static uint64_t runtime_string_assign_rva(void)
{
    return g_runtime_state.selected ?
        g_runtime_state.selected->string_assign_rva:STRING_ASSIGN_RVA;
}

static uint64_t runtime_native_random_rva(void)
{
    return g_runtime_state.selected ?
        g_runtime_state.selected->native_random_rva:0x000B9780ULL;
}

static const unsigned char *runtime_frame_builder_prologue(size_t *size)
{
    const RuntimeProfile *profile=g_runtime_state.selected;
    if(!profile) {
        if(size) *size=sizeof(g_frame_builder_prologue);
        return g_frame_builder_prologue;
    }
    if(size) *size=profile->frame_builder_prologue_size;
    return profile->frame_builder_prologue;
}

static uint64_t resolve_engine_rva(uint64_t rva)
{
    const RuntimeProfile *profile=g_runtime_state.selected;
    if(!profile) return rva;
    if(profile->rva_strategy==RUNTIME_RVA_EXPLICIT) {
        size_t low=0,high=profile->rva_map_count;
        while(low<high) {
            size_t middle=low+(high-low)/2;
            uint64_t supported=profile->rva_map[middle].supported_rva;
            if(rva<supported) high=middle;
            else if(rva>supported) low=middle+1;
            else return profile->rva_map[middle].runtime_rva;
        }
        return 0;
    }
    return rva+
        (rva>=0x00420000ULL ? profile->cluster_c_delta :
        (rva>=0x003C0000ULL ? profile->cluster_b_delta :
        (rva>=0x002B0000ULL ? profile->cluster_a_delta : 0ULL)));
}

#define RESOLVED_ENGINE_RVA(rva) resolve_engine_rva((uint64_t)(rva))
#define ENGINE_ADDRESS(rva) (g_shadow_engine.bootstrap.disrupt_base+RESOLVED_ENGINE_RVA(rva))
