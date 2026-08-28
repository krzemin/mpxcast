#include "audio/pcm.h"

#include <math.h>

#define PCM_FLOAT_FULL_SCALE 32768.0f
#define PCM_BASEBAND_VOLUME 0.4f

static int16_t pcm_saturate_s16(float sample) {
    const float clamped = fminf((float)INT16_MAX, fmaxf((float)INT16_MIN, sample));

    return (int16_t)clamped;
}

float pcm_audio_output_scale(float volume_gain) {
    return PCM_FLOAT_FULL_SCALE * PCM_BASEBAND_VOLUME * volume_gain;
}

void pcm_convert_f32_to_s16(const float *input, int16_t *output, size_t sample_count, float scale) {
    size_t i;

    for (i = 0u; i < sample_count; ++i) {
        const float sample = input[i] * scale;

        output[i] = pcm_saturate_s16(sample);
    }
}

void pcm_convert_stereo_f32_to_s16(const float *left, const float *right, int16_t *output,
                                   size_t frame_count, float scale) {
    size_t i;

    for (i = 0u; i < frame_count; ++i) {
        const float left_sample = left[i] * scale;
        const float right_sample = right[i] * scale;

        output[i * 2u] = pcm_saturate_s16(left_sample);
        output[i * 2u + 1u] = pcm_saturate_s16(right_sample);
    }
}
