#ifndef MPXCAST_INPUT_RTL_RTL_IQ_DECIMATOR_H
#define MPXCAST_INPUT_RTL_RTL_IQ_DECIMATOR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "input/rtl/rtl_source.h"

#define RTL_IQ_DECIMATOR_INPUT_RATE_HZ 1920000.0
#define RTL_IQ_DECIMATOR_OUTPUT_RATE_HZ 240000.0
#define RTL_IQ_DECIMATOR_FACTOR 8u
#define RTL_IQ_DECIMATOR_CUTOFF_HZ 120000.0
#define RTL_IQ_DECIMATOR_TAP_COUNT 16u
/* Invariant: this exact output block must match the FM pipeline DSP block size. */
#define RTL_IQ_DECIMATOR_OUTPUT_BLOCK_SAMPLES 5120u
#define RTL_IQ_DECIMATOR_BLOCK_INPUT_BYTES                                                         \
    (RTL_IQ_DECIMATOR_OUTPUT_BLOCK_SAMPLES * RTL_IQ_DECIMATOR_FACTOR * 2u)

struct rtl_iq_decimator_taps {
    bool configured;
    float values[RTL_IQ_DECIMATOR_TAP_COUNT];
};

struct rtl_iq_decimator_state {
    bool configured;
    struct rtl_iq_decimator_taps taps;
    float history_in_phase[RTL_IQ_DECIMATOR_TAP_COUNT * 2u];
    float history_quadrature[RTL_IQ_DECIMATOR_TAP_COUNT * 2u];
    size_t history_write_index;
    unsigned char input_bytes[RTL_IQ_DECIMATOR_BLOCK_INPUT_BYTES];
};

void rtl_iq_decimator_init(struct rtl_iq_decimator_state *state);
void rtl_iq_decimator_configure(struct rtl_iq_decimator_state *state);
int rtl_iq_decimator_read_block_float(struct rtl_iq_decimator_state *state,
                                      struct rtl_source *source, float *restrict output_iq);

#endif
