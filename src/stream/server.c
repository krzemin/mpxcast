#include "stream/server.h"

#include <errno.h>
#include <microhttpd.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "core/logging.h"
#include "input/rtl/rtl_source.h"
#include "stream/cover.h"
#include "stream/http.h"
#include "stream/icy.h"
#include "stream/mux_mpegts.h"
#include "stream/mux_wav.h"
#include "stream/session.h"

#define STREAM_BUFFER_SIZE 32768
#define STREAM_MAX_BUFFERED_CHUNKS 256u
#define STREAM_REQUEST_METHOD_MAX_LENGTH 16u
#define STREAM_REQUEST_PATH_MAX_LENGTH 2048u

struct client_stream;

struct stream_chunk {
    struct stream_chunk *next;
    size_t frame_count;
    int16_t samples[STREAM_SESSION_MAX_AUDIO_FRAMES_PER_CHUNK * STREAM_SESSION_MAX_CHANNELS];
};

struct server {
    struct server_config config;
    struct rtl_source rtl_source;
    struct stream_session session;
    struct stream_http_request session_request;
    pthread_mutex_t lock;
    uint64_t active_session_id;
    bool session_active;
    struct stream_chunk *chunk_head;
    struct stream_chunk *chunk_tail;
    size_t chunk_count;
    struct client_stream *clients;
};

struct client_stream {
    struct server *server;
    struct client_stream *next;
    struct stream_chunk *next_chunk;
    struct stream_mux_mpegts_state mpegts_mux;
    struct icy_metadata_state icy;
    uint64_t session_id;
    uint64_t logged_session_id;
    bool prelude_pending;
    bool dropped_for_lag;
    enum stream_mode mode;
    enum stream_container container;
    uint32_t requested_frequency_hz;
    char request_method[STREAM_REQUEST_METHOD_MAX_LENGTH];
    char request_path[STREAM_REQUEST_PATH_MAX_LENGTH];
    char requested_station_name[ICY_NAME_MAX_LENGTH + 1u];
    unsigned char buffer[STREAM_BUFFER_SIZE];
};

static volatile sig_atomic_t stop_requested;

static const char *stream_mode_name(enum stream_mode mode) {
    return mode == STREAM_MODE_STEREO ? "stereo" : "mono";
}

static const char *stream_container_name(enum stream_container container) {
    return container == STREAM_CONTAINER_MPEGTS ? "mpeg-ts" : "wav";
}

static void handle_stop_signal(int signum) {
    (void)signum;
    stop_requested = 1;
}

static double monotonic_seconds(void) {
    struct timespec now;

    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
        return 0.0;
    }

    return (double)now.tv_sec + (double)now.tv_nsec / 1000000000.0;
}

static void sleep_for_seconds(double seconds) {
    struct timespec delay;

    if (seconds <= 0.0) {
        return;
    }

    delay.tv_sec = (time_t)seconds;
    delay.tv_nsec = (long)((seconds - (double)delay.tv_sec) * 1000000000.0);
    nanosleep(&delay, NULL);
}

static bool parse_port(const char *text, uint16_t *port) {
    char *end = NULL;
    const unsigned long parsed = strtoul(text, &end, 10);

    if (end == text || *end != '\0' || parsed == 0ul || parsed > 65535ul) {
        return false;
    }

    *port = (uint16_t)parsed;
    return true;
}

static int create_listen_socket(const struct server_config *config) {
    struct addrinfo hints;
    struct addrinfo *addresses = NULL;
    struct addrinfo *current;
    int listen_fd = -1;
    int yes = 1;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_NUMERICSERV;

    if (getaddrinfo(config->bind_host, config->bind_port, &hints, &addresses) != 0) {
        return -1;
    }

    for (current = addresses; current != NULL; current = current->ai_next) {
        listen_fd = socket(current->ai_family, current->ai_socktype, current->ai_protocol);
        if (listen_fd < 0) {
            continue;
        }

        setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
        if (current->ai_family == AF_INET6) {
            setsockopt(listen_fd, IPPROTO_IPV6, IPV6_V6ONLY, &yes, sizeof(yes));
        }

        if (bind(listen_fd, current->ai_addr, current->ai_addrlen) == 0 &&
            listen(listen_fd, 16) == 0) {
            break;
        }

        close(listen_fd);
        listen_fd = -1;
    }

    freeaddrinfo(addresses);
    return listen_fd;
}

