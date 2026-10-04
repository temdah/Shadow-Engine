/* Actual compiler layout only. No native calls, game or ASI loading. */
#include "../src/modules/00_shared_config_state.inc"
#include <stddef.h>
int main(void)
{
    printf("{\"schema\":1,\"limiter_nodes\":%llu,\"candidate_records\":%llu,\"limiter_enabled\":%llu}\n",
        (unsigned long long)offsetof(VehicleLimiterChain,nodes),
        (unsigned long long)offsetof(VehicleCandidateSample,records),
        (unsigned long long)offsetof(VehicleLimiterChain,enabled));
    return 0;
}
