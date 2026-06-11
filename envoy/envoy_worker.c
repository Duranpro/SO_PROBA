#include "envoy_worker.h"

#include "envoy.h"

#include "../config/config.h"
#include "../network/frame.h"
#include "../transfer/transfer.h"
#include "../utils/utils.h"

#define ENVOY_WORKER_PLEDGE_TIMEOUT_SECONDS 120
#define ENVOY_WORKER_PRODUCTS_TIMEOUT_SECONDS 120
#define ENVOY_WORKER_TRADE_TIMEOUT_SECONDS 120

typedef struct {
    int pipe_fd;
    int envoy_id;
    EnvoyMissionType mission_type;
    char *config_path;
    char *stock_path;
    char *realm;
    char *file_path;
    char *direct_endpoint;
    char *response_action;
    char *target_endpoint;
    char *peer_stable_endpoint;
    CitadelConfig config;
    bool config_loaded;
} EnvoyWorkerContext;

static void envoy_worker_context_init(EnvoyWorkerContext *context) {
    if (context == NULL) {
        return;
    }

    memset(context, 0, sizeof(*context));
    context->pipe_fd = -1;
    context->envoy_id = 0;
    context->mission_type = ENVOY_MISSION_NONE;
    config_init(&context->config);
    context->config_loaded = false;
}

static void envoy_worker_context_free(EnvoyWorkerContext *context) {
    if (context == NULL) {
        return;
    }

    if (context->config_loaded) {
        config_free(&context->config);
        context->config_loaded = false;
    }

    free(context->config_path);
    free(context->stock_path);
    free(context->realm);
    free(context->file_path);
    free(context->direct_endpoint);
    free(context->response_action);
    free(context->target_endpoint);
    free(context->peer_stable_endpoint);
    context->config_path = NULL;
    context->stock_path = NULL;
    context->realm = NULL;
    context->file_path = NULL;
    context->direct_endpoint = NULL;
    context->response_action = NULL;
    context->target_endpoint = NULL;
    context->peer_stable_endpoint = NULL;
}

static EnvoyMissionType envoy_worker_parse_mission(const char *text) {
    if (text == NULL) {
        return ENVOY_MISSION_NONE;
    }

    if (strcmp(text, "pledge") == 0) {
        return ENVOY_MISSION_PLEDGE;
    }
    if (strcmp(text, "products") == 0) {
        return ENVOY_MISSION_PRODUCTS;
    }
    if (strcmp(text, "trade") == 0) {
        return ENVOY_MISSION_TRADE;
    }
    if (strcmp(text, "pledge-response") == 0) {
        return ENVOY_MISSION_PLEDGE_RESPONSE;
    }

    return ENVOY_MISSION_NONE;
}

static ssize_t envoy_worker_read_exact(int fd, void *buffer, size_t size) {
    unsigned char *cursor = (unsigned char *) buffer;
    size_t total = 0;

    while (total < size) {
        ssize_t bytes = read(fd, cursor + total, size - total);
        if (bytes == 0) {
            return total == 0 ? 0 : -1;
        }
        if (bytes < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        total += (size_t) bytes;
    }

    return (ssize_t) total;
}

static bool envoy_worker_parse_endpoint(const char *endpoint,
                                        char *ip_out,
                                        size_t ip_size,
                                        int *port_out) {
    const char *separator = NULL;
    long port_value = 0;
    char *end = NULL;
    size_t ip_length = 0;

    if (endpoint == NULL || ip_out == NULL || ip_size == 0 || port_out == NULL) {
        return false;
    }

    separator = strrchr(endpoint, ':');
    if (separator == NULL || separator == endpoint || separator[1] == '\0') {
        return false;
    }

    ip_length = (size_t) (separator - endpoint);
    if (ip_length >= ip_size) {
        return false;
    }

    memcpy(ip_out, endpoint, ip_length);
    ip_out[ip_length] = '\0';

    errno = 0;
    port_value = strtol(separator + 1, &end, 10);
    if (errno != 0 || end == separator + 1 || *end != '\0' || port_value < 1 || port_value > 65535) {
        return false;
    }

    *port_out = (int) port_value;
    return true;
}

static bool envoy_worker_build_endpoint(const char *ip,
                                        int port,
                                        char *out,
                                        size_t out_size) {
    int written = 0;

    if (ip == NULL || out == NULL || out_size == 0 || port < 1 || port > 65535) {
        return false;
    }

    written = snprintf(out, out_size, "%s:%d", ip, port);
    return written >= 0 && (size_t) written < out_size;
}

static int envoy_worker_create_private_listener(const CitadelConfig *config,
                                                char *endpoint_out,
                                                size_t endpoint_size) {
    int listener_fd = -1;
    int option = 1;
    struct sockaddr_in address;
    struct sockaddr_in bound_address;
    socklen_t bound_length = (socklen_t) sizeof(bound_address);
    const char *endpoint_ip = "127.0.0.1";

    if (config == NULL || endpoint_out == NULL || endpoint_size == 0) {
        return -1;
    }

    listener_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listener_fd < 0) {
        return -1;
    }

    if (setsockopt(listener_fd, SOL_SOCKET, SO_REUSEADDR, (const char *) &option, sizeof(option)) != 0) {
        close(listener_fd);
        return -1;
    }

    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(0);

    if (config->ip != NULL && inet_pton(AF_INET, config->ip, &address.sin_addr) > 0 &&
        bind(listener_fd, (struct sockaddr *) &address, sizeof(address)) == 0) {
        endpoint_ip = config->ip;
    } else {
        memset(&address, 0, sizeof(address));
        address.sin_family = AF_INET;
        address.sin_port = htons(0);
        address.sin_addr.s_addr = htonl(INADDR_ANY);
        if (bind(listener_fd, (struct sockaddr *) &address, sizeof(address)) != 0) {
            close(listener_fd);
            return -1;
        }
        if (config->ip != NULL && *config->ip != '\0') {
            endpoint_ip = config->ip;
        }
    }

    if (listen(listener_fd, 4) != 0) {
        close(listener_fd);
        return -1;
    }

    memset(&bound_address, 0, sizeof(bound_address));
    if (getsockname(listener_fd, (struct sockaddr *) &bound_address, &bound_length) != 0) {
        close(listener_fd);
        return -1;
    }

    if (!envoy_worker_build_endpoint(endpoint_ip,
                                     (int) ntohs(bound_address.sin_port),
                                     endpoint_out,
                                     endpoint_size)) {
        close(listener_fd);
        return -1;
    }

    return listener_fd;
}

