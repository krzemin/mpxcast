#ifndef MPXCAST_DSP_FM_QUALITY_H
#define MPXCAST_DSP_FM_QUALITY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define FM_QUALITY_SAMPLE_RATE_HZ 240000u

struct fm_quality_result {
    bool valid;
    double noise_db;
    double quality_percent;
};

/* Pipeline-owned; processing and reporting run on the DSP processing thread. */
struct fm_quality_state {
    void *filter;
    double report_interval_seconds; /* Zero means disabled. */
    size_t settling_samples;
    double smoothed_power;
    struct fm_quality_result result;
    bool have_reported;
    bool reported_valid;
    double last_report_time;
};

void fm_quality_init(struct fm_quality_state *state, double report_interval_seconds);
void fm_quality_destroy(struct fm_quality_state *state);
/* Preserve reporting history so repeated input failures log only one transition. */
void fm_quality_reset(struct fm_quality_state *state);
void fm_quality_process_block(struct fm_quality_state *state, const float *mpx, size_t count);
const struct fm_quality_result *fm_quality_get_result(const struct fm_quality_state *state);
void fm_quality_report(struct fm_quality_state *state, uint32_t frequency_hz);

#endif
