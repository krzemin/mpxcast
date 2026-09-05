#include "input/rtl/rtl_source.h"

#include <pthread.h>
#include <rtl-sdr.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "core/logging.h"

#define RTL_SOURCE_ASYNC_BLOCK_LENGTH (16u * 16384u)

static void rtl_source_signal_ready(struct rtl_source *source) {
    pthread_cond_broadcast((pthread_cond_t *)source->condition);
}

static void rtl_source_push_bytes(struct rtl_source *source, const unsigned char *input,
                                  size_t byte_count) {
    pthread_mutex_t *mutex = (pthread_mutex_t *)source->mutex;

    pthread_mutex_lock(mutex);

    if (byte_count >= RTL_SOURCE_BUFFER_BYTES) {
        input += byte_count - RTL_SOURCE_BUFFER_BYTES;
        byte_count = RTL_SOURCE_BUFFER_BYTES;
        source->read_offset = 0u;
        source->write_offset = 0u;
        source->buffered_bytes = 0u;
    }

    if (source->buffered_bytes + byte_count > RTL_SOURCE_BUFFER_BYTES) {
        const size_t dropped = source->buffered_bytes + byte_count - RTL_SOURCE_BUFFER_BYTES;

        source->read_offset = (source->read_offset + dropped) % RTL_SOURCE_BUFFER_BYTES;
        source->buffered_bytes -= dropped;
    }

    while (byte_count > 0u) {
        size_t contiguous = RTL_SOURCE_BUFFER_BYTES - source->write_offset;

        if (contiguous > byte_count) {
            contiguous = byte_count;
        }

        memcpy(source->buffer + source->write_offset, input, contiguous);
        source->write_offset = (source->write_offset + contiguous) % RTL_SOURCE_BUFFER_BYTES;
        source->buffered_bytes += contiguous;
        input += contiguous;
        byte_count -= contiguous;
    }

    rtl_source_signal_ready(source);
    pthread_mutex_unlock(mutex);
}

static void rtl_source_callback(unsigned char *buf, uint32_t len, void *ctx) {
    struct rtl_source *source = (struct rtl_source *)ctx;

    rtl_source_push_bytes(source, buf, (size_t)len);
}

static void *rtl_source_thread_main(void *arg) {
    struct rtl_source *source = (struct rtl_source *)arg;
    rtlsdr_dev_t *device = (rtlsdr_dev_t *)source->device;
    const int result =
        rtlsdr_read_async(device, rtl_source_callback, source, 0u, RTL_SOURCE_ASYNC_BLOCK_LENGTH);
    pthread_mutex_t *mutex = (pthread_mutex_t *)source->mutex;

    pthread_mutex_lock(mutex);
    source->running = false;
    if (result != 0) {
        source->failed = true;
    }
    rtl_source_signal_ready(source);
    pthread_mutex_unlock(mutex);
    return NULL;
}

void rtl_source_init(struct rtl_source *source) { memset(source, 0, sizeof(*source)); }

void rtl_source_log_devices(void) {
    const uint32_t device_count = rtlsdr_get_device_count();
    uint32_t i;

    if (device_count == 0u) {
        WARN("No RTL-SDR devices found.");
        return;
    }

    INFO("RTL-SDR devices detected: %u", device_count);

    for (i = 0u; i < device_count; ++i) {
        char manufacturer[256] = "";
        char product[256] = "";
        char serial[256] = "";
        const char *name = rtlsdr_get_device_name(i);

        if (rtlsdr_get_device_usb_strings(i, manufacturer, product, serial) == 0) {
            INFO("RTL-SDR device: index=%u name=\"%s\" manufacturer=\"%s\" product=\"%s\" "
                 "serial=\"%s\"",
                 i, name, manufacturer, product, serial);
        } else {
            INFO("RTL-SDR device: index=%u name=\"%s\"", i, name);
        }
    }
}

int rtl_source_start(struct rtl_source *source, uint32_t device_index) {
    rtlsdr_dev_t *device = NULL;
    pthread_t *thread;
    pthread_mutex_t *mutex;
    pthread_cond_t *condition;
    bool mutex_initialized = false;
    bool condition_initialized = false;

    source->buffer = (unsigned char *)malloc(RTL_SOURCE_BUFFER_BYTES);
    if (source->buffer == NULL) {
        return -1;
    }

    thread = (pthread_t *)malloc(sizeof(*thread));
    mutex = (pthread_mutex_t *)malloc(sizeof(*mutex));
    condition = (pthread_cond_t *)malloc(sizeof(*condition));
    if (thread == NULL || mutex == NULL || condition == NULL) {
        goto fail;
    }

    if (pthread_mutex_init(mutex, NULL) != 0) {
        goto fail;
    }
    mutex_initialized = true;

    if (pthread_cond_init(condition, NULL) != 0) {
        goto fail;
    }
    condition_initialized = true;

    if (rtlsdr_open(&device, device_index) != 0) {
        goto fail;
    }

    if (rtlsdr_set_sample_rate(device, RTL_SOURCE_SAMPLE_RATE_HZ) != 0 ||
        rtlsdr_set_tuner_gain_mode(device, 0) != 0) {
        goto fail;
    }

    source->device = device;
    source->thread = thread;
    source->mutex = mutex;
    source->condition = condition;
    source->running = false;
    source->failed = false;
    source->async_started = false;
    source->tuned_frequency_hz = 0u;

    return 0;

fail:
    if (device != NULL) {
        rtlsdr_close(device);
    }
    if (condition_initialized) {
        pthread_cond_destroy(condition);
    }
    if (mutex_initialized) {
        pthread_mutex_destroy(mutex);
    }
    free(thread);
    free(mutex);
    free(condition);
    free(source->buffer);
    memset(source, 0, sizeof(*source));
    return -1;
}

