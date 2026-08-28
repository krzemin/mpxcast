#include "rds/block_sync.h"

#include <string.h>

enum rds_offset {
    RDS_OFFSET_INVALID = 0,
    RDS_OFFSET_A = 1,
    RDS_OFFSET_B = 2,
    RDS_OFFSET_C = 3,
    RDS_OFFSET_C_PRIME = 4,
    RDS_OFFSET_D = 5
};

#define RDS_BLOCK_LENGTH 26u
#define RDS_CHECKWORD_LENGTH 10u
#define RDS_BLOCK_BITMASK ((1u << RDS_BLOCK_LENGTH) - 1u)
#define RDS_MAX_ERRORS_TOLERATED_OVER_50_BLOCKS 35u
#define RDS_SOFT_CANDIDATE_BITS 6u
#define RDS_SOFT_MAX_FLIPS 3u
#define RDS_SOFT_RELIABLE_MAX_FLIPS 2u
#define RDS_SOFT_RELIABLE_MIN_CONFIDENCE 0.0f

struct rds_error_correction_result {
    uint32_t corrected_bits;
    uint8_t corrected_bit_count;
    bool succeeded;
};

static uint8_t rds_get_block_number_for_offset(uint8_t offset) {
    switch (offset) {
    case RDS_OFFSET_A:
        return RDS_BLOCK_NUMBER_1;
    case RDS_OFFSET_B:
        return RDS_BLOCK_NUMBER_2;
    case RDS_OFFSET_C:
    case RDS_OFFSET_C_PRIME:
        return RDS_BLOCK_NUMBER_3;
    case RDS_OFFSET_D:
        return RDS_BLOCK_NUMBER_4;
    default:
        return RDS_BLOCK_NUMBER_1;
    }
}

static uint8_t rds_get_next_offset_for(uint8_t offset) {
    switch (offset) {
    case RDS_OFFSET_A:
        return RDS_OFFSET_B;
    case RDS_OFFSET_B:
        return RDS_OFFSET_C;
    case RDS_OFFSET_C:
    case RDS_OFFSET_C_PRIME:
        return RDS_OFFSET_D;
    case RDS_OFFSET_D:
        return RDS_OFFSET_A;
    default:
        return RDS_OFFSET_A;
    }
}

static uint32_t rds_calculate_syndrome(uint32_t input_vector) {
    static const uint32_t parity_check_matrix[26] = {
        0b1000000000u, 0b0100000000u, 0b0010000000u, 0b0001000000u, 0b0000100000u, 0b0000010000u,
        0b0000001000u, 0b0000000100u, 0b0000000010u, 0b0000000001u, 0b1011011100u, 0b0101101110u,
        0b0010110111u, 0b1010000111u, 0b1110011111u, 0b1100010011u, 0b1101010101u, 0b1101110110u,
        0b0110111011u, 0b1000000001u, 0b1111011100u, 0b0111101110u, 0b0011110111u, 0b1010100111u,
        0b1110001111u, 0b1100011011u};
    uint32_t result = 0u;
    size_t k;

    for (k = 0; k < 26u; ++k) {
        result ^= parity_check_matrix[25u - k] * ((input_vector >> k) & 1u);
    }

    return result;
}

static uint8_t rds_get_offset_for_syndrome(uint32_t syndrome) {
    switch (syndrome) {
    case 0b1111011000u:
        return RDS_OFFSET_A;
    case 0b1111010100u:
        return RDS_OFFSET_B;
    case 0b1001011100u:
        return RDS_OFFSET_C;
    case 0b1111001100u:
        return RDS_OFFSET_C_PRIME;
    case 0b1001011000u:
        return RDS_OFFSET_D;
    default:
        return RDS_OFFSET_INVALID;
    }
}

static const uint32_t *rds_get_offset_words(size_t *count_out) {
    static const uint32_t offset_words[] = {
        0u, 0b0011111100u, 0b0110011000u, 0b0101101000u, 0b1101010000u, 0b0110110100u};

    *count_out = sizeof(offset_words) / sizeof(offset_words[0]);
    return offset_words;
}

