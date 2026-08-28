#include "dsp/fm_discriminator.h"

#include <float.h>
#include <math.h>
#include <string.h>

#include "core/math_constants.h"

static void fm_discriminator_reset_state(struct fm_discriminator_state *state);
static void fm_discriminator_process_block_approx(struct fm_discriminator_state *state,
                                                  const float *restrict input_iq,
                                                  size_t sample_count,
                                                  float *restrict output_demodulated);
static void fm_discriminator_process_block_atan2_libm(struct fm_discriminator_state *state,
                                                      const float *restrict input_iq,
                                                      size_t sample_count,
                                                      float *restrict output_demodulated);
static void fm_discriminator_process_block_atan2_approx(struct fm_discriminator_state *state,
                                                        const float *restrict input_iq,
                                                        size_t sample_count,
                                                        float *restrict output_demodulated);
static float atan2_fast(float y, float x);

void fm_discriminator_init(struct fm_discriminator_state *state) {
    memset(state, 0, sizeof(*state));
    state->impl = FM_DISCRIMINATOR_IMPL_ATAN2_LIBM;
    state->process_block = fm_discriminator_process_block_atan2_libm;
    fm_discriminator_reset_state(state);
}

void fm_discriminator_set_impl(struct fm_discriminator_state *state,
                               enum fm_discriminator_impl impl) {
    state->impl = impl;
    switch (impl) {
    case FM_DISCRIMINATOR_IMPL_APPROX:
        state->process_block = fm_discriminator_process_block_approx;
        break;
    case FM_DISCRIMINATOR_IMPL_ATAN2_APPROX:
        state->process_block = fm_discriminator_process_block_atan2_approx;
        break;
    case FM_DISCRIMINATOR_IMPL_ATAN2_LIBM:
    default:
        state->process_block = fm_discriminator_process_block_atan2_libm;
        break;
    }
    fm_discriminator_reset_state(state);
}

void fm_discriminator_process_block_float(struct fm_discriminator_state *state,
                                          const float *restrict input_iq, size_t sample_count,
                                          float *restrict output_demodulated) {
    state->process_block(state, input_iq, sample_count, output_demodulated);
}

static void fm_discriminator_reset_state(struct fm_discriminator_state *state) {
    state->prev_i = 0.0f;
    state->prev_q = 0.0f;
    state->have_prev = 0;
}

static void fm_discriminator_process_block_approx(struct fm_discriminator_state *state,
                                                  const float *restrict input_iq,
                                                  size_t sample_count,
                                                  float *restrict output_demodulated) {
    size_t i = 0u;
    float prev_i = state->prev_i;
    float prev_q = state->prev_q;

    if (!state->have_prev && sample_count > 0u) {
        prev_i = input_iq[0];
        prev_q = input_iq[1];
        output_demodulated[0] = 0.0f;
        state->have_prev = 1;
        i = 1u;
    }

    for (; i < sample_count; ++i) {
        const float in_phase = input_iq[i * 2u];
        const float quadrature = input_iq[i * 2u + 1u];
        const float cross = (in_phase * prev_q) - (quadrature * prev_i);
        const float denom = (in_phase * in_phase) + (quadrature * quadrature) + 1.0f;

        output_demodulated[i] = cross / denom;
        prev_i = in_phase;
        prev_q = quadrature;
    }

    state->prev_i = prev_i;
    state->prev_q = prev_q;
}

static void fm_discriminator_process_block_atan2_libm(struct fm_discriminator_state *state,
                                                      const float *restrict input_iq,
                                                      size_t sample_count,
                                                      float *restrict output_demodulated) {
    size_t i = 0u;
    float prev_i = state->prev_i;
    float prev_q = state->prev_q;

    if (!state->have_prev && sample_count > 0u) {
        prev_i = input_iq[0];
        prev_q = input_iq[1];
        output_demodulated[0] = 0.0f;
        state->have_prev = 1;
        i = 1u;
    }

    for (; i < sample_count; ++i) {
        const float in_phase = input_iq[i * 2u];
        const float quadrature = input_iq[i * 2u + 1u];
        const float dot = (in_phase * prev_i) + (quadrature * prev_q);
        const float cross = (in_phase * prev_q) - (quadrature * prev_i);

        output_demodulated[i] = atan2f(cross, dot);
        prev_i = in_phase;
        prev_q = quadrature;
    }

    state->prev_i = prev_i;
    state->prev_q = prev_q;
}

static void fm_discriminator_process_block_atan2_approx(struct fm_discriminator_state *state,
                                                        const float *restrict input_iq,
                                                        size_t sample_count,
                                                        float *restrict output_demodulated) {
    size_t i = 0u;
    float prev_i = state->prev_i;
    float prev_q = state->prev_q;

    if (!state->have_prev && sample_count > 0u) {
        prev_i = input_iq[0];
        prev_q = input_iq[1];
        output_demodulated[0] = 0.0f;
        state->have_prev = 1;
        i = 1u;
    }

    for (; i < sample_count; ++i) {
        const float in_phase = input_iq[i * 2u];
        const float quadrature = input_iq[i * 2u + 1u];
        const float dot = (in_phase * prev_i) + (quadrature * prev_q);
        const float cross = (in_phase * prev_q) - (quadrature * prev_i);

        output_demodulated[i] = atan2_fast(cross, dot);
        prev_i = in_phase;
        prev_q = quadrature;
    }

    state->prev_i = prev_i;
    state->prev_q = prev_q;
}

static float atan2_fast(float y, float x) {
    const float abs_x = fabsf(x);
    const float abs_y = fabsf(y);
    const float high = fmaxf(abs_x, abs_y);
    const float z = fminf(abs_x, abs_y) / (high + FLT_MIN);
    const float z_squared = z * z;
    float angle =
        (((-0.0464964749f * z_squared + 0.15931422f) * z_squared - 0.327622764f) * z_squared * z) +
        z;

    angle += (float)(abs_y > abs_x) * (PI_2_F - 2.0f * angle);
    angle += (float)(x < 0.0f) * (PI_F - 2.0f * angle);
    return copysignf(angle, y);
}
