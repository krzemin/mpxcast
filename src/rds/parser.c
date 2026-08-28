#include "rds/parser.h"

#include <ctype.h>
#include <string.h>

static uint8_t rds_get_group_type_number(uint16_t block2) {
    return (uint8_t)((block2 >> 12) & 0x0Fu);
}

static char rds_get_group_version(uint16_t block2) { return (block2 & (1u << 11)) ? 'B' : 'A'; }

static uint8_t rds_get_pty(uint16_t block2) { return (uint8_t)((block2 >> 5) & 0x1Fu); }

static char rds_sanitize_char(uint8_t value) {
    if (value == 0u || value == '\r' || value == '\n') {
        return '\0';
    }

    if (isprint((int)value)) {
        return (char)value;
    }

    return ' ';
}

static void rds_trim_string(char *text) {
    size_t length = strlen(text);

    while (length > 0u && text[length - 1u] == ' ') {
        text[length - 1u] = '\0';
        length -= 1u;
    }
}

static void rds_build_ps_preview(const struct rds_parser_state *state, char *preview) {
    size_t segment;

    for (segment = 0; segment < 4u; ++segment) {
        const bool seen = (state->ps_seen_mask & (1u << segment)) != 0u;
        const size_t position = segment * 2u;

        preview[position] =
            seen && state->info.ps[position] != '\0' ? state->info.ps[position] : '.';
        preview[position + 1u] =
            seen && state->info.ps[position + 1u] != '\0' ? state->info.ps[position + 1u] : '.';
    }

    preview[8] = '\0';
}

static void rds_build_radiotext_preview(const struct rds_parser_state *state, char *preview) {
    size_t i;

    for (i = 0; i < 64u; ++i) {
        const bool seen = (state->radiotext_seen_mask & ((uint64_t)1u << i)) != 0u;
        const char value = state->info.radiotext[i];

        preview[i] = seen ? (value == '\0' ? ' ' : value) : '.';
    }

    preview[64] = '\0';
}

static float rds_block_quality(const struct rds_block *block) {
    float quality;

    if (!block->is_received) {
        return 0.0f;
    }

    quality = block->confidence / (1.0f + (float)block->corrected_bit_count);
    quality += block->had_errors ? 0.25f : 2.0f;
    return quality;
}

static float rds_group_text_quality(const struct rds_block *first, const struct rds_block *second) {
    return rds_block_quality(first) + rds_block_quality(second);
}

static float rds_group_text_quality_2a(const struct rds_block *block2,
                                       const struct rds_block *block3,
                                       const struct rds_block *block4) {
    return rds_block_quality(block2) + rds_block_quality(block3) + rds_block_quality(block4);
}

static void rds_update_ps(struct rds_parser_state *state, uint16_t block2, uint16_t block4,
                          float quality) {
    const uint8_t segment_address = (uint8_t)(block2 & 0x03u);
    const size_t position = (size_t)segment_address * 2u;
    const char first_char = rds_sanitize_char((uint8_t)(block4 >> 8));
    const char second_char = rds_sanitize_char((uint8_t)(block4 & 0xFFu));
    const bool has_existing = (state->ps_seen_mask & (1u << segment_address)) != 0u;
    const bool same_chars = has_existing && state->info.ps[position] == first_char &&
                            state->info.ps[position + 1u] == second_char;

    if (has_existing && !same_chars && quality < state->ps_segment_quality[segment_address]) {
        return;
    }

    state->info.ps[position] = first_char;
    state->info.ps[position + 1u] = second_char;
    state->ps_seen_mask |= (uint8_t)(1u << segment_address);
    if (!has_existing || quality > state->ps_segment_quality[segment_address]) {
        state->ps_segment_quality[segment_address] = quality;
    }
    state->info.ps[8] = '\0';
    rds_trim_string(state->info.ps);
}

static void rds_clear_radiotext(struct rds_parser_state *state) {
    memset(state->info.radiotext, 0, sizeof(state->info.radiotext));
    memset(state->radiotext_quality, 0, sizeof(state->radiotext_quality));
    state->radiotext_seen_mask = 0u;
}