static struct rds_error_correction_result rds_correct_burst_errors(uint32_t raw_block,
                                                                   uint8_t expected_offset) {
    struct rds_error_correction_result result;
    size_t offset_word_count;
    const uint32_t *offset_words = rds_get_offset_words(&offset_word_count);
    const uint32_t expected_offset_word =
        expected_offset < offset_word_count ? offset_words[expected_offset] : 0u;
    const uint32_t syndrome = rds_calculate_syndrome(raw_block);
    uint32_t error_bits;
    uint32_t shift;

    result.corrected_bits = raw_block;
    result.corrected_bit_count = 0u;
    result.succeeded = false;

    for (error_bits = 0b1u; error_bits <= 0b11u;
         error_bits = (error_bits == 0b1u) ? 0b11u : 0b100u) {
        for (shift = 0u; shift < RDS_BLOCK_LENGTH; ++shift) {
            const uint32_t error_vector = ((error_bits << shift) & RDS_BLOCK_BITMASK);
            const uint32_t candidate_syndrome =
                rds_calculate_syndrome(error_vector ^ expected_offset_word);

            if (candidate_syndrome == syndrome) {
                result.corrected_bits ^= error_vector;
                result.corrected_bit_count =
                    error_bits == 0b1u ? 1u : (error_bits == 0b11u ? 2u : 0u);
                result.succeeded = true;
                return result;
            }
        }

        if (error_bits == 0b11u) {
            break;
        }
    }

    return result;
}

static struct rds_error_correction_result
rds_correct_soft_errors(uint32_t raw_block, uint8_t expected_offset,
                        const float *confidence_register) {
    struct rds_error_correction_result result;
    size_t offset_word_count;
    const uint32_t *offset_words = rds_get_offset_words(&offset_word_count);
    const uint32_t expected_offset_word =
        expected_offset < offset_word_count ? offset_words[expected_offset] : 0u;
    float weakest_confidence[RDS_SOFT_CANDIDATE_BITS];
    uint8_t weakest_index[RDS_SOFT_CANDIDATE_BITS];
    size_t bit_index;
    size_t candidate_count = 0u;

    result.corrected_bits = raw_block;
    result.corrected_bit_count = 0u;
    result.succeeded = false;

    for (bit_index = 0u; bit_index < RDS_SOFT_CANDIDATE_BITS; ++bit_index) {
        weakest_confidence[bit_index] = 1.0e9f;
        weakest_index[bit_index] = 0u;
    }

    for (bit_index = 0u; bit_index < RDS_BLOCK_LENGTH; ++bit_index) {
        const float confidence = confidence_register[bit_index];
        size_t insert_at;

        for (insert_at = 0u; insert_at < candidate_count; ++insert_at) {
            if (confidence < weakest_confidence[insert_at]) {
                break;
            }
        }

        if (insert_at >= RDS_SOFT_CANDIDATE_BITS) {
            continue;
        }

        if (candidate_count < RDS_SOFT_CANDIDATE_BITS) {
            candidate_count += 1u;
        }

        for (size_t move = candidate_count - 1u; move > insert_at; --move) {
            weakest_confidence[move] = weakest_confidence[move - 1u];
            weakest_index[move] = weakest_index[move - 1u];
        }

        weakest_confidence[insert_at] = confidence;
        weakest_index[insert_at] = (uint8_t)bit_index;
    }

    if (candidate_count == 0u) {
        return result;
    }

    for (size_t i = 0u; i < candidate_count; ++i) {
        const uint32_t mask = 1u << (RDS_BLOCK_LENGTH - 1u - weakest_index[i]);
        const uint32_t candidate = raw_block ^ mask;

        if (rds_calculate_syndrome(candidate) == expected_offset_word) {
            result.corrected_bits = candidate;
            result.corrected_bit_count = 1u;
            result.succeeded = true;
            return result;
        }
    }

    if (candidate_count >= 2u && RDS_SOFT_MAX_FLIPS >= 2u) {
        for (size_t i = 0u; i < candidate_count; ++i) {
            for (size_t j = i + 1u; j < candidate_count; ++j) {
                const uint32_t mask = (1u << (RDS_BLOCK_LENGTH - 1u - weakest_index[i])) |
                                      (1u << (RDS_BLOCK_LENGTH - 1u - weakest_index[j]));
                const uint32_t candidate = raw_block ^ mask;

                if (rds_calculate_syndrome(candidate) == expected_offset_word) {
                    result.corrected_bits = candidate;
                    result.corrected_bit_count = 2u;
                    result.succeeded = true;
                    return result;
                }
            }
        }
    }

    if (candidate_count >= 3u && RDS_SOFT_MAX_FLIPS >= 3u) {
        for (size_t i = 0u; i < candidate_count; ++i) {
            for (size_t j = i + 1u; j < candidate_count; ++j) {
                for (size_t k = j + 1u; k < candidate_count; ++k) {
                    const uint32_t mask = (1u << (RDS_BLOCK_LENGTH - 1u - weakest_index[i])) |
                                          (1u << (RDS_BLOCK_LENGTH - 1u - weakest_index[j])) |
                                          (1u << (RDS_BLOCK_LENGTH - 1u - weakest_index[k]));
                    const uint32_t candidate = raw_block ^ mask;

                    if (rds_calculate_syndrome(candidate) == expected_offset_word) {
                        result.corrected_bits = candidate;
                        result.corrected_bit_count = 3u;
                        result.succeeded = true;
                        return result;
                    }
                }
            }
        }
    }

    if (candidate_count >= 4u && RDS_SOFT_MAX_FLIPS >= 4u) {
        for (size_t i = 0u; i < candidate_count; ++i) {
            for (size_t j = i + 1u; j < candidate_count; ++j) {
                for (size_t k = j + 1u; k < candidate_count; ++k) {
                    for (size_t l = k + 1u; l < candidate_count; ++l) {
                        const uint32_t mask = (1u << (RDS_BLOCK_LENGTH - 1u - weakest_index[i])) |
                                              (1u << (RDS_BLOCK_LENGTH - 1u - weakest_index[j])) |
                                              (1u << (RDS_BLOCK_LENGTH - 1u - weakest_index[k])) |
                                              (1u << (RDS_BLOCK_LENGTH - 1u - weakest_index[l]));
                        const uint32_t candidate = raw_block ^ mask;

                        if (rds_calculate_syndrome(candidate) == expected_offset_word) {
                            result.corrected_bits = candidate;
                            result.corrected_bit_count = 4u;
                            result.succeeded = true;
                            return result;
                        }
                    }
                }
            }
        }
    }

    return result;
}

