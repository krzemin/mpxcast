#include "core/app.h"

#include <getopt.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/logging.h"
#include "core/version.h"
#include "input/rtl/rtl_source.h"
#include "stream/server.h"

#define DEFAULT_BIND_HOST "0.0.0.0"
#define DEFAULT_BIND_PORT "2347"
#define DEFAULT_DEVICE_INDEX 0u
#define DEFAULT_STEREO_ENABLED true
#define DEFAULT_RDS_ENABLED true
#define DEFAULT_DEMOD_MATH FM_DISCRIMINATOR_IMPL_ATAN2_LIBM
#define DEFAULT_VOLUME_GAIN 1.0f
#define DEFAULT_DEEMPHASIS_TAU_US 50.0f

enum parse_cli_result {
    PARSE_CLI_RESULT_OK = 0,
    PARSE_CLI_RESULT_EXIT_SUCCESS = 1,
    PARSE_CLI_RESULT_ERROR = 2
};

static void print_usage(const char *program_name);
static enum parse_cli_result parse_cli_options(int argc, char **argv, struct server_config *config,
                                               enum logging_level *log_level);
static void print_config(const struct server_config *config, enum logging_level log_level);
static bool parse_bool_option(const char *value, bool *result);
static bool parse_float_option(const char *value, float min_value, float *result);
static bool parse_device_index_option(const char *value, uint32_t *result);
static bool parse_demod_math_option(const char *value, enum fm_discriminator_impl *result);
static bool parse_log_level_option(const char *value, enum logging_level *result);
static bool valid_port(const char *value);
static const char *demod_math_name(enum fm_discriminator_impl impl);
static const char *demod_math_options(void);
static const char *log_level_name(enum logging_level log_level);

int app_run(int argc, char **argv) {
    struct server_config config;
    enum parse_cli_result parse_result;
    enum logging_level log_level = LOG_INFO;

    memset(&config, 0, sizeof(config));
    config.bind_host = DEFAULT_BIND_HOST;
    config.bind_port = DEFAULT_BIND_PORT;
    config.device_index = DEFAULT_DEVICE_INDEX;
    config.demod_math = DEFAULT_DEMOD_MATH;
    config.stream_defaults.rds_enabled = DEFAULT_RDS_ENABLED;
    config.stream_defaults.volume_gain = DEFAULT_VOLUME_GAIN;
    config.stream_defaults.deemphasis_tau_us = DEFAULT_DEEMPHASIS_TAU_US;
    config.stream_defaults.mode = DEFAULT_STEREO_ENABLED ? STREAM_MODE_STEREO : STREAM_MODE_MONO;

    parse_result = parse_cli_options(argc, argv, &config, &log_level);
    if (parse_result == PARSE_CLI_RESULT_EXIT_SUCCESS) {
        return 0;
    }
    if (parse_result != PARSE_CLI_RESULT_OK) {
        return 1;
    }
    logging_set_level(log_level);
    INFO("mpxcast %s", MPXCAST_VERSION);
    if (config.input_file == NULL) {
        rtl_source_log_devices();
    }
    print_config(&config, log_level);

    return server_run(&config);
}

static void print_usage(const char *program_name) {
    fprintf(stderr,
            "Usage: %s [options]\n"
            "\n"
            "Options:\n"
            "  -H, --host <host>              Listen host [%s]\n"
            "  -p, --port <port>              Listen port [%s]\n"
            "  -d, --device <index>           RTL-SDR device index [%u]\n"
            "  -f, --input-file <path>        Loop raw 1.92Msps unsigned 8-bit IQ file\n"
            "  -s, --stereo <0|1>             Stereo mode [%u]\n"
            "  -r, --rds <0|1>                RDS mode [%u]\n"
            "  -m, --demod-math <name>        FM demod math: %s [%s]\n"
            "  -g, --volume-gain <gain>       PCM volume gain [%.3g]\n"
            "  -t, --deemphasis-tau <usec>    De-emphasis tau; 0 disables it [%.3g]\n"
            "  -v, --verbose                  Increase verbosity (-v: debug, -vv: trace)\n"
            "      --log-level <level>         Set level: trace, debug, info, warn, error\n"
            "  -V, --version                  Show version\n"
            "  -h, --help                     Show this help\n",
            program_name, DEFAULT_BIND_HOST, DEFAULT_BIND_PORT, DEFAULT_DEVICE_INDEX,
            DEFAULT_STEREO_ENABLED ? 1u : 0u, DEFAULT_RDS_ENABLED ? 1u : 0u, demod_math_options(),
            demod_math_name(DEFAULT_DEMOD_MATH), DEFAULT_VOLUME_GAIN, DEFAULT_DEEMPHASIS_TAU_US);
}