static int envoy_worker_connect_endpoint(const char *endpoint) {
    int socket_fd = -1;
    int port = 0;
    char ip[64];
    struct sockaddr_in address;

    if (!envoy_worker_parse_endpoint(endpoint, ip, sizeof(ip), &port)) {
        return -1;
    }

    socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd < 0) {
        return -1;
    }

    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons((uint16_t) port);
    if (inet_pton(AF_INET, ip, &address.sin_addr) <= 0) {
        close(socket_fd);
        return -1;
    }

    if (connect(socket_fd, (struct sockaddr *) &address, sizeof(address)) != 0) {
        close(socket_fd);
        return -1;
    }

    return socket_fd;
}

static bool envoy_worker_send_frame_to_endpoint(const char *endpoint,
                                                const NetworkFrame *frame) {
    int socket_fd = -1;
    unsigned char buffer[CITADEL_FRAME_SIZE];

    if (endpoint == NULL || frame == NULL) {
        return false;
    }

    socket_fd = envoy_worker_connect_endpoint(endpoint);
    if (socket_fd < 0) {
        return false;
    }

    frame_serialize(frame, buffer);
    if (utils_write_all(socket_fd, buffer, sizeof(buffer)) != (ssize_t) sizeof(buffer)) {
        close(socket_fd);
        return false;
    }

    close(socket_fd);
    return true;
}

static bool envoy_worker_route_has_address(const RouteInfo *route) {
    if (route == NULL || route->ip == NULL || route->port <= 0) {
        return false;
    }

    return strcmp(route->ip, "*.*.*.*") != 0;
}

static bool envoy_worker_resolve_realm_endpoint(const EnvoyWorkerContext *ctx,
                                                const char *realm,
                                                char *endpoint_out,
                                                size_t endpoint_size) {
    const RouteInfo *route = NULL;

    if (ctx == NULL || realm == NULL || endpoint_out == NULL || endpoint_size == 0) {
        return false;
    }

    if (ctx->direct_endpoint != NULL && ctx->direct_endpoint[0] != '\0') {
        int written = snprintf(endpoint_out, endpoint_size, "%s", ctx->direct_endpoint);
        return written >= 0 && (size_t) written < endpoint_size;
    }

    route = config_find_route(&ctx->config, realm);
    if (!envoy_worker_route_has_address(route)) {
        route = config_find_route(&ctx->config, "DEFAULT");
    }

    if (!envoy_worker_route_has_address(route)) {
        return false;
    }

    return envoy_worker_build_endpoint(route->ip, route->port, endpoint_out, endpoint_size);
}

static bool envoy_worker_send_frame_to_realm(const EnvoyWorkerContext *ctx,
                                             const char *realm,
                                             const NetworkFrame *frame) {
    char endpoint[128];

    if (!envoy_worker_resolve_realm_endpoint(ctx, realm, endpoint, sizeof(endpoint))) {
        return false;
    }

    return envoy_worker_send_frame_to_endpoint(endpoint, frame);
}

static bool envoy_worker_accept_frame_timeout(int listener_fd,
                                              int timeout_seconds,
                                              NetworkFrame *frame_out) {
    while (true) {
        fd_set readfds;
        struct timeval timeout;
        int ready = 0;
        int client_fd = -1;
        unsigned char buffer[CITADEL_FRAME_SIZE];

        if (listener_fd < 0 || frame_out == NULL || timeout_seconds < 0) {
            errno = EINVAL;
            return false;
        }

        FD_ZERO(&readfds);
        FD_SET(listener_fd, &readfds);
        timeout.tv_sec = timeout_seconds;
        timeout.tv_usec = 0;

        ready = select(listener_fd + 1, &readfds, NULL, NULL, &timeout);
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        if (ready == 0) {
            errno = ETIMEDOUT;
            return false;
        }

        client_fd = accept(listener_fd, NULL, NULL);
        if (client_fd < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }

        if (envoy_worker_read_exact(client_fd, buffer, sizeof(buffer)) != (ssize_t) sizeof(buffer)) {
            close(client_fd);
            errno = EPROTO;
            return false;
        }
        close(client_fd);

        if (!frame_deserialize(buffer, frame_out) || !frame_validate_checksum(frame_out)) {
            errno = EPROTO;
            return false;
        }

        return true;
    }
}

static bool envoy_worker_send_ack(const char *endpoint,
                                  const char *origin_endpoint,
                                  const char *destination,
                                  const char *status) {
    NetworkFrame frame;

    if (endpoint == NULL || origin_endpoint == NULL || destination == NULL || status == NULL) {
        return false;
    }

    if (!frame_set(&frame, FRAME_TYPE_ACK, origin_endpoint, destination, status, strlen(status))) {
        return false;
    }

    return envoy_worker_send_frame_to_endpoint(endpoint, &frame);
}

static bool envoy_worker_send_md5_ack(const char *endpoint,
                                      const char *origin_endpoint,
                                      const char *destination,
                                      const char *status) {
    NetworkFrame frame;

    if (endpoint == NULL || origin_endpoint == NULL || destination == NULL || status == NULL) {
        return false;
    }

    if (!frame_set(&frame, FRAME_TYPE_MD5_ACK, origin_endpoint, destination, status, strlen(status))) {
        return false;
    }

    return envoy_worker_send_frame_to_endpoint(endpoint, &frame);
}

