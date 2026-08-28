#ifndef MPXCAST_AUDIO_WAV_H
#define MPXCAST_AUDIO_WAV_H

#include <stddef.h>
#include <stdint.h>

#define WAV_HEADER_SIZE 44

void wav_write_stream_header(uint8_t *buffer, uint16_t channels, uint32_t sample_rate,
                             uint16_t bits_per_sample);

#endif
