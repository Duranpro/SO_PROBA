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
    int id_envoy;
    EnvoyMissionType tipus_missio;
    char *config_path;
    char *stock_path;
    char *regne;
    char *ruta_fitxer;
    char *direct_endpoint;
    char *response_action;
    char *endpoint_desti;
    char *endpoint_estable_peer;
    CitadelConfig config;
    bool config_carregada;
} EnvoyWorkerContext;

static void envoy_worker_context_init(EnvoyWorkerContext *context) {
    if (context == NULL) {
        return;
    }

    memset(context, 0, sizeof(*context));
    context->pipe_fd = -1;
    context->id_envoy = 0;
    context->tipus_missio = ENVOY_MISSION_NONE;
    config_init(&context->config);
    context->config_carregada = false;
}

static void envoy_worker_context_free(EnvoyWorkerContext *context) {
    if (context == NULL) {
        return;
    }

    if (context->config_carregada) {
        config_free(&context->config);
        context->config_carregada = false;
    }

    free(context->config_path);
    free(context->stock_path);
    free(context->regne);
    free(context->ruta_fitxer);
    free(context->direct_endpoint);
    free(context->response_action);
    free(context->endpoint_desti);
    free(context->endpoint_estable_peer);
    context->config_path = NULL;
    context->stock_path = NULL;
    context->regne = NULL;
    context->ruta_fitxer = NULL;
    context->direct_endpoint = NULL;
    context->response_action = NULL;
    context->endpoint_desti = NULL;
    context->endpoint_estable_peer = NULL;
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
            if (total == 0) {
                return 0;
            }

            return -1;
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

static bool envoy_worker_parse_endpoint(const char *endpoint, char *ip_out, size_t ip_size, int *port_out) {
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

static bool envoy_worker_build_endpoint(const char *ip, int port, char *out, size_t out_size) {
    int written = 0;

    if (ip == NULL || out == NULL || out_size == 0 || port < 1 || port > 65535) {
        return false;
    }

    written = snprintf(out, out_size, "%s:%d", ip, port);
    return written >= 0 && (size_t) written < out_size;
}

static int envoy_worker_create_private_listener(const CitadelConfig *config, char *endpoint_out, size_t endpoint_size) {
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

    if (config->ip_regne != NULL && inet_pton(AF_INET, config->ip_regne, &address.sin_addr) > 0 && bind(listener_fd, (struct sockaddr *) &address, sizeof(address)) == 0) {
        endpoint_ip = config->ip_regne;
    } else {
        memset(&address, 0, sizeof(address));
        address.sin_family = AF_INET;
        address.sin_port = htons(0);
        address.sin_addr.s_addr = htonl(INADDR_ANY);
        if (bind(listener_fd, (struct sockaddr *) &address, sizeof(address)) != 0) {
            close(listener_fd);
            return -1;
        }
        if (config->ip_regne != NULL && *config->ip_regne != '\0') {
            endpoint_ip = config->ip_regne;
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

    if (!envoy_worker_build_endpoint(endpoint_ip, (int) ntohs(bound_address.sin_port), endpoint_out, endpoint_size)) {
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

static bool envoy_worker_send_frame_to_endpoint(const char *endpoint, const NetworkFrame *frame) {
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
    if (route == NULL || route->ip_regne == NULL || route->port_regne <= 0) {
        return false;
    }

    return strcmp(route->ip_regne, "*.*.*.*") != 0;
}

static bool envoy_worker_resolve_realm_endpoint(const EnvoyWorkerContext *ctx, const char *realm, char *endpoint_out, size_t endpoint_size) {
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

    return envoy_worker_build_endpoint(route->ip_regne, route->port_regne, endpoint_out, endpoint_size);
}

static bool envoy_worker_send_frame_to_realm(const EnvoyWorkerContext *ctx, const char *realm, const NetworkFrame *frame) {
    char endpoint[128];

    if (!envoy_worker_resolve_realm_endpoint(ctx, realm, endpoint, sizeof(endpoint))) {
        return false;
    }

    return envoy_worker_send_frame_to_endpoint(endpoint, frame);
}

static bool envoy_worker_accept_frame_timeout(int listener_fd, int timeout_seconds, NetworkFrame *frame_out) {
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

        if (!frame_deserialize(buffer, frame_out) || !frame_validar_checksum(frame_out)) {
            errno = EPROTO;
            return false;
        }

        return true;
    }
}

static bool envoy_worker_send_ack(const char *endpoint, const char *endpoint_origen, const char *destination, const char *status) {
    NetworkFrame frame;

    if (endpoint == NULL || endpoint_origen == NULL || destination == NULL || status == NULL) {
        return false;
    }

    if (!frame_set(&frame, FRAME_TYPE_ACK, endpoint_origen, destination, status, strlen(status))) {
        return false;
    }

    return envoy_worker_send_frame_to_endpoint(endpoint, &frame);
}

static bool envoy_worker_send_md5_ack(const char *endpoint, const char *endpoint_origen, const char *destination, const char *status) {
    NetworkFrame frame;

    if (endpoint == NULL || endpoint_origen == NULL || destination == NULL || status == NULL) {
        return false;
    }

    if (!frame_set(&frame, FRAME_TYPE_MD5_ACK, endpoint_origen, destination, status, strlen(status))) {
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
            context->tipus_missio = envoy_worker_parse_mission(argv[++i]);
        } else if (strcmp(argv[i], "--realm") == 0 && i + 1 < argc) {
            free(context->regne);
            context->regne = utils_strdup_safe(argv[++i]);
        } else if (strcmp(argv[i], "--file") == 0 && i + 1 < argc) {
            free(context->ruta_fitxer);
            context->ruta_fitxer = utils_strdup_safe(argv[++i]);
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

            free(context->endpoint_desti);
            context->endpoint_desti = NULL;
            if (envoy_worker_parse_endpoint(candidate, ip, sizeof(ip), &port)) {
                context->endpoint_desti = utils_strdup_safe(candidate);
            }
        } else if (strcmp(argv[i], "--peer-stable-endpoint") == 0 && i + 1 < argc) {
            char ip[64];
            int port = 0;
            const char *candidate = argv[++i];

            free(context->endpoint_estable_peer);
            context->endpoint_estable_peer = NULL;
            if (candidate[0] == '\0') {
                context->endpoint_estable_peer = utils_strdup_safe("");
            } else if (envoy_worker_parse_endpoint(candidate, ip, sizeof(ip), &port)) {
                context->endpoint_estable_peer = utils_strdup_safe(candidate);
            }
        } else if (strcmp(argv[i], "--envoy-id") == 0 && i + 1 < argc) {
            context->id_envoy = atoi(argv[++i]);
        }
    }

    if (context->regne == NULL) {
        context->regne = utils_strdup_safe("");
    }
    if (context->ruta_fitxer == NULL) {
        context->ruta_fitxer = utils_strdup_safe("");
    }

    if (context->endpoint_estable_peer == NULL) {
        context->endpoint_estable_peer = utils_strdup_safe("");
    }

    if (!(context->pipe_fd >= 0 && context->id_envoy > 0 && context->tipus_missio != ENVOY_MISSION_NONE && context->config_path != NULL && context->stock_path != NULL && context->regne != NULL && context->ruta_fitxer != NULL && context->endpoint_estable_peer != NULL)) {
        return false;
    }

    if (context->tipus_missio == ENVOY_MISSION_PLEDGE_RESPONSE) {
        return context->response_action != NULL &&
               context->endpoint_desti != NULL &&
               context->endpoint_desti[0] != '\0' &&
               (utils_equals_ignore_case(context->response_action, "ACCEPT") || utils_equals_ignore_case(context->response_action, "REJECT"));
    }

    return true;
}

static bool envoy_worker_wait_frame_type(int listener_fd, int expected_type, int timeout_seconds, NetworkFrame *out) {
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

        if ((int) frame.tipus == expected_type) {
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

static bool envoy_worker_parse_header_triplet(const char *text, char **nom_fitxer_out, size_t *mida_out, char md5_out[CITADEL_MD5_LENGTH + 1]) {
    char *copy = NULL;
    char *nom_fitxer = NULL;
    char *size_text = NULL;
    char *md5 = NULL;
    long size_value = 0;
    char *end = NULL;

    if (text == NULL || nom_fitxer_out == NULL || mida_out == NULL || md5_out == NULL) {
        return false;
    }

    *nom_fitxer_out = NULL;
    *mida_out = 0;
    md5_out[0] = '\0';

    copy = utils_strdup_safe(text);
    if (copy == NULL) {
        return false;
    }

    nom_fitxer = strtok(copy, "&");
    size_text = strtok(NULL, "&");
    md5 = strtok(NULL, "&");
    if (nom_fitxer == NULL || size_text == NULL || md5 == NULL || *nom_fitxer == '\0' || *md5 == '\0') {
        free(copy);
        return false;
    }

    errno = 0;
    size_value = strtol(size_text, &end, 10);
    if (errno != 0 || end == size_text || *end != '\0' || size_value < 0) {
        free(copy);
        return false;
    }

    *nom_fitxer_out = utils_strdup_safe(nom_fitxer);
    if (*nom_fitxer_out == NULL) {
        free(copy);
        return false;
    }

    *mida_out = (size_t) size_value;
    strncpy(md5_out, md5, CITADEL_MD5_LENGTH);
    md5_out[CITADEL_MD5_LENGTH] = '\0';
    free(copy);
    return true;
}

static bool envoy_worker_send_file_fragments(const EnvoyWorkerContext *ctx, const char *endpoint_origen, const char *regne_desti, const char *ruta_fitxer, uint8_t frame_type) {
    int fd = -1;
    unsigned char block[CITADEL_FRAME_DATA_SIZE];
    bool ok = true;

    if (ctx == NULL || endpoint_origen == NULL || regne_desti == NULL || ruta_fitxer == NULL) {
        return false;
    }

    fd = open(ruta_fitxer, O_RDONLY);
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

        if (!frame_set(&frame, frame_type, endpoint_origen, regne_desti, block, (size_t) bytes) || !envoy_worker_send_frame_to_realm(ctx, regne_desti, &frame)) {
            ok = false;
            break;
        }
    }

    close(fd);
    return ok;
}

static bool envoy_worker_receive_file_payload(int listener_fd, int expected_type, size_t expected_size, int timeout_seconds, char **data_out, size_t *data_size_out) {
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

        if ((int) frame.tipus != expected_type) {
            free(buffer);
            errno = EPROTO;
            return false;
        }

        if (total + frame.mida_data > expected_size) {
            free(buffer);
            errno = EPROTO;
            return false;
        }

        memcpy(buffer + total, frame.data, frame.mida_data);
        total += frame.mida_data;
    }

    buffer[expected_size] = '\0';
    *data_out = buffer;
    *data_size_out = expected_size;
    return true;
}

static bool envoy_worker_compute_md5_from_memory(EnvoyWorkerContext *ctx, const char *data, size_t size, char md5_out[CITADEL_MD5_LENGTH + 1]) {
    char *template_path = NULL;
    int fd = -1;
    bool ok = false;
    const char *base_dir = NULL;

    if (ctx == NULL || data == NULL || md5_out == NULL) {
        return false;
    }

    if (ctx->config.directori_carpeta != NULL && ctx->config.directori_carpeta[0] != '\0') {
        base_dir = ctx->config.directori_carpeta;
    } else {
        base_dir = "/tmp";
    }
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

static EnvoyResultStatus envoy_worker_run_trade(EnvoyWorkerContext *ctx, char *remote_endpoint_out, size_t remote_endpoint_size, char **payload_out) {
    char *order_text = NULL;
    char *nom_fitxer = NULL;
    size_t mida_fitxer = 0;
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

    if (ctx->ruta_fitxer == NULL || ctx->ruta_fitxer[0] == '\0') {
        *payload_out = utils_strdup_safe("Could not prepare trade order.");
        return ENVOY_RESULT_FAILED;
    }

    order_text = utils_read_file(ctx->ruta_fitxer, NULL);
    if (order_text == NULL || !transfer_obtenir_info_fitxer(ctx->ruta_fitxer, &nom_fitxer, &mida_fitxer, md5)) {
        free(order_text);
        free(nom_fitxer);
        *payload_out = utils_strdup_safe("Could not prepare trade order.");
        return ENVOY_RESULT_FAILED;
    }

    listener_fd = envoy_worker_create_private_listener(&ctx->config, private_endpoint, sizeof(private_endpoint));
    if (listener_fd < 0) {
        free(order_text);
        free(nom_fitxer);
        *payload_out = utils_strdup_safe("Could not create private Envoy listener.");
        return ENVOY_RESULT_FAILED;
    }

    if (asprintf(&header_payload, "%s&%s&%zu&%s", ctx->config.nom_regne, nom_fitxer, mida_fitxer, md5) < 0 || header_payload == NULL || !frame_set(&trade_header, FRAME_TYPE_TRADE_HEADER, private_endpoint, ctx->regne, header_payload, strlen(header_payload)) || !envoy_worker_send_frame_to_realm(ctx, ctx->regne, &trade_header)) {
        *payload_out = utils_strdup_safe("Could not send trade header.");
        goto cleanup;
    }

    if (!envoy_worker_wait_frame_type(listener_fd, FRAME_TYPE_ACK, ENVOY_WORKER_TRADE_TIMEOUT_SECONDS, &ack_frame)) {
        if (errno == ETIMEDOUT) {
            result = ENVOY_RESULT_TIMEOUT;
        } else {
            result = ENVOY_RESULT_FAILED;
        }

        if (result == ENVOY_RESULT_TIMEOUT) {
            *payload_out = utils_strdup_safe("Timed out waiting for trade ACK.");
        } else {
            *payload_out = utils_strdup_safe("Invalid trade ACK received.");
        }
        goto cleanup;
    }

    frame_payload = envoy_worker_frame_data_text(&ack_frame);
    if (frame_payload == NULL || !envoy_worker_payload_starts_with(frame_payload, "OK&")) {
        *payload_out = utils_strdup_safe("Trade request was not acknowledged.");
        goto cleanup;
    }
    free(frame_payload);
    frame_payload = NULL;

    if (!envoy_worker_send_file_fragments(ctx, private_endpoint, ctx->regne, ctx->ruta_fitxer, FRAME_TYPE_TRADE_DATA)) {
        *payload_out = utils_strdup_safe("Could not send trade order data.");
        goto cleanup;
    }

    if (!envoy_worker_wait_frame_type(listener_fd, FRAME_TYPE_MD5_ACK, ENVOY_WORKER_TRADE_TIMEOUT_SECONDS, &md5_ack_frame)) {
        if (errno == ETIMEDOUT) {
            result = ENVOY_RESULT_TIMEOUT;
        } else {
            result = ENVOY_RESULT_FAILED;
        }

        if (result == ENVOY_RESULT_TIMEOUT) {
            *payload_out = utils_strdup_safe("Timed out waiting for trade MD5 ACK.");
        } else {
            *payload_out = utils_strdup_safe("Invalid trade MD5 ACK received.");
        }
        goto cleanup;
    }

    frame_payload = envoy_worker_frame_data_text(&md5_ack_frame);
    if (frame_payload == NULL || !envoy_worker_payload_starts_with(frame_payload, "CHECK_OK&")) {
        *payload_out = utils_strdup_safe("Trade order verification failed.");
        goto cleanup;
    }
    free(frame_payload);
    frame_payload = NULL;

    if (!envoy_worker_wait_frame_type(listener_fd, FRAME_TYPE_TRADE_RESPONSE, ENVOY_WORKER_TRADE_TIMEOUT_SECONDS, &response_frame)) {
        if (errno == ETIMEDOUT) {
            result = ENVOY_RESULT_TIMEOUT;
        } else {
            result = ENVOY_RESULT_FAILED;
        }

        if (result == ENVOY_RESULT_TIMEOUT) {
            *payload_out = utils_strdup_safe("Timed out waiting for trade response.");
        } else {
            *payload_out = utils_strdup_safe("Invalid trade response received.");
        }
        goto cleanup;
    }

    strncpy(remote_endpoint_out, response_frame.origen, remote_endpoint_size - 1);
    remote_endpoint_out[remote_endpoint_size - 1] = '\0';

    frame_payload = envoy_worker_frame_data_text(&response_frame);
    if (frame_payload == NULL) {
        *payload_out = utils_strdup_safe("Invalid trade response received.");
        goto cleanup;
    }

    if (strcmp(frame_payload, "OK") == 0) {
        *payload_out = utils_strdup_safe(order_text);
        if (*payload_out != NULL) {
            result = ENVOY_RESULT_OK;
        } else {
            result = ENVOY_RESULT_FAILED;
        }
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
    free(nom_fitxer);
    return result;
}

static EnvoyResultStatus envoy_worker_run_pledge_response(EnvoyWorkerContext *ctx, char *remote_endpoint_out, size_t remote_endpoint_size, char **payload_out) {
    int listener_fd = -1;
    char private_endpoint[128];
    char local_stable_endpoint[128];
    char *response_text = NULL;
    NetworkFrame response_frame;
    NetworkFrame ack_frame;
    char *frame_payload = NULL;
    bool acceptat = false;
    EnvoyResultStatus result = ENVOY_RESULT_FAILED;

    if (ctx == NULL || remote_endpoint_out == NULL || payload_out == NULL || ctx->endpoint_desti == NULL || ctx->response_action == NULL) {
        return ENVOY_RESULT_FAILED;
    }

    remote_endpoint_out[0] = '\0';
    (void) remote_endpoint_size;
    *payload_out = NULL;
    memset(private_endpoint, 0, sizeof(private_endpoint));
    memset(local_stable_endpoint, 0, sizeof(local_stable_endpoint));
    memset(&response_frame, 0, sizeof(response_frame));
    memset(&ack_frame, 0, sizeof(ack_frame));

    acceptat = utils_equals_ignore_case(ctx->response_action, "ACCEPT");
    if (!acceptat && !utils_equals_ignore_case(ctx->response_action, "REJECT")) {
        *payload_out = utils_strdup_safe("Invalid pledge response action.");
        return ENVOY_RESULT_FAILED;
    }

    if (!envoy_worker_build_endpoint(ctx->config.ip_regne, ctx->config.port_regne, local_stable_endpoint, sizeof(local_stable_endpoint))) {
        *payload_out = utils_strdup_safe("Could not build local stable endpoint.");
        return ENVOY_RESULT_FAILED;
    }

    listener_fd = envoy_worker_create_private_listener(&ctx->config, private_endpoint, sizeof(private_endpoint));
    if (listener_fd < 0) {
        *payload_out = utils_strdup_safe("Could not create private Envoy listener.");
        return ENVOY_RESULT_FAILED;
    }

    if ((acceptat && asprintf(&response_text, "ACCEPT&%s&%s", ctx->config.nom_regne, local_stable_endpoint) < 0) || (!acceptat && asprintf(&response_text, "REJECT&%s", ctx->config.nom_regne) < 0) || response_text == NULL || !frame_set(&response_frame, FRAME_TYPE_PLEDGE_RESPONSE, private_endpoint, ctx->regne, response_text, strlen(response_text)) || !envoy_worker_send_frame_to_endpoint(ctx->endpoint_desti, &response_frame)) {
        *payload_out = utils_strdup_safe("Could not send pledge response.");
        goto cleanup;
    }

    if (!envoy_worker_wait_frame_type(listener_fd, FRAME_TYPE_ACK, ENVOY_WORKER_PLEDGE_TIMEOUT_SECONDS, &ack_frame)) {
        if (errno == ETIMEDOUT) {
            result = ENVOY_RESULT_TIMEOUT;
        } else {
            result = ENVOY_RESULT_FAILED;
        }

        if (result == ENVOY_RESULT_TIMEOUT) {
            *payload_out = utils_strdup_safe("Timed out waiting for final ACK.");
        } else {
            *payload_out = utils_strdup_safe("Invalid final ACK received.");
        }
        goto cleanup;
    }

    frame_payload = envoy_worker_frame_data_text(&ack_frame);
    if (frame_payload == NULL || !envoy_worker_payload_starts_with(frame_payload, "OK&")) {
        *payload_out = utils_strdup_safe("Pledge response was not acknowledged.");
        goto cleanup;
    }

    result = ENVOY_RESULT_OK;
    if (acceptat) {
        if (ctx->endpoint_estable_peer != NULL) {
            *payload_out = utils_strdup_safe(ctx->endpoint_estable_peer);
        } else {
            *payload_out = utils_strdup_safe("");
        }
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

static EnvoyResultStatus envoy_worker_run_stub(EnvoyWorkerContext *ctx, char *remote_endpoint_out, size_t remote_endpoint_size, char **payload_out) {
    int listener_fd = -1;
    char endpoint[128];
    char *message = NULL;

    if (ctx == NULL || remote_endpoint_out == NULL || payload_out == NULL) {
        return ENVOY_RESULT_FAILED;
    }

    (void) remote_endpoint_size;
    remote_endpoint_out[0] = '\0';
    *payload_out = NULL;
    memset(endpoint, 0, sizeof(endpoint));

    listener_fd = envoy_worker_create_private_listener(&ctx->config, endpoint, sizeof(endpoint));
    if (listener_fd < 0) {
        *payload_out = utils_strdup_safe("Envoy worker stub failed to create private listener");
        return ENVOY_RESULT_FAILED;
    }

    close(listener_fd);

    if (asprintf(&message, "Envoy worker stub executed correctly. Private endpoint: %s", endpoint) >= 0 && message != NULL) {
        *payload_out = message;
    } else {
        *payload_out = utils_strdup_safe("Envoy worker stub executed correctly");
    }

    return ENVOY_RESULT_FAILED;
}

static EnvoyResultStatus envoy_worker_run_pledge(EnvoyWorkerContext *ctx, char *remote_endpoint_out, size_t remote_endpoint_size, char **payload_out) {
    char *sigil_path = NULL;
    char *nom_fitxer = NULL;
    char stable_endpoint[128];
    char md5[CITADEL_MD5_LENGTH + 1];
    size_t mida_fitxer = 0;
    int listener_fd = -1;
    char private_endpoint[128];
    NetworkFrame pledge_frame;
    NetworkFrame ack_frame;
    NetworkFrame md5_ack_frame;
    NetworkFrame response_frame;
    char *payload_text = NULL;
    char *frame_payload = NULL;
    char *ack_final = NULL;
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

    sigil_path = transfer_resolve_sigil_path(&ctx->config, ctx->ruta_fitxer);
    if (sigil_path == NULL || !transfer_obtenir_info_fitxer(sigil_path, &nom_fitxer, &mida_fitxer, md5)) {
        free(sigil_path);
        free(nom_fitxer);
        *payload_out = utils_strdup_safe("Could not prepare pledge sigil.");
        return ENVOY_RESULT_FAILED;
    }

    listener_fd = envoy_worker_create_private_listener(&ctx->config, private_endpoint, sizeof(private_endpoint));
    if (listener_fd < 0) {
        free(sigil_path);
        free(nom_fitxer);
        *payload_out = utils_strdup_safe("Could not create private Envoy listener.");
        return ENVOY_RESULT_FAILED;
    }

    if (!envoy_worker_build_endpoint(ctx->config.ip_regne, ctx->config.port_regne, stable_endpoint, sizeof(stable_endpoint)) || asprintf(&payload_text, "%s&%s&%zu&%s&%s", ctx->config.nom_regne, nom_fitxer, mida_fitxer, md5, stable_endpoint) < 0 || payload_text == NULL || !frame_set(&pledge_frame, FRAME_TYPE_PLEDGE, private_endpoint, ctx->regne, payload_text, strlen(payload_text)) || !envoy_worker_send_frame_to_realm(ctx, ctx->regne, &pledge_frame)) {
        *payload_out = utils_strdup_safe("Could not send pledge request.");
        goto cleanup;
    }
    free(payload_text);
    payload_text = NULL;

    if (!envoy_worker_wait_frame_type(listener_fd, FRAME_TYPE_ACK, ENVOY_WORKER_PLEDGE_TIMEOUT_SECONDS, &ack_frame)) {
        if (errno == ETIMEDOUT) {
            result = ENVOY_RESULT_TIMEOUT;
        } else {
            result = ENVOY_RESULT_FAILED;
        }

        if (result == ENVOY_RESULT_TIMEOUT) {
            *payload_out = utils_strdup_safe("Timed out waiting for pledge ACK.");
        } else {
            *payload_out = utils_strdup_safe("Invalid pledge ACK received.");
        }
        goto cleanup;
    }

    frame_payload = envoy_worker_frame_data_text(&ack_frame);
    if (frame_payload == NULL || !envoy_worker_payload_starts_with(frame_payload, "OK&")) {
        *payload_out = utils_strdup_safe("Pledge request was not acknowledged.");
        goto cleanup;
    }
    free(frame_payload);
    frame_payload = NULL;

    if (!envoy_worker_send_file_fragments(ctx, private_endpoint, ctx->regne, sigil_path, FRAME_TYPE_SIGIL_DATA)) {
        *payload_out = utils_strdup_safe("Could not send pledge sigil data.");
        goto cleanup;
    }

    if (!envoy_worker_wait_frame_type(listener_fd, FRAME_TYPE_MD5_ACK, ENVOY_WORKER_PLEDGE_TIMEOUT_SECONDS, &md5_ack_frame)) {
        if (errno == ETIMEDOUT) {
            result = ENVOY_RESULT_TIMEOUT;
        } else {
            result = ENVOY_RESULT_FAILED;
        }

        if (result == ENVOY_RESULT_TIMEOUT) {
            *payload_out = utils_strdup_safe("Timed out waiting for MD5 ACK.");
        } else {
            *payload_out = utils_strdup_safe("Invalid MD5 ACK received.");
        }
        goto cleanup;
    }

    frame_payload = envoy_worker_frame_data_text(&md5_ack_frame);
    if (frame_payload == NULL || !envoy_worker_payload_starts_with(frame_payload, "CHECK_OK&")) {
        *payload_out = utils_strdup_safe("Pledge sigil verification failed.");
        goto cleanup;
    }
    free(frame_payload);
    frame_payload = NULL;

    if (!envoy_worker_wait_frame_type(listener_fd, FRAME_TYPE_PLEDGE_RESPONSE, ENVOY_WORKER_PLEDGE_TIMEOUT_SECONDS, &response_frame)) {
        if (errno == ETIMEDOUT) {
            result = ENVOY_RESULT_TIMEOUT;
        } else {
            result = ENVOY_RESULT_FAILED;
        }

        if (result == ENVOY_RESULT_TIMEOUT) {
            *payload_out = utils_strdup_safe("Timed out waiting for pledge response.");
        } else {
            *payload_out = utils_strdup_safe("Invalid pledge response received.");
        }
        goto cleanup;
    }

    frame_payload = envoy_worker_frame_data_text(&response_frame);
    if (frame_payload == NULL) {
        *payload_out = utils_strdup_safe("Invalid pledge response received.");
        goto cleanup;
    }

    if (asprintf(&ack_final, "OK&%s", ctx->config.nom_regne) >= 0 && ack_final != NULL) {
        (void) envoy_worker_send_ack(response_frame.origen, private_endpoint, "", ack_final);
        free(ack_final);
        ack_final = NULL;
    }

    if (envoy_worker_payload_starts_with(frame_payload, "ACCEPT&")) {
        char *response_copy = utils_strdup_safe(frame_payload);
        char *decision = NULL;
        char *nom_regne = NULL;
        char *stable = NULL;

        if (response_copy == NULL) {
            *payload_out = utils_strdup_safe("Invalid pledge response received.");
            goto cleanup;
        }

        decision = strtok(response_copy, "&");
        nom_regne = strtok(NULL, "&");
        stable = strtok(NULL, "&");
        (void) decision;
        (void) nom_regne;

        if (stable != NULL && stable[0] != '\0') {
            char ip[64];
            int port = 0;
            if (envoy_worker_parse_endpoint(stable, ip, sizeof(ip), &port)) {
                strncpy(remote_endpoint_out, stable, remote_endpoint_size - 1);
                remote_endpoint_out[remote_endpoint_size - 1] = '\0';
            }
        }
        if (remote_endpoint_out[0] == '\0') {
            strncpy(remote_endpoint_out, response_frame.origen, remote_endpoint_size - 1);
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
    free(ack_final);
    if (listener_fd >= 0) {
        close(listener_fd);
    }
    free(sigil_path);
    free(nom_fitxer);
    return result;
}

static EnvoyResultStatus envoy_worker_run_products(EnvoyWorkerContext *ctx, char *remote_endpoint_out, size_t remote_endpoint_size, char **payload_out) {
    int listener_fd = -1;
    char private_endpoint[128];
    NetworkFrame request_frame;
    NetworkFrame response_frame;
    char *response_text = NULL;
    char *nom_fitxer = NULL;
    size_t expected_size = 0;
    char expected_md5[CITADEL_MD5_LENGTH + 1];
    char *catalog_text = NULL;
    size_t catalog_size = 0;
    char actual_md5[CITADEL_MD5_LENGTH + 1];
    char *ack_payload = NULL;
    char *md5_payload = NULL;
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

    if (!frame_set(&request_frame, FRAME_TYPE_PRODUCTS_REQUEST, private_endpoint, ctx->regne, ctx->config.nom_regne, strlen(ctx->config.nom_regne)) || !envoy_worker_send_frame_to_realm(ctx, ctx->regne, &request_frame)) {
        *payload_out = utils_strdup_safe("Could not send products request.");
        goto cleanup;
    }

    if (!envoy_worker_wait_frame_type(listener_fd, FRAME_TYPE_PRODUCTS_RESPONSE, ENVOY_WORKER_PRODUCTS_TIMEOUT_SECONDS, &response_frame)) {
        if (errno == ETIMEDOUT) {
            result = ENVOY_RESULT_TIMEOUT;
        } else {
            result = ENVOY_RESULT_FAILED;
        }

        if (result == ENVOY_RESULT_TIMEOUT) {
            *payload_out = utils_strdup_safe("Timed out waiting for products response.");
        } else {
            *payload_out = utils_strdup_safe("Invalid products response received.");
        }
        goto cleanup;
    }

    strncpy(remote_endpoint_out, response_frame.origen, remote_endpoint_size - 1);
    remote_endpoint_out[remote_endpoint_size - 1] = '\0';

    response_text = envoy_worker_frame_data_text(&response_frame);
    if (response_text == NULL || !envoy_worker_parse_header_triplet(response_text, &nom_fitxer, &expected_size, expected_md5)) {
        *payload_out = utils_strdup_safe("Invalid products response received.");
        goto cleanup;
    }

    if (asprintf(&ack_payload, "OK&%s", ctx->config.nom_regne) < 0 || ack_payload == NULL) {
        *payload_out = utils_strdup_safe("Could not acknowledge products response.");
        goto cleanup;
    }
    if (!envoy_worker_send_ack(response_frame.origen, private_endpoint, "", ack_payload)) {
        *payload_out = utils_strdup_safe("Could not acknowledge products response.");
        goto cleanup;
    }
    free(ack_payload);
    ack_payload = NULL;

    if (!envoy_worker_receive_file_payload(listener_fd, FRAME_TYPE_PRODUCTS_DATA, expected_size, ENVOY_WORKER_PRODUCTS_TIMEOUT_SECONDS, &catalog_text, &catalog_size)) {
        if (errno == ETIMEDOUT) {
            result = ENVOY_RESULT_TIMEOUT;
        } else {
            result = ENVOY_RESULT_FAILED;
        }

        if (result == ENVOY_RESULT_TIMEOUT) {
            *payload_out = utils_strdup_safe("Timed out receiving products data.");
        } else {
            *payload_out = utils_strdup_safe("Invalid products data received.");
        }
        goto cleanup;
    }

    if (!envoy_worker_compute_md5_from_memory(ctx, catalog_text, catalog_size, actual_md5)) {
        *payload_out = utils_strdup_safe("Could not verify products payload.");
        goto cleanup;
    }

    {
        const char *md5_status = NULL;

        if (strcmp(actual_md5, expected_md5) == 0) {
            md5_status = "CHECK_OK";
        } else {
            md5_status = "CHECK_KO";
        }

        if (asprintf(&md5_payload, "%s&%s", md5_status, ctx->config.nom_regne) < 0 || md5_payload == NULL) {
            *payload_out = utils_strdup_safe("Could not send products MD5 acknowledgement.");
            goto cleanup;
        }
    }
    if (!envoy_worker_send_md5_ack(response_frame.origen, private_endpoint, "", md5_payload)) {
        *payload_out = utils_strdup_safe("Could not send products MD5 acknowledgement.");
        goto cleanup;
    }

    if (strcmp(actual_md5, expected_md5) != 0) {
        *payload_out = utils_strdup_safe("products payload failed MD5 verification.");
        goto cleanup;
    }

    *payload_out = catalog_text;
    catalog_text = NULL;
    result = ENVOY_RESULT_OK;

cleanup:
    free(ack_payload);
    free(md5_payload);
    if (listener_fd >= 0) {
        close(listener_fd);
    }
    free(response_text);
    free(nom_fitxer);
    free(catalog_text);
    return result;
}

int envoy_worker_main(int argc, char **argv) {
    EnvoyWorkerContext context;
    EnvoyResultHeader header;
    char endpoint_remot[128];
    char *payload_text = NULL;
    bool write_ok = false;
    EnvoyResultStatus result = ENVOY_RESULT_FAILED;

    envoy_worker_context_init(&context);
    memset(&header, 0, sizeof(header));
    memset(endpoint_remot, 0, sizeof(endpoint_remot));

    if (!envoy_worker_parse_arguments(&context, argc, argv)) {
        envoy_worker_context_free(&context);
        return EXIT_FAILURE;
    }

    context.config_carregada = config_load(context.config_path, &context.config);
    if (!context.config_carregada) {
        payload_text = utils_strdup_safe("Envoy worker could not load config.");
    } else if (context.tipus_missio == ENVOY_MISSION_PLEDGE_RESPONSE) {
        result = envoy_worker_run_pledge_response(&context, endpoint_remot, sizeof(endpoint_remot), &payload_text);
    } else if (context.tipus_missio == ENVOY_MISSION_PLEDGE && strcmp(context.ruta_fitxer, "stub-sigil") != 0) {
        result = envoy_worker_run_pledge(&context, endpoint_remot, sizeof(endpoint_remot), &payload_text);
    } else if (context.tipus_missio == ENVOY_MISSION_PRODUCTS) {
        result = envoy_worker_run_products(&context, endpoint_remot, sizeof(endpoint_remot), &payload_text);
    } else if (context.tipus_missio == ENVOY_MISSION_TRADE) {
        result = envoy_worker_run_trade(&context, endpoint_remot, sizeof(endpoint_remot), &payload_text);
    } else {
        result = envoy_worker_run_stub(&context, endpoint_remot, sizeof(endpoint_remot), &payload_text);
    }

    header.magic = ENVOY_RESULT_MAGIC;
    header.id_envoy = context.id_envoy;
    header.tipus_missio = (int) context.tipus_missio;
    header.estat_resultat = (int) result;
    if (context.regne != NULL) {
        strncpy(header.regne, context.regne, sizeof(header.regne) - 1);
    } else {
        strncpy(header.regne, "", sizeof(header.regne) - 1);
    }
    header.regne[sizeof(header.regne) - 1] = '\0';
    strncpy(header.endpoint_remot, endpoint_remot, sizeof(header.endpoint_remot) - 1);
    header.endpoint_remot[sizeof(header.endpoint_remot) - 1] = '\0';
    if (payload_text != NULL) {
        header.mida_payload = (uint32_t) strlen(payload_text);
    } else {
        header.mida_payload = (uint32_t) strlen("");
    }

    if (payload_text != NULL) {
        write_ok = envoy_result_write(context.pipe_fd, &header, payload_text);
    } else {
        write_ok = envoy_result_write(context.pipe_fd, &header, "");
    }
    close(context.pipe_fd);

    free(payload_text);
    envoy_worker_context_free(&context);
    if (write_ok) {
        return 0;
    }

    return 1;
}
