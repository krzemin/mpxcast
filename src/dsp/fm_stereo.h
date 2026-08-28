#ifndef MPXCAST_DSP_FM_STEREO_H
#define MPXCAST_DSP_FM_STEREO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "dsp/deemphasis.h"
#include "dsp/fir.h"
#include "dsp/fm_pipeline.h"
#include "dsp/mpx_filter_bank.h"

#define FM_STEREO_AUDIO_BLOCK_FRAMES FM_PIPELINE_AUDIO_BLOCK_SAMPLES
#define FM_STEREO_PILOT_NOMINAL_HZ 19000.0f

struct fm_stereo_state {
    struct deemphasis_state deemphasis_left;
    struct deemphasis_state deemphasis_right;
    struct mpx_filter_bank_state mpx_filter_bank;
    struct fir_decimator_state sum_audio_decimator;
    struct fir_decimator_state diff_audio_decimator;
    float output_scale;
    float prev_pilot_sample;
    float pilot_sin_step;
    float pilot_cos_step;
    float mono_mpx[FM_PIPELINE_MAX_DECIMATED_SAMPLES];
    float pilot_mpx[FM_PIPELINE_MAX_DECIMATED_SAMPLES];
    float stereo_subchannel[FM_PIPELINE_MAX_DECIMATED_SAMPLES];
    float stereo_difference[FM_PIPELINE_MAX_DECIMATED_SAMPLES];
};

void fm_stereo_init(struct fm_stereo_state *state);
void fm_stereo_configure(struct fm_stereo_state *state, float volume_gain,
                         float deemphasis_tau_seconds);
void fm_stereo_process_block(struct fm_stereo_state *state, const float *demodulated,
                             int16_t *output_samples);

#endif