static float rds_get_block_confidence(const struct rds_block_stream_state *state) {
    float sum = 0.0f;
    size_t i;

    for (i = 0u; i < RDS_BLOCK_LENGTH; ++i) {
        sum += state->input_confidence_register[i];
    }

    return sum / (float)RDS_BLOCK_LENGTH;
}

static bool rds_sync_pulse_could_follow(uint8_t offset, uint32_t bit_position, uint8_t other_offset,
                                        uint32_t other_bit_position) {
    const uint32_t sync_distance = bit_position - other_bit_position;

    return sync_distance % RDS_BLOCK_LENGTH == 0u && sync_distance / RDS_BLOCK_LENGTH <= 6u &&
           offset != RDS_OFFSET_INVALID && other_offset != RDS_OFFSET_INVALID &&
           (rds_get_block_number_for_offset(other_offset) + sync_distance / RDS_BLOCK_LENGTH) %
                   4u ==
               rds_get_block_number_for_offset(offset);
}

static void rds_sync_buffer_push(struct rds_block_stream_state *state, uint8_t offset,
                                 uint32_t bit_position) {
    size_t i;

    for (i = 0; i < 7u; ++i) {
        state->sync_offsets[i] = state->sync_offsets[i + 1u];
        state->sync_positions[i] = state->sync_positions[i + 1u];
    }

    state->sync_offsets[7u] = offset;
    state->sync_positions[7u] = bit_position;
}

static bool rds_sync_buffer_is_sequence_found(const struct rds_block_stream_state *state) {
    size_t i_first;
    size_t i_second;
    const uint8_t third_offset = state->sync_offsets[7u];
    const uint32_t third_position = state->sync_positions[7u];

    for (i_first = 0; i_first < 6u; ++i_first) {
        for (i_second = i_first + 1u; i_second < 7u; ++i_second) {
            if (rds_sync_pulse_could_follow(third_offset, third_position,
                                            state->sync_offsets[i_second],
                                            state->sync_positions[i_second]) &&
                rds_sync_pulse_could_follow(
                    state->sync_offsets[i_second], state->sync_positions[i_second],
                    state->sync_offsets[i_first], state->sync_positions[i_first])) {
                return true;
            }
        }
    }

    return false;
}

static void rds_recent_error_push(struct rds_block_stream_state *state, bool had_error) {
    if (state->recent_block_errors_count == 50u) {
        state->recent_block_error_sum -=
            state->recent_block_errors[state->recent_block_errors_pos] ? 1u : 0u;
    } else {
        state->recent_block_errors_count += 1u;
    }

    state->recent_block_errors[state->recent_block_errors_pos] = had_error;
    state->recent_block_error_sum += had_error ? 1u : 0u;
    state->recent_block_errors_pos = (state->recent_block_errors_pos + 1u) % 50u;
}

static void rds_recent_error_clear(struct rds_block_stream_state *state) {
    memset(state->recent_block_errors, 0, sizeof(state->recent_block_errors));
    state->recent_block_errors_pos = 0u;
    state->recent_block_errors_count = 0u;
    state->recent_block_error_sum = 0u;
}

static void rds_acquire_sync(struct rds_block_stream_state *state, uint8_t offset,
                             uint32_t bit_position) {
    if (state->is_in_sync) {
        return;
    }

    if (offset != RDS_OFFSET_INVALID) {
        rds_sync_buffer_push(state, offset, bit_position);
        if (rds_sync_buffer_is_sequence_found(state)) {
            state->is_in_sync = true;
            state->sync_acquired_event = true;
            state->expected_offset = offset;
            memset(&state->current_group, 0, sizeof(state->current_group));
            rds_recent_error_clear(state);
        }
    }
}

