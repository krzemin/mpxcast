#include "dsp/mpx_filter_bank.h"

#include <math.h>
#include <string.h>

#include "core/math_constants.h"

static void mpx_filter_bank_design_bandpass_taps(float sample_rate, float low_hz, float high_hz,
                                                 float *taps);

void mpx_filter_bank_init(struct mpx_filter_bank_state *state) { memset(state, 0, sizeof(*state)); }

void mpx_filter_bank_configure(struct mpx_filter_bank_state *state, float sample_rate,
                               bool stereo_enabled) {
    if (state->configured_sample_rate == sample_rate && state->stereo_enabled == stereo_enabled) {
        return;
    }

    {
        float taps[MPX_FILTER_BANK_TAP_COUNT];

        mpx_filter_bank_design_lowpass_taps(sample_rate, MPX_FILTER_BANK_MONO_CUTOFF_HZ, taps,
                                            MPX_FILTER_BANK_TAP_COUNT);
        fir_configure(&state->mono_filter, taps, MPX_FILTER_BANK_TAP_COUNT);
    }
    if (stereo_enabled) {
        float taps[MPX_FILTER_BANK_TAP_COUNT];

        mpx_filter_bank_design_bandpass_taps(sample_rate, MPX_FILTER_BANK_PILOT_LOW_HZ,
                                             MPX_FILTER_BANK_PILOT_HIGH_HZ, taps);
        fir_configure(&state->pilot_filter, taps, MPX_FILTER_BANK_TAP_COUNT);

        mpx_filter_bank_design_bandpass_taps(sample_rate, MPX_FILTER_BANK_STEREO_LOW_HZ,
                                             MPX_FILTER_BANK_STEREO_HIGH_HZ, taps);
        fir_configure(&state->stereo_filter, taps, MPX_FILTER_BANK_TAP_COUNT);
    } else {
        fir_init(&state->pilot_filter);
        fir_init(&state->stereo_filter);
    }

    state->configured_sample_rate = sample_rate;
    state->stereo_enabled = stereo_enabled;
}

void mpx_filter_bank_process_block(struct mpx_filter_bank_state *state, const float *input,
                                   size_t sample_count, float *mono_output, float *pilot_output,
                                   float *stereo_output) {
    if (state->mono_filter.configured && mono_output != NULL) {
        fir_execute_block(&state->mono_filter, input, sample_count, mono_output);
    }

    if (state->stereo_enabled && state->pilot_filter.configured && pilot_output != NULL) {
        fir_execute_block(&state->pilot_filter, input, sample_count, pilot_output);
    }

    if (state->stereo_enabled && state->stereo_filter.configured && stereo_output != NULL) {
        fir_execute_block(&state->stereo_filter, input, sample_count, stereo_output);
    }
}

void mpx_filter_bank_design_lowpass_taps(float sample_rate, float cutoff_hz, float *taps,
                                         size_t tap_count) {
    const float center = ((float)tap_count - 1.0f) * 0.5f;
    const float normalized_cutoff = cutoff_hz / sample_rate;
    size_t i;

    for (i = 0u; i < tap_count; ++i) {
        const float x = (float)i - center;
        const float sinc = fabsf(x) < 1.0e-9f ? 2.0f * normalized_cutoff
                                              : sinf(PI2_F * normalized_cutoff * x) / (PI_F * x);
        const float hamming = 0.54f - 0.46f * cosf((PI2_F * (float)i) / ((float)tap_count - 1.0f));

        taps[i] = sinc * hamming;
    }
}

static void mpx_filter_bank_design_bandpass_taps(float sample_rate, float low_hz, float high_hz,
                                                 float *taps) {
    const float center = ((float)MPX_FILTER_BANK_TAP_COUNT - 1.0f) * 0.5f;
    const float normalized_low = low_hz / sample_rate;
    const float normalized_high = high_hz / sample_rate;
    unsigned int i;

    for (i = 0u; i < MPX_FILTER_BANK_TAP_COUNT; ++i) {
        const float x = (float)i - center;
        const float band =
            fabsf(x) < 1.0e-9f
                ? 2.0f * (normalized_high - normalized_low)
                : (sinf(PI2_F * normalized_high * x) - sinf(PI2_F * normalized_low * x)) /
                      (PI_F * x);
        const float hamming =
            0.54f - 0.46f * cosf((PI2_F * (float)i) / ((float)MPX_FILTER_BANK_TAP_COUNT - 1.0f));

        taps[i] = band * hamming;
    }
}
