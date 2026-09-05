#ifndef MPXCAST_STREAM_SERVER_H
#define MPXCAST_STREAM_SERVER_H

#include <stdbool.h>
#include <stdint.h>

#include "dsp/fm_discriminator.h"
#include "stream/http.h"

struct server_config {
    const char *bind_host;
    const char *bind_port;
    const char *input_file;
    uint32_t device_index;
    enum fm_discriminator_impl demod_math;
    float fm_quality_interval_seconds; /* Zero disables measurement. */
    struct stream_http_defaults stream_defaults;
};

int server_run(const struct server_config *config);

#endif
