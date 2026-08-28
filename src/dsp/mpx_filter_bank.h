#ifndef MPXCAST_DSP_MPX_FILTER_BANK_H
#define MPXCAST_DSP_MPX_FILTER_BANK_H

#include <stdbool.h>
#include <stddef.h>

#include "dsp/fir.h"

#define MPX_FILTER_BANK_TAP_COUNT 90u
#define MPX_FILTER_BANK_MONO_CUTOFF_HZ 16000.0f
#define MPX_FILTER_BANK_PILOT_LOW_HZ 18000.0f
#define MPX_FILTER_BANK_PILOT_HIGH_HZ 20000.0f
#define MPX_FILTER_BANK_STEREO_LOW_HZ 21000.0f
#define MPX_FILTER_BANK_STEREO_HIGH_HZ 55000.0f

struct mpx_filter_bank_state {
    float configured_sample_rate;
    bool stereo_enabled;
    struct fir_state mono_filter;
    struct fir_state pilot_filter;
    struct fir_state stereo_filter;
};

void mpx_filter_bank_init(struct mpx_filter_bank_state *state);
void mpx_filter_bank_design_lowpass_taps(float sample_rate, float cutoff_hz, float *taps,
                                         size_t tap_count);
void mpx_filter_bank_configure(struct mpx_filter_bank_state *state, float sample_rate,
                               bool stereo_enabled);
void mpx_filter_bank_process_block(struct mpx_filter_bank_state *state, const float *input,
                                   size_t sample_count, float *mono_output, float *pilot_output,
                                   float *stereo_output);

#endif