static bool envoy_worker_parse_arguments(EnvoyWorkerContext *context, int argc, char **argv) {
    int i = 0;

    if (context == NULL || argv == NULL) {
        return false;
    }

    for (i = 2; i < argc; ++i) {
        if (strcmp(argv[i], "--pipe-fd") == 0 && i + 1 < argc) {
            context->pipe_fd = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--config") == 0 && i + 1 < argc) {
            free(context->config_path);
            context->config_path = utils_strdup_safe(argv[++i]);
        } else if (strcmp(argv[i], "--stock") == 0 && i + 1 < argc) {
            free(context->stock_path);
            context->stock_path = utils_strdup_safe(argv[++i]);
        } else if (strcmp(argv[i], "--mission") == 0 && i + 1 < argc) {
            context->mission_type = envoy_worker_parse_mission(argv[++i]);
        } else if (strcmp(argv[i], "--realm") == 0 && i + 1 < argc) {
            free(context->realm);
            context->realm = utils_strdup_safe(argv[++i]);
        } else if (strcmp(argv[i], "--file") == 0 && i + 1 < argc) {
            free(context->file_path);
            context->file_path = utils_strdup_safe(argv[++i]);
        } else if (strcmp(argv[i], "--direct-endpoint") == 0 && i + 1 < argc) {
            char ip[64];
            int port = 0;
            const char *candidate = argv[++i];

            free(context->direct_endpoint);
            context->direct_endpoint = NULL;
            if (envoy_worker_parse_endpoint(candidate, ip, sizeof(ip), &port)) {
                context->direct_endpoint = utils_strdup_safe(candidate);
            }
        } else if (strcmp(argv[i], "--response") == 0 && i + 1 < argc) {
            free(context->response_action);
            context->response_action = utils_strdup_safe(argv[++i]);
        } else if (strcmp(argv[i], "--target-endpoint") == 0 && i + 1 < argc) {
            char ip[64];
            int port = 0;
            const char *candidate = argv[++i];

            free(context->target_endpoint);
            context->target_endpoint = NULL;
            if (envoy_worker_parse_endpoint(candidate, ip, sizeof(ip), &port)) {
                context->target_endpoint = utils_strdup_safe(candidate);
            }
        } else if (strcmp(argv[i], "--peer-stable-endpoint") == 0 && i + 1 < argc) {
            char ip[64];
            int port = 0;
            const char *candidate = argv[++i];

            free(context->peer_stable_endpoint);
            context->peer_stable_endpoint = NULL;
            if (candidate[0] == '\0') {
                context->peer_stable_endpoint = utils_strdup_safe("");
            } else if (envoy_worker_parse_endpoint(candidate, ip, sizeof(ip), &port)) {
                context->peer_stable_endpoint = utils_strdup_safe(candidate);
            }
        } else if (strcmp(argv[i], "--envoy-id") == 0 && i + 1 < argc) {
            context->envoy_id = atoi(argv[++i]);
        }
    }

    if (context->realm == NULL) {
        context->realm = utils_strdup_safe("");
    }
    if (context->file_path == NULL) {
        context->file_path = utils_strdup_safe("");
    }

    if (context->peer_stable_endpoint == NULL) {
        context->peer_stable_endpoint = utils_strdup_safe("");
    }

    if (!(context->pipe_fd >= 0 &&
           context->envoy_id > 0 &&
           context->mission_type != ENVOY_MISSION_NONE &&
           context->config_path != NULL &&
           context->stock_path != NULL &&
           context->realm != NULL &&
           context->file_path != NULL &&
           context->peer_stable_endpoint != NULL)) {
        return false;
    }

    if (context->mission_type == ENVOY_MISSION_PLEDGE_RESPONSE) {
        return context->response_action != NULL &&
               context->target_endpoint != NULL &&
               context->target_endpoint[0] != '\0' &&
               (utils_equals_ignore_case(context->response_action, "ACCEPT") ||
                utils_equals_ignore_case(context->response_action, "REJECT"));
    }

    return true;
}

static bool envoy_worker_wait_frame_type(int listener_fd,
                                         int expected_type,
                                         int timeout_seconds,
                                         NetworkFrame *out) {
    time_t deadline = time(NULL) + timeout_seconds;

    if (listener_fd < 0 || timeout_seconds < 0 || out == NULL) {
        return false;
    }

    while (true) {
        NetworkFrame frame;
        int remaining = (int) (deadline - time(NULL));

        if (remaining <= 0) {
            errno = ETIMEDOUT;
            return false;
        }

        if (!envoy_worker_accept_frame_timeout(listener_fd, remaining, &frame)) {
            return false;
        }

        if ((int) frame.type == expected_type) {
            *out = frame;
            return true;
        }
    }
}

static char *envoy_worker_frame_data_text(const NetworkFrame *frame) {
    if (frame == NULL) {
        return NULL;
    }

    return frame_data_to_text(frame);
}

static bool envoy_worker_payload_starts_with(const char *payload, const char *prefix) {
    if (payload == NULL || prefix == NULL) {
        return false;
    }

    return strncmp(payload, prefix, strlen(prefix)) == 0;
}

static bool envoy_worker_parse_header_triplet(const char *text,
                                              char **file_name_out,
                                              size_t *size_out,
                                              char md5_out[CITADEL_MD5_LENGTH + 1]) {
    char *copy = NULL;
    char *file_name = NULL;
    char *size_text = NULL;
    char *md5 = NULL;
    long size_value = 0;
    char *end = NULL;

    if (text == NULL || file_name_out == NULL || size_out == NULL || md5_out == NULL) {
        return false;
    }

    *file_name_out = NULL;
    *size_out = 0;
    md5_out[0] = '\0';

    copy = utils_strdup_safe(text);
    if (copy == NULL) {
        return false;
    }

    file_name = strtok(copy, "&");
    size_text = strtok(NULL, "&");
    md5 = strtok(NULL, "&");
    if (file_name == NULL || size_text == NULL || md5 == NULL || *file_name == '\0' || *md5 == '\0') {
        free(copy);
        return false;
    }

    errno = 0;
    size_value = strtol(size_text, &end, 10);
    if (errno != 0 || end == size_text || *end != '\0' || size_value < 0) {
        free(copy);
        return false;
    }

    *file_name_out = utils_strdup_safe(file_name);
    if (*file_name_out == NULL) {
        free(copy);
        return false;
    }

    *size_out = (size_t) size_value;
    strncpy(md5_out, md5, CITADEL_MD5_LENGTH);
    md5_out[CITADEL_MD5_LENGTH] = '\0';
    free(copy);
    return true;
}

