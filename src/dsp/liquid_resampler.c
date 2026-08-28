#include "dsp/liquid_resampler.h"

#include <string.h>

#include <liquid/liquid.h>

void liquid_resampler_init(struct liquid_resampler_state *state) {
    memset(state, 0, sizeof(*state));
}

void liquid_resampler_destroy(struct liquid_resampler_state *state) {
    if (state->resampler != NULL) {
        msresamp_rrrf_destroy((msresamp_rrrf)state->resampler);
    }

    memset(state, 0, sizeof(*state));
}

void liquid_resampler_configure(struct liquid_resampler_state *state, float input_rate,
                                float output_rate) {
    const float rate = output_rate / input_rate;

    if (state->configured_input_rate == input_rate &&
        state->configured_output_rate == output_rate) {
        return;
    }

    liquid_resampler_destroy(state);

    state->resampler = msresamp_rrrf_create(rate, LIQUID_RESAMPLER_STOPBAND_AS_DB);
    state->configured_input_rate = input_rate;
    state->configured_output_rate = output_rate;
}

size_t liquid_resampler_process_block(struct liquid_resampler_state *state,
                                      const float *input_samples, size_t input_sample_count,
                                      float *output_samples) {
    unsigned int output_count = 0u;

    if (state->resampler == NULL || input_sample_count == 0u) {
        return 0u;
    }

    msresamp_rrrf_execute((msresamp_rrrf)state->resampler, (float *)input_samples,
                          (unsigned int)input_sample_count, output_samples, &output_count);
    return (size_t)output_count;
}
