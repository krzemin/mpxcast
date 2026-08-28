#include "dsp/fm_pipeline.h"

#include <string.h>

static void fm_pipeline_destroy(struct fm_pipeline_state *state);

void fm_pipeline_init(struct fm_pipeline_state *state) {
    memset(state, 0, sizeof(*state));
    liquid_resampler_init(&state->rds_resampler);
    fm_discriminator_init(&state->discriminator);
    rtl_iq_decimator_init(&state->rtl_iq_decimator);
    rds_demod_init(&state->rds_demod);
}

void fm_pipeline_reset(struct fm_pipeline_state *state) {
    fm_pipeline_destroy(state);
    fm_pipeline_init(state);
}

void fm_pipeline_configure(struct fm_pipeline_state *state, bool rds_enabled) {
    rtl_iq_decimator_configure(&state->rtl_iq_decimator);
    state->rds_enabled = rds_enabled;

    if (rds_enabled) {
        liquid_resampler_configure(&state->rds_resampler, FM_PIPELINE_MPX_RATE_HZ,
                                   FM_PIPELINE_RDS_SAMPLE_RATE);
        rds_demod_configure(&state->rds_demod, FM_PIPELINE_RDS_SAMPLE_RATE);
    }
}

void fm_pipeline_process_live_block(struct fm_pipeline_state *state, struct rtl_source *source) {
    if (!state->rtl_iq_decimator.configured) {
        rtl_iq_decimator_configure(&state->rtl_iq_decimator);
    }

    if (rtl_iq_decimator_read_block_float(&state->rtl_iq_decimator, source, state->decimated_iq) !=
        0) {
        memset(state->decimated_iq, 0, sizeof(state->decimated_iq));
    }

    fm_discriminator_process_block_float(&state->discriminator, state->decimated_iq,
                                         FM_PIPELINE_DSP_BLOCK_SAMPLES, state->demodulated);

    if (state->rds_enabled) {
        const size_t produced_rds_sample_count =
            liquid_resampler_process_block(&state->rds_resampler, state->demodulated,
                                           FM_PIPELINE_DSP_BLOCK_SAMPLES, state->rds_input);

        if (produced_rds_sample_count > 0u &&
            produced_rds_sample_count <= FM_PIPELINE_MAX_RDS_SAMPLES) {
            rds_demod_process_mpx_block(&state->rds_demod, state->rds_input,
                                        produced_rds_sample_count);
        }
    }
}

const struct rds_station_info *fm_pipeline_get_station_info(const struct fm_pipeline_state *state) {
    return rds_parser_get_station_info(&state->rds_demod.parser);
}

bool fm_pipeline_has_complete_radiotext(const struct fm_pipeline_state *state) {
    return rds_parser_has_complete_radiotext(&state->rds_demod.parser);
}

static void fm_pipeline_destroy(struct fm_pipeline_state *state) {
    liquid_resampler_destroy(&state->rds_resampler);
    rds_demod_destroy(&state->rds_demod);
}
