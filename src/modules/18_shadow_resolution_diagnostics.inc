/* Bounded CPU resource provenance. The capture subsystem owns storage and the
 * nonwaiting ticket. These helpers own no registry, native resource or policy.
 * Existing renderer+2D2 proof covers candidate+58 -> backing+40. */
#define SHADOW_RESOURCE_BACKING_READ 1U
#define SHADOW_RESOURCE_POINTER_READ 2U
#define SHADOW_RESOURCE_DIMENSIONS_READ 4U

static void copy_shadow_resource_before(ShadowResourceSnapshot *out,
    const void *candidate)
{
    const unsigned char *source=(const unsigned char *)candidate;
    memset(out,0,sizeof(*out));
    if(!source || !readable_memory(source,SHADOW_CANDIDATE_BACKING_OFFSET+sizeof(void *)))
        return;
    memcpy(&out->backing,source+SHADOW_CANDIDATE_BACKING_OFFSET,sizeof(void *));
    out->valid=SHADOW_RESOURCE_BACKING_READ;
    if(!out->backing || !readable_memory(out->backing,
       SHADOW_BACKING_RESOURCE_OFFSET+sizeof(void *))) return;
    memcpy(&out->resource,(const unsigned char *)out->backing+
        SHADOW_BACKING_RESOURCE_OFFSET,sizeof(void *));
    out->valid|=SHADOW_RESOURCE_POINTER_READ;
    if(!out->resource || !readable_memory(out->resource,
       VEHICLE_RESOURCE_HEIGHT_OFFSET+sizeof(uint32_t))) return;
    memcpy(&out->width,(const unsigned char *)out->resource+
        VEHICLE_RESOURCE_WIDTH_OFFSET,sizeof(uint32_t));
    memcpy(&out->height,(const unsigned char *)out->resource+
        VEHICLE_RESOURCE_HEIGHT_OFFSET,sizeof(uint32_t));
    out->valid|=SHADOW_RESOURCE_DIMENSIONS_READ;
}

/* Pure comparisons on copied values. A matching address does not prove an
 * allocation survived or that native work completed on the GPU. */
static const char *shadow_resource_transition(const VehicleQueueRecord *record)
{
    const ShadowResourceSnapshot *before=&record->resource_before;
    if(!record->submitted) return "notSubmitted";
    if(!(before->valid&SHADOW_RESOURCE_BACKING_READ)) return "beforeUnavailable";
    if(!(record->valid&8U)) return "afterUnavailable";
    if(!before->backing) return "noBackingBefore";
    if(!(before->valid&SHADOW_RESOURCE_POINTER_READ)) return "beforeUnavailable";
    if(!before->resource) return "noResourceBefore";
    return before->resource==record->resource ? "sameAddress":"differentAddress";
}

static void log_shadow_resource_transition(LONG serial,LONG sample_index,
    const VehicleQueueRecord *record)
{
    const ShadowResourceSnapshot *before=&record->resource_before;
    append_log("STAGE_SHADOW_RESOURCE_TRANSITION serial=%ld sample=%ld entry=%u beforeValid=0x%X backingBefore=%p resourceBefore=%p widthBefore=%u heightBefore=%u resourceAfter=%p widthAfter=%u heightAfter=%u transition=%s beforeRequestMismatch=%u metadataOnly=1 allocationGenerationKnown=0",
        (long)serial,(long)sample_index,record->entry_index,before->valid,
        before->backing,before->resource,before->width,before->height,
        record->resource,record->resource_width,record->resource_height,
        shadow_resource_transition(record),
        record->policy_sampled && (before->valid&SHADOW_RESOURCE_DIMENSIONS_READ) &&
            (before->width!=record->width || before->height!=record->height));
}
