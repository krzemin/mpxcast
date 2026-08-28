#ifndef MPXCAST_DSP_FIR_H
#define MPXCAST_DSP_FIR_H

#include <stdbool.h>
#include <stddef.h>

#define FIR_MAX_TAPS 128u

struct fir_state {
    bool configured;
    size_t tap_count;
    size_t pair_count;
    float taps[FIR_MAX_TAPS];
    float history[FIR_MAX_TAPS * 2u];
    size_t write_index;
};

struct fir_decimator_state {
    bool configured;
    unsigned int factor;
    unsigned int phase;
    struct fir_state fir;
};

void fir_init(struct fir_state *state);
void fir_configure(struct fir_state *state, const float *taps, size_t tap_count);
void fir_execute_block(struct fir_state *state, const float *restrict input, size_t sample_count,
                       float *restrict output);

void fir_decimator_init(struct fir_decimator_state *state);
void fir_decimator_configure(struct fir_decimator_state *state, unsigned int factor,
                             const float *taps, size_t tap_count);
size_t fir_decimator_process_block(struct fir_decimator_state *state, const float *restrict input,
                                   size_t input_sample_count, float *restrict output,
                                   size_t max_output_sample_count);
size_t fir_audio_decimator_process_block(struct fir_decimator_state *state,
                                         const float *restrict input, size_t input_sample_count,
                                         float *restrict output, size_t max_output_sample_count);

#endif