static enum MHD_Result queue_empty_response(struct MHD_Connection *connection,
                                            unsigned int status) {
    struct MHD_Response *response = MHD_create_response_from_buffer(0u, "", MHD_RESPMEM_PERSISTENT);
    enum MHD_Result result;

    if (response == NULL) {
        return MHD_NO;
    }

    MHD_add_response_header(response, MHD_HTTP_HEADER_CONNECTION, "close");
    result = MHD_queue_response(connection, status, response);
    MHD_destroy_response(response);
    return result;
}

static enum MHD_Result queue_cover_response(struct MHD_Connection *connection) {
    const char *if_none_match;
    struct MHD_Response *response;
    enum MHD_Result result;
    unsigned int status = MHD_HTTP_OK;

    if_none_match = MHD_lookup_connection_value(connection, MHD_HEADER_KIND, "If-None-Match");
    if (if_none_match != NULL && strcmp(if_none_match, mpxcast_cover_etag) == 0) {
        response = MHD_create_response_from_buffer(0u, "", MHD_RESPMEM_PERSISTENT);
        status = MHD_HTTP_NOT_MODIFIED;
        INFO("GET /cover.png: cache hit.");
    } else {
        response = MHD_create_response_from_buffer(
            mpxcast_cover_png_size, (void *)mpxcast_cover_png, MHD_RESPMEM_PERSISTENT);
        INFO("GET /cover.png: served %zu bytes.", mpxcast_cover_png_size);
    }
    if (response == NULL) {
        ERROR("GET /cover.png: failed to create response.");
        return MHD_NO;
    }

    MHD_add_response_header(response, MHD_HTTP_HEADER_CACHE_CONTROL, "public, max-age=86400");
    MHD_add_response_header(response, "ETag", mpxcast_cover_etag);
    if (status == MHD_HTTP_OK) {
        MHD_add_response_header(response, MHD_HTTP_HEADER_CONTENT_TYPE, "image/png");
    }
    result = MHD_queue_response(connection, status, response);
    MHD_destroy_response(response);
    return result;
}

static const char *stream_content_type(enum stream_container container) {
    return container == STREAM_CONTAINER_WAV ? "audio/wav" : "video/mp2t";
}

static bool stream_request_matches_session(const struct server *server,
                                           const struct stream_http_request *request) {
    const struct stream_http_request *current = &server->session_request;

    return server->session_active && current->frequency_hz == request->frequency_hz &&
           current->volume_gain == request->volume_gain &&
           current->deemphasis_tau_us == request->deemphasis_tau_us &&
           current->mode == request->mode && current->container == request->container &&
           current->rds_enabled == request->rds_enabled &&
           current->icy_metadata_requested == request->icy_metadata_requested;
}

static void server_clear_chunks(struct server *server) {
    while (server->chunk_head != NULL) {
        struct stream_chunk *chunk = server->chunk_head;

        server->chunk_head = chunk->next;
        free(chunk);
    }
    server->chunk_tail = NULL;
    server->chunk_count = 0u;
}

static size_t server_client_count(const struct server *server, uint64_t session_id) {
    const struct client_stream *client;
    size_t count = 0u;

    for (client = server->clients; client != NULL; client = client->next) {
        if (client->session_id == session_id) {
            count += 1u;
        }
    }
    return count;
}

static bool server_has_current_clients(const struct server *server) {
    return server_client_count(server, server->active_session_id) > 0u;
}

static void server_prune_chunks(struct server *server) {
    while (server->chunk_head != NULL) {
        const struct client_stream *client;
        struct stream_chunk *chunk = server->chunk_head;

        for (client = server->clients; client != NULL; client = client->next) {
            if (client->session_id == server->active_session_id && client->next_chunk == chunk) {
                return;
            }
        }

        server->chunk_head = chunk->next;
        if (server->chunk_tail == chunk) {
            server->chunk_tail = NULL;
        }
        free(chunk);
        server->chunk_count -= 1u;
    }
}

