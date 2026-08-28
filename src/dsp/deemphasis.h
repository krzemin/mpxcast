#ifndef MPXCAST_DSP_DEEMPHASIS_H
#define MPXCAST_DSP_DEEMPHASIS_H

#include <stddef.h>

struct deemphasis_state {
    float alpha;
    float output;
};

void deemphasis_init(struct deemphasis_state *state, float sample_rate, float tau_seconds);
void deemphasis_process_buffer(struct deemphasis_state *state, float *samples, size_t sample_count);

#endif