static bool envoy_worker_send_file_fragments(const EnvoyWorkerContext *ctx,
                                             const char *origin_endpoint,
                                             const char *destination_realm,
                                             const char *file_path,
                                             uint8_t frame_type) {
    int fd = -1;
    unsigned char block[CITADEL_FRAME_DATA_SIZE];
    bool ok = true;

    if (ctx == NULL || origin_endpoint == NULL || destination_realm == NULL || file_path == NULL) {
        return false;
    }

    fd = open(file_path, O_RDONLY);
    if (fd < 0) {
        return false;
    }

    while (ok) {
        ssize_t bytes = read(fd, block, sizeof(block));
        NetworkFrame frame;

        if (bytes == 0) {
            break;
        }
        if (bytes < 0) {
            if (errno == EINTR) {
                continue;
            }
            ok = false;
            break;
        }

        if (!frame_set(&frame, frame_type, origin_endpoint, destination_realm, block, (size_t) bytes) ||
            !envoy_worker_send_frame_to_realm(ctx, destination_realm, &frame)) {
            ok = false;
            break;
        }
    }

    close(fd);
    return ok;
}

static bool envoy_worker_receive_file_payload(int listener_fd,
                                              int expected_type,
                                              size_t expected_size,
                                              int timeout_seconds,
                                              char **data_out,
                                              size_t *data_size_out) {
    time_t deadline = 0;
    char *buffer = NULL;
    size_t total = 0;

    if (listener_fd < 0 || data_out == NULL || data_size_out == NULL) {
        return false;
    }

    *data_out = NULL;
    *data_size_out = 0;
    deadline = time(NULL) + timeout_seconds;

    buffer = (char *) malloc(expected_size + 1);
    if (buffer == NULL) {
        return false;
    }

    while (total < expected_size) {
        NetworkFrame frame;
        int remaining = (int) (deadline - time(NULL));

        if (remaining <= 0) {
            errno = ETIMEDOUT;
            free(buffer);
            return false;
        }

        if (!envoy_worker_accept_frame_timeout(listener_fd, remaining, &frame)) {
            free(buffer);
            return false;
        }

        if ((int) frame.type != expected_type) {
            free(buffer);
            errno = EPROTO;
            return false;
        }

        if (total + frame.data_length > expected_size) {
            free(buffer);
            errno = EPROTO;
            return false;
        }

        memcpy(buffer + total, frame.data, frame.data_length);
        total += frame.data_length;
    }

    buffer[expected_size] = '\0';
    *data_out = buffer;
    *data_size_out = expected_size;
    return true;
}

static bool envoy_worker_compute_md5_from_memory(EnvoyWorkerContext *ctx,
                                                 const char *data,
                                                 size_t size,
                                                 char md5_out[CITADEL_MD5_LENGTH + 1]) {
    char *template_path = NULL;
    int fd = -1;
    bool ok = false;
    const char *base_dir = NULL;

    if (ctx == NULL || data == NULL || md5_out == NULL) {
        return false;
    }

    base_dir = (ctx->config.workdir != NULL && ctx->config.workdir[0] != '\0') ? ctx->config.workdir : "/tmp";
    if (asprintf(&template_path, "%s/envoy-products-XXXXXX", base_dir) < 0 || template_path == NULL) {
        return false;
    }

    fd = mkstemp(template_path);
    if (fd < 0) {
        free(template_path);
        return false;
    }

    if (size == 0 || utils_write_all(fd, data, size) == (ssize_t) size) {
        ok = true;
    }
    close(fd);

    if (ok) {
        ok = transfer_compute_md5sum(template_path, md5_out);
    }

    unlink(template_path);
    free(template_path);
    return ok;
}