static void rds_update_radiotext_chars(struct rds_parser_state *state, size_t position,
                                       const uint8_t *chars, size_t char_count, float quality) {
    size_t i;

    for (i = 0; i < char_count; ++i) {
        const size_t text_index = position + i;
        const char sanitized = rds_sanitize_char(chars[i]);
        const bool seen = (state->radiotext_seen_mask & ((uint64_t)1u << text_index)) != 0u;
        const bool same_char = seen && state->info.radiotext[text_index] == sanitized;

        if (text_index >= 64u) {
            break;
        }

        if (seen && !same_char && quality < state->radiotext_quality[text_index]) {
            continue;
        }

        state->info.radiotext[text_index] = sanitized;
        state->radiotext_seen_mask |= ((uint64_t)1u << text_index);
        if (!seen || quality > state->radiotext_quality[text_index]) {
            state->radiotext_quality[text_index] = quality;
        }
    }

    state->info.radiotext[64] = '\0';
}

static bool rds_block_is_usable_for_text(const struct rds_block *block) {
    return block->is_received && (!block->had_errors || block->corrected_bit_count > 0u);
}

static bool rds_parser_radiotext_is_complete(const struct rds_parser_state *state) {
    size_t i;
    size_t end_index = 64u;
    bool found_end = false;
    bool has_non_space = false;

    if (state->info.radiotext[0] == '\0') {
        return false;
    }

    for (i = 0u; i < 64u; ++i) {
        const bool seen = (state->radiotext_seen_mask & ((uint64_t)1u << i)) != 0u;

        if (!seen) {
            continue;
        }

        if (state->info.radiotext[i] == '\0') {
            end_index = i;
            found_end = true;
            break;
        }

        if (state->info.radiotext[i] != ' ') {
            has_non_space = true;
        }
    }

    if (!found_end) {
        if (state->radiotext_seen_mask != UINT64_MAX) {
            return false;
        }

        for (i = 0u; i < 64u; ++i) {
            if (state->info.radiotext[i] != ' ') {
                has_non_space = true;
                break;
            }
        }
    }

    for (i = 0u; i < end_index; ++i) {
        if ((state->radiotext_seen_mask & ((uint64_t)1u << i)) == 0u) {
            return false;
        }
    }

    return has_non_space;
}

void rds_parser_init(struct rds_parser_state *state) {
    memset(state, 0, sizeof(*state));
    rds_station_info_init(&state->info);
}

