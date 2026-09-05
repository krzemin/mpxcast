#ifndef MPXCAST_INPUT_RTL_RTL_SOURCE_H
#define MPXCAST_INPUT_RTL_RTL_SOURCE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define RTL_SOURCE_DEFAULT_DEVICE_INDEX 0u
#define RTL_SOURCE_SAMPLE_RATE_HZ 1920000u
#define RTL_SOURCE_BUFFER_BYTES (RTL_SOURCE_SAMPLE_RATE_HZ * 2u)

struct rtl_source {
    void *device;
    void *thread;
    void *mutex;
    void *condition;
    void *file;
    bool file_input;
    bool running;
    bool failed;
    bool async_started;
    uint32_t tuned_frequency_hz;
    unsigned char *buffer;
    size_t read_offset;
    size_t write_offset;
    size_t buffered_bytes;
};

void rtl_source_init(struct rtl_source *source);
void rtl_source_log_devices(void);
int rtl_source_start(struct rtl_source *source, uint32_t device_index);
int rtl_source_start_file(struct rtl_source *source, const char *path);
void rtl_source_stop(struct rtl_source *source);
int rtl_source_tune(struct rtl_source *source, uint32_t frequency_hz);
int rtl_source_read(struct rtl_source *source, unsigned char *buffer, size_t byte_count);

#endif
