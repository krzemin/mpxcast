#include "audio/wav.h"

static void write_u16le(uint8_t *buffer, uint16_t value) {
    buffer[0] = (uint8_t)(value & 0xffu);
    buffer[1] = (uint8_t)((value >> 8) & 0xffu);
}

static void write_u32le(uint8_t *buffer, uint32_t value) {
    buffer[0] = (uint8_t)(value & 0xffu);
    buffer[1] = (uint8_t)((value >> 8) & 0xffu);
    buffer[2] = (uint8_t)((value >> 16) & 0xffu);
    buffer[3] = (uint8_t)((value >> 24) & 0xffu);
}

void wav_write_stream_header(uint8_t *buffer, uint16_t channels, uint32_t sample_rate,
                             uint16_t bits_per_sample) {
    const uint16_t block_align = (uint16_t)(channels * (bits_per_sample / 8u));
    const uint32_t byte_rate = sample_rate * (uint32_t)block_align;

    buffer[0] = 'R';
    buffer[1] = 'I';
    buffer[2] = 'F';
    buffer[3] = 'F';
    write_u32le(buffer + 4, 0u);
    buffer[8] = 'W';
    buffer[9] = 'A';
    buffer[10] = 'V';
    buffer[11] = 'E';

    buffer[12] = 'f';
    buffer[13] = 'm';
    buffer[14] = 't';
    buffer[15] = ' ';
    write_u32le(buffer + 16, 16u);
    write_u16le(buffer + 20, 1u);
    write_u16le(buffer + 22, channels);
    write_u32le(buffer + 24, sample_rate);
    write_u32le(buffer + 28, byte_rate);
    write_u16le(buffer + 32, block_align);
    write_u16le(buffer + 34, bits_per_sample);

    buffer[36] = 'd';
    buffer[37] = 'a';
    buffer[38] = 't';
    buffer[39] = 'a';
    write_u32le(buffer + 40, 0u);
}