static bool server_make_chunk_room(struct server *server) {
    while (server->chunk_count >= STREAM_MAX_BUFFERED_CHUNKS) {
        struct client_stream *client;
        bool dropped_client = false;

        for (client = server->clients; client != NULL; client = client->next) {
            if (client->session_id == server->active_session_id &&
                client->next_chunk == server->chunk_head) {
                WARN("%s %s: dropped slow client: frequency=%.3fMHz session=%llu "
                     "buffered-chunks=%u",
                     client->request_method, client->request_path,
                     (double)client->requested_frequency_hz / 1000000.0,
                     (unsigned long long)client->session_id, STREAM_MAX_BUFFERED_CHUNKS);
                client->session_id = 0u;
                client->dropped_for_lag = true;
                dropped_client = true;
            }
        }
        if (!dropped_client) {
            return false;
        }

        server_prune_chunks(server);
    }

    return true;
}

static int client_stream_build_prelude(struct client_stream *stream, size_t *length) {
    if (stream->container == STREAM_CONTAINER_MPEGTS) {
        return stream_mux_mpegts_build_prelude(&stream->icy, &stream->mpegts_mux, stream->buffer,
                                               sizeof(stream->buffer), length);
    }

    return stream_mux_wav_build_prelude(stream->mode == STREAM_MODE_STEREO ? 2u : 1u, &stream->icy,
                                        stream->buffer, sizeof(stream->buffer), length);
}

static int client_stream_pack_chunk(struct client_stream *stream, const struct stream_chunk *chunk,
                                    const struct icy_metadata_state *metadata, size_t *length) {
    const unsigned char *body;
    size_t body_length;
    int wrapped_length;

    if (stream->icy.enabled) {
        memcpy(stream->icy.metadata_block, metadata->metadata_block,
               sizeof(stream->icy.metadata_block));
        stream->icy.metadata_length = metadata->metadata_length;
    }

    if (stream->container == STREAM_CONTAINER_MPEGTS) {
        if (stream_mux_mpegts_write_audio(&stream->mpegts_mux, chunk->samples, chunk->frame_count,
                                          stream->buffer, sizeof(stream->buffer),
                                          &body_length) != 0) {
            return -1;
        }
        body = stream->buffer;
    } else {
        body_length = stream_mux_wav_body_size(stream->mode == STREAM_MODE_STEREO ? 2u : 1u,
                                               chunk->frame_count);
        body = (const unsigned char *)chunk->samples;
    }

    if (!stream->icy.enabled) {
        if (body != stream->buffer) {
            memcpy(stream->buffer, body, body_length);
        }
        *length = body_length;
        return 0;
    }

    if (body == stream->buffer) {
        unsigned char body_copy[STREAM_SESSION_MAX_BODY_BYTES];

        memcpy(body_copy, body, body_length);
        wrapped_length = icy_metadata_wrap(&stream->icy, body_copy, body_length, stream->buffer,
                                           sizeof(stream->buffer));
    } else {
        wrapped_length = icy_metadata_wrap(&stream->icy, body, body_length, stream->buffer,
                                           sizeof(stream->buffer));
    }
    if (wrapped_length < 0) {
        return -1;
    }

    *length = (size_t)wrapped_length;
    return 0;
}

static int server_produce_chunk(struct server *server) {
    struct stream_chunk *chunk;
    struct client_stream *client;

    if (!server_make_chunk_room(server)) {
        return -1;
    }

    chunk = calloc(1u, sizeof(*chunk));
    if (chunk == NULL || stream_session_fill_pcm_chunk(&server->session, &server->rtl_source,
                                                       &chunk->frame_count) != 0) {
        free(chunk);
        return -1;
    }

    memcpy(chunk->samples, server->session.pcm_buffer, sizeof(chunk->samples));
    if (server->chunk_tail != NULL) {
        server->chunk_tail->next = chunk;
    } else {
        server->chunk_head = chunk;
    }
    server->chunk_tail = chunk;
    server->chunk_count += 1u;
    for (client = server->clients; client != NULL; client = client->next) {
        if (client->session_id == server->active_session_id && client->next_chunk == NULL) {
            client->next_chunk = chunk;
        }
    }
    stream_session_note_queued_frames(&server->session, chunk->frame_count);
    return 0;
}

