#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>

#include "clog.h"
#include "core/logging.h"

static atomic_int logging_min_level = LOG_INFO;

static clog_level_t logging_to_clog_level(enum logging_level level) {
    switch (level) {
    case LOG_TRACE:
        return CLOG_TRACE;
    case LOG_DEBUG:
        return CLOG_DEBUG;
    case LOG_INFO:
        return CLOG_INFO;
    case LOG_WARN:
        return CLOG_WARN;
    case LOG_ERROR:
    default:
        return CLOG_ERROR;
    }
}

void logging_set_level(enum logging_level level) {
    atomic_store(&logging_min_level, level);
    clog_set_level(CLOG_TRACE);
}

enum logging_level logging_get_level(void) {
    return (enum logging_level)atomic_load(&logging_min_level);
}

void logging_log(enum logging_level level, const char *file, int line, const char *function,
                 const char *format, ...) {
    char message[CLOG_MAX_MESSAGE_SIZE];
    va_list arguments;

    if (level < logging_get_level()) {
        return;
    }

    va_start(arguments, format);
    (void)vsnprintf(message, sizeof(message), format, arguments);
    va_end(arguments);

    clog_log(logging_to_clog_level(level), file, line, function, "%s", message);
}