int rtl_source_start_file(struct rtl_source *source, const char *path) {
    struct stat file_status;
    FILE *file;

    if (stat(path, &file_status) != 0 || !S_ISREG(file_status.st_mode) ||
        file_status.st_size == 0 || (file_status.st_size % 2) != 0) {
        return -1;
    }

    file = fopen(path, "rb");
    if (file == NULL) {
        return -1;
    }

    source->file = file;
    source->file_input = true;
    return 0;
}

void rtl_source_stop(struct rtl_source *source) {
    pthread_t *thread = (pthread_t *)source->thread;
    pthread_mutex_t *mutex = (pthread_mutex_t *)source->mutex;
    pthread_cond_t *condition = (pthread_cond_t *)source->condition;

    if (source->file != NULL) {
        fclose((FILE *)source->file);
        memset(source, 0, sizeof(*source));
        return;
    }

    if (source->device != NULL && source->running) {
        rtlsdr_cancel_async((rtlsdr_dev_t *)source->device);
    }
    if (thread != NULL && source->async_started) {
        pthread_join(*thread, NULL);
    }
    if (source->device != NULL) {
        rtlsdr_close((rtlsdr_dev_t *)source->device);
    }
    if (condition != NULL) {
        pthread_cond_destroy(condition);
    }
    if (mutex != NULL) {
        pthread_mutex_destroy(mutex);
    }
    free(thread);
    free(mutex);
    free(condition);
    free(source->buffer);
    memset(source, 0, sizeof(*source));
}

int rtl_source_tune(struct rtl_source *source, uint32_t frequency_hz) {
    pthread_mutex_t *mutex = (pthread_mutex_t *)source->mutex;
    pthread_t *thread = (pthread_t *)source->thread;

    if (source->file_input) {
        WARN("Ignoring requested frequency %.3fMHz while reading an input file.",
             (double)frequency_hz / 1000000.0);
        return 0;
    }

    if (source->device == NULL) {
        return -1;
    }

    if (rtlsdr_set_center_freq((rtlsdr_dev_t *)source->device, frequency_hz) != 0) {
        return -1;
    }

    pthread_mutex_lock(mutex);
    source->tuned_frequency_hz = frequency_hz;
    source->read_offset = 0u;
    source->write_offset = 0u;
    source->buffered_bytes = 0u;
    pthread_mutex_unlock(mutex);

    if (!source->async_started) {
        if (rtlsdr_reset_buffer((rtlsdr_dev_t *)source->device) != 0) {
            return -1;
        }

        pthread_mutex_lock(mutex);
        source->running = true;
        source->failed = false;
        pthread_mutex_unlock(mutex);

        if (pthread_create(thread, NULL, rtl_source_thread_main, source) != 0) {
            pthread_mutex_lock(mutex);
            source->running = false;
            pthread_mutex_unlock(mutex);
            return -1;
        }
        source->async_started = true;
    }

    return 0;
}

int rtl_source_read(struct rtl_source *source, unsigned char *buffer, size_t byte_count) {
    size_t copied = 0u;
    pthread_mutex_t *mutex = (pthread_mutex_t *)source->mutex;
    pthread_cond_t *condition = (pthread_cond_t *)source->condition;

    if (source->file_input) {
        FILE *file = (FILE *)source->file;

        while (copied < byte_count) {
            copied += fread(buffer + copied, 1u, byte_count - copied, file);
            if (copied == byte_count) {
                return 0;
            }
            if (ferror(file)) {
                return -1;
            }
            clearerr(file);
            if (fseek(file, 0, SEEK_SET) != 0) {
                return -1;
            }
        }
        return 0;
    }

    pthread_mutex_lock(mutex);

    while (copied < byte_count) {
        while (source->buffered_bytes == 0u && source->running && !source->failed) {
            pthread_cond_wait(condition, mutex);
        }

        if (source->buffered_bytes == 0u) {
            pthread_mutex_unlock(mutex);
            return -1;
        }

        while (copied < byte_count && source->buffered_bytes > 0u) {
            size_t contiguous = RTL_SOURCE_BUFFER_BYTES - source->read_offset;

            if (contiguous > source->buffered_bytes) {
                contiguous = source->buffered_bytes;
            }
            if (contiguous > byte_count - copied) {
                contiguous = byte_count - copied;
            }

            memcpy(buffer + copied, source->buffer + source->read_offset, contiguous);
            source->read_offset = (source->read_offset + contiguous) % RTL_SOURCE_BUFFER_BYTES;
            source->buffered_bytes -= contiguous;
            copied += contiguous;
        }
    }

    pthread_mutex_unlock(mutex);
    return 0;
}
