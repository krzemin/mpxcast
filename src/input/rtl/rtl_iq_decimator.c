#include "input/rtl/rtl_iq_decimator.h"

#include <math.h>
#include <string.h>

#include "core/math_constants.h"

#if RTL_IQ_DECIMATOR_TAP_COUNT == 16u && RTL_IQ_DECIMATOR_FACTOR == 8u
#define RTL_IQ_DECIMATOR_USE_SPECIALIZED_16X8 1
#define RTL_IQ_DECIMATOR_SPECIALIZED_OVERLAP_SAMPLES                                               \
    (RTL_IQ_DECIMATOR_TAP_COUNT - RTL_IQ_DECIMATOR_FACTOR)
#endif

static int rtl_iq_decimator_read_block_generic(struct rtl_iq_decimator_state *state,
                                               struct rtl_source *source,
                                               float *restrict output_iq);
#if defined(RTL_IQ_DECIMATOR_USE_SPECIALIZED_16X8)
static int rtl_iq_decimator_read_block_specialized_16x8(struct rtl_iq_decimator_state *state,
                                                        struct rtl_source *source,
                                                        float *restrict output_iq);
#endif
static void rtl_iq_decimator_design_taps(struct rtl_iq_decimator_taps *taps);

void rtl_iq_decimator_init(struct rtl_iq_decimator_state *state) {
    memset(state, 0, sizeof(*state));
}

void rtl_iq_decimator_configure(struct rtl_iq_decimator_state *state) {
    rtl_iq_decimator_design_taps(&state->taps);
    state->configured = true;
}

int rtl_iq_decimator_read_block_float(struct rtl_iq_decimator_state *state,
                                      struct rtl_source *source, float *restrict output_iq) {
#if defined(RTL_IQ_DECIMATOR_USE_SPECIALIZED_16X8)
    return rtl_iq_decimator_read_block_specialized_16x8(state, source, output_iq);
#else
    return rtl_iq_decimator_read_block_generic(state, source, output_iq);
#endif
}

#if defined(RTL_IQ_DECIMATOR_USE_SPECIALIZED_16X8)
static int rtl_iq_decimator_read_block_specialized_16x8(struct rtl_iq_decimator_state *state,
                                                        struct rtl_source *source,
                                                        float *restrict output_iq) {
    const float t0 = state->taps.values[0];
    const float t1 = state->taps.values[1];
    const float t2 = state->taps.values[2];
    const float t3 = state->taps.values[3];
    const float t4 = state->taps.values[4];
    const float t5 = state->taps.values[5];
    const float t6 = state->taps.values[6];
    const float t7 = state->taps.values[7];
    float window_i[RTL_IQ_DECIMATOR_TAP_COUNT];
    float window_q[RTL_IQ_DECIMATOR_TAP_COUNT];
    const unsigned char *input;
    size_t output_index;

    if (rtl_source_read(source, state->input_bytes, RTL_IQ_DECIMATOR_BLOCK_INPUT_BYTES) != 0) {
        return -1;
    }

    memcpy(window_i, state->history_in_phase,
           RTL_IQ_DECIMATOR_SPECIALIZED_OVERLAP_SAMPLES * sizeof(float));
    memcpy(window_q, state->history_quadrature,
           RTL_IQ_DECIMATOR_SPECIALIZED_OVERLAP_SAMPLES * sizeof(float));
    input = state->input_bytes;

    for (output_index = 0u; output_index < RTL_IQ_DECIMATOR_OUTPUT_BLOCK_SAMPLES; ++output_index) {
        const size_t output_base = output_index * 2u;

        window_i[8] = ((float)input[0] * 2.0f) - 255.0f;
        window_q[8] = ((float)input[1] * 2.0f) - 255.0f;
        window_i[9] = ((float)input[2] * 2.0f) - 255.0f;
        window_q[9] = ((float)input[3] * 2.0f) - 255.0f;
        window_i[10] = ((float)input[4] * 2.0f) - 255.0f;
        window_q[10] = ((float)input[5] * 2.0f) - 255.0f;
        window_i[11] = ((float)input[6] * 2.0f) - 255.0f;
        window_q[11] = ((float)input[7] * 2.0f) - 255.0f;
        window_i[12] = ((float)input[8] * 2.0f) - 255.0f;
        window_q[12] = ((float)input[9] * 2.0f) - 255.0f;
        window_i[13] = ((float)input[10] * 2.0f) - 255.0f;
        window_q[13] = ((float)input[11] * 2.0f) - 255.0f;
        window_i[14] = ((float)input[12] * 2.0f) - 255.0f;
        window_q[14] = ((float)input[13] * 2.0f) - 255.0f;
        window_i[15] = ((float)input[14] * 2.0f) - 255.0f;
        window_q[15] = ((float)input[15] * 2.0f) - 255.0f;
        input += RTL_IQ_DECIMATOR_FACTOR * 2u;

        output_iq[output_base] =
            t0 * (window_i[0] + window_i[15]) + t1 * (window_i[1] + window_i[14]) +
            t2 * (window_i[2] + window_i[13]) + t3 * (window_i[3] + window_i[12]) +
            t4 * (window_i[4] + window_i[11]) + t5 * (window_i[5] + window_i[10]) +
            t6 * (window_i[6] + window_i[9]) + t7 * (window_i[7] + window_i[8]);
        output_iq[output_base + 1u] =
            t0 * (window_q[0] + window_q[15]) + t1 * (window_q[1] + window_q[14]) +
            t2 * (window_q[2] + window_q[13]) + t3 * (window_q[3] + window_q[12]) +
            t4 * (window_q[4] + window_q[11]) + t5 * (window_q[5] + window_q[10]) +
            t6 * (window_q[6] + window_q[9]) + t7 * (window_q[7] + window_q[8]);

        window_i[0] = window_i[8];
        window_i[1] = window_i[9];
        window_i[2] = window_i[10];
        window_i[3] = window_i[11];
        window_i[4] = window_i[12];
        window_i[5] = window_i[13];
        window_i[6] = window_i[14];
        window_i[7] = window_i[15];

        window_q[0] = window_q[8];
        window_q[1] = window_q[9];
        window_q[2] = window_q[10];
        window_q[3] = window_q[11];
        window_q[4] = window_q[12];
        window_q[5] = window_q[13];
        window_q[6] = window_q[14];
        window_q[7] = window_q[15];
    }

    memcpy(state->history_in_phase, window_i,
           RTL_IQ_DECIMATOR_SPECIALIZED_OVERLAP_SAMPLES * sizeof(float));
    memcpy(state->history_quadrature, window_q,
           RTL_IQ_DECIMATOR_SPECIALIZED_OVERLAP_SAMPLES * sizeof(float));
    state->history_write_index = 0u;
    return 0;
}
#endif