static EnvoyResultStatus envoy_worker_run_trade(EnvoyWorkerContext *ctx,
                                                char *remote_endpoint_out,
                                                size_t remote_endpoint_size,
                                                char **payload_out) {
    char *order_text = NULL;
    char *file_name = NULL;
    size_t file_size = 0;
    char md5[CITADEL_MD5_LENGTH + 1];
    int listener_fd = -1;
    char private_endpoint[128];
    char *header_payload = NULL;
    NetworkFrame trade_header;
    NetworkFrame ack_frame;
    NetworkFrame md5_ack_frame;
    NetworkFrame response_frame;
    char *frame_payload = NULL;
    EnvoyResultStatus result = ENVOY_RESULT_FAILED;

    if (ctx == NULL || remote_endpoint_out == NULL || payload_out == NULL) {
        return ENVOY_RESULT_FAILED;
    }

    remote_endpoint_out[0] = '\0';
    *payload_out = NULL;
    memset(md5, 0, sizeof(md5));
    memset(private_endpoint, 0, sizeof(private_endpoint));
    memset(&trade_header, 0, sizeof(trade_header));
    memset(&ack_frame, 0, sizeof(ack_frame));
    memset(&md5_ack_frame, 0, sizeof(md5_ack_frame));
    memset(&response_frame, 0, sizeof(response_frame));

    if (ctx->file_path == NULL || ctx->file_path[0] == '\0') {
        *payload_out = utils_strdup_safe("Could not prepare trade order.");
        return ENVOY_RESULT_FAILED;
    }

    order_text = utils_read_file(ctx->file_path, NULL);
    if (order_text == NULL || !transfer_get_file_info(ctx->file_path, &file_name, &file_size, md5)) {
        free(order_text);
        free(file_name);
        *payload_out = utils_strdup_safe("Could not prepare trade order.");
        return ENVOY_RESULT_FAILED;
    }

    listener_fd = envoy_worker_create_private_listener(&ctx->config, private_endpoint, sizeof(private_endpoint));
    if (listener_fd < 0) {
        free(order_text);
        free(file_name);
        *payload_out = utils_strdup_safe("Could not create private Envoy listener.");
        return ENVOY_RESULT_FAILED;
    }

    if (asprintf(&header_payload, "%s&%s&%zu&%s", ctx->config.realm_name, file_name, file_size, md5) < 0 ||
        header_payload == NULL ||
        !frame_set(&trade_header, FRAME_TYPE_TRADE_HEADER, private_endpoint, ctx->realm,
                   header_payload, strlen(header_payload)) ||
        !envoy_worker_send_frame_to_realm(ctx, ctx->realm, &trade_header)) {
        *payload_out = utils_strdup_safe("Could not send trade header.");
        goto cleanup;
    }

    if (!envoy_worker_wait_frame_type(listener_fd, FRAME_TYPE_ACK,
                                      ENVOY_WORKER_TRADE_TIMEOUT_SECONDS, &ack_frame)) {
        result = (errno == ETIMEDOUT) ? ENVOY_RESULT_TIMEOUT : ENVOY_RESULT_FAILED;
        *payload_out = utils_strdup_safe(result == ENVOY_RESULT_TIMEOUT ?
                                         "Timed out waiting for trade ACK." :
                                         "Invalid trade ACK received.");
        goto cleanup;
    }

    frame_payload = envoy_worker_frame_data_text(&ack_frame);
    if (frame_payload == NULL || !envoy_worker_payload_starts_with(frame_payload, "OK&")) {
        *payload_out = utils_strdup_safe("Trade request was not acknowledged.");
        goto cleanup;
    }
    free(frame_payload);
    frame_payload = NULL;

    if (!envoy_worker_send_file_fragments(ctx, private_endpoint, ctx->realm,
                                          ctx->file_path, FRAME_TYPE_TRADE_DATA)) {
        *payload_out = utils_strdup_safe("Could not send trade order data.");
        goto cleanup;
    }

    if (!envoy_worker_wait_frame_type(listener_fd, FRAME_TYPE_MD5_ACK,
                                      ENVOY_WORKER_TRADE_TIMEOUT_SECONDS, &md5_ack_frame)) {
        result = (errno == ETIMEDOUT) ? ENVOY_RESULT_TIMEOUT : ENVOY_RESULT_FAILED;
        *payload_out = utils_strdup_safe(result == ENVOY_RESULT_TIMEOUT ?
                                         "Timed out waiting for trade MD5 ACK." :
                                         "Invalid trade MD5 ACK received.");
        goto cleanup;
    }

    frame_payload = envoy_worker_frame_data_text(&md5_ack_frame);
    if (frame_payload == NULL || !envoy_worker_payload_starts_with(frame_payload, "CHECK_OK&")) {
        *payload_out = utils_strdup_safe("Trade order verification failed.");
        goto cleanup;
    }
    free(frame_payload);
    frame_payload = NULL;

    if (!envoy_worker_wait_frame_type(listener_fd, FRAME_TYPE_TRADE_RESPONSE,
                                      ENVOY_WORKER_TRADE_TIMEOUT_SECONDS, &response_frame)) {
        result = (errno == ETIMEDOUT) ? ENVOY_RESULT_TIMEOUT : ENVOY_RESULT_FAILED;
        *payload_out = utils_strdup_safe(result == ENVOY_RESULT_TIMEOUT ?
                                         "Timed out waiting for trade response." :
                                         "Invalid trade response received.");
        goto cleanup;
    }

    strncpy(remote_endpoint_out, response_frame.origin, remote_endpoint_size - 1);
    remote_endpoint_out[remote_endpoint_size - 1] = '\0';

    frame_payload = envoy_worker_frame_data_text(&response_frame);
    if (frame_payload == NULL) {
        *payload_out = utils_strdup_safe("Invalid trade response received.");
        goto cleanup;
    }

    if (strcmp(frame_payload, "OK") == 0) {
        *payload_out = utils_strdup_safe(order_text);
        result = (*payload_out != NULL) ? ENVOY_RESULT_OK : ENVOY_RESULT_FAILED;
    } else if (strncmp(frame_payload, "REJECT&", 7) == 0) {
        *payload_out = utils_strdup_safe(frame_payload + 7);
        result = ENVOY_RESULT_REJECTED;
    } else {
        *payload_out = utils_strdup_safe("Invalid trade response received.");
    }

cleanup:
    if (listener_fd >= 0) {
        close(listener_fd);
    }
    free(frame_payload);
    free(header_payload);
    free(order_text);
    free(file_name);
    return result;
}

static EnvoyResultStatus envoy_worker_run_pledge_response(EnvoyWorkerContext *ctx,
                                                          char *remote_endpoint_out,
                                                          size_t remote_endpoint_size,
                                                          char **payload_out) {
    int listener_fd = -1;
    char private_endpoint[128];
    char local_stable_endpoint[128];
    char *response_text = NULL;
    NetworkFrame response_frame;
    NetworkFrame ack_frame;
    char *frame_payload = NULL;
    bool accepted = false;
    EnvoyResultStatus result = ENVOY_RESULT_FAILED;

    if (ctx == NULL || remote_endpoint_out == NULL || payload_out == NULL ||
        ctx->target_endpoint == NULL || ctx->response_action == NULL) {
        return ENVOY_RESULT_FAILED;
    }

    remote_endpoint_out[0] = '\0';
    (void) remote_endpoint_size;
    *payload_out = NULL;
    memset(private_endpoint, 0, sizeof(private_endpoint));
    memset(local_stable_endpoint, 0, sizeof(local_stable_endpoint));
    memset(&response_frame, 0, sizeof(response_frame));
    memset(&ack_frame, 0, sizeof(ack_frame));

    accepted = utils_equals_ignore_case(ctx->response_action, "ACCEPT");
    if (!accepted && !utils_equals_ignore_case(ctx->response_action, "REJECT")) {
        *payload_out = utils_strdup_safe("Invalid pledge response action.");
        return ENVOY_RESULT_FAILED;
    }

    if (!envoy_worker_build_endpoint(ctx->config.ip, ctx->config.port,
                                     local_stable_endpoint, sizeof(local_stable_endpoint))) {
        *payload_out = utils_strdup_safe("Could not build local stable endpoint.");
        return ENVOY_RESULT_FAILED;
    }

    listener_fd = envoy_worker_create_private_listener(&ctx->config, private_endpoint, sizeof(private_endpoint));
    if (listener_fd < 0) {
        *payload_out = utils_strdup_safe("Could not create private Envoy listener.");
        return ENVOY_RESULT_FAILED;
    }

    if ((accepted &&
         asprintf(&response_text, "ACCEPT&%s&%s", ctx->config.realm_name, local_stable_endpoint) < 0) ||
        (!accepted &&
         asprintf(&response_text, "REJECT&%s", ctx->config.realm_name) < 0) ||
        response_text == NULL ||
        !frame_set(&response_frame, FRAME_TYPE_PLEDGE_RESPONSE, private_endpoint, ctx->realm,
                   response_text, strlen(response_text)) ||
        !envoy_worker_send_frame_to_endpoint(ctx->target_endpoint, &response_frame)) {
        *payload_out = utils_strdup_safe("Could not send pledge response.");
        goto cleanup;
    }

    if (!envoy_worker_wait_frame_type(listener_fd, FRAME_TYPE_ACK,
                                      ENVOY_WORKER_PLEDGE_TIMEOUT_SECONDS, &ack_frame)) {
        result = (errno == ETIMEDOUT) ? ENVOY_RESULT_TIMEOUT : ENVOY_RESULT_FAILED;
        *payload_out = utils_strdup_safe(result == ENVOY_RESULT_TIMEOUT ?
                                         "Timed out waiting for final ACK." :
                                         "Invalid final ACK received.");
        goto cleanup;
    }

    frame_payload = envoy_worker_frame_data_text(&ack_frame);
    if (frame_payload == NULL || !envoy_worker_payload_starts_with(frame_payload, "OK&")) {
        *payload_out = utils_strdup_safe("Pledge response was not acknowledged.");
        goto cleanup;
    }

    result = ENVOY_RESULT_OK;
    if (accepted) {
        *payload_out = utils_strdup_safe(ctx->peer_stable_endpoint != NULL ? ctx->peer_stable_endpoint : "");
    } else {
        *payload_out = utils_strdup_safe("Rejected");
    }

cleanup:
    if (listener_fd >= 0) {
        close(listener_fd);
    }
    free(frame_payload);
    free(response_text);
    return result;
}

