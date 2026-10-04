/* Shared policy switches, independent of presentation and diagnostic capture.
 * Bypass changes future admission/request processing, not native capacities,
 * resources already acquired, sun cascades, or external-result ownership. */
#define SHADOW_POLICY_DISABLE_LIMITER 1L
#define SHADOW_POLICY_DISABLE_QUALITY 2L
#define SHADOW_POLICY_DISABLE_ALL 3L
/* Distinguish selected10 from selected0 with a remembered count of10. */
#define SHADOW_POLICY_TEN_UNLIMITED 4L
#define SHADOW_POLICY_LIMIT_SHIFT 8U
#define SHADOW_POLICY_LIMIT_MASK (15L<<SHADOW_POLICY_LIMIT_SHIFT)
#define SHADOW_POLICY_RESOLUTION_SHIFT 16U
#define SHADOW_POLICY_RESOLUTION_MASK (7L<<SHADOW_POLICY_RESOLUTION_SHIFT)
#define SHADOW_WORLD_ENABLED 1L
#define SHADOW_WORLD_RESOLUTION_SHIFT 8U
#define SHADOW_WORLD_RESOLUTION_MASK (7L<<SHADOW_WORLD_RESOLUTION_SHIFT)
#define SHADOW_CAPTURE_ARM 1L
#define SHADOW_CAPTURE_MARK 2L
#define SHADOW_CAPTURE_FINISH 4L
#define SHADOW_CAPTURE_VEHICLE 8L
#define VEHICLE_RESIDENCY_RESET_EXTRAS 2L

static int shadow_policy_enabled(LONG bit)
{
    return !(InterlockedCompareExchange(
        &g_shadow_engine.policy_control.disabled,0,0)&bit);
}

static uint32_t shadow_policy_stable_limit(void)
{
    LONG value=InterlockedCompareExchange(&g_shadow_engine.policy_control.disabled,0,0);
    uint32_t limit=(uint32_t)((value&SHADOW_POLICY_LIMIT_MASK)>>SHADOW_POLICY_LIMIT_SHIFT);
    return limit?limit:VEHICLE_SELECTION_STABLE_VEHICLES;
}

static uint32_t shadow_policy_selected_limit_from(LONG value)
{
    uint32_t limit=(uint32_t)((value&SHADOW_POLICY_LIMIT_MASK)>>SHADOW_POLICY_LIMIT_SHIFT);
    if(value&SHADOW_POLICY_TEN_UNLIMITED) return 10U;
    if(value&SHADOW_POLICY_DISABLE_LIMITER) return 0U;
    return limit?limit:VEHICLE_SELECTION_STABLE_VEHICLES;
}

static uint32_t shadow_policy_selected_limit(void)
{
    return shadow_policy_selected_limit_from(InterlockedCompareExchange(
        &g_shadow_engine.policy_control.disabled,0,0));
}

/* Temporary internal comparison. Zero-initialized process state retains both
 * extra slots; this choice never enters the durable settings record. */
static uint32_t vehicle_selection_extra_slots(void)
{
#if SHADOW_ENGINE_INTERNAL_DIAGNOSTICS
    return InterlockedCompareExchange(
        &g_shadow_engine.vehicle_diagnostics.extra_slots_disabled,0,0)?0U:2U;
#else
    return 2U;
#endif
}

#if SHADOW_ENGINE_INTERNAL_DIAGNOSTICS
static int vehicle_selection_set_extra_slots(unsigned slots)
{
    VehicleLightDiagnosticState *d=&g_shadow_engine.vehicle_diagnostics;
    LONG disabled;
    if(slots!=0U && slots!=2U) return 0;
    disabled=slots==0U?1L:0L;
    /* The synchronous residency owner consumes this request; the tools worker
     * never clears or edits its retained vehicle slots. */
    if(InterlockedExchange(&d->extra_slots_disabled,disabled)!=disabled)
        atomic_or_long(&d->residency_reset_requested,VEHICLE_RESIDENCY_RESET_EXTRAS);
    return 1;
}
#endif

