#ifndef MPXCAST_DSP_LIQUID_RESAMPLER_H
#define MPXCAST_DSP_LIQUID_RESAMPLER_H

#include <stddef.h>

#define LIQUID_RESAMPLER_STOPBAND_AS_DB 70.0f

struct liquid_resampler_state {
    float configured_input_rate;
    float configured_output_rate;
    void *resampler;
};

void liquid_resampler_init(struct liquid_resampler_state *state);
void liquid_resampler_destroy(struct liquid_resampler_state *state);
void liquid_resampler_configure(struct liquid_resampler_state *state, float input_rate,
                                float output_rate);
size_t liquid_resampler_process_block(struct liquid_resampler_state *state,
                                      const float *input_samples, size_t input_sample_count,
                                      float *output_samples);

#endif
