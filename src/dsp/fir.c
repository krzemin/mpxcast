#include "dsp/fir.h"

#include <string.h>

#include "dsp/fm_pipeline.h"
#include "dsp/mpx_filter_bank.h"

#define FIR_SPECIALIZED_DECIMATOR_TAP_COUNT 90u
#define FIR_SPECIALIZED_DECIMATOR_FACTOR 5u
#define FIR_SPECIALIZED_DECIMATOR_OVERLAP_SAMPLES                                                  \
    (FIR_SPECIALIZED_DECIMATOR_TAP_COUNT - FIR_SPECIALIZED_DECIMATOR_FACTOR)

static float fir_compute_current_from_window(const struct fir_state *state, const float *window);
static size_t fir_decimator_process_block_specialized_90x5(struct fir_decimator_state *state,
                                                           const float *restrict input,
                                                           size_t input_sample_count,
                                                           float *restrict output,
                                                           size_t max_output_sample_count);

void fir_init(struct fir_state *state) { memset(state, 0, sizeof(*state)); }

void fir_configure(struct fir_state *state, const float *taps, size_t tap_count) {
    fir_init(state);

    if (tap_count > FIR_MAX_TAPS) {
        tap_count = FIR_MAX_TAPS;
    }

    memcpy(state->taps, taps, tap_count * sizeof(float));
    state->tap_count = tap_count;
    state->pair_count = tap_count / 2u;
    state->configured = true;
}

void fir_execute_block(struct fir_state *state, const float *restrict input, size_t sample_count,
                       float *restrict output) {
    const size_t tap_count = state->tap_count;
    size_t write_index = state->write_index;
    size_t i;

    for (i = 0u; i < sample_count; ++i) {
        const float sample = input[i];
        const float *window;
        float acc;

        state->history[write_index] = sample;
        state->history[write_index + tap_count] = sample;
        write_index += 1u;
        if (write_index == tap_count) {
            write_index = 0u;
        }

        window = state->history + write_index;
        acc = fir_compute_current_from_window(state, window);
        output[i] = acc;
    }

    state->write_index = write_index;
}

void fir_decimator_init(struct fir_decimator_state *state) { memset(state, 0, sizeof(*state)); }

void fir_decimator_configure(struct fir_decimator_state *state, unsigned int factor,
                             const float *taps, size_t tap_count) {
    fir_decimator_init(state);
    fir_configure(&state->fir, taps, tap_count);
    state->factor = factor;
    state->configured = factor >= 2u && state->fir.configured;
}

size_t fir_decimator_process_block(struct fir_decimator_state *state, const float *restrict input,
                                   size_t input_sample_count, float *restrict output,
                                   size_t max_output_sample_count) {
    struct fir_state *fir = &state->fir;
    const size_t tap_count = fir->tap_count;
    const unsigned int factor = state->factor;
    size_t write_index = fir->write_index;
    unsigned int phase = state->phase;
    size_t produced = 0u;

    if (!state->configured || factor < 2u) {
        return 0u;
    }

    for (size_t input_index = 0u; input_index < input_sample_count;) {
        const size_t available_sample_count = input_sample_count - input_index;
        const size_t samples_until_output = (size_t)(factor - phase);
        const size_t batch_sample_count = available_sample_count < samples_until_output
                                              ? available_sample_count
                                              : samples_until_output;
        const float *window;
        float acc;

        for (size_t batch_index = 0u; batch_index < batch_sample_count; ++batch_index) {
            const float sample = input[input_index++];

            fir->history[write_index] = sample;
            fir->history[write_index + tap_count] = sample;
            write_index += 1u;
            if (write_index == tap_count) {
                write_index = 0u;
            }
        }

        phase += (unsigned int)batch_sample_count;
        if (phase != factor) {
            break;
        }

        phase = 0u;
        window = fir->history + write_index;
        acc = fir_compute_current_from_window(fir, window);
        if (produced < max_output_sample_count) {
            output[produced++] = acc;
        }
    }

    fir->write_index = write_index;
    state->phase = phase;
    return produced;
}

size_t fir_audio_decimator_process_block(struct fir_decimator_state *state,
                                         const float *restrict input, size_t input_sample_count,
                                         float *restrict output, size_t max_output_sample_count) {
#if FM_PIPELINE_AUDIO_DECIMATION_FACTOR == FIR_SPECIALIZED_DECIMATOR_FACTOR &&                     \
    MPX_FILTER_BANK_TAP_COUNT == FIR_SPECIALIZED_DECIMATOR_TAP_COUNT
    return fir_decimator_process_block_specialized_90x5(state, input, input_sample_count, output,
                                                        max_output_sample_count);
#else
    return fir_decimator_process_block(state, input, input_sample_count, output,
                                       max_output_sample_count);
#endif
}

static float fir_compute_current_from_window(const struct fir_state *state, const float *window) {
    float acc = 0.0f;
    size_t tap_index;

    for (tap_index = 0u; tap_index < state->pair_count; ++tap_index) {
        const float sample_sum = window[tap_index] + window[state->tap_count - 1u - tap_index];

        acc += sample_sum * state->taps[tap_index];
    }

    if ((state->tap_count & 1u) != 0u) {
        acc += window[state->pair_count] * state->taps[state->pair_count];
    }

    return acc;
}

