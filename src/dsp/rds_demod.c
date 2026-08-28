#include "dsp/rds_demod.h"

#include <complex.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include <liquid/liquid.h>

#include "core/logging.h"
#include "core/math_constants.h"
#include "rds/printer.h"

#define RDS_SUBCARRIER_HZ 57000.0f
#define RDS_BASEBAND_CUTOFF_HZ 2400.0f

static void rds_demod_reset_parser_for_phase(struct rds_demod_state *state) {
    rds_parser_init(&state->parser);
}

static void rds_demod_activate_phase(struct rds_demod_state *state, int phase_index,
                                     const char *reason) {
    if (state->active_phase == phase_index) {
        return;
    }

    if (state->active_phase < 0) {
        TRACE("RDS lock: phase=%d reason=%s", phase_index, reason);
    } else {
        TRACE("RDS re-lock: from=%d to=%d reason=%s", state->active_phase, phase_index, reason);
    }

    state->active_phase = phase_index;
    /* Preserve parser state across re-locks instead of clearing partial PS/RT. */
}

static float rds_clamp(float value, float min_value, float max_value) {
    if (value < min_value) {
        return min_value;
    }
    if (value > max_value) {
        return max_value;
    }
    return value;
}

static void rds_carrier_correct_sample(struct rds_demod_state *state, float *in_phase,
                                       float *quadrature) {
    const float rotation_i = state->carrier_rotation_i;
    const float rotation_q = state->carrier_rotation_q;
    const float corrected_i = (*in_phase * rotation_i) - (*quadrature * rotation_q);
    const float corrected_q = (*in_phase * rotation_q) + (*quadrature * rotation_i);
    const float energy = (corrected_i * corrected_i) + (corrected_q * corrected_q) + 1.0e-6f;
    const float error = rds_clamp((corrected_i * corrected_q) / energy, -1.0f, 1.0f);

    *in_phase = corrected_i;
    *quadrature = corrected_q;

    /*
     * Apply a light BPSK-style carrier loop so residual phase/frequency drift
     * from the 57 kHz downmix does not keep rotating the RDS symbols.
     */
    state->carrier_frequency += RDS_CARRIER_LOOP_BETA * error;
    {
        const float delta = state->carrier_frequency + (RDS_CARRIER_LOOP_ALPHA * error);
        const float step_i = 1.0f - (0.5f * delta * delta);
        const float step_q = -delta;
        const float next_rotation_i = (rotation_i * step_i) - (rotation_q * step_q);
        const float next_rotation_q = (rotation_i * step_q) + (rotation_q * step_i);

        state->carrier_rotation_i = next_rotation_i;
        state->carrier_rotation_q = next_rotation_q;
    }

    state->carrier_rotation_renorm_count += 1u;
    if (state->carrier_rotation_renorm_count == RDS_CARRIER_ROTATOR_RENORM_PERIOD) {
        const float magnitude = sqrtf((state->carrier_rotation_i * state->carrier_rotation_i) +
                                      (state->carrier_rotation_q * state->carrier_rotation_q));

        if (magnitude > 0.0f) {
            state->carrier_rotation_i /= magnitude;
            state->carrier_rotation_q /= magnitude;
        } else {
            state->carrier_rotation_i = 1.0f;
            state->carrier_rotation_q = 0.0f;
        }
        state->carrier_rotation_renorm_count = 0u;
    }
}

