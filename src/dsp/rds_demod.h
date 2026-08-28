#ifndef MPXCAST_DSP_RDS_DEMOD_H
#define MPXCAST_DSP_RDS_DEMOD_H

#include <complex.h>
#include <stdbool.h>
#include <stddef.h>

#include "rds/block_sync.h"
#include "rds/parser.h"

#define RDS_DECIMATION_FACTOR 24u
#define RDS_SYMBOL_PHASES 3u
/*
 * Expected geometry:
 * - 5120 MPX samples at 240 kHz become 3648 samples at 171 kHz
 * - 3648 / 24 = 152 baseband samples into liquid's symbol sync
 *
 * Keep a small slack margin for resampler output rounding and carried remainder.
 */
#define RDS_INPUT_BLOCK_SAMPLES 3648u
#define RDS_INPUT_BLOCK_SLACK_SAMPLES 16u
#define RDS_MAX_INPUT_SAMPLES (RDS_INPUT_BLOCK_SAMPLES + RDS_INPUT_BLOCK_SLACK_SAMPLES)
#define RDS_MAX_RESAMPLED_BLOCK_SAMPLES                                                            \
    ((RDS_MAX_INPUT_SAMPLES + RDS_DECIMATION_FACTOR - 1u) / RDS_DECIMATION_FACTOR)
#define RDS_RESAMPLED_RATE 7125.0f
#define RDS_FIR_TAP_COUNT 255u
#define RDS_FIR_STOPBAND_AS_DB 70.0f
#define RDS_PHASE_SCORE_MAX 255u
#define RDS_BIPHASE_CLOCK_HISTORY_LENGTH 128u
#define RDS_SYMSYNC_FILTER_DELAY 3u
#define RDS_SYMSYNC_FILTER_BANKS 32u
#define RDS_SYMSYNC_BETA 0.8f
#define RDS_SYMSYNC_LOOP_BW 0.012865f
#define RDS_CARRIER_LOOP_ALPHA 0.0040f
#define RDS_CARRIER_LOOP_BETA 0.00006f
#define RDS_CARRIER_ROTATOR_RENORM_PERIOD 32u
#define RDS_AGC_BW 0.0045f
#define RDS_AGC_INITIAL_GAIN 0.12f

struct rds_phase_state {
    struct rds_block_stream_state block_stream;
    float prev_symbol_i;
    float prev_symbol_q;
    uint32_t phase_score;
    float biphase_clock_history[RDS_BIPHASE_CLOCK_HISTORY_LENGTH];
    uint32_t biphase_clock;
    uint32_t biphase_clock_polarity;
    int have_prev_biphase_bit;
    bool prev_biphase_bit;
    int have_prev_symbol;
};

struct rds_demod_state {
    float configured_input_rate;
    void *subcarrier_nco;
    float complex decimator_input[RDS_DECIMATION_FACTOR];
    size_t decimator_input_count;
    float carrier_frequency;
    float carrier_rotation_i;
    float carrier_rotation_q;
    uint32_t carrier_rotation_renorm_count;
    void *baseband_decimator;
    void *symbol_sync;
    void *agc;
    int active_phase;
    struct rds_phase_state phases[RDS_SYMBOL_PHASES];
    struct rds_parser_state parser;
};

void rds_demod_init(struct rds_demod_state *state);
void rds_demod_destroy(struct rds_demod_state *state);
void rds_demod_configure(struct rds_demod_state *state, float sample_rate);
void rds_demod_process_mpx_block(struct rds_demod_state *state, const float *mpx_samples,
                                 size_t sample_count);

#endif