static size_t fir_decimator_process_block_specialized_90x5(struct fir_decimator_state *state,
                                                           const float *restrict input,
                                                           size_t input_sample_count,
                                                           float *restrict output,
                                                           size_t max_output_sample_count) {
    struct fir_state *fir = &state->fir;
    const float *const taps = fir->taps;
    float window[FIR_SPECIALIZED_DECIMATOR_TAP_COUNT * 2u];
    size_t window_start = 0u;
    size_t input_index = 0u;
    size_t produced = 0u;

    memcpy(window, fir->history, FIR_SPECIALIZED_DECIMATOR_OVERLAP_SAMPLES * sizeof(float));

    while (input_index + FIR_SPECIALIZED_DECIMATOR_FACTOR <= input_sample_count) {
        float acc;

        if (window_start == FIR_SPECIALIZED_DECIMATOR_TAP_COUNT) {
            memcpy(window, window + window_start,
                   FIR_SPECIALIZED_DECIMATOR_OVERLAP_SAMPLES * sizeof(float));
            window_start = 0u;
        }

        window[window_start + 85u] = input[input_index];
        window[window_start + 86u] = input[input_index + 1u];
        window[window_start + 87u] = input[input_index + 2u];
        window[window_start + 88u] = input[input_index + 3u];
        window[window_start + 89u] = input[input_index + 4u];

        acc = taps[0] * (window[window_start + 0u] + window[window_start + 89u]) +
              taps[1] * (window[window_start + 1u] + window[window_start + 88u]) +
              taps[2] * (window[window_start + 2u] + window[window_start + 87u]) +
              taps[3] * (window[window_start + 3u] + window[window_start + 86u]) +
              taps[4] * (window[window_start + 4u] + window[window_start + 85u]) +
              taps[5] * (window[window_start + 5u] + window[window_start + 84u]) +
              taps[6] * (window[window_start + 6u] + window[window_start + 83u]) +
              taps[7] * (window[window_start + 7u] + window[window_start + 82u]) +
              taps[8] * (window[window_start + 8u] + window[window_start + 81u]) +
              taps[9] * (window[window_start + 9u] + window[window_start + 80u]) +
              taps[10] * (window[window_start + 10u] + window[window_start + 79u]) +
              taps[11] * (window[window_start + 11u] + window[window_start + 78u]) +
              taps[12] * (window[window_start + 12u] + window[window_start + 77u]) +
              taps[13] * (window[window_start + 13u] + window[window_start + 76u]) +
              taps[14] * (window[window_start + 14u] + window[window_start + 75u]) +
              taps[15] * (window[window_start + 15u] + window[window_start + 74u]) +
              taps[16] * (window[window_start + 16u] + window[window_start + 73u]) +
              taps[17] * (window[window_start + 17u] + window[window_start + 72u]) +
              taps[18] * (window[window_start + 18u] + window[window_start + 71u]) +
              taps[19] * (window[window_start + 19u] + window[window_start + 70u]) +
              taps[20] * (window[window_start + 20u] + window[window_start + 69u]) +
              taps[21] * (window[window_start + 21u] + window[window_start + 68u]) +
              taps[22] * (window[window_start + 22u] + window[window_start + 67u]) +
              taps[23] * (window[window_start + 23u] + window[window_start + 66u]) +
              taps[24] * (window[window_start + 24u] + window[window_start + 65u]) +
              taps[25] * (window[window_start + 25u] + window[window_start + 64u]) +
              taps[26] * (window[window_start + 26u] + window[window_start + 63u]) +
              taps[27] * (window[window_start + 27u] + window[window_start + 62u]) +
              taps[28] * (window[window_start + 28u] + window[window_start + 61u]) +
              taps[29] * (window[window_start + 29u] + window[window_start + 60u]) +
              taps[30] * (window[window_start + 30u] + window[window_start + 59u]) +
              taps[31] * (window[window_start + 31u] + window[window_start + 58u]) +
              taps[32] * (window[window_start + 32u] + window[window_start + 57u]) +
              taps[33] * (window[window_start + 33u] + window[window_start + 56u]) +
              taps[34] * (window[window_start + 34u] + window[window_start + 55u]) +
              taps[35] * (window[window_start + 35u] + window[window_start + 54u]) +
              taps[36] * (window[window_start + 36u] + window[window_start + 53u]) +
              taps[37] * (window[window_start + 37u] + window[window_start + 52u]) +
              taps[38] * (window[window_start + 38u] + window[window_start + 51u]) +
              taps[39] * (window[window_start + 39u] + window[window_start + 50u]) +
              taps[40] * (window[window_start + 40u] + window[window_start + 49u]) +
              taps[41] * (window[window_start + 41u] + window[window_start + 48u]) +
              taps[42] * (window[window_start + 42u] + window[window_start + 47u]) +
              taps[43] * (window[window_start + 43u] + window[window_start + 46u]) +
              taps[44] * (window[window_start + 44u] + window[window_start + 45u]);

        if (produced < max_output_sample_count) {
            output[produced++] = acc;
        }

        window_start += FIR_SPECIALIZED_DECIMATOR_FACTOR;
        input_index += FIR_SPECIALIZED_DECIMATOR_FACTOR;
    }

    memcpy(fir->history, window + window_start,
           FIR_SPECIALIZED_DECIMATOR_OVERLAP_SAMPLES * sizeof(float));
    fir->write_index = 0u;
    state->phase = 0u;
    return produced;
}
