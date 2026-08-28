#ifndef MPXCAST_STREAM_HTTP_H
#define MPXCAST_STREAM_HTTP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "stream/icy.h"

enum stream_mode { STREAM_MODE_MONO = 0, STREAM_MODE_STEREO = 1 };

enum stream_container { STREAM_CONTAINER_WAV = 0, STREAM_CONTAINER_MPEGTS = 1 };

struct stream_http_request {
    bool icy_metadata_requested;
    bool rds_enabled;
    bool explicit_station_name;
    uint32_t frequency_hz;
    float volume_gain;
    float deemphasis_tau_us;
    enum stream_mode mode;
    enum stream_container container;
    char requested_station_name[ICY_NAME_MAX_LENGTH + 1u];
};

struct stream_http_defaults {
    bool rds_enabled;
    float volume_gain;
    float deemphasis_tau_us;
    enum stream_mode mode;
};

typedef const char *(*stream_http_query_lookup)(void *context, const char *name);

int stream_http_parse_request(const char *method, const char *url, bool icy_metadata_requested,
                              const struct stream_http_defaults *defaults,
                              stream_http_query_lookup lookup, void *lookup_context,
                              struct stream_http_request *request, int *status_code,
                              const char **reason);

#endif
