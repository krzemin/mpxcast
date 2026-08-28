#ifndef MPXCAST_CORE_LOGGING_H
#define MPXCAST_CORE_LOGGING_H

enum logging_level {
    LOG_TRACE = 0,
    LOG_DEBUG = 1,
    LOG_INFO = 2,
    LOG_WARN = 3,
    LOG_ERROR = 4,
};

void logging_set_level(enum logging_level level);
enum logging_level logging_get_level(void);
void logging_log(enum logging_level level, const char *file, int line, const char *function,
                 const char *format, ...);

#ifdef TRACE
#undef TRACE
#undef DEBUG
#undef INFO
#undef WARN
#undef ERROR
#endif

#define TRACE(...) logging_log(LOG_TRACE, __FILE__, __LINE__, __func__, __VA_ARGS__)
#define DEBUG(...) logging_log(LOG_DEBUG, __FILE__, __LINE__, __func__, __VA_ARGS__)
#define INFO(...) logging_log(LOG_INFO, __FILE__, __LINE__, __func__, __VA_ARGS__)
#define WARN(...) logging_log(LOG_WARN, __FILE__, __LINE__, __func__, __VA_ARGS__)
#define ERROR(...) logging_log(LOG_ERROR, __FILE__, __LINE__, __func__, __VA_ARGS__)

#endif
