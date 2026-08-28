#include "stream/mux_mpegts.h"

#include <string.h>

#define STREAM_MUX_MPEGTS_TRANSPORT_STREAM_ID 1u
#define STREAM_MUX_MPEGTS_PROGRAM_NUMBER 1u
#define STREAM_MUX_MPEGTS_PMT_PID 0x0100u
#define STREAM_MUX_MPEGTS_AUDIO_PID 0x1100u
#define STREAM_MUX_MPEGTS_PRIVATE_STREAM_ID 0xbdu

static uint32_t stream_mux_mpegts_crc32(const unsigned char *data, size_t length) {
    uint32_t crc = 0xffffffffu;
    size_t i;

    for (i = 0u; i < length; ++i) {
        unsigned int bit;

        crc ^= (uint32_t)data[i] << 24;
        for (bit = 0u; bit < 8u; ++bit) {
            crc = (crc & 0x80000000u) != 0u ? (crc << 1) ^ 0x04c11db7u : (crc << 1);
        }
    }

    return crc;
}

static void stream_mux_mpegts_write_ts_header(unsigned char *packet, bool payload_unit_start,
                                              uint16_t pid, uint8_t adaptation_field_control,
                                              uint8_t continuity_counter) {
    packet[0] = 0x47;
    packet[1] = (unsigned char)(((payload_unit_start ? 0x40u : 0x00u) | ((pid >> 8) & 0x1fu)));
    packet[2] = (unsigned char)(pid & 0xffu);
    packet[3] =
        (unsigned char)(((adaptation_field_control & 0x03u) << 4) | (continuity_counter & 0x0fu));
}

static void stream_mux_mpegts_write_pat_packet(struct stream_mux_mpegts_state *state,
                                               unsigned char *packet) {
    unsigned char section[16];
    uint32_t crc;

    memset(packet, 0xff, STREAM_MUX_MPEGTS_PACKET_SIZE);
    stream_mux_mpegts_write_ts_header(packet, true, 0x0000u, 1u, state->pat_continuity_counter++);
    state->pat_continuity_counter &= 0x0fu;
    packet[4] = 0x00;

    section[0] = 0x00;
    section[1] = 0xb0;
    section[2] = 0x0d;
    section[3] = 0x00;
    section[4] = STREAM_MUX_MPEGTS_TRANSPORT_STREAM_ID;
    section[5] = 0xc1;
    section[6] = 0x00;
    section[7] = 0x00;
    section[8] = 0x00;
    section[9] = STREAM_MUX_MPEGTS_PROGRAM_NUMBER;
    section[10] = (unsigned char)(0xe0u | ((STREAM_MUX_MPEGTS_PMT_PID >> 8) & 0x1fu));
    section[11] = (unsigned char)(STREAM_MUX_MPEGTS_PMT_PID & 0xffu);
    crc = stream_mux_mpegts_crc32(section, 12u);
    section[12] = (unsigned char)(crc >> 24);
    section[13] = (unsigned char)(crc >> 16);
    section[14] = (unsigned char)(crc >> 8);
    section[15] = (unsigned char)crc;

    memcpy(packet + 5u, section, sizeof(section));
}

static void stream_mux_mpegts_write_pmt_packet(struct stream_mux_mpegts_state *state,
                                               unsigned char *packet) {
    static const unsigned char program_descriptors[] = {
        0x05, 0x04, 'H', 'D', 'M', 'V', 0x88, 0x04, 0x0f, 0xff, 0xfc, 0xfcu,
    };
    unsigned char section[64];
    size_t section_length = 0u;
    uint32_t crc;

    memset(packet, 0xff, STREAM_MUX_MPEGTS_PACKET_SIZE);
    stream_mux_mpegts_write_ts_header(packet, true, STREAM_MUX_MPEGTS_PMT_PID, 1u,
                                      state->pmt_continuity_counter++);
    state->pmt_continuity_counter &= 0x0fu;
    packet[4] = 0x00;

    section[section_length++] = 0x02;
    section[section_length++] = 0xb0;
    section[section_length++] = 0x1e;
    section[section_length++] = 0x00;
    section[section_length++] = STREAM_MUX_MPEGTS_PROGRAM_NUMBER;
    section[section_length++] = 0xc1;
    section[section_length++] = 0x00;
    section[section_length++] = 0x00;
    section[section_length++] =
        (unsigned char)(0xe0u | ((STREAM_MUX_MPEGTS_AUDIO_PID >> 8) & 0x1fu));
    section[section_length++] = (unsigned char)(STREAM_MUX_MPEGTS_AUDIO_PID & 0xffu);
    section[section_length++] = 0xf0;
    section[section_length++] = sizeof(program_descriptors);
    memcpy(section + section_length, program_descriptors, sizeof(program_descriptors));
    section_length += sizeof(program_descriptors);
    section[section_length++] = 0x80;
    section[section_length++] =
        (unsigned char)(0xe0u | ((STREAM_MUX_MPEGTS_AUDIO_PID >> 8) & 0x1fu));
    section[section_length++] = (unsigned char)(STREAM_MUX_MPEGTS_AUDIO_PID & 0xffu);
    section[section_length++] = 0xf0;
    section[section_length++] = 0x00;
    crc = stream_mux_mpegts_crc32(section, section_length);
    section[section_length++] = (unsigned char)(crc >> 24);
    section[section_length++] = (unsigned char)(crc >> 16);
    section[section_length++] = (unsigned char)(crc >> 8);
    section[section_length++] = (unsigned char)crc;

    memcpy(packet + 5u, section, section_length);
}

