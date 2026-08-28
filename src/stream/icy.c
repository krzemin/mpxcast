#include "stream/icy.h"

#include <stdio.h>
#include <string.h>

void icy_metadata_init(struct icy_metadata_state *state, bool enabled) {
    memset(state, 0, sizeof(*state));
    state->enabled = enabled;
    state->audio_bytes_until_metadata = ICY_METAINT;
}

size_t icy_bytes_until_next_metadata(size_t stream_body_bytes) {
    const size_t remainder = stream_body_bytes % ICY_METAINT;

    if (remainder == 0u) {
        return ICY_METAINT;
    }

    return ICY_METAINT - remainder;
}

void icy_copy_text(char *dest, size_t capacity, const char *source) {
    size_t write_pos = 0u;
    bool started = false;

    if (capacity == 0u) {
        return;
    }

    while (*source != '\0' && write_pos + 1u < capacity) {
        unsigned char c = (unsigned char)*source++;

        if (c == '\'' || c == ';') {
            if (!started) {
                continue;
            }
            dest[write_pos++] = ' ';
            started = true;
        } else if (c >= 32u && c < 127u) {
            if (!started && c == ' ') {
                continue;
            }
            dest[write_pos++] = (char)c;
            started = true;
        }
    }

    while (write_pos > 0u && dest[write_pos - 1u] == ' ') {
        write_pos -= 1u;
    }

    dest[write_pos] = '\0';
}

static int icy_build_metadata_block(unsigned char *buffer, size_t capacity, const char *title) {
    char metadata[ICY_TITLE_MAX_LENGTH];
    size_t metadata_size;
    size_t padded_size;

    if (title[0] == '\0') {
        if (capacity == 0u) {
            return -1;
        }

        buffer[0] = 0u;
        return 1;
    }

    metadata_size = (size_t)snprintf(metadata, sizeof(metadata), "StreamTitle='%s';", title);
    if (metadata_size >= sizeof(metadata)) {
        return -1;
    }

    padded_size = ((metadata_size + 15u) / 16u) * 16u;
    if (1u + padded_size > capacity || padded_size / 16u > 255u) {
        return -1;
    }

    buffer[0] = (unsigned char)(padded_size / 16u);
    memcpy(buffer + 1u, metadata, metadata_size);
    memset(buffer + 1u + metadata_size, 0, padded_size - metadata_size);
    return (int)(1u + padded_size);
}

int icy_metadata_update(struct icy_metadata_state *state, const char *radiotext,
                        bool has_complete_radiotext) {
    char title[ICY_TITLE_MAX_LENGTH + 1u];

    title[0] = '\0';

    if (has_complete_radiotext && radiotext[0] != '\0') {
        icy_copy_text(title, sizeof(title), radiotext);
    } else if (state->stable_title[0] != '\0') {
        strcpy(title, state->stable_title);
    }

    if (has_complete_radiotext && title[0] != '\0') {
        strcpy(state->stable_title, title);
    }

    state->metadata_length = (size_t)icy_build_metadata_block(state->metadata_block,
                                                              sizeof(state->metadata_block), title);
    return state->metadata_length == (size_t)-1 ? -1 : 0;
}

int icy_metadata_wrap(struct icy_metadata_state *state, const unsigned char *body,
                      size_t body_length, unsigned char *buffer, size_t capacity) {
    size_t body_offset = 0u;
    size_t output_offset = 0u;

    if (!state->enabled) {
        if (body_length > capacity) {
            return -1;
        }
        memcpy(buffer, body, body_length);
        return (int)body_length;
    }

    while (body_offset < body_length) {
        const size_t chunk = body_length - body_offset < state->audio_bytes_until_metadata
                                 ? body_length - body_offset
                                 : state->audio_bytes_until_metadata;

        if (output_offset + chunk > capacity) {
            return -1;
        }

        memcpy(buffer + output_offset, body + body_offset, chunk);
        output_offset += chunk;
        body_offset += chunk;
        state->audio_bytes_until_metadata -= chunk;

        if (state->audio_bytes_until_metadata == 0u) {
            if (output_offset + state->metadata_length > capacity) {
                return -1;
            }

            memcpy(buffer + output_offset, state->metadata_block, state->metadata_length);
            output_offset += state->metadata_length;
            state->audio_bytes_until_metadata = ICY_METAINT;
        }
    }

    return (int)output_offset;
}
