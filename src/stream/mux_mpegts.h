#ifndef MPXCAST_STREAM_MUX_MPEGTS_H
#define MPXCAST_STREAM_MUX_MPEGTS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "stream/icy.h"

#define STREAM_MUX_MPEGTS_PACKET_SIZE 188u
#define STREAM_MUX_MPEGTS_MAX_PES_BYTES 4608u
#define STREAM_MUX_MPEGTS_FIRST_PAYLOAD_BYTES (STREAM_MUX_MPEGTS_PACKET_SIZE - 4u - 1u - 7u)
#define STREAM_MUX_MPEGTS_CONTINUATION_PAYLOAD_BYTES (STREAM_MUX_MPEGTS_PACKET_SIZE - 4u)
#define STREAM_MUX_MPEGTS_MAX_AUDIO_PACKET_COUNT                                                   \
    (1u + (STREAM_MUX_MPEGTS_MAX_PES_BYTES > STREAM_MUX_MPEGTS_FIRST_PAYLOAD_BYTES                 \
               ? (STREAM_MUX_MPEGTS_MAX_PES_BYTES - STREAM_MUX_MPEGTS_FIRST_PAYLOAD_BYTES +        \
                  STREAM_MUX_MPEGTS_CONTINUATION_PAYLOAD_BYTES - 1u) /                             \
                     STREAM_MUX_MPEGTS_CONTINUATION_PAYLOAD_BYTES                                  \
               : 0u))
#define STREAM_MUX_MPEGTS_MAX_AUDIO_BYTES                                                          \
    ((2u + STREAM_MUX_MPEGTS_MAX_AUDIO_PACKET_COUNT) * STREAM_MUX_MPEGTS_PACKET_SIZE)

struct stream_mux_mpegts_state {
    uint8_t pat_continuity_counter;
    uint8_t pmt_continuity_counter;
    uint8_t audio_continuity_counter;
    uint16_t lpcm_header;
    uint16_t channels;
    uint64_t pts_90khz;
    uint32_t pts_numerator_remainder;
};

void stream_mux_mpegts_init(struct stream_mux_mpegts_state *state, uint16_t channels);
int stream_mux_mpegts_build_prelude(struct icy_metadata_state *icy,
                                    struct stream_mux_mpegts_state *state, unsigned char *buffer,
                                    size_t capacity, size_t *length);
int stream_mux_mpegts_write_audio(struct stream_mux_mpegts_state *state, const int16_t *samples,
                                  size_t frame_count, unsigned char *buffer, size_t capacity,
                                  size_t *length);

#endif