static EnvoyResultStatus envoy_worker_run_stub(EnvoyWorkerContext *ctx,
                                               char *remote_endpoint_out,
                                               size_t remote_endpoint_size,
                                               char **payload_out) {
    int listener_fd = -1;
    char endpoint[128];
    char message[256];

    if (ctx == NULL || remote_endpoint_out == NULL || payload_out == NULL) {
        return ENVOY_RESULT_FAILED;
    }

    (void) remote_endpoint_size;
    remote_endpoint_out[0] = '\0';
    *payload_out = NULL;
    memset(endpoint, 0, sizeof(endpoint));
    memset(message, 0, sizeof(message));

    listener_fd = envoy_worker_create_private_listener(&ctx->config, endpoint, sizeof(endpoint));
    if (listener_fd < 0) {
        *payload_out = utils_strdup_safe("Envoy worker stub failed to create private listener");
        return ENVOY_RESULT_FAILED;
    }

    close(listener_fd);

    if (snprintf(message, sizeof(message),
                 "Envoy worker stub executed correctly. Private endpoint: %s",
                 endpoint) >= 0) {
        *payload_out = utils_strdup_safe(message);
    } else {
        *payload_out = utils_strdup_safe("Envoy worker stub executed correctly");
    }

    return ENVOY_RESULT_FAILED;
}