static void print_config(const struct server_config *config, enum logging_level log_level) {
    char input_description[1024];

    if (config->input_file != NULL) {
        snprintf(input_description, sizeof(input_description), "file=\"%s\"", config->input_file);
    } else {
        snprintf(input_description, sizeof(input_description), "device=%u", config->device_index);
    }

    if (config->stream_defaults.deemphasis_tau_us == 0.0f) {
        INFO("Configuration: listen=%s:%s input=%s mode=%s rds=%s gain=%.3g de-emphasis=off "
             "demod=%s log-level=%s",
             config->bind_host, config->bind_port, input_description,
             config->stream_defaults.mode == STREAM_MODE_STEREO ? "stereo" : "mono",
             config->stream_defaults.rds_enabled ? "on" : "off",
             config->stream_defaults.volume_gain, demod_math_name(config->demod_math),
             log_level_name(log_level));
    } else {
        INFO("Configuration: listen=%s:%s input=%s mode=%s rds=%s gain=%.3g de-emphasis=%.3gus "
             "demod=%s log-level=%s",
             config->bind_host, config->bind_port, input_description,
             config->stream_defaults.mode == STREAM_MODE_STEREO ? "stereo" : "mono",
             config->stream_defaults.rds_enabled ? "on" : "off",
             config->stream_defaults.volume_gain, config->stream_defaults.deemphasis_tau_us,
             demod_math_name(config->demod_math), log_level_name(log_level));
    }
}

static enum parse_cli_result parse_cli_options(int argc, char **argv, struct server_config *config,
                                               enum logging_level *log_level) {
    bool device_option_seen = false;
    bool input_file_option_seen = false;
    bool log_level_explicit = false;
    unsigned int verbosity = 0u;
    int option;

    for (;;) {
        static const struct option long_options[] = {
            {"host", required_argument, NULL, 'H'},
            {"port", required_argument, NULL, 'p'},
            {"device", required_argument, NULL, 'd'},
            {"input-file", required_argument, NULL, 'f'},
            {"stereo", required_argument, NULL, 's'},
            {"rds", required_argument, NULL, 'r'},
            {"demod-math", required_argument, NULL, 'm'},
            {"volume-gain", required_argument, NULL, 'g'},
            {"deemphasis-tau", required_argument, NULL, 't'},
            {"verbose", no_argument, NULL, 'v'},
            {"log-level", required_argument, NULL, 'L'},
            {"version", no_argument, NULL, 'V'},
            {"help", no_argument, NULL, 'h'},
            {NULL, 0, NULL, 0},
        };

        option = getopt_long(argc, argv, "H:p:d:f:s:r:m:g:t:vL:Vh", long_options, NULL);
        if (option < 0) {
            break;
        }

        switch (option) {
        case 'H':
            config->bind_host = optarg;
            break;
        case 'p':
            if (!valid_port(optarg)) {
                ERROR("Invalid port: %s", optarg);
                return PARSE_CLI_RESULT_ERROR;
            }
            config->bind_port = optarg;
            break;
        case 'd':
            if (!parse_device_index_option(optarg, &config->device_index)) {
                ERROR("Invalid RTL-SDR device index: %s", optarg);
                return PARSE_CLI_RESULT_ERROR;
            }
            device_option_seen = true;
            break;
        case 'f':
            if (optarg[0] == '\0') {
                ERROR("Input file path must not be empty.");
                return PARSE_CLI_RESULT_ERROR;
            }
            config->input_file = optarg;
            input_file_option_seen = true;
            break;
        case 's': {
            bool enabled;

            if (!parse_bool_option(optarg, &enabled)) {
                ERROR("Invalid stereo value: %s", optarg);
                return PARSE_CLI_RESULT_ERROR;
            }
            config->stream_defaults.mode = enabled ? STREAM_MODE_STEREO : STREAM_MODE_MONO;
            break;
        }
        case 'r':
            if (!parse_bool_option(optarg, &config->stream_defaults.rds_enabled)) {
                ERROR("Invalid RDS value: %s", optarg);
                return PARSE_CLI_RESULT_ERROR;
            }
            break;
        case 'm':
            if (!parse_demod_math_option(optarg, &config->demod_math)) {
                ERROR("Invalid demod math: %s (valid: %s)", optarg, demod_math_options());
                return PARSE_CLI_RESULT_ERROR;
            }
            break;
        case 'g':
            if (!parse_float_option(optarg, 0.0f, &config->stream_defaults.volume_gain) ||
                config->stream_defaults.volume_gain == 0.0f) {
                ERROR("Invalid volume gain: %s", optarg);
                return PARSE_CLI_RESULT_ERROR;
            }
            break;
        case 'v':
            if (verbosity < 2u) {
                verbosity += 1u;
            }
            break;
        case 'L':
            if (!parse_log_level_option(optarg, log_level)) {
                ERROR("Invalid log level: %s (valid: trace, debug, info, warn, error)", optarg);
                return PARSE_CLI_RESULT_ERROR;
            }
            log_level_explicit = true;
            break;
        case 't':
            if (!parse_float_option(optarg, 0.0f, &config->stream_defaults.deemphasis_tau_us)) {
                ERROR("Invalid de-emphasis tau: %s", optarg);
                return PARSE_CLI_RESULT_ERROR;
            }
            break;
        case 'V':
            printf("mpxcast %s\n", MPXCAST_VERSION);
            return PARSE_CLI_RESULT_EXIT_SUCCESS;
        case 'h':
            print_usage(argv[0]);
            return PARSE_CLI_RESULT_EXIT_SUCCESS;
        default:
            print_usage(argv[0]);
            return PARSE_CLI_RESULT_ERROR;
        }
    }

    if (optind != argc) {
        print_usage(argv[0]);
        return PARSE_CLI_RESULT_ERROR;
    }

    if (device_option_seen && input_file_option_seen) {
        ERROR("--device and --input-file cannot be used together.");
        return PARSE_CLI_RESULT_ERROR;
    }

    if (!log_level_explicit) {
        *log_level = verbosity == 0u ? LOG_INFO : verbosity == 1u ? LOG_DEBUG : LOG_TRACE;
    }

    return PARSE_CLI_RESULT_OK;
}