static void rds_handle_ready_group(struct rds_block_stream_state *state) {
    state->ready_group = state->current_group;
    state->has_group_ready = true;
    state->groups_ready_count += 1u;
    memset(&state->current_group, 0, sizeof(state->current_group));
}

static void rds_find_block_in_register(struct rds_block_stream_state *state) {
    uint32_t raw = state->input_register & RDS_BLOCK_BITMASK;
    uint8_t offset = rds_get_offset_for_syndrome(rds_calculate_syndrome(raw));
    bool had_errors;
    uint16_t data;
    uint8_t corrected_bit_count = 0u;
    float block_confidence;

    rds_acquire_sync(state, offset, state->bit_count);

    if (!state->is_in_sync) {
        return;
    }

    state->total_blocks_seen += 1u;
    block_confidence = rds_get_block_confidence(state);

    if (state->expected_offset == RDS_OFFSET_C && offset == RDS_OFFSET_C_PRIME) {
        state->expected_offset = RDS_OFFSET_C_PRIME;
    }

    had_errors = (offset != state->expected_offset);
    rds_recent_error_push(state, had_errors);

    if (state->recent_block_error_sum > RDS_MAX_ERRORS_TOLERATED_OVER_50_BLOCKS) {
        state->is_in_sync = false;
        state->sync_lost_event = true;
        rds_recent_error_clear(state);
        return;
    }

    data = (uint16_t)(raw >> RDS_CHECKWORD_LENGTH);

    if (had_errors) {
        struct rds_error_correction_result correction =
            rds_correct_burst_errors(raw, state->expected_offset);

        if (!correction.succeeded) {
            correction = rds_correct_soft_errors(raw, state->expected_offset,
                                                 state->input_confidence_register);
        }

        if (correction.succeeded) {
            data = (uint16_t)(correction.corrected_bits >> RDS_CHECKWORD_LENGTH);
            offset = state->expected_offset;
            corrected_bit_count = correction.corrected_bit_count;
            state->corrected_blocks_seen += 1u;
        }
    }

    if (offset == state->expected_offset) {
        const uint8_t block_number = rds_get_block_number_for_offset(state->expected_offset);

        state->current_group.blocks[block_number].data = data;
        state->current_group.blocks[block_number].is_received = true;
        state->current_group.blocks[block_number].had_errors = had_errors;
        state->current_group.blocks[block_number].corrected_bit_count = corrected_bit_count;
        state->current_group.blocks[block_number].confidence = block_confidence;
        state->current_group.blocks[block_number].soft_reliable =
            had_errors && corrected_bit_count > 0u &&
            corrected_bit_count <= RDS_SOFT_RELIABLE_MAX_FLIPS &&
            block_confidence >= RDS_SOFT_RELIABLE_MIN_CONFIDENCE;
    }

    if (rds_get_next_offset_for(state->expected_offset) == RDS_OFFSET_A) {
        rds_handle_ready_group(state);
    }

    state->expected_offset = rds_get_next_offset_for(state->expected_offset);
}

void rds_block_stream_init(struct rds_block_stream_state *state) {
    memset(state, 0, sizeof(*state));
    state->bits_until_next_block = 1u;
}

void rds_block_stream_push_bit(struct rds_block_stream_state *state, bool bit) {
    rds_block_stream_push_bit_with_confidence(state, bit, 1.0f);
}

void rds_block_stream_push_bit_with_confidence(struct rds_block_stream_state *state, bool bit,
                                               float confidence) {
    size_t i;

    state->input_register = (state->input_register << 1u) | (bit ? 1u : 0u);
    for (i = 0u; i + 1u < RDS_BLOCK_LENGTH; ++i) {
        state->input_confidence_register[i] = state->input_confidence_register[i + 1u];
    }
    state->input_confidence_register[RDS_BLOCK_LENGTH - 1u] = confidence;
    state->bits_until_next_block -= 1u;
    state->bit_count += 1u;

    if (state->bits_until_next_block == 0u) {
        rds_find_block_in_register(state);
        state->bits_until_next_block = state->is_in_sync ? RDS_BLOCK_LENGTH : 1u;
    }
}

bool rds_block_stream_has_group_ready(const struct rds_block_stream_state *state) {
    return state->has_group_ready;
}

struct rds_group rds_block_stream_pop_group(struct rds_block_stream_state *state) {
    state->has_group_ready = false;
    return state->ready_group;
}

bool rds_block_stream_consume_sync_acquired(struct rds_block_stream_state *state) {
    const bool value = state->sync_acquired_event;

    state->sync_acquired_event = false;
    return value;
}

bool rds_block_stream_consume_sync_lost(struct rds_block_stream_state *state) {
    const bool value = state->sync_lost_event;

    state->sync_lost_event = false;
    return value;
}
