#ifndef MPXCAST_RDS_BLOCK_SYNC_H
#define MPXCAST_RDS_BLOCK_SYNC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum rds_block_number {
    RDS_BLOCK_NUMBER_1 = 0,
    RDS_BLOCK_NUMBER_2 = 1,
    RDS_BLOCK_NUMBER_3 = 2,
    RDS_BLOCK_NUMBER_4 = 3
};

struct rds_block {
    uint16_t data;
    bool is_received;
    bool had_errors;
    bool soft_reliable;
    uint8_t corrected_bit_count;
    float confidence;
};

struct rds_group {
    struct rds_block blocks[4];
};

struct rds_block_stream_state {
    uint32_t input_register;
    float input_confidence_register[26];
    uint32_t bit_count;
    uint32_t bits_until_next_block;
    bool is_in_sync;
    bool sync_acquired_event;
    bool sync_lost_event;
    uint8_t expected_offset;
    struct rds_group current_group;
    struct rds_group ready_group;
    bool has_group_ready;
    uint64_t total_blocks_seen;
    uint64_t corrected_blocks_seen;
    uint64_t groups_ready_count;
    bool recent_block_errors[50];
    size_t recent_block_errors_pos;
    size_t recent_block_errors_count;
    size_t recent_block_error_sum;
    uint8_t sync_offsets[8];
    uint32_t sync_positions[8];
};

void rds_block_stream_init(struct rds_block_stream_state *state);
void rds_block_stream_push_bit(struct rds_block_stream_state *state, bool bit);
void rds_block_stream_push_bit_with_confidence(struct rds_block_stream_state *state, bool bit,
                                               float confidence);
bool rds_block_stream_has_group_ready(const struct rds_block_stream_state *state);
struct rds_group rds_block_stream_pop_group(struct rds_block_stream_state *state);
bool rds_block_stream_consume_sync_acquired(struct rds_block_stream_state *state);
bool rds_block_stream_consume_sync_lost(struct rds_block_stream_state *state);

#endif