static bool parse_bool_option(const char *value, bool *result) {
    if (strcmp(value, "0") == 0) {
        *result = false;
        return true;
    }
    if (strcmp(value, "1") == 0) {
        *result = true;
        return true;
    }
    return false;
}

static bool parse_float_option(const char *value, float min_value, float *result) {
    char *end = NULL;
    const float parsed = strtof(value, &end);

    if (end == value || *end != '\0' || !isfinite(parsed) || parsed < min_value) {
        return false;
    }

    *result = parsed;
    return true;
}

static bool parse_device_index_option(const char *value, uint32_t *result) {
    char *end = NULL;
    const unsigned long index = strtoul(value, &end, 10);

    if (end == value || *end != '\0' || index > UINT32_MAX) {
        return false;
    }

    *result = (uint32_t)index;
    return true;
}

static bool parse_demod_math_option(const char *value, enum fm_discriminator_impl *result) {
    if (strcmp(value, "approx") == 0) {
        *result = FM_DISCRIMINATOR_IMPL_APPROX;
        return true;
    }
    if (strcmp(value, "atan2_fast") == 0) {
        *result = FM_DISCRIMINATOR_IMPL_ATAN2_APPROX;
        return true;
    }
    if (strcmp(value, "atan2_std") == 0) {
        *result = FM_DISCRIMINATOR_IMPL_ATAN2_LIBM;
        return true;
    }
    return false;
}

static bool parse_log_level_option(const char *value, enum logging_level *result) {
    if (strcmp(value, "trace") == 0) {
        *result = LOG_TRACE;
        return true;
    }
    if (strcmp(value, "debug") == 0) {
        *result = LOG_DEBUG;
        return true;
    }
    if (strcmp(value, "info") == 0) {
        *result = LOG_INFO;
        return true;
    }
    if (strcmp(value, "warn") == 0) {
        *result = LOG_WARN;
        return true;
    }
    if (strcmp(value, "error") == 0) {
        *result = LOG_ERROR;
        return true;
    }
    return false;
}

static bool valid_port(const char *value) {
    char *end = NULL;
    const unsigned long port = strtoul(value, &end, 10);

    return end != value && *end == '\0' && port > 0ul && port <= 65535ul;
}

static const char *demod_math_name(enum fm_discriminator_impl impl) {
    switch (impl) {
    case FM_DISCRIMINATOR_IMPL_APPROX:
        return "approx";
    case FM_DISCRIMINATOR_IMPL_ATAN2_APPROX:
        return "atan2_fast";
    case FM_DISCRIMINATOR_IMPL_ATAN2_LIBM:
    default:
        return "atan2_std";
    }
}

static const char *demod_math_options(void) { return "atan2_std|atan2_fast|approx"; }

static const char *log_level_name(enum logging_level log_level) {
    switch (log_level) {
    case LOG_TRACE:
        return "trace";
    case LOG_DEBUG:
        return "debug";
    case LOG_INFO:
        return "info";
    case LOG_WARN:
        return "warn";
    case LOG_ERROR:
        return "error";
    default:
        return "unknown";
    }
}
