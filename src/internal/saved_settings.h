#ifndef SHADOW_ENGINE_SAVED_SETTINGS_H
#define SHADOW_ENGINE_SAVED_SETTINGS_H
/* Module17 owns this process-lifetime control-plane state. Native callbacks
 * only enqueue scalar requests; its event worker owns durable file changes. */
#define SETTINGS_CHANGE_LIMIT 1U
#define SETTINGS_CHANGE_VEHICLE 2U
#define SETTINGS_CHANGE_WORLD 4U
#define SETTINGS_CHANGE_POLICY 8U
#define SETTINGS_CHANGE_WORLD_WORD 16U
typedef struct SavedSettingsRecord {
    /* Policy retains its bounded count behind0; explicit10 has its own marker.
     * SESAVE1 active10 normalizes on read. Experimental SESAVE3 discards
     * retired background bits on read; subsequent edits write SESAVE2. */
    LONG policy,world;
    DWORD revision;
} SavedSettingsRecord;
typedef struct SavedSettingsRequest {
    unsigned mask,limit,vehicle_quality,world_quality;
    LONG policy_mask,policy_value,world_value;
    DWORD sequence;
} SavedSettingsRequest;
typedef struct SavedSettingsStatus {
    SavedSettingsRecord current;
    DWORD processed_sequence,pending_sequence,error;
    int result;
    unsigned load_reason;
} SavedSettingsStatus;
typedef struct SavedSettingsState {
    volatile LONG lock,initialized,busy,worker_ready;
    HANDLE event;
    wchar_t path[MAX_PATH],temporary[MAX_PATH];
    SavedSettingsRecord current;
    SavedSettingsRequest pending;
    DWORD processed_sequence,error;
    int result;
    unsigned load_reason;
} SavedSettingsState;
static int saved_settings_submit(const SavedSettingsRequest *request);
static int saved_settings_queue_policy(LONG mask,LONG value,DWORD sequence);
static int saved_settings_status(SavedSettingsStatus *out);
static void saved_settings_initialize(const wchar_t *directory);
#endif