static ssize_t stream_content_reader(void *cls, uint64_t pos, char *buffer, size_t max) {
    struct client_stream *stream = cls;

    (void)pos;

    if (stream->prelude_pending) {
        size_t length;

        if (client_stream_build_prelude(stream, &length) != 0 || length > max) {
            ERROR("%s %s: failed to build stream prelude: session=%llu", stream->request_method,
                  stream->request_path, (unsigned long long)stream->session_id);
            return MHD_CONTENT_READER_END_WITH_ERROR;
        }
        memcpy(buffer, stream->buffer, length);
        stream->prelude_pending = false;
        return (ssize_t)length;
    }

    for (;;) {
        struct server *server = stream->server;
        double delay;

        pthread_mutex_lock(&server->lock);
        if (!server->session_active || server->active_session_id != stream->session_id) {
            pthread_mutex_unlock(&server->lock);
            return MHD_CONTENT_READER_END_OF_STREAM;
        }

        if (stream->next_chunk != NULL) {
            const struct stream_chunk *chunk = stream->next_chunk;
            size_t length;

            stream->next_chunk = chunk->next;
            if (client_stream_pack_chunk(stream, chunk, &server->session.icy, &length) != 0 ||
                length > max) {
                ERROR("%s %s: failed to package audio chunk: session=%llu", stream->request_method,
                      stream->request_path, (unsigned long long)stream->session_id);
                pthread_mutex_unlock(&server->lock);
                return MHD_CONTENT_READER_END_WITH_ERROR;
            }
            server_prune_chunks(server);
            pthread_mutex_unlock(&server->lock);
            memcpy(buffer, stream->buffer, length);
            return (ssize_t)length;
        }

        stream_session_start_clock(&server->session, monotonic_seconds());
        delay = stream_session_compute_audio_delay(&server->session, monotonic_seconds());
        if (delay <= 0.0 && server_produce_chunk(server) != 0) {
            ERROR("%s %s: failed to produce audio chunk: session=%llu", stream->request_method,
                  stream->request_path, (unsigned long long)stream->session_id);
            pthread_mutex_unlock(&server->lock);
            return MHD_CONTENT_READER_END_WITH_ERROR;
        }
        pthread_mutex_unlock(&server->lock);

        if (delay > 0.0) {
            sleep_for_seconds(delay);
        }
    }
}

static void stream_content_free(void *cls) {
    struct client_stream *stream = cls;
    struct server *server = stream->server;
    struct client_stream **current;
    size_t remaining_clients;

    pthread_mutex_lock(&server->lock);
    current = &server->clients;
    while (*current != NULL && *current != stream) {
        current = &(*current)->next;
    }
    if (*current == stream) {
        *current = stream->next;
    }
    remaining_clients = server_client_count(server, stream->session_id);
    INFO("%s %s: client disconnected: frequency=%.3fMHz session=%llu clients=%zu reason=%s",
         stream->request_method, stream->request_path,
         (double)stream->requested_frequency_hz / 1000000.0,
         (unsigned long long)stream->logged_session_id, remaining_clients,
         stream->dropped_for_lag ? "slow" : "closed");
    if (server->session_active && !server_has_current_clients(server)) {
        server_clear_chunks(server);
        server->session_active = false;
        INFO("Stream session %llu stopped: no clients.",
             (unsigned long long)server->active_session_id);
    } else {
        server_prune_chunks(server);
    }
    pthread_mutex_unlock(&server->lock);
    free(stream);
}

static enum MHD_Result queue_stream_response(struct MHD_Connection *connection,
                                             struct client_stream *stream) {
    struct MHD_Response *response;
    enum MHD_Result result;

    response = MHD_create_response_from_callback(
        MHD_SIZE_UNKNOWN, STREAM_BUFFER_SIZE, stream_content_reader, stream, stream_content_free);
    if (response == NULL) {
        ERROR("%s %s: failed to create stream response.", stream->request_method,
              stream->request_path);
        stream_content_free(stream);
        return MHD_NO;
    }

    MHD_add_response_header(response, MHD_HTTP_HEADER_CONTENT_TYPE,
                            stream_content_type(stream->container));
    MHD_add_response_header(response, MHD_HTTP_HEADER_CACHE_CONTROL, "no-cache");
    MHD_add_response_header(response, MHD_HTTP_HEADER_CONNECTION, "close");
    if (stream->icy.enabled) {
        MHD_add_response_header(response, "icy-metaint", ICY_METAINT_TEXT);
    }
    if (stream->requested_station_name[0] != '\0') {
        MHD_add_response_header(response, "icy-name", stream->requested_station_name);
    }

    result = MHD_queue_response(connection, MHD_HTTP_OK, response);
    MHD_destroy_response(response);
    return result;
}

