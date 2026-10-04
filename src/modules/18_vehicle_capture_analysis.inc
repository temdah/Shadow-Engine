#include <float.h>
/* Internal capture-only analysis. No engine dereferences or policy writes.
 * Manager/renderer tickets own the live snapshots; only the frozen worker
 * reads them for formatting. Four samples, no persistent movement registry. */
static void vehicle_capture_phase(VehicleCaptureTicket ticket,uint32_t phase)
{
#if SHADOW_ENGINE_INTERNAL_DIAGNOSTICS
    LARGE_INTEGER now;
    if(!ticket.active || ticket.sample_index>=VEHICLE_CAPTURE_SAMPLES || phase>=4U)
        return;
    if(QueryPerformanceCounter(&now))
        g_shadow_engine.vehicle_diagnostics.samples[ticket.sample_index].phase_qpc[phase]=now.QuadPart;
#else
    (void)ticket; (void)phase;
#endif
}

static void vehicle_capture_renderer_phase(VehicleQueueTicket ticket,uint32_t phase)
{
#if SHADOW_ENGINE_INTERNAL_DIAGNOSTICS
    LARGE_INTEGER now;
    if(!ticket.capture.active || ticket.capture.sample_index>=VEHICLE_CAPTURE_SAMPLES ||
       phase>=2U) return;
    if(QueryPerformanceCounter(&now))
        g_shadow_engine.vehicle_diagnostics.queue_samples[ticket.capture.sample_index].native_qpc[phase]=now.QuadPart;
#else
    (void)ticket; (void)phase;
#endif
}

static double vehicle_capture_elapsed_us(LONGLONG start,LONGLONG end,LONGLONG frequency)
{
    if(start<=0 || end<start || frequency<=0) return -1.0;
    return (double)(end-start)*1000000.0/(double)frequency;
}

/* Derivative of squared distance, not metres/second or a smoothed predictor.
 * Full light handle plus owner and unique recent joins reject ordinary reuse.
 * Publication age adjusts the sample interval; missing data has no direction. */
static int vehicle_capture_rope(const VehicleCandidateRecord *before,DWORD before_tick,
    const VehicleCandidateRecord *after,DWORD after_tick,double *rate)
{
    DWORD dt;
    if(!before || !after || !rate || !before->descriptor_handle_valid ||
       !after->descriptor_handle_valid || before->descriptor_handle!=after->descriptor_handle ||
       !before->selection_vehicle || before->selection_vehicle!=after->selection_vehicle ||
       before->selection_matches!=1U || after->selection_matches!=1U ||
       before->selection_candidate_instances!=1U || after->selection_candidate_instances!=1U ||
       !before->selection_renderer_recent || !after->selection_renderer_recent ||
       before->selection_age_ms>1000U || after->selection_age_ms>1000U ||
       !(before->selection_distance_squared>=0.0f) ||
       !(after->selection_distance_squared>=0.0f) ||
       before->selection_distance_squared>FLT_MAX ||
       after->selection_distance_squared>FLT_MAX) return 0;
    dt=(after_tick-after->selection_age_ms)-(before_tick-before->selection_age_ms);
    if(dt<50U || dt>2000U) return 0;
    *rate=((double)after->selection_distance_squared-before->selection_distance_squared)*
        1000.0/(double)dt;
    return 1;
}

static const char *vehicle_capture_admission_reason(const VehicleCandidateSample *sample,
    const VehicleCandidateRecord *record)
{
    if(!sample->admission_observed) return "nativeAdmissionUnavailable";
    if(record->admitted) return "nativeAdmitted";
    if(sample->limiter_fail_open) return "limiterFailOpenNativeOmitted";
    if(!sample->limiter_enabled) return "limiterOffNativeOmitted";
    if(!record->selection_group_complete || !record->selection_renderer_recent)
        return "notEligibleForLimiterNativeOmitted";
    if(record->policy_protected) return "policyKeptNativeOmitted";
    return sample->residency_degraded ? "notKeptDuringDegradedEvidence":"policyNotKept";
}

static void log_vehicle_capture_analysis(LONG serial,LONG index,
    const VehicleCandidateSample *sample,const VehicleCandidateSample *previous)
{
    uint32_t i,j,logged=0;
    LONGLONG frequency=g_shadow_engine.bootstrap.snapshot.frequency.QuadPart;
    append_log("STAGE_VEHICLE_COST serial=%ld sample=%ld main=%u enabled=%u prepareUs=%.3f nativeManagerUs=%.3f restoreUs=%.3f candidates=%u suppressed=%u admitted=%u degraded=%u freezeMs=%lu freezeCalls=%u peakFreezeMs=%lu peakFreezeCalls=%u measuredCalls=1 clock=QPC cpuWallElapsed=1 gpuTime=0 preCaptureExcluded=1",
        (long)serial,(long)index,sample->selection_main_limit,sample->limiter_enabled,
        vehicle_capture_elapsed_us(sample->phase_qpc[0],sample->phase_qpc[1],frequency),
        vehicle_capture_elapsed_us(sample->phase_qpc[1],sample->phase_qpc[2],frequency),
        vehicle_capture_elapsed_us(sample->phase_qpc[2],sample->phase_qpc[3],frequency),
        sample->reported_candidates,sample->limiter_suppressed,sample->admitted_candidates,
        sample->residency_degraded,(unsigned long)sample->freeze_ms,sample->freeze_calls,
        (unsigned long)sample->freeze_peak_ms,sample->freeze_peak_calls);
    for(i=0;i<sample->copied_candidates && logged<32U;++i) {
        const VehicleCandidateRecord *record=&sample->records[i],*old=NULL;
        double rate=0;
        int valid=0;
        if(record->renderer_type!=3 || !record->selection_vehicle) continue;
        for(j=0;j<i;++j)
            if(sample->records[j].selection_vehicle==record->selection_vehicle &&
               sample->records[j].renderer_type==3) break;
        if(j<i) continue;
        if(previous) {
            for(j=0;j<previous->copied_candidates;++j)
                if(previous->records[j].descriptor_handle==record->descriptor_handle &&
                   previous->records[j].selection_vehicle==record->selection_vehicle) {
                    old=&previous->records[j]; break;
                }
            valid=vehicle_capture_rope(old,previous->tick,record,sample->tick,&rate);
        }
        append_log("STAGE_VEHICLE_ROPE serial=%ld sample=%ld vehicle=0x%llX handle=0x%llX distanceSquared=%.3f ageMs=%lu trendValid=%u distanceSquaredDeltaPerSecond=%.3f direction=%s reason=%s representativeLight=1 policyChange=0",
            (long)serial,(long)index,(unsigned long long)record->selection_vehicle,
            (unsigned long long)record->descriptor_handle,record->selection_distance_squared,
            (unsigned long)record->selection_age_ms,valid,rate,
            !valid?"unknown":rate< -0.01?"closing":rate>0.01?"opening":"steady",
            vehicle_capture_admission_reason(sample,record));
        ++logged;
    }
    append_log("STAGE_VEHICLE_ROPE_COVERAGE serial=%ld sample=%ld rows=%u maxRows=32 absentVehiclesNotObservable=1",
        (long)serial,(long)index,logged);
}
