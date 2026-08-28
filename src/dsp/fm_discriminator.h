#ifndef MPXCAST_DSP_FM_DISCRIMINATOR_H
#define MPXCAST_DSP_FM_DISCRIMINATOR_H

#include <stddef.h>

enum fm_discriminator_impl {
    FM_DISCRIMINATOR_IMPL_APPROX = 0,
    FM_DISCRIMINATOR_IMPL_ATAN2_APPROX = 1,
    FM_DISCRIMINATOR_IMPL_ATAN2_LIBM = 2
};

struct fm_discriminator_state;

typedef void (*fm_discriminator_process_block_fn)(struct fm_discriminator_state *state,
                                                  const float *restrict input_iq,
                                                  size_t sample_count,
                                                  float *restrict output_demodulated);

struct fm_discriminator_state {
    float prev_i;
    float prev_q;
    enum fm_discriminator_impl impl;
    fm_discriminator_process_block_fn process_block;
    int have_prev;
};

void fm_discriminator_init(struct fm_discriminator_state *state);
void fm_discriminator_set_impl(struct fm_discriminator_state *state,
                               enum fm_discriminator_impl impl);
void fm_discriminator_process_block_float(struct fm_discriminator_state *state,
                                          const float *restrict input_iq, size_t sample_count,
                                          float *restrict output_demodulated);

#endif