/* Zero initialization means the default2048 cap. Other valid encodings1-4
 * select512/1024/2048/4096. Consumers decode a single atomic policy snapshot. */
static uint32_t shadow_policy_resolution_from(LONG value)
{
    unsigned index=(unsigned)((value&SHADOW_POLICY_RESOLUTION_MASK)>>SHADOW_POLICY_RESOLUTION_SHIFT);
    return index>=1U && index<=4U ? (256U<<index) : VEHICLE_SHADOW_MAX_DIMENSION;
}

static uint32_t shadow_policy_resolution(void)
{
    return shadow_policy_resolution_from(InterlockedCompareExchange(
        &g_shadow_engine.policy_control.disabled,0,0));
}

static int shadow_policy_world_enabled(void)
{
    return (InterlockedCompareExchange(
        &g_shadow_engine.policy_control.world_configuration,0,0)&
        SHADOW_WORLD_ENABLED)!=0;
}

static uint32_t shadow_policy_world_resolution(void)
{
    LONG value=InterlockedCompareExchange(
        &g_shadow_engine.policy_control.world_configuration,0,0);
    unsigned index=(unsigned)((value&SHADOW_WORLD_RESOLUTION_MASK)>>
        SHADOW_WORLD_RESOLUTION_SHIFT);
    return index>=1U && index<=4U ? (256U<<index) : VEHICLE_SHADOW_MAX_DIMENSION;
}

static int shadow_policy_set_world_comparison(unsigned resolution_index,
    unsigned enabled)
{
    SavedSettingsRequest request={0};
    if(resolution_index<1U || resolution_index>4U || enabled>1U) return 0;
    request.mask=SETTINGS_CHANGE_WORLD_WORD;
    request.world_value=((LONG)resolution_index<<SHADOW_WORLD_RESOLUTION_SHIFT)|
        (enabled?SHADOW_WORLD_ENABLED:0L);
    return saved_settings_submit(&request)==0;
}

static int shadow_policy_set_disabled(LONG mask,LONG value)
{
    return saved_settings_queue_policy(mask,value,0U)==0;
}

/* Acceptance is asynchronous. Module17 saves before publishing either policy
 * word. Zero retains the last count; ten is an explicitly recorded bypass. */
static int shadow_policy_set_vehicle_limit(unsigned limit)
{
    SavedSettingsRequest request={0};
    if(limit>VEHICLE_SELECTION_MAX_STABLE_VEHICLES) return 0;
    request.mask=SETTINGS_CHANGE_LIMIT; request.limit=limit;
    return saved_settings_submit(&request)==0;
}

static int shadow_policy_set_suite_settings(unsigned limit,unsigned quality_disabled)
{
    SavedSettingsRequest request={0};
    if(limit>VEHICLE_SELECTION_MAX_STABLE_VEHICLES || quality_disabled>1U) return 0;
    request.mask=SETTINGS_CHANGE_LIMIT|SETTINGS_CHANGE_POLICY;
    request.limit=limit; request.policy_mask=SHADOW_POLICY_DISABLE_QUALITY;
    request.policy_value=quality_disabled?SHADOW_POLICY_DISABLE_QUALITY:0;
    return saved_settings_submit(&request)==0;
}

static int shadow_policy_set_comparison(unsigned limit,unsigned resolution_index,unsigned quality_disabled)
{
    SavedSettingsRequest request={0};
    if(limit>VEHICLE_SELECTION_MAX_STABLE_VEHICLES ||
       resolution_index<1U || resolution_index>4U || quality_disabled>1U) return 0;
    request.mask=SETTINGS_CHANGE_LIMIT|SETTINGS_CHANGE_POLICY;
    request.limit=limit;
    request.policy_mask=SHADOW_POLICY_DISABLE_QUALITY|SHADOW_POLICY_RESOLUTION_MASK;
    request.policy_value=(LONG)resolution_index<<SHADOW_POLICY_RESOLUTION_SHIFT;
    if(quality_disabled) request.policy_value|=SHADOW_POLICY_DISABLE_QUALITY;
    return saved_settings_submit(&request)==0;
}
