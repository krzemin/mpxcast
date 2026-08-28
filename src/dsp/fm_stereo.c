#include <math.h>
#include <string.h>

#include "audio/pcm.h"
#include "core/math_constants.h"
#include "dsp/fm_stereo.h"

#define FM_STEREO_PILOT_ANGULAR_HZ (PI2_F * FM_STEREO_PILOT_NOMINAL_HZ)

static void fm_stereo_configure_audio_decimators(struct fm_stereo_state *state);
static void fm_stereo_configure_pilot_doubler(struct fm_stereo_state *state);
static size_t fm_stereo_process_mpx_block(struct fm_stereo_state *state, const float *demodulated,
                                          int16_t *output_samples);
static size_t fm_stereo_output_audio_block(struct fm_stereo_state *state, size_t frame_count,
                                           int16_t *output_samples);
static size_t fm_stereo_decimate_audio_block(struct fm_stereo_state *state);
static void fm_stereo_process_stereo_difference(struct fm_stereo_state *state);

void fm_stereo_init(struct fm_stereo_state *state) {
    memset(state, 0, sizeof(*state));
    mpx_filter_bank_init(&state->mpx_filter_bank);
    fir_decimator_init(&state->sum_audio_decimator);
    fir_decimator_init(&state->diff_audio_decimator);
}

void fm_stereo_configure(struct fm_stereo_state *state, float volume_gain,
                         float deemphasis_tau_seconds) {
    deemphasis_init(&state->deemphasis_left, (float)FM_PIPELINE_AUDIO_OUTPUT_RATE_HZ,
                    deemphasis_tau_seconds);
    deemphasis_init(&state->deemphasis_right, (float)FM_PIPELINE_AUDIO_OUTPUT_RATE_HZ,
                    deemphasis_tau_seconds);
    state->output_scale = pcm_audio_output_scale(volume_gain);
    mpx_filter_bank_configure(&state->mpx_filter_bank, FM_PIPELINE_MPX_RATE_HZ, true);
    fm_stereo_configure_audio_decimators(state);
    fm_stereo_configure_pilot_doubler(state);
}

void fm_stereo_process_block(struct fm_stereo_state *state, const float *demodulated,
                             int16_t *output_samples) {
    const size_t written = fm_stereo_process_mpx_block(state, demodulated, output_samples);

    if (written < FM_STEREO_AUDIO_BLOCK_FRAMES) {
        memset(output_samples + written * 2u, 0,
               (FM_STEREO_AUDIO_BLOCK_FRAMES - written) * 2u * sizeof(int16_t));
    }
}

static size_t fm_stereo_process_mpx_block(struct fm_stereo_state *state, const float *demodulated,
                                          int16_t *output_samples) {
    size_t frame_count;

    mpx_filter_bank_process_block(&state->mpx_filter_bank, demodulated,
                                  FM_PIPELINE_DSP_BLOCK_SAMPLES, state->mono_mpx, state->pilot_mpx,
                                  state->stereo_subchannel);
    fm_stereo_process_stereo_difference(state);
    frame_count = fm_stereo_decimate_audio_block(state);

    return fm_stereo_output_audio_block(state, frame_count, output_samples);
}

static size_t fm_stereo_output_audio_block(struct fm_stereo_state *state, size_t frame_count,
                                           int16_t *output_samples) {
    float *left_channel = state->pilot_mpx;
    float *right_channel = state->stereo_subchannel;
    size_t i;

    if (frame_count > FM_STEREO_AUDIO_BLOCK_FRAMES) {
        frame_count = FM_STEREO_AUDIO_BLOCK_FRAMES;
    }

    /* Reuse the decimated sum/difference scratch as left/right channels from here on. */
    for (i = 0u; i < frame_count; ++i) {
        const float sum_sample = left_channel[i];
        const float diff_sample = right_channel[i];

        left_channel[i] = sum_sample + diff_sample;
        right_channel[i] = sum_sample - diff_sample;
    }

    deemphasis_process_buffer(&state->deemphasis_left, left_channel, frame_count);
    deemphasis_process_buffer(&state->deemphasis_right, right_channel, frame_count);
    pcm_convert_stereo_f32_to_s16(left_channel, right_channel, output_samples, frame_count,
                                  state->output_scale);
    return frame_count;
}

static size_t fm_stereo_decimate_audio_block(struct fm_stereo_state *state) {
    const size_t produced_sum_count = fir_audio_decimator_process_block(
        &state->sum_audio_decimator, state->mono_mpx, FM_PIPELINE_DSP_BLOCK_SAMPLES,
        state->pilot_mpx, FM_STEREO_AUDIO_BLOCK_FRAMES);
    const size_t produced_diff_count = fir_audio_decimator_process_block(
        &state->diff_audio_decimator, state->stereo_difference, FM_PIPELINE_DSP_BLOCK_SAMPLES,
        state->stereo_subchannel, FM_STEREO_AUDIO_BLOCK_FRAMES);

    return produced_sum_count < produced_diff_count ? produced_sum_count : produced_diff_count;
}

static void fm_stereo_process_stereo_difference(struct fm_stereo_state *state) {
    size_t i;

    for (i = 0u; i < FM_PIPELINE_DSP_BLOCK_SAMPLES; ++i) {
        const float pilot_sample = state->pilot_mpx[i];
        const float x = pilot_sample * state->pilot_sin_step;
        const float y = pilot_sample * state->pilot_cos_step - state->prev_pilot_sample;
        const float denom = x * x + y * y;
        const float doubled_pilot = denom <= 0.0f ? 0.0f : (2.0f * x * y) / denom;

        state->stereo_difference[i] = state->stereo_subchannel[i] * doubled_pilot;
        state->prev_pilot_sample = pilot_sample;
    }
}

static void fm_stereo_configure_audio_decimators(struct fm_stereo_state *state) {
    float taps[MPX_FILTER_BANK_TAP_COUNT];

    mpx_filter_bank_design_lowpass_taps(FM_PIPELINE_MPX_RATE_HZ, MPX_FILTER_BANK_MONO_CUTOFF_HZ,
                                        taps, MPX_FILTER_BANK_TAP_COUNT);
    fir_decimator_configure(&state->sum_audio_decimator, FM_PIPELINE_AUDIO_DECIMATION_FACTOR, taps,
                            MPX_FILTER_BANK_TAP_COUNT);
    fir_decimator_configure(&state->diff_audio_decimator, FM_PIPELINE_AUDIO_DECIMATION_FACTOR, taps,
                            MPX_FILTER_BANK_TAP_COUNT);
}

static void fm_stereo_configure_pilot_doubler(struct fm_stereo_state *state) {
    const float wf = FM_STEREO_PILOT_ANGULAR_HZ / FM_PIPELINE_MPX_RATE_HZ;

    state->prev_pilot_sample = 0.0f;
    state->pilot_sin_step = sinf(wf);
    state->pilot_cos_step = cosf(wf);
}
