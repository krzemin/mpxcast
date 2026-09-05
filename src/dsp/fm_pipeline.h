#ifndef MPXCAST_DSP_FM_PIPELINE_H
#define MPXCAST_DSP_FM_PIPELINE_H

#include <stdbool.h>
#include <stddef.h>

#include "dsp/fm_discriminator.h"
#include "dsp/fm_quality.h"
#include "dsp/liquid_resampler.h"
#include "dsp/rds_demod.h"
#include "input/rtl/rtl_iq_decimator.h"
#include "input/rtl/rtl_source.h"

#define FM_PIPELINE_MPX_RATE_HZ RTL_IQ_DECIMATOR_OUTPUT_RATE_HZ
_Static_assert((unsigned int)FM_PIPELINE_MPX_RATE_HZ == FM_QUALITY_SAMPLE_RATE_HZ,
               "FM quality filter coefficients require 240 kHz MPX");
#define FM_PIPELINE_AUDIO_OUTPUT_RATE_HZ 48000u
/* Invariant: 240 kHz MPX to 48 kHz audio is an exact /5 decimation. */
#define FM_PIPELINE_AUDIO_DECIMATION_FACTOR 5u
/*
 * Invariants:
 * - one live DSP block must map to one whole audio block with no FIFO buffering
 * - the resulting 5120-sample MPX block must stay within fixed front-end buffer ceilings
 */
#define FM_PIPELINE_AUDIO_BLOCK_SAMPLES 1024u
#define FM_PIPELINE_DSP_BLOCK_SAMPLES                                                              \
    (FM_PIPELINE_AUDIO_BLOCK_SAMPLES * FM_PIPELINE_AUDIO_DECIMATION_FACTOR)
#if FM_PIPELINE_DSP_BLOCK_SAMPLES != RTL_IQ_DECIMATOR_OUTPUT_BLOCK_SAMPLES
#error "FM pipeline DSP block size must match the RTL IQ decimator output block size"
#endif
#define FM_PIPELINE_MAX_DECIMATED_SAMPLES FM_PIPELINE_DSP_BLOCK_SAMPLES
#define FM_PIPELINE_MAX_DECIMATED_IQ_VALUES (FM_PIPELINE_MAX_DECIMATED_SAMPLES * 2u)
#define FM_PIPELINE_RDS_SAMPLE_RATE 171000.0f
#define FM_PIPELINE_MAX_RDS_SAMPLES RDS_MAX_INPUT_SAMPLES

struct fm_pipeline_state {
    bool rds_enabled;
    struct liquid_resampler_state rds_resampler;
    struct fm_discriminator_state discriminator;
    struct fm_quality_state quality;
    struct rtl_iq_decimator_state rtl_iq_decimator;
    struct rds_demod_state rds_demod;
    float decimated_iq[FM_PIPELINE_MAX_DECIMATED_IQ_VALUES];
    float demodulated[FM_PIPELINE_MAX_DECIMATED_SAMPLES];
    float rds_input[FM_PIPELINE_MAX_RDS_SAMPLES];
};

void fm_pipeline_init(struct fm_pipeline_state *state);
void fm_pipeline_destroy(struct fm_pipeline_state *state);
void fm_pipeline_reset(struct fm_pipeline_state *state);
void fm_pipeline_configure(struct fm_pipeline_state *state, bool rds_enabled,
                           float quality_interval_seconds);
void fm_pipeline_process_live_block(struct fm_pipeline_state *state, struct rtl_source *source);
const struct rds_station_info *fm_pipeline_get_station_info(const struct fm_pipeline_state *state);
bool fm_pipeline_has_complete_radiotext(const struct fm_pipeline_state *state);

#endif
