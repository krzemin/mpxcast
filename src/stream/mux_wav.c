#include "stream/mux_wav.h"

#include <string.h>

#include "audio/wav.h"

int stream_mux_wav_build_prelude(uint16_t channels, struct icy_metadata_state *icy,
                                 unsigned char *buffer, size_t capacity, size_t *length) {
    if (WAV_HEADER_SIZE > capacity) {
        return -1;
    }

    wav_write_stream_header(buffer, channels, 48000u, 16u);
    *length = WAV_HEADER_SIZE;

    if (icy->enabled) {
        icy->audio_bytes_until_metadata = icy_bytes_until_next_metadata(WAV_HEADER_SIZE);
    }

    return 0;
}

size_t stream_mux_wav_body_size(uint16_t channels, size_t frame_count) {
    return frame_count * (size_t)channels * sizeof(int16_t);
}