static void stream_mux_mpegts_write_pts(unsigned char *buffer, uint64_t pts_90khz) {
    const uint64_t pts = pts_90khz & ((1ull << 33) - 1ull);

    buffer[0] = (unsigned char)(0x21u | (((pts >> 30) & 0x07u) << 1));
    buffer[1] = (unsigned char)(pts >> 22);
    buffer[2] = (unsigned char)((((pts >> 15) & 0x7fu) << 1) | 0x01u);
    buffer[3] = (unsigned char)(pts >> 7);
    buffer[4] = (unsigned char)(((pts & 0x7fu) << 1) | 0x01u);
}

static void stream_mux_mpegts_write_pcr(unsigned char *buffer, uint64_t pcr_27mhz) {
    const uint64_t base = pcr_27mhz / 300ull;
    const uint16_t extension = (uint16_t)(pcr_27mhz % 300ull);

    buffer[0] = (unsigned char)(base >> 25);
    buffer[1] = (unsigned char)(base >> 17);
    buffer[2] = (unsigned char)(base >> 9);
    buffer[3] = (unsigned char)(base >> 1);
    buffer[4] = (unsigned char)(((base & 0x01u) << 7) | 0x7eu | (extension >> 8));
    buffer[5] = (unsigned char)(extension & 0xffu);
}

static int stream_mux_mpegts_build_pes(const struct stream_mux_mpegts_state *state,
                                       const int16_t *samples, size_t frame_count,
                                       unsigned char *buffer, size_t capacity, size_t *length) {
    size_t payload_length = 0u;
    size_t i;
    const bool stereo = state->channels == 2u;
    const size_t audio_bytes_per_frame = 4u;
    const size_t lpcm_payload_bytes = 4u + frame_count * audio_bytes_per_frame;
    const size_t pes_length = 14u + lpcm_payload_bytes;
    const uint16_t pes_packet_length = (uint16_t)(lpcm_payload_bytes + 8u);

    if (pes_length > capacity) {
        return -1;
    }

    buffer[payload_length++] = 0x00;
    buffer[payload_length++] = 0x00;
    buffer[payload_length++] = 0x01;
    buffer[payload_length++] = STREAM_MUX_MPEGTS_PRIVATE_STREAM_ID;
    buffer[payload_length++] = (unsigned char)(pes_packet_length >> 8);
    buffer[payload_length++] = (unsigned char)(pes_packet_length & 0xffu);
    buffer[payload_length++] = 0x80;
    buffer[payload_length++] = 0x80;
    buffer[payload_length++] = 0x05;
    stream_mux_mpegts_write_pts(buffer + payload_length, state->pts_90khz);
    payload_length += 5u;
    buffer[payload_length++] = (unsigned char)((lpcm_payload_bytes - 4u) >> 8);
    buffer[payload_length++] = (unsigned char)((lpcm_payload_bytes - 4u) & 0xffu);
    buffer[payload_length++] = (unsigned char)(state->lpcm_header >> 8);
    buffer[payload_length++] = (unsigned char)(state->lpcm_header & 0xffu);

    if (stereo) {
        for (i = 0u; i < frame_count * 2u; ++i) {
            const uint16_t sample = (uint16_t)samples[i];

            buffer[payload_length++] = (unsigned char)(sample >> 8);
            buffer[payload_length++] = (unsigned char)(sample & 0xffu);
        }
    } else {
        for (i = 0u; i < frame_count; ++i) {
            const uint16_t sample = (uint16_t)samples[i];

            buffer[payload_length++] = (unsigned char)(sample >> 8);
            buffer[payload_length++] = (unsigned char)(sample & 0xffu);
            buffer[payload_length++] = 0x00;
            buffer[payload_length++] = 0x00;
        }
    }

    *length = payload_length;
    return 0;
}

static void stream_mux_mpegts_advance_pts(struct stream_mux_mpegts_state *state,
                                          size_t frame_count) {
    const uint64_t numerator =
        (uint64_t)frame_count * 90000ull + (uint64_t)state->pts_numerator_remainder;

    state->pts_90khz += numerator / 48000ull;
    state->pts_numerator_remainder = (uint32_t)(numerator % 48000ull);
}

