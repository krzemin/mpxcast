#include "stream/http.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool parse_frequency_hz(const char *text, uint32_t *frequency_hz) {
    const char *dot = strchr(text, '.');

    if (text[0] == '\0') {
        return false;
    }

    if (dot != NULL) {
        char *end = NULL;
        double mhz = strtod(text, &end);
        double hz;

        if (end == text || *end != '\0' || !isfinite(mhz) || mhz <= 0.0) {
            return false;
        }

        hz = mhz * 1000000.0;
        if (!isfinite(hz) || hz < 1.0 || hz > (double)UINT32_MAX) {
            return false;
        }

        *frequency_hz = (uint32_t)llround(hz);
        return true;
    }

    {
        char *end = NULL;
        unsigned long hz = strtoul(text, &end, 10);

        if (end == text || *end != '\0' || hz == 0ul || hz > (unsigned long)UINT32_MAX) {
            return false;
        }

        *frequency_hz = (uint32_t)hz;
        return true;
    }
}

static int split_container_extension(char *text, enum stream_container *container) {
    char *dot = strrchr(text, '.');

    *container = STREAM_CONTAINER_MPEGTS;
    if (dot == NULL) {
        return 0;
    }

    if (strcmp(dot, ".wav") == 0) {
        *dot = '\0';
        *container = STREAM_CONTAINER_WAV;
        return 0;
    }

    if (strcmp(dot, ".ts") == 0) {
        *dot = '\0';
        *container = STREAM_CONTAINER_MPEGTS;
        return 0;
    }

    return -1;
}

static bool parse_bool_parameter(const char *value, bool *enabled) {
    if (value == NULL || value[0] == '\0') {
        return true;
    }

    if (strcmp(value, "1") == 0) {
        *enabled = true;
        return true;
    }

    if (strcmp(value, "0") == 0) {
        *enabled = false;
        return true;
    }

    return false;
}

static bool parse_float_parameter(const char *value, float min_value, float *result) {
    char *end = NULL;
    float parsed;

    if (value == NULL || value[0] == '\0') {
        return true;
    }

    parsed = strtof(value, &end);
    if (end == value || *end != '\0' || !isfinite(parsed) || parsed < min_value) {
        return false;
    }

    *result = parsed;
    return true;
}

static bool format_default_station_name(uint32_t frequency_hz, char *buffer, size_t capacity) {
    char mhz_text[32];
    int length;

    length = snprintf(mhz_text, sizeof(mhz_text), "%.3f", (double)frequency_hz / 1000000.0);
    if (length < 0 || (size_t)length >= sizeof(mhz_text)) {
        return false;
    }

    while (length > 0 && mhz_text[length - 1] == '0') {
        mhz_text[--length] = '\0';
    }
    if (length > 0 && mhz_text[length - 1] == '.') {
        mhz_text[--length] = '\0';
    }

    length = snprintf(buffer, capacity, "%s MHz", mhz_text);
    return length >= 0 && (size_t)length < capacity;
}

static const char *lookup_first_parameter(stream_http_query_lookup lookup, void *lookup_context,
                                          const char *name, const char *alias) {
    const char *value = lookup(lookup_context, name);

    if (value == NULL && alias != NULL) {
        value = lookup(lookup_context, alias);
    }

    return value;
}

int stream_http_parse_request(const char *method, const char *url, bool icy_metadata_requested,
                              const struct stream_http_defaults *defaults,
                              stream_http_query_lookup lookup, void *lookup_context,
                              struct stream_http_request *request, int *status_code,
                              const char **reason) {
    char frequency_text[64];
    const char *path_frequency;
    const char *value;

    request->icy_metadata_requested = icy_metadata_requested;
    request->mode = defaults->mode;
    request->rds_enabled = defaults->rds_enabled;
    request->volume_gain = defaults->volume_gain;
    request->deemphasis_tau_us = defaults->deemphasis_tau_us;
    request->requested_station_name[0] = '\0';

    if (strcmp(method, "GET") != 0) {
        *status_code = 405;
        *reason = "Method Not Allowed";
        return -1;
    }

    if (url[0] != '/') {
        *status_code = 404;
        *reason = "Not Found";
        return -1;
    }

    path_frequency = url + 1;
    if (*path_frequency == '\0' || strchr(path_frequency, '/') != NULL ||
        strlen(path_frequency) >= sizeof(frequency_text)) {
        *status_code = 404;
        *reason = "Not Found";
        return -1;
    }

    strncpy(frequency_text, path_frequency, sizeof(frequency_text) - 1u);
    frequency_text[sizeof(frequency_text) - 1u] = '\0';

    if (split_container_extension(frequency_text, &request->container) != 0 ||
        !parse_frequency_hz(frequency_text, &request->frequency_hz)) {
        *status_code = 404;
        *reason = "Not Found";
        return -1;
    }

    {
        bool stereo_enabled = request->mode == STREAM_MODE_STEREO;

        value = lookup_first_parameter(lookup, lookup_context, "stereo", NULL);
        if (!parse_bool_parameter(value, &stereo_enabled)) {
            *status_code = 400;
            *reason = "Bad Request";
            return -1;
        }
        request->mode = stereo_enabled ? STREAM_MODE_STEREO : STREAM_MODE_MONO;
    }

    value = lookup_first_parameter(lookup, lookup_context, "rds", NULL);
    if (!parse_bool_parameter(value, &request->rds_enabled)) {
        *status_code = 400;
        *reason = "Bad Request";
        return -1;
    }

    value = lookup_first_parameter(lookup, lookup_context, "volume-gain", "vol");
    if (!parse_float_parameter(value, 0.0f, &request->volume_gain) ||
        request->volume_gain == 0.0f) {
        *status_code = 400;
        *reason = "Bad Request";
        return -1;
    }

    value = lookup_first_parameter(lookup, lookup_context, "deemphasis-tau", "deemphasis");
    if (!parse_float_parameter(value, 0.0f, &request->deemphasis_tau_us)) {
        *status_code = 400;
        *reason = "Bad Request";
        return -1;
    }

    value = lookup_first_parameter(lookup, lookup_context, "name", NULL);
    if (value != NULL) {
        strncpy(request->requested_station_name, value,
                sizeof(request->requested_station_name) - 1u);
        request->requested_station_name[sizeof(request->requested_station_name) - 1u] = '\0';
    }

    request->explicit_station_name = request->requested_station_name[0] != '\0';
    if (!request->explicit_station_name &&
        !format_default_station_name(request->frequency_hz, request->requested_station_name,
                                     sizeof(request->requested_station_name))) {
        *status_code = 500;
        *reason = "Internal Server Error";
        return -1;
    }

    return 0;
}
