#ifndef MPXCAST_STREAM_MUX_WAV_H
#define MPXCAST_STREAM_MUX_WAV_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "stream/icy.h"

int stream_mux_wav_build_prelude(uint16_t channels, struct icy_metadata_state *icy,
                                 unsigned char *buffer, size_t capacity, size_t *length);
size_t stream_mux_wav_body_size(uint16_t channels, size_t frame_count);

#endif
