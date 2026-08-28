#include "dsp/deemphasis.h"

#include <math.h>
#include <stddef.h>

void deemphasis_init(struct deemphasis_state *state, float sample_rate, float tau_seconds) {
    const float dt = 1.0f / sample_rate;
    float alpha;

    if (tau_seconds <= 0.0f) {
        state->alpha = 1.0f;
        state->output = 0.0f;
        return;
    }

    alpha = 1.0f - expf(-dt / tau_seconds);
    if (alpha < 0.0f) {
        alpha = 0.0f;
    }
    if (alpha > 1.0f) {
        alpha = 1.0f;
    }

    state->alpha = alpha;
    state->output = 0.0f;
}

void deemphasis_process_buffer(struct deemphasis_state *state, float *samples,
                               size_t sample_count) {
    size_t i;

    for (i = 0u; i < sample_count; ++i) {
        state->output += state->alpha * (samples[i] - state->output);
        samples[i] = state->output;
    }
}