static const char *lookup_query_parameter(void *cls, const char *name) {
    return MHD_lookup_connection_value(cls, MHD_GET_ARGUMENT_KIND, name);
}

static enum MHD_Result handle_request(void *cls, struct MHD_Connection *connection, const char *url,
                                      const char *method, const char *version,
                                      const char *upload_data, size_t *upload_data_size,
                                      void **con_cls) {
    struct server *server = cls;
    struct stream_http_request request;
    struct client_stream *stream;
    int status_code = 0;
    const char *reason = NULL;
    const char *icy_metadata;

    (void)version;
    (void)upload_data;

    if (*con_cls == NULL) {
        *con_cls = connection;
        return MHD_YES;
    }

    if (*upload_data_size != 0u) {
        *upload_data_size = 0u;
        return MHD_YES;
    }

    if (strcmp(method, MHD_HTTP_METHOD_GET) == 0 && strcmp(url, "/cover.png") == 0) {
        return queue_cover_response(connection);
    }

    icy_metadata = MHD_lookup_connection_value(connection, MHD_HEADER_KIND, "Icy-MetaData");
    if (stream_http_parse_request(method, url,
                                  icy_metadata != NULL && strcmp(icy_metadata, "1") == 0,
                                  &server->config.stream_defaults, lookup_query_parameter,
                                  connection, &request, &status_code, &reason) != 0) {
        WARN("%s %s: rejected: status=%d reason=%s", method, url, status_code,
             reason != NULL ? reason : "invalid request");
        return queue_empty_response(connection, (unsigned int)status_code);
    }

    stream = calloc(1u, sizeof(*stream));
    if (stream == NULL) {
        ERROR("%s %s: unable to allocate client stream.", method, url);
        return queue_empty_response(connection, MHD_HTTP_INTERNAL_SERVER_ERROR);
    }

    pthread_mutex_lock(&server->lock);
    if (!stream_request_matches_session(server, &request)) {
        if (rtl_source_tune(&server->rtl_source, request.frequency_hz) != 0) {
            ERROR("%s %s: failed to tune RTL-SDR: frequency=%.3fMHz", method, url,
                  (double)request.frequency_hz / 1000000.0);
            pthread_mutex_unlock(&server->lock);
            free(stream);
            return queue_empty_response(connection, MHD_HTTP_SERVICE_UNAVAILABLE);
        }

        server_clear_chunks(server);
        stream_session_init(&server->session);
        stream_session_configure(&server->session, request.frequency_hz, request.volume_gain,
                                 request.deemphasis_tau_us, request.mode, request.container,
                                 request.requested_station_name, request.explicit_station_name,
                                 request.rds_enabled, request.icy_metadata_requested,
                                 server->config.demod_math);
        if (stream_session_refresh_metadata(&server->session) != 0) {
            ERROR("%s %s: failed to initialize stream metadata: frequency=%.3fMHz", method, url,
                  (double)request.frequency_hz / 1000000.0);
            pthread_mutex_unlock(&server->lock);
            free(stream);
            return queue_empty_response(connection, MHD_HTTP_INTERNAL_SERVER_ERROR);
        }
        server->session_request = request;
        server->session_active = true;
        server->active_session_id += 1u;
        if (request.deemphasis_tau_us == 0.0f) {
            INFO("Stream session %llu tuned: frequency=%.3fMHz mode=%s container=%s rds=%s "
                 "icy=%s gain=%.3g de-emphasis=off",
                 (unsigned long long)server->active_session_id,
                 (double)request.frequency_hz / 1000000.0, stream_mode_name(request.mode),
                 stream_container_name(request.container), request.rds_enabled ? "on" : "off",
                 request.icy_metadata_requested ? "on" : "off", request.volume_gain);
        } else {
            INFO("Stream session %llu tuned: frequency=%.3fMHz mode=%s container=%s rds=%s "
                 "icy=%s gain=%.3g de-emphasis=%.3gus",
                 (unsigned long long)server->active_session_id,
                 (double)request.frequency_hz / 1000000.0, stream_mode_name(request.mode),
                 stream_container_name(request.container), request.rds_enabled ? "on" : "off",
                 request.icy_metadata_requested ? "on" : "off", request.volume_gain,
                 request.deemphasis_tau_us);
        }
    }

    stream->server = server;
    stream->session_id = server->active_session_id;
    stream->logged_session_id = stream->session_id;
    stream->prelude_pending = true;
    stream->mode = request.mode;
    stream->container = request.container;
    stream->requested_frequency_hz = request.frequency_hz;
    strncpy(stream->request_method, method, sizeof(stream->request_method) - 1u);
    stream->request_method[sizeof(stream->request_method) - 1u] = '\0';
    strncpy(stream->request_path, url, sizeof(stream->request_path) - 1u);
    stream->request_path[sizeof(stream->request_path) - 1u] = '\0';
    strncpy(stream->requested_station_name, request.requested_station_name,
            sizeof(stream->requested_station_name) - 1u);
    stream->requested_station_name[sizeof(stream->requested_station_name) - 1u] = '\0';
    stream->icy = server->session.icy;
    stream_mux_mpegts_init(&stream->mpegts_mux, request.mode == STREAM_MODE_STEREO ? 2u : 1u);
    stream->next = server->clients;
    server->clients = stream;
    INFO("%s %s: client connected: frequency=%.3fMHz session=%llu clients=%zu",
         stream->request_method, stream->request_path, (double)request.frequency_hz / 1000000.0,
         (unsigned long long)stream->session_id, server_client_count(server, stream->session_id));
    pthread_mutex_unlock(&server->lock);

    return queue_stream_response(connection, stream);
}