static EnvoyResultStatus envoy_worker_run_pledge(EnvoyWorkerContext *ctx,
                                                 char *remote_endpoint_out,
                                                 size_t remote_endpoint_size,
                                                 char **payload_out) {
    char *sigil_path = NULL;
    char *file_name = NULL;
    char stable_endpoint[128];
    char md5[CITADEL_MD5_LENGTH + 1];
    size_t file_size = 0;
    int listener_fd = -1;
    char private_endpoint[128];
    NetworkFrame pledge_frame;
    NetworkFrame ack_frame;
    NetworkFrame md5_ack_frame;
    NetworkFrame response_frame;
    char *payload_text = NULL;
    char *frame_payload = NULL;
    char ack_final[64];
    EnvoyResultStatus result = ENVOY_RESULT_FAILED;

    if (ctx == NULL || remote_endpoint_out == NULL || payload_out == NULL) {
        return ENVOY_RESULT_FAILED;
    }

    remote_endpoint_out[0] = '\0';
    *payload_out = NULL;
    memset(stable_endpoint, 0, sizeof(stable_endpoint));
    memset(md5, 0, sizeof(md5));
    memset(private_endpoint, 0, sizeof(private_endpoint));
    memset(&pledge_frame, 0, sizeof(pledge_frame));
    memset(&ack_frame, 0, sizeof(ack_frame));
    memset(&md5_ack_frame, 0, sizeof(md5_ack_frame));
    memset(&response_frame, 0, sizeof(response_frame));

    sigil_path = transfer_resolve_sigil_path(&ctx->config, ctx->file_path);
    if (sigil_path == NULL || !transfer_get_file_info(sigil_path, &file_name, &file_size, md5)) {
        free(sigil_path);
        free(file_name);
        *payload_out = utils_strdup_safe("Could not prepare pledge sigil.");
        return ENVOY_RESULT_FAILED;
    }

    listener_fd = envoy_worker_create_private_listener(&ctx->config, private_endpoint, sizeof(private_endpoint));
    if (listener_fd < 0) {
        free(sigil_path);
        free(file_name);
        *payload_out = utils_strdup_safe("Could not create private Envoy listener.");
        return ENVOY_RESULT_FAILED;
    }

    if (!envoy_worker_build_endpoint(ctx->config.ip, ctx->config.port,
                                     stable_endpoint, sizeof(stable_endpoint)) ||
        asprintf(&payload_text, "%s&%s&%zu&%s&%s",
                 ctx->config.realm_name,
                 file_name,
                 file_size,
                 md5,
                 stable_endpoint) < 0 ||
        payload_text == NULL ||
        !frame_set(&pledge_frame, FRAME_TYPE_PLEDGE, private_endpoint, ctx->realm,
                   payload_text, strlen(payload_text)) ||
        !envoy_worker_send_frame_to_realm(ctx, ctx->realm, &pledge_frame)) {
        *payload_out = utils_strdup_safe("Could not send pledge request.");
        goto cleanup;
    }
    free(payload_text);
    payload_text = NULL;

    if (!envoy_worker_wait_frame_type(listener_fd, FRAME_TYPE_ACK,
                                      ENVOY_WORKER_PLEDGE_TIMEOUT_SECONDS, &ack_frame)) {
        result = (errno == ETIMEDOUT) ? ENVOY_RESULT_TIMEOUT : ENVOY_RESULT_FAILED;
        *payload_out = utils_strdup_safe(result == ENVOY_RESULT_TIMEOUT ?
                                         "Timed out waiting for pledge ACK." :
                                         "Invalid pledge ACK received.");
        goto cleanup;
    }

    frame_payload = envoy_worker_frame_data_text(&ack_frame);
    if (frame_payload == NULL || !envoy_worker_payload_starts_with(frame_payload, "OK&")) {
        *payload_out = utils_strdup_safe("Pledge request was not acknowledged.");
        goto cleanup;
    }
    free(frame_payload);
    frame_payload = NULL;

    if (!envoy_worker_send_file_fragments(ctx, private_endpoint, ctx->realm,
                                          sigil_path, FRAME_TYPE_SIGIL_DATA)) {
        *payload_out = utils_strdup_safe("Could not send pledge sigil data.");
        goto cleanup;
    }

    if (!envoy_worker_wait_frame_type(listener_fd, FRAME_TYPE_MD5_ACK,
                                      ENVOY_WORKER_PLEDGE_TIMEOUT_SECONDS,
                                      &md5_ack_frame)) {
        result = (errno == ETIMEDOUT) ? ENVOY_RESULT_TIMEOUT : ENVOY_RESULT_FAILED;
        *payload_out = utils_strdup_safe(result == ENVOY_RESULT_TIMEOUT ?
                                         "Timed out waiting for MD5 ACK." :
                                         "Invalid MD5 ACK received.");
        goto cleanup;
    }

    frame_payload = envoy_worker_frame_data_text(&md5_ack_frame);
    if (frame_payload == NULL || !envoy_worker_payload_starts_with(frame_payload, "CHECK_OK&")) {
        *payload_out = utils_strdup_safe("Pledge sigil verification failed.");
        goto cleanup;
    }
    free(frame_payload);
    frame_payload = NULL;

    if (!envoy_worker_wait_frame_type(listener_fd, FRAME_TYPE_PLEDGE_RESPONSE,
                                      ENVOY_WORKER_PLEDGE_TIMEOUT_SECONDS,
                                      &response_frame)) {
        result = (errno == ETIMEDOUT) ? ENVOY_RESULT_TIMEOUT : ENVOY_RESULT_FAILED;
        *payload_out = utils_strdup_safe(result == ENVOY_RESULT_TIMEOUT ?
                                         "Timed out waiting for pledge response." :
                                         "Invalid pledge response received.");
        goto cleanup;
    }

    frame_payload = envoy_worker_frame_data_text(&response_frame);
    if (frame_payload == NULL) {
        *payload_out = utils_strdup_safe("Invalid pledge response received.");
        goto cleanup;
    }

    snprintf(ack_final, sizeof(ack_final), "OK&%s", ctx->config.realm_name);
    (void) envoy_worker_send_ack(response_frame.origin, private_endpoint, "", ack_final);

    if (envoy_worker_payload_starts_with(frame_payload, "ACCEPT&")) {
        char *response_copy = utils_strdup_safe(frame_payload);
        char *decision = NULL;
        char *realm_name = NULL;
        char *stable = NULL;

        if (response_copy == NULL) {
            *payload_out = utils_strdup_safe("Invalid pledge response received.");
            goto cleanup;
        }

        decision = strtok(response_copy, "&");
        realm_name = strtok(NULL, "&");
        stable = strtok(NULL, "&");
        (void) decision;
        (void) realm_name;

        if (stable != NULL && stable[0] != '\0') {
            char ip[64];
            int port = 0;
            if (envoy_worker_parse_endpoint(stable, ip, sizeof(ip), &port)) {
                strncpy(remote_endpoint_out, stable, remote_endpoint_size - 1);
                remote_endpoint_out[remote_endpoint_size - 1] = '\0';
            }
        }
        if (remote_endpoint_out[0] == '\0') {
            strncpy(remote_endpoint_out, response_frame.origin, remote_endpoint_size - 1);
            remote_endpoint_out[remote_endpoint_size - 1] = '\0';
        }

        free(response_copy);
        result = ENVOY_RESULT_OK;
    } else if (envoy_worker_payload_starts_with(frame_payload, "REJECT&")) {
        result = ENVOY_RESULT_REJECTED;
        *payload_out = utils_strdup_safe("Rejected");
    } else {
        *payload_out = utils_strdup_safe("Invalid pledge response received.");
    }

cleanup:
    free(frame_payload);
    free(payload_text);
    if (listener_fd >= 0) {
        close(listener_fd);
    }
    free(sigil_path);
    free(file_name);
    return result;
}