bool rds_parser_process_group(struct rds_parser_state *state, const struct rds_group *group,
                              struct rds_station_event *event) {
    uint16_t block1;
    uint16_t block2;
    uint8_t group_type;
    char group_version;

    if (!(group->blocks[RDS_BLOCK_NUMBER_1].is_received &&
          group->blocks[RDS_BLOCK_NUMBER_2].is_received)) {
        return false;
    }

    rds_station_event_init(event);

    block1 = group->blocks[RDS_BLOCK_NUMBER_1].data;
    block2 = group->blocks[RDS_BLOCK_NUMBER_2].data;
    group_type = rds_get_group_type_number(block2);
    group_version = rds_get_group_version(block2);
    event->corrected_block_count = (uint8_t)(group->blocks[0].had_errors ? 1u : 0u) +
                                   (uint8_t)(group->blocks[1].had_errors ? 1u : 0u) +
                                   (uint8_t)(group->blocks[2].had_errors ? 1u : 0u) +
                                   (uint8_t)(group->blocks[3].had_errors ? 1u : 0u);

    /* Only let a noisy block A change PI if we don't have a stable PI yet. */
    if (rds_block_is_usable_for_text(&group->blocks[RDS_BLOCK_NUMBER_1]) || !state->info.has_pi) {
        state->info.has_pi = true;
        state->info.pi = block1;
    }
    state->info.has_group = true;
    if (group_type < 10u) {
        state->info.group[0] = (char)('0' + group_type);
    } else {
        state->info.group[0] = (char)('A' + (group_type - 10u));
    }
    state->info.group[1] = group_version;
    state->info.group[2] = '\0';
    state->info.tp = (block2 & (1u << 10)) != 0u;
    state->info.pty = rds_get_pty(block2);

    if (group_type == 0u) {
        state->info.ta = (block2 & (1u << 4)) != 0u;
        if (rds_block_is_usable_for_text(&group->blocks[RDS_BLOCK_NUMBER_2]) &&
            rds_block_is_usable_for_text(&group->blocks[RDS_BLOCK_NUMBER_4])) {
            rds_update_ps(state, block2, group->blocks[RDS_BLOCK_NUMBER_4].data,
                          rds_group_text_quality(&group->blocks[RDS_BLOCK_NUMBER_2],
                                                 &group->blocks[RDS_BLOCK_NUMBER_4]));
        }
    }

    if (group_type == 2u) {
        const bool ab = (block2 & (1u << 4)) != 0u;
        const size_t segment_address = (size_t)(block2 & 0x0Fu);

        if (!state->radiotext_ab_valid || state->radiotext_ab != ab) {
            state->radiotext_ab = ab;
            state->radiotext_ab_valid = true;
            rds_clear_radiotext(state);
        }

        if (group_version == 'A') {
            uint8_t chars[4];

            if (rds_block_is_usable_for_text(&group->blocks[RDS_BLOCK_NUMBER_2]) &&
                rds_block_is_usable_for_text(&group->blocks[RDS_BLOCK_NUMBER_3]) &&
                rds_block_is_usable_for_text(&group->blocks[RDS_BLOCK_NUMBER_4])) {
                chars[0] = (uint8_t)(group->blocks[RDS_BLOCK_NUMBER_3].data >> 8);
                chars[1] = (uint8_t)(group->blocks[RDS_BLOCK_NUMBER_3].data & 0xFFu);
                chars[2] = (uint8_t)(group->blocks[RDS_BLOCK_NUMBER_4].data >> 8);
                chars[3] = (uint8_t)(group->blocks[RDS_BLOCK_NUMBER_4].data & 0xFFu);
                rds_update_radiotext_chars(
                    state, segment_address * 4u, chars, 4u,
                    rds_group_text_quality_2a(&group->blocks[RDS_BLOCK_NUMBER_2],
                                              &group->blocks[RDS_BLOCK_NUMBER_3],
                                              &group->blocks[RDS_BLOCK_NUMBER_4]));
            }
        } else if (rds_block_is_usable_for_text(&group->blocks[RDS_BLOCK_NUMBER_2]) &&
                   rds_block_is_usable_for_text(&group->blocks[RDS_BLOCK_NUMBER_4])) {
            uint8_t chars[2];

            chars[0] = (uint8_t)(group->blocks[RDS_BLOCK_NUMBER_4].data >> 8);
            chars[1] = (uint8_t)(group->blocks[RDS_BLOCK_NUMBER_4].data & 0xFFu);
            rds_update_radiotext_chars(state, segment_address * 2u, chars, 2u,
                                       rds_group_text_quality(&group->blocks[RDS_BLOCK_NUMBER_2],
                                                              &group->blocks[RDS_BLOCK_NUMBER_4]));
        }
    }

    event->station_changed = true;
    event->info = state->info;
    rds_build_ps_preview(state, event->ps_preview);
    rds_build_radiotext_preview(state, event->radiotext_preview);

    if (strcmp(state->last_reported_ps, state->info.ps) != 0 && state->info.ps[0] != '\0') {
        strcpy(state->last_reported_ps, state->info.ps);
        event->ps_changed = true;
    }

    if (rds_parser_radiotext_is_complete(state) &&
        strcmp(state->last_reported_radiotext, state->info.radiotext) != 0 &&
        state->info.radiotext[0] != '\0') {
        strcpy(state->last_reported_radiotext, state->info.radiotext);
        event->radiotext_changed = true;
    }

    if (strcmp(state->last_reported_ps_preview, event->ps_preview) != 0) {
        strcpy(state->last_reported_ps_preview, event->ps_preview);
        event->ps_preview_changed = true;
    }

    if (strcmp(state->last_reported_radiotext_preview, event->radiotext_preview) != 0) {
        strcpy(state->last_reported_radiotext_preview, event->radiotext_preview);
        event->radiotext_preview_changed = true;
    }

    return true;
}

const struct rds_station_info *rds_parser_get_station_info(const struct rds_parser_state *state) {
    return &state->info;
}

bool rds_parser_has_complete_radiotext(const struct rds_parser_state *state) {
    return rds_parser_radiotext_is_complete(state);
}
