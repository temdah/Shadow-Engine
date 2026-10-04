#ifndef SHADOW_ENGINE_DRIVER_IDENTITY_TRACE_H
#define SHADOW_ENGINE_DRIVER_IDENTITY_TRACE_H
#include <stdint.h>

/* Optional caller-owned snapshot. No pointers are retained or dereferenced by
 * consumers; owner is only a numeric identity-comparison token. Zero IDs mean
 * that their read phase was not reached, not a native invalid-ID convention. */
typedef struct DriverIdentityTrace {
    uint32_t stage,detail;
    uint64_t player_id,vehicle_id,seat_player_id,owner;
} DriverIdentityTrace;

enum {
    DRIVER_TRACE_NONE, DRIVER_TRACE_PROOF, DRIVER_TRACE_METADATA,
    DRIVER_TRACE_PLAYER_CHAIN, DRIVER_TRACE_PLAYER_REFERENCE,
    DRIVER_TRACE_PAWN_COMPONENT, DRIVER_TRACE_PAWN_REFERENCE,
    DRIVER_TRACE_VEHICLE_REFERENCE, DRIVER_TRACE_VEHICLE_FLAGS,
    DRIVER_TRACE_VEHICLE_COMPONENT, DRIVER_TRACE_COMPONENT_REFERENCE,
    DRIVER_TRACE_DRIVER_SEAT, DRIVER_TRACE_RECHECK, DRIVER_TRACE_SUCCESS
};
enum {
    DRIVER_DETAIL_NONE, DRIVER_DETAIL_INVALID_OUTPUT, DRIVER_DETAIL_READ,
    DRIVER_DETAIL_MISSING, DRIVER_DETAIL_ID_MISMATCH, DRIVER_DETAIL_ENTITY_MISMATCH,
    DRIVER_DETAIL_MISSING_ARRAY, DRIVER_DETAIL_MISSING_CACHE,
    DRIVER_DETAIL_COUNT_BOUND, DRIVER_DETAIL_CACHE_CLASS_MISSING,
    DRIVER_DETAIL_CACHE_INDEX, DRIVER_DETAIL_COMPONENT_NULL,
    DRIVER_DETAIL_READ_OR_CHANGED, DRIVER_DETAIL_ENTITY_FLAGS,
    DRIVER_DETAIL_SEAT_NOT_FOUND, DRIVER_DETAIL_SEAT_OCCUPANT_MISMATCH,
    DRIVER_DETAIL_FULL_ID_INVALID, DRIVER_DETAIL_METADATA_MISMATCH
};

static inline const char *driver_identity_stage_name(unsigned value)
{
    static const char *const names[]={"none","proof","metadata","playerChain",
        "playerReference","pawnComponent","pawnReference","vehicleReference","vehicleFlags",
        "vehicleComponent","componentReference","driverSeat","recheck","success"};
    return value<sizeof(names)/sizeof(names[0])?names[value]:"unknown";
}
static inline const char *driver_identity_detail_name(unsigned value)
{
    static const char *const names[]={"none","invalidOutput","read","missing",
        "idMismatch","entityMismatch","missingArray","missingCache","countBound",
        "missingCachedClass","badCacheIndex","nullComponent","readOrChanged",
        "entityFlags","seatNotFound","seatOccupantMismatch","invalidFullId",
        "metadataMismatch"};
    return value<sizeof(names)/sizeof(names[0])?names[value]:"unknown";
}
#endif
