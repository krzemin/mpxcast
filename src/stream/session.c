#include "stream/session.h"

#include <string.h>

#include "audio/pcm.h"
#include "core/logging.h"
#include "stream/mux_mpegts.h"
#include "stream/mux_wav.h"

static const struct rds_station_info *
stream_session_current_rds_info(const struct stream_session *session) {
    static const struct rds_station_info empty_info;

    if (!session->rds_enabled) {
        return &empty_info;
    }
    return fm_pipeline_get_station_info(&session->fm_pipeline);
}

static bool stream_session_has_complete_radiotext(const struct stream_session *session) {
    if (!session->rds_enabled) {
        return false;
    }
    return fm_pipeline_has_complete_radiotext(&session->fm_pipeline);
}

static void stream_session_log_radiotext_update(struct stream_session *session) {
    const struct rds_station_info *info;
    char radiotext[ICY_TITLE_MAX_LENGTH + 1u];

    if (logging_get_level() != LOG_DEBUG && logging_get_level() != LOG_TRACE) {
        return;
    }
    if (!stream_session_has_complete_radiotext(session)) {
        return;
    }

    info = stream_session_current_rds_info(session);
    icy_copy_text(radiotext, sizeof(radiotext), info->radiotext);
    if (radiotext[0] == '\0' || strcmp(radiotext, session->logged_radiotext) == 0) {
        return;
    }

    strcpy(session->logged_radiotext, radiotext);
    DEBUG("Stream metadata updated: title=\"%s\"", radiotext);
}

void stream_session_init(struct stream_session *session) {
    memset(session, 0, sizeof(*session));
    fm_pipeline_init(&session->fm_pipeline);
    fm_mono_init(&session->fm_mono);
    fm_stereo_init(&session->fm_stereo);
}

void stream_session_configure(struct stream_session *session, uint32_t requested_frequency_hz,
                              float volume_gain, float deemphasis_tau_us, enum stream_mode mode,
                              enum stream_container container, const char *requested_station_name,
                              bool explicit_station_name, bool rds_enabled,
                              bool icy_metadata_enabled, enum fm_discriminator_impl demod_math) {
    const bool allow_icy_metadata = rds_enabled && icy_metadata_enabled;

    fm_pipeline_reset(&session->fm_pipeline);
    fm_pipeline_configure(&session->fm_pipeline, rds_enabled);
    fm_discriminator_set_impl(&session->fm_pipeline.discriminator, demod_math);
    fm_mono_init(&session->fm_mono);
    fm_stereo_init(&session->fm_stereo);
    if (mode == STREAM_MODE_STEREO) {
        fm_stereo_configure(&session->fm_stereo, volume_gain, deemphasis_tau_us * 1.0e-6f);
    } else {
        fm_mono_configure(&session->fm_mono, volume_gain, deemphasis_tau_us * 1.0e-6f);
    }

    session->rds_enabled = rds_enabled;
    session->explicit_station_name = explicit_station_name;
    session->requested_frequency_hz = requested_frequency_hz;
    session->mode = mode;
    session->container = container;
    strncpy(session->requested_station_name, requested_station_name,
            sizeof(session->requested_station_name) - 1u);
    session->requested_station_name[sizeof(session->requested_station_name) - 1u] = '\0';
    icy_metadata_init(&session->icy, allow_icy_metadata);
    stream_mux_mpegts_init(&session->mpegts_mux, mode == STREAM_MODE_STEREO ? 2u : 1u);
}

int stream_session_refresh_metadata(struct stream_session *session) {
    const struct rds_station_info *info = stream_session_current_rds_info(session);

    if (!session->icy.enabled) {
        return 0;
    }

    if (icy_metadata_update(&session->icy, info->radiotext,
                            stream_session_has_complete_radiotext(session)) != 0) {
        return -1;
    }

    session->metadata_length = session->icy.metadata_length;
    return 0;
}

int stream_session_build_prelude(struct stream_session *session, unsigned char *buffer,
                                 size_t capacity, size_t *length) {
    if (session->container == STREAM_CONTAINER_MPEGTS) {
        return stream_mux_mpegts_build_prelude(&session->icy, &session->mpegts_mux, buffer,
                                               capacity, length);
    }

    return stream_mux_wav_build_prelude(session->mode == STREAM_MODE_STEREO ? 2u : 1u,
                                        &session->icy, buffer, capacity, length);
}

int stream_session_fill_pcm_chunk(struct stream_session *session, struct rtl_source *rtl_source,
                                  size_t *frame_count) {
    *frame_count = STREAM_SESSION_MAX_AUDIO_FRAMES_PER_CHUNK;

    fm_pipeline_process_live_block(&session->fm_pipeline, rtl_source);
    stream_session_log_radiotext_update(session);

    if (session->mode == STREAM_MODE_STEREO) {
        fm_stereo_process_block(&session->fm_stereo, session->fm_pipeline.demodulated,
                                session->pcm_buffer);
    } else {
        fm_mono_process_block(&session->fm_mono, session->fm_pipeline.demodulated,
                              session->pcm_buffer);
    }

    if (session->icy.enabled) {
        if (stream_session_refresh_metadata(session) != 0) {
            return -1;
        }
    }

    return 0;
}

int stream_session_fill_audio_chunk(struct stream_session *session, struct rtl_source *rtl_source,
                                    unsigned char *wrap_buffer, size_t capacity,
                                    const unsigned char **chunk_data, size_t *frame_count,
                                    size_t *length) {
    const unsigned char *body;
    int wrapped_length;
    size_t body_length;

    if (stream_session_fill_pcm_chunk(session, rtl_source, frame_count) != 0) {
        return -1;
    }

    if (session->container == STREAM_CONTAINER_MPEGTS) {
        if (stream_mux_mpegts_write_audio(&session->mpegts_mux, session->pcm_buffer, *frame_count,
                                          session->body_buffer, sizeof(session->body_buffer),
                                          &body_length) != 0) {
            return -1;
        }
        body = session->body_buffer;
    } else {
        body_length =
            stream_mux_wav_body_size(session->mode == STREAM_MODE_STEREO ? 2u : 1u, *frame_count);
        body = (const unsigned char *)session->pcm_buffer;
    }

    if (!session->icy.enabled) {
        *chunk_data = body;
        *length = body_length;
        return 0;
    }

    wrapped_length = icy_metadata_wrap(&session->icy, body, body_length, wrap_buffer, capacity);
    if (wrapped_length < 0) {
        return -1;
    }

    *chunk_data = wrap_buffer;
    *length = (size_t)wrapped_length;
    return 0;
}

void stream_session_start_clock(struct stream_session *session, double now) {
    if (!session->stream_clock_started) {
        session->stream_started_at = now;
        session->stream_clock_started = true;
    }
}

double stream_session_compute_audio_delay(const struct stream_session *session, double now) {
    return session->stream_started_at +
           ((double)session->audio_frames_queued / (double)STREAM_SESSION_SAMPLE_RATE) - now;
}

void stream_session_note_queued_frames(struct stream_session *session, size_t frame_count) {
    session->audio_frames_queued += frame_count;
}
