#ifndef MPXCAST_STREAM_ICY_H
#define MPXCAST_STREAM_ICY_H

#include <stdbool.h>
#include <stddef.h>

#define ICY_METAINT 16384u
#define ICY_METAINT_TEXT "16384"
#define ICY_NAME_MAX_LENGTH 128u
#define ICY_TITLE_MAX_LENGTH 240u
#define ICY_METADATA_BLOCK_CAPACITY 256u

struct icy_metadata_state {
    bool enabled;
    size_t audio_bytes_until_metadata;
    size_t metadata_length;
    unsigned char metadata_block[ICY_METADATA_BLOCK_CAPACITY];
    char stable_title[ICY_TITLE_MAX_LENGTH + 1u];
};

void icy_metadata_init(struct icy_metadata_state *state, bool enabled);
size_t icy_bytes_until_next_metadata(size_t stream_body_bytes);
void icy_copy_text(char *dest, size_t capacity, const char *source);
int icy_metadata_update(struct icy_metadata_state *state, const char *radiotext,
                        bool has_complete_radiotext);
int icy_metadata_wrap(struct icy_metadata_state *state, const unsigned char *body,
                      size_t body_length, unsigned char *buffer, size_t capacity);

#endif
