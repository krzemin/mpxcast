#ifndef MPXCAST_STREAM_SESSION_H
#define MPXCAST_STREAM_SESSION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "dsp/fm_mono.h"
#include "dsp/fm_pipeline.h"
#include "dsp/fm_stereo.h"
#include "input/rtl/rtl_source.h"
#include "stream/http.h"
#include "stream/icy.h"
#include "stream/mux_mpegts.h"

#define STREAM_SESSION_SAMPLE_RATE FM_PIPELINE_AUDIO_OUTPUT_RATE_HZ
#define STREAM_SESSION_BITS_PER_SAMPLE 16u
#define STREAM_SESSION_MAX_AUDIO_FRAMES_PER_CHUNK FM_PIPELINE_AUDIO_BLOCK_SAMPLES
#define STREAM_SESSION_MAX_CHANNELS 2u
#define STREAM_SESSION_MAX_PCM_BODY_BYTES                                                          \
    (STREAM_SESSION_MAX_AUDIO_FRAMES_PER_CHUNK * STREAM_SESSION_MAX_CHANNELS * sizeof(int16_t))
#define STREAM_SESSION_MAX_BODY_BYTES                                                              \
    (STREAM_MUX_MPEGTS_MAX_AUDIO_BYTES > STREAM_SESSION_MAX_PCM_BODY_BYTES                         \
         ? STREAM_MUX_MPEGTS_MAX_AUDIO_BYTES                                                       \
         : STREAM_SESSION_MAX_PCM_BODY_BYTES)

struct stream_session {
    bool stream_clock_started;
    bool close_after_flush;
    bool rds_enabled;
    bool explicit_station_name;
    size_t metadata_length;
    uint64_t audio_frames_queued;
    double stream_started_at;
    uint32_t requested_frequency_hz;
    char requested_station_name[ICY_NAME_MAX_LENGTH + 1u];
    char logged_radiotext[ICY_TITLE_MAX_LENGTH + 1u];
    enum stream_mode mode;
    enum stream_container container;
    struct icy_metadata_state icy;
    struct stream_mux_mpegts_state mpegts_mux;
    struct fm_pipeline_state fm_pipeline;
    struct fm_mono_state fm_mono;
    struct fm_stereo_state fm_stereo;
    int16_t pcm_buffer[STREAM_SESSION_MAX_AUDIO_FRAMES_PER_CHUNK * STREAM_SESSION_MAX_CHANNELS];
    unsigned char body_buffer[STREAM_SESSION_MAX_BODY_BYTES];
};

void stream_session_init(struct stream_session *session);
void stream_session_configure(struct stream_session *session, uint32_t requested_frequency_hz,
                              float volume_gain, float deemphasis_tau_us, enum stream_mode mode,
                              enum stream_container container, const char *requested_station_name,
                              bool explicit_station_name, bool rds_enabled,
                              bool icy_metadata_enabled, enum fm_discriminator_impl demod_math,
                              float fm_quality_interval_seconds);
int stream_session_refresh_metadata(struct stream_session *session);
int stream_session_build_prelude(struct stream_session *session, unsigned char *buffer,
                                 size_t capacity, size_t *length);
int stream_session_fill_audio_chunk(struct stream_session *session, struct rtl_source *rtl_source,
                                    unsigned char *wrap_buffer, size_t capacity,
                                    const unsigned char **chunk_data, size_t *frame_count,
                                    size_t *length);
int stream_session_fill_pcm_chunk(struct stream_session *session, struct rtl_source *rtl_source,
                                  size_t *frame_count);
void stream_session_start_clock(struct stream_session *session, double now);
double stream_session_compute_audio_delay(const struct stream_session *session, double now);
void stream_session_note_queued_frames(struct stream_session *session, size_t frame_count);

#endif