static void rds_demod_process_symbol_sample(struct rds_demod_state *state, float symbol_i,
                                            float symbol_q) {
    struct rds_phase_state *phase = &state->phases[0];

    if (phase->phase_score > 0u) {
        phase->phase_score -= 1u;
    }

    if (phase->have_prev_symbol) {
        const float biphase_symbol_i = (symbol_i - phase->prev_symbol_i) * 0.5f;
        const bool biphase_bit = biphase_symbol_i >= 0.0f;
        bool have_decoded_bit = false;
        bool decoded_bit = false;

        phase->biphase_clock_history[phase->biphase_clock] = fabsf(biphase_symbol_i);

        if ((phase->biphase_clock % 2u) == phase->biphase_clock_polarity) {
            if (phase->have_prev_biphase_bit) {
                decoded_bit = biphase_bit != phase->prev_biphase_bit;
                have_decoded_bit = true;
            }
            phase->prev_biphase_bit = biphase_bit;
            phase->have_prev_biphase_bit = 1;
        }

        phase->biphase_clock += 1u;
        if (phase->biphase_clock == RDS_BIPHASE_CLOCK_HISTORY_LENGTH) {
            float even_sum = 0.0f;
            float odd_sum = 0.0f;
            size_t i;

            for (i = 0u; i < RDS_BIPHASE_CLOCK_HISTORY_LENGTH; i += 2u) {
                even_sum += phase->biphase_clock_history[i];
                odd_sum += phase->biphase_clock_history[i + 1u];
            }

            if (even_sum > odd_sum) {
                phase->biphase_clock_polarity = 0u;
            } else if (odd_sum > even_sum) {
                phase->biphase_clock_polarity = 1u;
            }

            memset(phase->biphase_clock_history, 0, sizeof(phase->biphase_clock_history));
            phase->biphase_clock = 0u;
        }

        if (!have_decoded_bit) {
            phase->prev_symbol_i = symbol_i;
            phase->prev_symbol_q = symbol_q;
            phase->have_prev_symbol = 1;
            return;
        }

        rds_block_stream_push_bit_with_confidence(&phase->block_stream, decoded_bit,
                                                  fabsf(biphase_symbol_i));

        if (rds_block_stream_consume_sync_acquired(&phase->block_stream)) {
            TRACE("RDS sync acquired: phase=0");
            if (phase->phase_score < RDS_PHASE_SCORE_MAX - 16u) {
                phase->phase_score += 16u;
            } else {
                phase->phase_score = RDS_PHASE_SCORE_MAX;
            }
            if (state->active_phase < 0) {
                rds_demod_activate_phase(state, 0, "liquid-symsync");
            }
        }

        if (rds_block_stream_consume_sync_lost(&phase->block_stream)) {
            TRACE("RDS sync lost: phase=0");
            phase->phase_score = 0u;
            if (state->active_phase == 0) {
                state->active_phase = -1;
                /* Keep parser state even when the timing loop loses sync temporarily. */
            }
        }

        while (rds_block_stream_has_group_ready(&phase->block_stream)) {
            struct rds_group group = rds_block_stream_pop_group(&phase->block_stream);
            struct rds_station_event event;
            uint8_t corrected_blocks = 0u;

            corrected_blocks = (uint8_t)(group.blocks[0].had_errors ? 1u : 0u) +
                               (uint8_t)(group.blocks[1].had_errors ? 1u : 0u) +
                               (uint8_t)(group.blocks[2].had_errors ? 1u : 0u) +
                               (uint8_t)(group.blocks[3].had_errors ? 1u : 0u);
            if (phase->phase_score < RDS_PHASE_SCORE_MAX - (corrected_blocks == 0u ? 8u : 4u)) {
                phase->phase_score += corrected_blocks == 0u ? 8u : 4u;
            } else {
                phase->phase_score = RDS_PHASE_SCORE_MAX;
            }

            if (rds_parser_process_group(&state->parser, &group, &event) &&
                logging_get_level() == LOG_TRACE) {
                rds_printer_print_event(&event);
            }
        }
    }

    phase->prev_symbol_i = symbol_i;
    phase->prev_symbol_q = symbol_q;
    phase->have_prev_symbol = 1;
}

static firdecim_crcf rds_create_baseband_decimator(float sample_rate) {
    float taps[RDS_FIR_TAP_COUNT];

    liquid_firdes_kaiser(RDS_FIR_TAP_COUNT, RDS_BASEBAND_CUTOFF_HZ / sample_rate,
                         RDS_FIR_STOPBAND_AS_DB, 0.0f, taps);
    return firdecim_crcf_create(RDS_DECIMATION_FACTOR, taps, RDS_FIR_TAP_COUNT);
}

static symsync_crcf rds_create_symbol_sync(void) {
    return symsync_crcf_create_rnyquist(LIQUID_FIRFILT_RRC, RDS_SYMBOL_PHASES,
                                        RDS_SYMSYNC_FILTER_DELAY, RDS_SYMSYNC_BETA,
                                        RDS_SYMSYNC_FILTER_BANKS);
}

void rds_demod_configure(struct rds_demod_state *state, float sample_rate) {
    if (state->configured_input_rate == sample_rate) {
        return;
    }

    {
        const float step = (PI2_F * RDS_SUBCARRIER_HZ) / sample_rate;

        state->configured_input_rate = sample_rate;
        if (state->subcarrier_nco == NULL) {
            state->subcarrier_nco = nco_crcf_create(LIQUID_NCO);
        } else {
            nco_crcf_reset((nco_crcf)state->subcarrier_nco);
        }
        nco_crcf_set_frequency((nco_crcf)state->subcarrier_nco, step);
        nco_crcf_set_phase((nco_crcf)state->subcarrier_nco, 0.0f);
        memset(state->decimator_input, 0, sizeof(state->decimator_input));
        state->decimator_input_count = 0u;
        state->carrier_frequency = 0.0f;
        state->carrier_rotation_i = 1.0f;
        state->carrier_rotation_q = 0.0f;
        state->carrier_rotation_renorm_count = 0u;
        if (state->baseband_decimator != NULL) {
            firdecim_crcf_destroy((firdecim_crcf)state->baseband_decimator);
        }
        state->baseband_decimator = rds_create_baseband_decimator(sample_rate);
        if (state->symbol_sync == NULL) {
            /*
             * The RDS baseband arrives here at 7.125 kS/s, exactly 3 samples
             * per 2375 symbols/s PSK symbol. liquid-dsp's symsync follows
             * redsea's shape here: recover PSK symbols first, then biphase and
             * delta-decode them into RDS bits.
             */
            state->symbol_sync = rds_create_symbol_sync();
            symsync_crcf_set_output_rate((symsync_crcf)state->symbol_sync, 1u);
            symsync_crcf_set_lf_bw((symsync_crcf)state->symbol_sync, RDS_SYMSYNC_LOOP_BW);
        } else {
            symsync_crcf_reset((symsync_crcf)state->symbol_sync);
        }
        if (state->agc == NULL) {
            state->agc = agc_crcf_create();
            agc_crcf_set_bandwidth((agc_crcf)state->agc, RDS_AGC_BW);
            agc_crcf_set_gain((agc_crcf)state->agc, RDS_AGC_INITIAL_GAIN);
        } else {
            agc_crcf_reset((agc_crcf)state->agc);
            agc_crcf_set_gain((agc_crcf)state->agc, RDS_AGC_INITIAL_GAIN);
        }
    }
    state->active_phase = -1;
    rds_demod_reset_parser_for_phase(state);
}

