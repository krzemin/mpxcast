#ifndef MPXCAST_DSP_FM_MONO_H
#define MPXCAST_DSP_FM_MONO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "dsp/deemphasis.h"
#include "dsp/fir.h"
#include "dsp/fm_pipeline.h"
#include "dsp/mpx_filter_bank.h"

#define FM_MONO_AUDIO_BLOCK_SAMPLES FM_PIPELINE_AUDIO_BLOCK_SAMPLES

struct fm_mono_state {
    struct deemphasis_state deemphasis;
    struct mpx_filter_bank_state mpx_filter_bank;
    struct fir_decimator_state audio_decimator;
    float output_scale;
    float mono_mpx[FM_PIPELINE_MAX_DECIMATED_SAMPLES];
    float audio_block[FM_PIPELINE_MAX_DECIMATED_SAMPLES];
};

void fm_mono_init(struct fm_mono_state *state);
void fm_mono_configure(struct fm_mono_state *state, float volume_gain,
                       float deemphasis_tau_seconds);
void fm_mono_process_block(struct fm_mono_state *state, const float *demodulated,
                           int16_t *output_samples);

#endif