static int rtl_iq_decimator_read_block_generic(struct rtl_iq_decimator_state *state,
                                               struct rtl_source *source,
                                               float *restrict output_iq) {
    const float *const taps = state->taps.values;
    size_t write_index = state->history_write_index;
    size_t output_index;

    if (rtl_source_read(source, state->input_bytes, RTL_IQ_DECIMATOR_BLOCK_INPUT_BYTES) != 0) {
        return -1;
    }

    for (output_index = 0u; output_index < RTL_IQ_DECIMATOR_OUTPUT_BLOCK_SAMPLES; ++output_index) {
        const unsigned char *input =
            state->input_bytes + output_index * RTL_IQ_DECIMATOR_FACTOR * 2u;
        size_t sample_index;

        for (sample_index = 0u; sample_index < RTL_IQ_DECIMATOR_FACTOR; ++sample_index) {
            const float in_phase = ((float)input[0] * 2.0f) - 255.0f;
            const float quadrature = ((float)input[1] * 2.0f) - 255.0f;

            state->history_in_phase[write_index] = in_phase;
            state->history_in_phase[write_index + RTL_IQ_DECIMATOR_TAP_COUNT] = in_phase;
            state->history_quadrature[write_index] = quadrature;
            state->history_quadrature[write_index + RTL_IQ_DECIMATOR_TAP_COUNT] = quadrature;
            write_index += 1u;
            if (write_index == RTL_IQ_DECIMATOR_TAP_COUNT) {
                write_index = 0u;
            }

            input += 2u;
        }

        {
            const float *window_i = state->history_in_phase + write_index;
            const float *window_q = state->history_quadrature + write_index;
            const size_t output_base = output_index * 2u;

            output_iq[output_base] =
                taps[0] * (window_i[0] + window_i[15]) + taps[1] * (window_i[1] + window_i[14]) +
                taps[2] * (window_i[2] + window_i[13]) + taps[3] * (window_i[3] + window_i[12]) +
                taps[4] * (window_i[4] + window_i[11]) + taps[5] * (window_i[5] + window_i[10]) +
                taps[6] * (window_i[6] + window_i[9]) + taps[7] * (window_i[7] + window_i[8]);
            output_iq[output_base + 1u] =
                taps[0] * (window_q[0] + window_q[15]) + taps[1] * (window_q[1] + window_q[14]) +
                taps[2] * (window_q[2] + window_q[13]) + taps[3] * (window_q[3] + window_q[12]) +
                taps[4] * (window_q[4] + window_q[11]) + taps[5] * (window_q[5] + window_q[10]) +
                taps[6] * (window_q[6] + window_q[9]) + taps[7] * (window_q[7] + window_q[8]);
        }
    }

    state->history_write_index = write_index;
    return 0;
}

static void rtl_iq_decimator_design_taps(struct rtl_iq_decimator_taps *taps) {
    const float center = ((float)RTL_IQ_DECIMATOR_TAP_COUNT - 1.0f) * 0.5f;
    const float normalized_cutoff =
        (float)(RTL_IQ_DECIMATOR_CUTOFF_HZ / RTL_IQ_DECIMATOR_INPUT_RATE_HZ);
    size_t i;

    if (taps->configured) {
        return;
    }

    for (i = 0u; i < RTL_IQ_DECIMATOR_TAP_COUNT; ++i) {
        const float x = (float)i - center;
        const float sinc = fabsf(x) < 1.0e-9f ? 2.0f * normalized_cutoff
                                              : sinf(PI2_F * normalized_cutoff * x) / (PI_F * x);
        const float hamming =
            0.54f - 0.46f * cosf((PI2_F * (float)i) / ((float)RTL_IQ_DECIMATOR_TAP_COUNT - 1.0f));

        taps->values[i] = sinc * hamming;
    }

    taps->configured = true;
}
