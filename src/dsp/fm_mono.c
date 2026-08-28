#include <string.h>

#include "audio/pcm.h"
#include "dsp/fm_mono.h"

static void fm_mono_configure_audio_decimator(struct fm_mono_state *state);
static size_t fm_mono_process_mpx_block(struct fm_mono_state *state, const float *demodulated,
                                        int16_t *output_samples);
static size_t fm_mono_output_audio_block(struct fm_mono_state *state, size_t sample_count,
                                         int16_t *output_samples);

void fm_mono_init(struct fm_mono_state *state) {
    memset(state, 0, sizeof(*state));
    mpx_filter_bank_init(&state->mpx_filter_bank);
    fir_decimator_init(&state->audio_decimator);
}

void fm_mono_configure(struct fm_mono_state *state, float volume_gain,
                       float deemphasis_tau_seconds) {
    deemphasis_init(&state->deemphasis, (float)FM_PIPELINE_AUDIO_OUTPUT_RATE_HZ,
                    deemphasis_tau_seconds);
    state->output_scale = pcm_audio_output_scale(volume_gain);
    mpx_filter_bank_configure(&state->mpx_filter_bank, FM_PIPELINE_MPX_RATE_HZ, false);
    fm_mono_configure_audio_decimator(state);
}

void fm_mono_process_block(struct fm_mono_state *state, const float *demodulated,
                           int16_t *output_samples) {
    const size_t written = fm_mono_process_mpx_block(state, demodulated, output_samples);

    if (written < FM_MONO_AUDIO_BLOCK_SAMPLES) {
        memset(output_samples + written, 0,
               (FM_MONO_AUDIO_BLOCK_SAMPLES - written) * sizeof(int16_t));
    }
}

static size_t fm_mono_process_mpx_block(struct fm_mono_state *state, const float *demodulated,
                                        int16_t *output_samples) {
    size_t produced_audio_count;

    mpx_filter_bank_process_block(&state->mpx_filter_bank, demodulated,
                                  FM_PIPELINE_DSP_BLOCK_SAMPLES, state->mono_mpx, NULL, NULL);
    produced_audio_count = fir_audio_decimator_process_block(
        &state->audio_decimator, state->mono_mpx, FM_PIPELINE_DSP_BLOCK_SAMPLES, state->audio_block,
        FM_MONO_AUDIO_BLOCK_SAMPLES);

    return fm_mono_output_audio_block(state, produced_audio_count, output_samples);
}

static size_t fm_mono_output_audio_block(struct fm_mono_state *state, size_t sample_count,
                                         int16_t *output_samples) {
    if (sample_count > FM_MONO_AUDIO_BLOCK_SAMPLES) {
        sample_count = FM_MONO_AUDIO_BLOCK_SAMPLES;
    }

    deemphasis_process_buffer(&state->deemphasis, state->audio_block, sample_count);
    pcm_convert_f32_to_s16(state->audio_block, output_samples, sample_count, state->output_scale);
    return sample_count;
}

static void fm_mono_configure_audio_decimator(struct fm_mono_state *state) {
    float taps[MPX_FILTER_BANK_TAP_COUNT];

    mpx_filter_bank_design_lowpass_taps(FM_PIPELINE_MPX_RATE_HZ, MPX_FILTER_BANK_MONO_CUTOFF_HZ,
                                        taps, MPX_FILTER_BANK_TAP_COUNT);
    fir_decimator_configure(&state->audio_decimator, FM_PIPELINE_AUDIO_DECIMATION_FACTOR, taps,
                            MPX_FILTER_BANK_TAP_COUNT);
}