void stream_mux_mpegts_init(struct stream_mux_mpegts_state *state, uint16_t channels) {
    memset(state, 0, sizeof(*state));
    state->channels = channels;
    state->lpcm_header = (uint16_t)(((((channels == 2u ? 3u : 1u) << 4) | 1u) << 8) | (1u << 6));
}

int stream_mux_mpegts_build_prelude(struct icy_metadata_state *icy,
                                    struct stream_mux_mpegts_state *state, unsigned char *buffer,
                                    size_t capacity, size_t *length) {
    if (2u * STREAM_MUX_MPEGTS_PACKET_SIZE > capacity) {
        return -1;
    }

    stream_mux_mpegts_write_pat_packet(state, buffer);
    stream_mux_mpegts_write_pmt_packet(state, buffer + STREAM_MUX_MPEGTS_PACKET_SIZE);
    *length = 2u * STREAM_MUX_MPEGTS_PACKET_SIZE;

    if (icy->enabled) {
        icy->audio_bytes_until_metadata =
            icy_bytes_until_next_metadata(2u * STREAM_MUX_MPEGTS_PACKET_SIZE);
    }

    return 0;
}

int stream_mux_mpegts_write_audio(struct stream_mux_mpegts_state *state, const int16_t *samples,
                                  size_t frame_count, unsigned char *buffer, size_t capacity,
                                  size_t *length) {
    unsigned char pes[STREAM_MUX_MPEGTS_MAX_PES_BYTES];
    size_t pes_length;
    size_t pes_offset = 0u;
    size_t written = 0u;
    bool first_packet = true;

    if (capacity < 2u * STREAM_MUX_MPEGTS_PACKET_SIZE) {
        return -1;
    }

    stream_mux_mpegts_write_pat_packet(state, buffer + written);
    written += STREAM_MUX_MPEGTS_PACKET_SIZE;
    stream_mux_mpegts_write_pmt_packet(state, buffer + written);
    written += STREAM_MUX_MPEGTS_PACKET_SIZE;

    if (stream_mux_mpegts_build_pes(state, samples, frame_count, pes, sizeof(pes), &pes_length) !=
        0) {
        return -1;
    }

    while (pes_offset < pes_length) {
        unsigned char *packet;
        const size_t remaining = pes_length - pes_offset;
        size_t payload_capacity;
        size_t payload_size;

        if (written + STREAM_MUX_MPEGTS_PACKET_SIZE > capacity) {
            return -1;
        }

        packet = buffer + written;
        memset(packet, 0xff, STREAM_MUX_MPEGTS_PACKET_SIZE);

        if (first_packet) {
            size_t adaptation_length = 7u;

            payload_capacity = STREAM_MUX_MPEGTS_PACKET_SIZE - 4u - 1u - adaptation_length;
            if (remaining < payload_capacity) {
                adaptation_length += payload_capacity - remaining;
                payload_capacity = remaining;
            }

            stream_mux_mpegts_write_ts_header(packet, true, STREAM_MUX_MPEGTS_AUDIO_PID, 3u,
                                              state->audio_continuity_counter++);
            packet[4] = (unsigned char)adaptation_length;
            packet[5] = 0x50;
            stream_mux_mpegts_write_pcr(packet + 6u, state->pts_90khz * 300ull);
            if (adaptation_length > 7u) {
                memset(packet + 12u, 0xff, adaptation_length - 7u);
            }
        } else if (remaining < STREAM_MUX_MPEGTS_PACKET_SIZE - 4u) {
            const size_t adaptation_total = STREAM_MUX_MPEGTS_PACKET_SIZE - 4u - remaining;

            stream_mux_mpegts_write_ts_header(packet, false, STREAM_MUX_MPEGTS_AUDIO_PID, 3u,
                                              state->audio_continuity_counter++);
            packet[4] = (unsigned char)(adaptation_total - 1u);
            if (adaptation_total > 1u) {
                packet[5] = 0x00;
                if (adaptation_total > 2u) {
                    memset(packet + 6u, 0xff, adaptation_total - 2u);
                }
            }
            payload_capacity = remaining;
        } else {
            stream_mux_mpegts_write_ts_header(packet, false, STREAM_MUX_MPEGTS_AUDIO_PID, 1u,
                                              state->audio_continuity_counter++);
            payload_capacity = STREAM_MUX_MPEGTS_PACKET_SIZE - 4u;
        }

        state->audio_continuity_counter &= 0x0fu;
        payload_size = remaining < payload_capacity ? remaining : payload_capacity;
        memcpy(packet + STREAM_MUX_MPEGTS_PACKET_SIZE - payload_capacity, pes + pes_offset,
               payload_size);
        pes_offset += payload_size;
        written += STREAM_MUX_MPEGTS_PACKET_SIZE;
        first_packet = false;
    }

    stream_mux_mpegts_advance_pts(state, frame_count);
    *length = written;
    return 0;
}
