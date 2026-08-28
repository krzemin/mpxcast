#ifndef MPXCAST_AUDIO_PCM_H
#define MPXCAST_AUDIO_PCM_H

#include <stddef.h>
#include <stdint.h>

float pcm_audio_output_scale(float volume_gain);
void pcm_convert_f32_to_s16(const float *input, int16_t *output, size_t sample_count, float scale);
void pcm_convert_stereo_f32_to_s16(const float *left, const float *right, int16_t *output,
                                   size_t frame_count, float scale);

#endif