static void print_listen_message(const struct server_config *config) {
    char url_host[512];
    const bool bracket_host = strchr(config->bind_host, ':') != NULL;

    if (bracket_host) {
        snprintf(url_host, sizeof(url_host), "[%s]", config->bind_host);
    } else {
        snprintf(url_host, sizeof(url_host), "%s", config->bind_host);
    }

    INFO("HTTP server listening: http://%s:%s/ (path=/<frequency>[.ts|.wav])", url_host,
         config->bind_port);
    DEBUG("Stream query parameters: stereo=0|1 rds=0|1 volume-gain=<gain> "
          "deemphasis-tau=<usec> name=<station>");
}

int server_run(const struct server_config *config) {
    struct server server;
    struct MHD_Daemon *daemon;
    uint16_t port;
    int listen_fd;
    struct sigaction action;

    if (!parse_port(config->bind_port, &port)) {
        ERROR("Invalid listen port: %s", config->bind_port);
        return 1;
    }

    memset(&server, 0, sizeof(server));
    server.config = *config;
    pthread_mutex_init(&server.lock, NULL);
    rtl_source_init(&server.rtl_source);
    if ((config->input_file != NULL &&
         rtl_source_start_file(&server.rtl_source, config->input_file) < 0) ||
        (config->input_file == NULL &&
         rtl_source_start(&server.rtl_source, config->device_index) < 0)) {
        if (config->input_file != NULL) {
            ERROR("Failed to open input file: %s", config->input_file);
        } else {
            ERROR("Failed to open RTL-SDR device: index=%u", config->device_index);
        }
        pthread_mutex_destroy(&server.lock);
        return 1;
    }

    listen_fd = create_listen_socket(config);
    if (listen_fd < 0) {
        ERROR("Failed to bind HTTP server: %s:%s", config->bind_host, config->bind_port);
        rtl_source_stop(&server.rtl_source);
        pthread_mutex_destroy(&server.lock);
        return 1;
    }

    daemon = MHD_start_daemon(MHD_USE_INTERNAL_POLLING_THREAD | MHD_USE_THREAD_PER_CONNECTION, port,
                              NULL, NULL, handle_request, &server, MHD_OPTION_LISTEN_SOCKET,
                              listen_fd, MHD_OPTION_END);
    if (daemon == NULL) {
        ERROR("Failed to start HTTP server.");
        close(listen_fd);
        rtl_source_stop(&server.rtl_source);
        pthread_mutex_destroy(&server.lock);
        return 1;
    }

    print_listen_message(config);

    memset(&action, 0, sizeof(action));
    action.sa_handler = handle_stop_signal;
    sigaction(SIGINT, &action, NULL);
    sigaction(SIGTERM, &action, NULL);

    while (!stop_requested) {
        pause();
    }

    INFO("Stopping HTTP server.");
    MHD_stop_daemon(daemon);
    server_clear_chunks(&server);
    rtl_source_stop(&server.rtl_source);
    pthread_mutex_destroy(&server.lock);
    INFO("HTTP server stopped.");
    return 0;
}