void rds_demod_init(struct rds_demod_state *state) {
    size_t i;

    memset(state, 0, sizeof(*state));
    state->active_phase = -1;

    for (i = 0; i < RDS_SYMBOL_PHASES; ++i) {
        rds_block_stream_init(&state->phases[i].block_stream);
    }
}

void rds_demod_destroy(struct rds_demod_state *state) {
    if (state->subcarrier_nco != NULL) {
        nco_crcf_destroy((nco_crcf)state->subcarrier_nco);
    }
    if (state->baseband_decimator != NULL) {
        firdecim_crcf_destroy((firdecim_crcf)state->baseband_decimator);
    }
    if (state->symbol_sync != NULL) {
        symsync_crcf_destroy((symsync_crcf)state->symbol_sync);
    }
    if (state->agc != NULL) {
        agc_crcf_destroy((agc_crcf)state->agc);
    }

    memset(state, 0, sizeof(*state));
}

void rds_demod_process_mpx_block(struct rds_demod_state *state, const float *mpx_samples,
                                 size_t sample_count) {
    size_t resampled_count = 0u;
    liquid_float_complex symbol_sync_input[RDS_MAX_RESAMPLED_BLOCK_SAMPLES];
    liquid_float_complex symbol_sync_output[RDS_MAX_RESAMPLED_BLOCK_SAMPLES];
    size_t i;

    if (sample_count == 0u || sample_count > RDS_MAX_INPUT_SAMPLES) {
        return;
    }

    for (i = 0; i < sample_count; ++i) {
        const liquid_float_complex input = (liquid_float_complex)mpx_samples[i];
        liquid_float_complex mixed;

        nco_crcf_mix_down((nco_crcf)state->subcarrier_nco, input, &mixed);
        nco_crcf_step((nco_crcf)state->subcarrier_nco);

        state->decimator_input[state->decimator_input_count] = mixed;
        state->decimator_input_count += 1u;
        if (state->decimator_input_count == RDS_DECIMATION_FACTOR) {
            liquid_float_complex decimator_output;

            firdecim_crcf_execute((firdecim_crcf)state->baseband_decimator, state->decimator_input,
                                  &decimator_output);
            state->decimator_input_count = 0u;
            if (resampled_count < RDS_MAX_RESAMPLED_BLOCK_SAMPLES) {
                liquid_float_complex agc_input;
                liquid_float_complex agc_output;
                float corrected_i;
                float corrected_q;

                agc_input = decimator_output;
                agc_crcf_execute((agc_crcf)state->agc, agc_input, &agc_output);
                corrected_i = crealf(agc_output);
                corrected_q = cimagf(agc_output);
                rds_carrier_correct_sample(state, &corrected_i, &corrected_q);
                symbol_sync_input[resampled_count] =
                    (liquid_float_complex)(corrected_i + (corrected_q * I));
                resampled_count += 1u;
            }
        }
    }

    if (resampled_count == 0u) {
        return;
    }

    if (state->symbol_sync != NULL) {
        unsigned int symbol_count = 0u;

        symsync_crcf_execute((symsync_crcf)state->symbol_sync, symbol_sync_input,
                             (unsigned int)resampled_count, symbol_sync_output, &symbol_count);

        if (logging_get_level() == LOG_TRACE && getenv("RDS_DEBUG_LIQUID") != NULL &&
            symbol_count > 0u) {
            TRACE("RDS liquid sync: symbols=%u tau=%f", symbol_count,
                  symsync_crcf_get_tau((symsync_crcf)state->symbol_sync));
        }

        for (i = 0u; i < (size_t)symbol_count; ++i) {
            rds_demod_process_symbol_sample(state, crealf(symbol_sync_output[i]),
                                            cimagf(symbol_sync_output[i]));
        }
    }
}