static EnvoyResultStatus envoy_worker_run_products(EnvoyWorkerContext *ctx,
                                                   char *remote_endpoint_out,
                                                   size_t remote_endpoint_size,
                                                   char **payload_out) {
    int listener_fd = -1;
    char private_endpoint[128];
    NetworkFrame request_frame;
    NetworkFrame response_frame;
    char *response_text = NULL;
    char *file_name = NULL;
    size_t expected_size = 0;
    char expected_md5[CITADEL_MD5_LENGTH + 1];
    char *catalog_text = NULL;
    size_t catalog_size = 0;
    char actual_md5[CITADEL_MD5_LENGTH + 1];
    char ack_payload[64];
    char md5_payload[64];
    EnvoyResultStatus result = ENVOY_RESULT_FAILED;

    if (ctx == NULL || remote_endpoint_out == NULL || payload_out == NULL) {
        return ENVOY_RESULT_FAILED;
    }

    remote_endpoint_out[0] = '\0';
    *payload_out = NULL;
    memset(private_endpoint, 0, sizeof(private_endpoint));
    memset(&request_frame, 0, sizeof(request_frame));
    memset(&response_frame, 0, sizeof(response_frame));
    memset(expected_md5, 0, sizeof(expected_md5));
    memset(actual_md5, 0, sizeof(actual_md5));

    listener_fd = envoy_worker_create_private_listener(&ctx->config, private_endpoint, sizeof(private_endpoint));
    if (listener_fd < 0) {
        *payload_out = utils_strdup_safe("Could not create private Envoy listener.");
        return ENVOY_RESULT_FAILED;
    }

    if (!frame_set(&request_frame, FRAME_TYPE_PRODUCTS_REQUEST, private_endpoint, ctx->realm,
                   ctx->config.realm_name, strlen(ctx->config.realm_name)) ||
        !envoy_worker_send_frame_to_realm(ctx, ctx->realm, &request_frame)) {
        *payload_out = utils_strdup_safe("Could not send products request.");
        goto cleanup;
    }

    if (!envoy_worker_wait_frame_type(listener_fd, FRAME_TYPE_PRODUCTS_RESPONSE,
                                      ENVOY_WORKER_PRODUCTS_TIMEOUT_SECONDS, &response_frame)) {
        result = (errno == ETIMEDOUT) ? ENVOY_RESULT_TIMEOUT : ENVOY_RESULT_FAILED;
        *payload_out = utils_strdup_safe(result == ENVOY_RESULT_TIMEOUT ?
                                         "Timed out waiting for products response." :
                                         "Invalid products response received.");
        goto cleanup;
    }

    strncpy(remote_endpoint_out, response_frame.origin, remote_endpoint_size - 1);
    remote_endpoint_out[remote_endpoint_size - 1] = '\0';

    response_text = envoy_worker_frame_data_text(&response_frame);
    if (response_text == NULL ||
        !envoy_worker_parse_header_triplet(response_text, &file_name, &expected_size, expected_md5)) {
        *payload_out = utils_strdup_safe("Invalid products response received.");
        goto cleanup;
    }

    snprintf(ack_payload, sizeof(ack_payload), "OK&%s", ctx->config.realm_name);
    if (!envoy_worker_send_ack(response_frame.origin, private_endpoint, "", ack_payload)) {
        *payload_out = utils_strdup_safe("Could not acknowledge products response.");
        goto cleanup;
    }

    if (!envoy_worker_receive_file_payload(listener_fd, FRAME_TYPE_PRODUCTS_DATA, expected_size,
                                           ENVOY_WORKER_PRODUCTS_TIMEOUT_SECONDS,
                                           &catalog_text, &catalog_size)) {
        result = (errno == ETIMEDOUT) ? ENVOY_RESULT_TIMEOUT : ENVOY_RESULT_FAILED;
        *payload_out = utils_strdup_safe(result == ENVOY_RESULT_TIMEOUT ?
                                         "Timed out receiving products data." :
                                         "Invalid products data received.");
        goto cleanup;
    }

    if (!envoy_worker_compute_md5_from_memory(ctx, catalog_text, catalog_size, actual_md5)) {
        *payload_out = utils_strdup_safe("Could not verify products payload.");
        goto cleanup;
    }

    snprintf(md5_payload, sizeof(md5_payload), "%s&%s",
             strcmp(actual_md5, expected_md5) == 0 ? "CHECK_OK" : "CHECK_KO",
             ctx->config.realm_name);
    if (!envoy_worker_send_md5_ack(response_frame.origin, private_endpoint, "", md5_payload)) {
        *payload_out = utils_strdup_safe("Could not send products MD5 acknowledgement.");
        goto cleanup;
    }

    if (strcmp(actual_md5, expected_md5) != 0) {
        *payload_out = utils_strdup_safe("Products payload failed MD5 verification.");
        goto cleanup;
    }

    *payload_out = catalog_text;
    catalog_text = NULL;
    result = ENVOY_RESULT_OK;

cleanup:
    if (listener_fd >= 0) {
        close(listener_fd);
    }
    free(response_text);
    free(file_name);
    free(catalog_text);
    return result;
}

int envoy_worker_main(int argc, char **argv) {
    EnvoyWorkerContext context;
    EnvoyResultHeader header;
    char remote_endpoint[128];
    char *payload_text = NULL;
    bool write_ok = false;
    EnvoyResultStatus result = ENVOY_RESULT_FAILED;

    envoy_worker_context_init(&context);
    memset(&header, 0, sizeof(header));
    memset(remote_endpoint, 0, sizeof(remote_endpoint));

    if (!envoy_worker_parse_arguments(&context, argc, argv)) {
        envoy_worker_context_free(&context);
        return EXIT_FAILURE;
    }

    context.config_loaded = config_load(context.config_path, &context.config);
    if (!context.config_loaded) {
        payload_text = utils_strdup_safe("Envoy worker could not load config.");
    } else if (context.mission_type == ENVOY_MISSION_PLEDGE_RESPONSE) {
        result = envoy_worker_run_pledge_response(&context, remote_endpoint, sizeof(remote_endpoint), &payload_text);
    } else if (context.mission_type == ENVOY_MISSION_PLEDGE &&
               strcmp(context.file_path, "stub-sigil") != 0) {
        result = envoy_worker_run_pledge(&context, remote_endpoint, sizeof(remote_endpoint), &payload_text);
    } else if (context.mission_type == ENVOY_MISSION_PRODUCTS) {
        result = envoy_worker_run_products(&context, remote_endpoint, sizeof(remote_endpoint), &payload_text);
    } else if (context.mission_type == ENVOY_MISSION_TRADE) {
        result = envoy_worker_run_trade(&context, remote_endpoint, sizeof(remote_endpoint), &payload_text);
    } else {
        result = envoy_worker_run_stub(&context, remote_endpoint, sizeof(remote_endpoint), &payload_text);
    }

    header.magic = ENVOY_RESULT_MAGIC;
    header.envoy_id = context.envoy_id;
    header.mission_type = (int) context.mission_type;
    header.result_status = (int) result;
    strncpy(header.realm, context.realm != NULL ? context.realm : "", sizeof(header.realm) - 1);
    header.realm[sizeof(header.realm) - 1] = '\0';
    strncpy(header.remote_endpoint, remote_endpoint, sizeof(header.remote_endpoint) - 1);
    header.remote_endpoint[sizeof(header.remote_endpoint) - 1] = '\0';
    header.payload_size = (uint32_t) strlen(payload_text != NULL ? payload_text : "");

    write_ok = envoy_result_write(context.pipe_fd, &header, payload_text != NULL ? payload_text : "");
    close(context.pipe_fd);

    free(payload_text);
    envoy_worker_context_free(&context);
    return write_ok ? 0 : 1;
}
