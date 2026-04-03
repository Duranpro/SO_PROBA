#include "network.h"

#include "../utils/utils.h"

typedef struct {
    char ip[64];
    int port;
} ParsedEndpoint;

static void network_log_line(const char *text) {
    char *message = NULL;

    if (text == NULL) {
        return;
    }

    if (asprintf(&message, ">>> %s\n", text) >= 0 && message != NULL) {
        utils_print(message);
        free(message);
    }
}

static char *network_build_self_endpoint(const CitadelConfig *config) {
    char *endpoint = NULL;

    if (config == NULL || config->ip == NULL) {
        return NULL;
    }

    if (asprintf(&endpoint, "%s:%d", config->ip, config->port) < 0) {
        return NULL;
    }

    return endpoint;
}

static bool network_parse_endpoint(const char *text, ParsedEndpoint *endpoint) {
    char *copy = NULL;
    char *separator = NULL;

    if (text == NULL || endpoint == NULL) {
        return false;
    }

    copy = utils_strdup_safe(text);
    if (copy == NULL) {
        return false;
    }

    separator = strrchr(copy, ':');
    if (separator == NULL) {
        free(copy);
        return false;
    }

    *separator = '\0';
    separator++;

    if (!utils_parse_int(separator, &endpoint->port) || endpoint->port <= 0) {
        free(copy);
        return false;
    }

    strncpy(endpoint->ip, copy, sizeof(endpoint->ip) - 1);
    endpoint->ip[sizeof(endpoint->ip) - 1] = '\0';
    free(copy);
    return endpoint->ip[0] != '\0';
}

static bool network_route_has_address(const RouteInfo *route) {
    if (route == NULL || route->ip == NULL || route->port <= 0) {
        return false;
    }

    return strcmp(route->ip, "*.*.*.*") != 0;
}

static const char *network_status_text(AllianceStatus status) {
    switch (status) {
        case ALLIANCE_PENDING_OUT:
            return "PENDING";
        case ALLIANCE_PENDING_IN:
            return "AWAITING_RESPONSE";
        case ALLIANCE_ALLIED:
            return "ALLIED";
        case ALLIANCE_INACTIVE:
            return "INACTIVE";
        case ALLIANCE_REJECTED:
            return "REJECTED";
        case ALLIANCE_FAILED:
            return "FAILED";
        case ALLIANCE_NONE:
        default:
            return "NONE";
    }
}

static void network_free_catalog(AllianceEntry *entry) {
    if (entry == NULL) {
        return;
    }

    stock_free_products(entry->catalog, entry->catalog_count);
    entry->catalog = NULL;
    entry->catalog_count = 0;
}

static void network_alliance_free(AllianceEntry *entry) {
    if (entry == NULL) {
        return;
    }

    free(entry->realm_name);
    free(entry->known_endpoint);
    free(entry->pending_origin_endpoint);
    network_free_catalog(entry);
    memset(entry, 0, sizeof(*entry));
}

static AllianceEntry *network_find_entry_locked(NetworkContext *network, const char *realm_name) {
    size_t i = 0;

    if (network == NULL || realm_name == NULL) {
        return NULL;
    }

    for (i = 0; i < network->alliance_count; ++i) {
        if (utils_equals_ignore_case(network->alliances[i].realm_name, realm_name)) {
            return &network->alliances[i];
        }
    }

    return NULL;
}

static AllianceEntry *network_find_entry_by_endpoint_locked(NetworkContext *network, const char *endpoint) {
    size_t i = 0;

    if (network == NULL || endpoint == NULL) {
        return NULL;
    }

    for (i = 0; i < network->alliance_count; ++i) {
        if (network->alliances[i].known_endpoint != NULL &&
            strcmp(network->alliances[i].known_endpoint, endpoint) == 0) {
            return &network->alliances[i];
        }
        if (network->alliances[i].pending_origin_endpoint != NULL &&
            strcmp(network->alliances[i].pending_origin_endpoint, endpoint) == 0) {
            return &network->alliances[i];
        }
    }

    return NULL;
}

static char *network_find_realm_from_route(NetworkContext *network, const char *endpoint) {
    ParsedEndpoint parsed;
    size_t i = 0;

    if (network == NULL || endpoint == NULL || !network_parse_endpoint(endpoint, &parsed)) {
        return NULL;
    }

    for (i = 0; i < network->config->route_count; ++i) {
        const RouteInfo *route = &network->config->routes[i];
        if (utils_equals_ignore_case(route->realm_name, "DEFAULT")) {
            continue;
        }
        if (route->ip != NULL && route->port == parsed.port && strcmp(route->ip, parsed.ip) == 0) {
            return utils_strdup_safe(route->realm_name);
        }
    }

    return NULL;
}

static char *network_find_realm_by_endpoint(NetworkContext *network, const char *endpoint) {
    AllianceEntry *entry = NULL;
    char *realm = NULL;

    pthread_mutex_lock(&network->lock);
    entry = network_find_entry_by_endpoint_locked(network, endpoint);
    if (entry != NULL) {
        realm = utils_strdup_safe(entry->realm_name);
    }
    pthread_mutex_unlock(&network->lock);

    if (realm != NULL) {
        return realm;
    }

    return network_find_realm_from_route(network, endpoint);
}

static bool network_set_entry_endpoint(AllianceEntry *entry, const char *endpoint) {
    char *copy = NULL;

    if (entry == NULL || endpoint == NULL) {
        return false;
    }

    copy = utils_strdup_safe(endpoint);
    if (copy == NULL) {
        return false;
    }

    free(entry->known_endpoint);
    entry->known_endpoint = copy;
    return true;
}

static bool network_store_pending_origin(AllianceEntry *entry, const char *endpoint) {
    char *copy = NULL;

    if (entry == NULL || endpoint == NULL) {
        return false;
    }

    copy = utils_strdup_safe(endpoint);
    if (copy == NULL) {
        return false;
    }

    free(entry->pending_origin_endpoint);
    entry->pending_origin_endpoint = copy;
    return true;
}

static bool network_set_catalog(AllianceEntry *entry, Product *products, size_t count) {
    if (entry == NULL) {
        return false;
    }

    network_free_catalog(entry);
    entry->catalog = products;
    entry->catalog_count = count;
    return true;
}

static void network_outbound_reset(NetworkContext *network) {
    if (network == NULL) {
        return;
    }

    free(network->outbound.realm_name);
    free(network->outbound.file_name);
    free(network->outbound.file_path);
    memset(&network->outbound, 0, sizeof(network->outbound));
}

static void network_inbound_reset(NetworkContext *network) {
    if (network == NULL) {
        return;
    }

    if (network->inbound.file_fd >= 0) {
        close(network->inbound.file_fd);
    }
    free(network->inbound.realm_name);
    free(network->inbound.origin_endpoint);
    free(network->inbound.file_name);
    free(network->inbound.file_path);
    memset(&network->inbound, 0, sizeof(network->inbound));
    network->inbound.file_fd = -1;
}

static bool network_socket_init(void) {
#ifdef _WIN32
    WSADATA data;
    return WSAStartup(MAKEWORD(2, 2), &data) == 0;
#else
    return true;
#endif
}

static void network_socket_cleanup(void) {
#ifdef _WIN32
    WSACleanup();
#endif
}

static citadel_socket_t network_create_listener(const CitadelConfig *config) {
    citadel_socket_t server_fd = CITADEL_INVALID_SOCKET;
    struct sockaddr_in address;
    int option = 1;

    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons((uint16_t) config->port);

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == CITADEL_INVALID_SOCKET) {
        return CITADEL_INVALID_SOCKET;
    }

    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, (const char *) &option, sizeof(option));

    if (inet_pton(AF_INET, config->ip, &address.sin_addr) <= 0) {
        address.sin_addr.s_addr = htonl(INADDR_ANY);
    }

    if (bind(server_fd, (struct sockaddr *) &address, sizeof(address)) != 0) {
        address.sin_addr.s_addr = htonl(INADDR_ANY);
        if (bind(server_fd, (struct sockaddr *) &address, sizeof(address)) != 0) {
            CITADEL_SOCKET_CLOSE(server_fd);
            return CITADEL_INVALID_SOCKET;
        }
    }

    if (listen(server_fd, 16) != 0) {
        CITADEL_SOCKET_CLOSE(server_fd);
        return CITADEL_INVALID_SOCKET;
    }

    return server_fd;
}

static bool network_send_exact(citadel_socket_t socket_fd, const unsigned char *buffer, size_t size) {
    ssize_t written = 0;

    if (buffer == NULL || size != CITADEL_FRAME_SIZE) {
        return false;
    }

    written = send(socket_fd, (const char *) buffer, (int) size, 0);
    return written == (ssize_t) size;
}

static bool network_recv_exact(citadel_socket_t socket_fd, unsigned char *buffer, size_t size) {
    ssize_t bytes = 0;

    if (buffer == NULL || size != CITADEL_FRAME_SIZE) {
        return false;
    }

#ifdef _WIN32
    bytes = recv(socket_fd, (char *) buffer, (int) size, 0);
#else
    bytes = recv(socket_fd, (char *) buffer, (int) size, MSG_WAITALL);
#endif
    return bytes == (ssize_t) size;
}

static bool network_is_known_type(uint8_t type) {
    switch (type) {
        case FRAME_TYPE_PLEDGE:
        case FRAME_TYPE_SIGIL_DATA:
        case FRAME_TYPE_PLEDGE_RESPONSE:
        case FRAME_TYPE_PRODUCTS_REQUEST:
        case FRAME_TYPE_PRODUCTS_RESPONSE:
        case FRAME_TYPE_PRODUCTS_DATA:
        case FRAME_TYPE_TRADE_HEADER:
        case FRAME_TYPE_TRADE_DATA:
        case FRAME_TYPE_TRADE_RESPONSE:
        case FRAME_TYPE_UNKNOWN_REALM:
        case FRAME_TYPE_AUTH_ERROR:
        case FRAME_TYPE_PING:
        case FRAME_TYPE_DISCONNECT:
        case FRAME_TYPE_ACK:
        case FRAME_TYPE_MD5_ACK:
        case FRAME_TYPE_NACK:
            return true;
        default:
            return false;
    }
}

static bool network_is_blank_destination_type(uint8_t type) {
    return type == FRAME_TYPE_ACK || type == FRAME_TYPE_MD5_ACK || type == FRAME_TYPE_NACK;
}

static char *network_extract_origin_from_raw(const unsigned char buffer[CITADEL_FRAME_SIZE]) {
    char endpoint[CITADEL_FRAME_ORIGIN_SIZE + 1];

    if (buffer == NULL) {
        return NULL;
    }

    memset(endpoint, 0, sizeof(endpoint));
    memcpy(endpoint, buffer + 1, CITADEL_FRAME_ORIGIN_SIZE);
    endpoint[CITADEL_FRAME_ORIGIN_SIZE] = '\0';
    if (strchr(endpoint, ':') == NULL) {
        return NULL;
    }

    return utils_strdup_safe(endpoint);
}

static bool network_resolve_next_endpoint(NetworkContext *network, const char *realm_name, char **endpoint_out) {
    AllianceEntry *entry = NULL;
    const RouteInfo *route = NULL;

    if (network == NULL || realm_name == NULL || endpoint_out == NULL) {
        return false;
    }

    *endpoint_out = NULL;

    pthread_mutex_lock(&network->lock);
    entry = network_find_entry_locked(network, realm_name);
    if (entry != NULL && entry->status == ALLIANCE_ALLIED && entry->known_endpoint != NULL) {
        *endpoint_out = utils_strdup_safe(entry->known_endpoint);
        pthread_mutex_unlock(&network->lock);
        return *endpoint_out != NULL;
    }
    pthread_mutex_unlock(&network->lock);

    route = config_find_route(network->config, realm_name);
    if (network_route_has_address(route)) {
        return asprintf(endpoint_out, "%s:%d", route->ip, route->port) >= 0;
    }

    route = config_find_route(network->config, "DEFAULT");
    if (network_route_has_address(route)) {
        return asprintf(endpoint_out, "%s:%d", route->ip, route->port) >= 0;
    }

    return false;
}

static bool network_send_serialized_to_endpoint(const char *endpoint_text,
                                                const unsigned char buffer[CITADEL_FRAME_SIZE]) {
    ParsedEndpoint endpoint;
    citadel_socket_t socket_fd = CITADEL_INVALID_SOCKET;
    struct sockaddr_in address;

    if (!network_parse_endpoint(endpoint_text, &endpoint)) {
        return false;
    }

    socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd == CITADEL_INVALID_SOCKET) {
        return false;
    }

    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons((uint16_t) endpoint.port);
    if (inet_pton(AF_INET, endpoint.ip, &address.sin_addr) <= 0) {
        CITADEL_SOCKET_CLOSE(socket_fd);
        return false;
    }

    if (connect(socket_fd, (struct sockaddr *) &address, sizeof(address)) != 0) {
        CITADEL_SOCKET_CLOSE(socket_fd);
        return false;
    }

    if (!network_send_exact(socket_fd, buffer, CITADEL_FRAME_SIZE)) {
        CITADEL_SOCKET_CLOSE(socket_fd);
        return false;
    }

    CITADEL_SOCKET_CLOSE(socket_fd);
    return true;
}

static bool network_send_frame_to_endpoint(const char *endpoint, const NetworkFrame *frame) {
    unsigned char buffer[CITADEL_FRAME_SIZE];

    if (endpoint == NULL || frame == NULL) {
        return false;
    }

    frame_serialize(frame, buffer);
    return network_send_serialized_to_endpoint(endpoint, buffer);
}

static bool network_send_frame_to_realm(NetworkContext *network, const char *realm_name, const NetworkFrame *frame) {
    unsigned char buffer[CITADEL_FRAME_SIZE];
    char *endpoint = NULL;
    char *line = NULL;
    bool ok = false;

    if (!network_resolve_next_endpoint(network, realm_name, &endpoint)) {
        if (asprintf(&line, "No route available to %s.", realm_name) >= 0 && line != NULL) {
            network_log_line(line);
            free(line);
        }
        return false;
    }

    frame_serialize(frame, buffer);
    ok = network_send_serialized_to_endpoint(endpoint, buffer);
    if (!ok && asprintf(&line, "Could not reach %s using endpoint %s.", realm_name, endpoint) >= 0 && line != NULL) {
        network_log_line(line);
        free(line);
    }

    free(endpoint);
    return ok;
}

static bool network_send_blank_reply(const char *endpoint, uint8_t type, const char *status, const char *realm_name) {
    NetworkFrame frame;
    char *data = NULL;
    bool ok = false;

    if (endpoint == NULL || status == NULL || realm_name == NULL) {
        return false;
    }

    if (asprintf(&data, "%s&%s", status, realm_name) < 0 || data == NULL) {
        return false;
    }

    ok = frame_set(&frame, type, "", "", data, strlen(data)) && network_send_frame_to_endpoint(endpoint, &frame);
    free(data);
    return ok;
}

static bool network_send_blank_reply_with_realm_fallback(NetworkContext *network, const char *endpoint,
                                                         const char *realm_name, uint8_t type,
                                                         const char *status, const char *ack_realm) {
    NetworkFrame frame;
    char *data = NULL;
    char *fallback_endpoint = NULL;
    bool ok = false;

    if (network == NULL || status == NULL || ack_realm == NULL) {
        return false;
    }

    ok = network_send_blank_reply(endpoint, type, status, ack_realm);

    if (!ok && realm_name != NULL && realm_name[0] != '\0' &&
        network_resolve_next_endpoint(network, realm_name, &fallback_endpoint)) {
        if (asprintf(&data, "%s&%s", status, ack_realm) < 0 || data == NULL) {
            free(fallback_endpoint);
            return false;
        }
        if (!frame_set(&frame, type, "", "", data, strlen(data))) {
            free(data);
            free(fallback_endpoint);
            return false;
        }
        ok = network_send_frame_to_endpoint(fallback_endpoint, &frame);
        free(data);
    }

    free(fallback_endpoint);
    return ok;
}

static bool network_send_blank_payload(const char *endpoint, uint8_t type, const char *payload) {
    NetworkFrame frame;

    if (endpoint == NULL || payload == NULL) {
        return false;
    }

    if (!frame_set(&frame, type, "", "", payload, strlen(payload))) {
        return false;
    }

    return network_send_frame_to_endpoint(endpoint, &frame);
}

static bool network_send_protocol_nack(NetworkContext *network, const char *endpoint) {
    if (network == NULL || endpoint == NULL) {
        return false;
    }

    return network_send_blank_payload(endpoint, FRAME_TYPE_NACK, network->config->realm_name);
}

static char *network_extract_first_token(const char *text) {
    char *copy = NULL;
    char *separator = NULL;

    if (text == NULL) {
        return NULL;
    }

    copy = utils_strdup_safe(text);
    if (copy == NULL) {
        return NULL;
    }

    separator = strchr(copy, '&');
    if (separator != NULL) {
        *separator = '\0';
    }
    utils_trim(copy);
    return copy;
}

static char *network_derive_origin_realm(NetworkContext *network, const NetworkFrame *frame) {
    char *data = NULL;
    char *realm = NULL;

    if (network == NULL || frame == NULL) {
        return NULL;
    }

    if (frame->type == FRAME_TYPE_PLEDGE || frame->type == FRAME_TYPE_PRODUCTS_REQUEST) {
        data = frame_data_to_text(frame);
        realm = network_extract_first_token(data);
        free(data);
        return realm;
    }

    return network_find_realm_by_endpoint(network, frame->origin);
}

static bool network_send_unknown_realm(NetworkContext *network, const char *origin_realm, const char *unknown_realm) {
    NetworkFrame frame;
    char *origin = NULL;
    char *data = NULL;
    bool ok = false;

    if (network == NULL || origin_realm == NULL || unknown_realm == NULL) {
        return false;
    }

    origin = network_build_self_endpoint(network->config);
    if (origin == NULL) {
        return false;
    }

    if (asprintf(&data, "UNKNOWN_REALM&%s", unknown_realm) < 0 || data == NULL) {
        free(origin);
        return false;
    }

    ok = frame_set(&frame, FRAME_TYPE_UNKNOWN_REALM, origin, origin_realm, data, strlen(data)) &&
         network_send_frame_to_realm(network, origin_realm, &frame);
    free(origin);
    free(data);
    return ok;
}

static bool network_send_auth_error(NetworkContext *network, const char *origin_realm, const char *realm_name) {
    NetworkFrame frame;
    char *origin = NULL;
    char *data = NULL;
    bool ok = false;

    if (network == NULL || origin_realm == NULL || realm_name == NULL) {
        return false;
    }

    origin = network_build_self_endpoint(network->config);
    if (origin == NULL) {
        return false;
    }

    if (asprintf(&data, "AUTH&%s", realm_name) < 0 || data == NULL) {
        free(origin);
        return false;
    }

    ok = frame_set(&frame, FRAME_TYPE_AUTH_ERROR, origin, origin_realm, data, strlen(data)) &&
         network_send_frame_to_realm(network, origin_realm, &frame);
    free(origin);
    free(data);
    return ok;
}

static bool network_parse_header_triplet(const char *data_text, char **file_name_out, size_t *size_out,
                                         char md5_out[CITADEL_MD5_LENGTH + 1]) {
    char *copy = NULL;
    char *file_name = NULL;
    char *size_text = NULL;
    char *md5 = NULL;
    int size_value = 0;

    if (data_text == NULL || file_name_out == NULL || size_out == NULL || md5_out == NULL) {
        return false;
    }

    copy = utils_strdup_safe(data_text);
    if (copy == NULL) {
        return false;
    }

    file_name = strtok(copy, "&");
    size_text = strtok(NULL, "&");
    md5 = strtok(NULL, "&");

    if (file_name == NULL || size_text == NULL || md5 == NULL || strlen(md5) != CITADEL_MD5_LENGTH ||
        !utils_parse_int(size_text, &size_value) || size_value < 0) {
        free(copy);
        return false;
    }

    *file_name_out = utils_strdup_safe(file_name);
    if (*file_name_out == NULL) {
        free(copy);
        return false;
    }

    *size_out = (size_t) size_value;
    memcpy(md5_out, md5, CITADEL_MD5_LENGTH);
    md5_out[CITADEL_MD5_LENGTH] = '\0';
    free(copy);
    return true;
}

static bool network_begin_inbound_transfer(NetworkContext *network, TransferKind kind, const char *realm_name,
                                           const char *origin_endpoint, const char *file_name, size_t file_size,
                                           const char *md5_text) {
    char *file_path = NULL;
    int file_fd = -1;

    if (network == NULL || realm_name == NULL || origin_endpoint == NULL || file_name == NULL || md5_text == NULL) {
        return false;
    }

    if (!utils_ensure_directory(network->config->workdir)) {
        return false;
    }

    file_path = utils_build_path(network->config->workdir, file_name);
    if (file_path == NULL) {
        return false;
    }

    file_fd = open(file_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (file_fd < 0) {
        free(file_path);
        return false;
    }

    network_inbound_reset(network);
    network->inbound.active = true;
    network->inbound.kind = kind;
    network->inbound.realm_name = utils_strdup_safe(realm_name);
    network->inbound.origin_endpoint = utils_strdup_safe(origin_endpoint);
    network->inbound.file_name = utils_strdup_safe(file_name);
    network->inbound.file_path = file_path;
    network->inbound.file_fd = file_fd;
    network->inbound.file_size = file_size;
    network->inbound.bytes_received = 0;
    strncpy(network->inbound.md5, md5_text, CITADEL_MD5_LENGTH);
    network->inbound.md5[CITADEL_MD5_LENGTH] = '\0';

    return network->inbound.realm_name != NULL &&
           network->inbound.origin_endpoint != NULL &&
           network->inbound.file_name != NULL;
}

static bool network_send_outbound_file_data(NetworkContext *network) {
    int fd = -1;
    unsigned char block[CITADEL_FRAME_DATA_SIZE];
    char *origin = NULL;
    bool ok = true;

    if (network == NULL || !network->outbound.active || network->outbound.file_path == NULL ||
        network->outbound.realm_name == NULL) {
        return false;
    }

    fd = open(network->outbound.file_path, O_RDONLY);
    if (fd < 0) {
        return false;
    }

    origin = network_build_self_endpoint(network->config);
    if (origin == NULL) {
        close(fd);
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

        if (!frame_set(&frame, network->outbound.data_type, origin, network->outbound.realm_name,
                       block, (size_t) bytes) ||
            !network_send_frame_to_realm(network, network->outbound.realm_name, &frame)) {
            ok = false;
            break;
        }
    }

    free(origin);
    close(fd);
    return ok;
}

static void network_print_catalog(const char *realm_name, const Product *products, size_t count) {
    size_t i = 0;
    char *line = NULL;

    if (realm_name == NULL) {
        return;
    }

    if (asprintf(&line, "Listing products from %s:", realm_name) >= 0 && line != NULL) {
        utils_println(line);
        free(line);
    }

    for (i = 0; i < count; ++i) {
        if (asprintf(&line, "%zu. %s (%d units)\n", i + 1, products[i].name, products[i].amount) >= 0 &&
            line != NULL) {
            utils_print(line);
            free(line);
        }
    }
}

static bool network_finalize_inbound_transfer(NetworkContext *network) {
    char md5[CITADEL_MD5_LENGTH + 1];
    bool ok = false;
    Product *products = NULL;
    size_t count = 0;
    char *reason = NULL;
    AllianceEntry *entry = NULL;

    if (network == NULL || !network->inbound.active || network->inbound.file_path == NULL ||
        network->inbound.origin_endpoint == NULL || network->inbound.realm_name == NULL) {
        return false;
    }

    if (network->inbound.file_fd >= 0) {
        close(network->inbound.file_fd);
        network->inbound.file_fd = -1;
    }

    ok = transfer_compute_md5sum(network->inbound.file_path, md5) &&
         strcmp(md5, network->inbound.md5) == 0;

    if (!network_send_blank_reply_with_realm_fallback(network, network->inbound.origin_endpoint,
                                                      network->inbound.realm_name, FRAME_TYPE_MD5_ACK,
                                                      ok ? "CHECK_OK" : "CHECK_KO",
                                                      network->config->realm_name)) {
        ok = false;
    }

    if (!ok) {
        if (network->inbound.kind == TRANSFER_SIGIL) {
            pthread_mutex_lock(&network->lock);
            entry = network_find_entry_locked(network, network->inbound.realm_name);
            if (entry != NULL) {
                entry->sigil_verified = false;
                entry->status = ALLIANCE_FAILED;
                free(entry->pending_origin_endpoint);
                entry->pending_origin_endpoint = NULL;
            }
            pthread_mutex_unlock(&network->lock);
        }
        network_inbound_reset(network);
        return false;
    }

    if (network->inbound.kind == TRANSFER_SIGIL) {
        pthread_mutex_lock(&network->lock);
        entry = network_find_entry_locked(network, network->inbound.realm_name);
        if (entry != NULL) {
            entry->sigil_verified = true;
        }
        pthread_mutex_unlock(&network->lock);
    } else if (network->inbound.kind == TRANSFER_PRODUCTS) {
        if (transfer_parse_catalog_file(network->inbound.file_path, &products, &count)) {
            pthread_mutex_lock(&network->lock);
            entry = network_find_entry_locked(network, network->inbound.realm_name);
            if (entry != NULL) {
                network_set_catalog(entry, products, count);
                entry->waiting_products = false;
                products = NULL;
                count = 0;
            }
            pthread_mutex_unlock(&network->lock);
            if (entry != NULL) {
                network_print_catalog(entry->realm_name, entry->catalog, entry->catalog_count);
            }
        }
        stock_free_products(products, count);
    } else if (network->inbound.kind == TRANSFER_ORDER) {
        if (transfer_parse_order_file(network->inbound.file_path, &products, &count)) {
            NetworkFrame response;
            char *origin = network_build_self_endpoint(network->config);
            const char *payload = NULL;

            if (stock_apply_order(network->stock, products, count, &reason)) {
                payload = "OK";
                network_log_line("Order processed successfully. Stock updated.");
            } else {
                payload = reason != NULL ? reason : "REJECT";
            }

            if (origin != NULL) {
                char *data = NULL;
                if (strcmp(payload, "OK") == 0) {
                    data = utils_strdup_safe("OK");
                } else if (asprintf(&data, "REJECT&%s", payload) < 0) {
                    data = NULL;
                }

                if (data != NULL &&
                    frame_set(&response, FRAME_TYPE_TRADE_RESPONSE, origin, network->inbound.realm_name,
                              data, strlen(data))) {
                    network_send_frame_to_realm(network, network->inbound.realm_name, &response);
                }

                free(data);
                free(origin);
            }
            free(reason);
        }
        stock_free_products(products, count);
    }

    network_inbound_reset(network);
    return true;
}

static void network_mark_timeout(AllianceEntry *entry) {
    if (entry == NULL) {
        return;
    }

    entry->status = ALLIANCE_FAILED;
    entry->deadline = 0;
}

static void network_check_timeouts(NetworkContext *network) {
    size_t i = 0;
    time_t now = time(NULL);

    pthread_mutex_lock(&network->lock);
    for (i = 0; i < network->alliance_count; ++i) {
        if (network->alliances[i].status == ALLIANCE_PENDING_OUT &&
            network->alliances[i].deadline > 0 &&
            now >= network->alliances[i].deadline) {
            network_mark_timeout(&network->alliances[i]);
            {
                char *line = NULL;
                if (asprintf(&line, "Pledge to %s has failed (TIMEOUT).", network->alliances[i].realm_name) >= 0 &&
                    line != NULL) {
                    network_log_line(line);
                    free(line);
                }
            }
        }
    }
    pthread_mutex_unlock(&network->lock);
}

static void network_handle_pledge(NetworkContext *network, const NetworkFrame *frame) {
    char *data = frame_data_to_text(frame);
    char *copy = NULL;
    char *origin_realm = NULL;
    char *sigil_name = NULL;
    char *size_text = NULL;
    char *md5 = NULL;
    int size_value = 0;
    AllianceEntry *entry = NULL;
    bool ok = false;

    if (data == NULL) {
        return;
    }

    copy = utils_strdup_safe(data);
    free(data);
    if (copy == NULL) {
        return;
    }

    origin_realm = strtok(copy, "&");
    sigil_name = strtok(NULL, "&");
    size_text = strtok(NULL, "&");
    md5 = strtok(NULL, "&");
    if (origin_realm == NULL || sigil_name == NULL || size_text == NULL || md5 == NULL ||
        !utils_parse_int(size_text, &size_value) || size_value < 0) {
        free(copy);
        return;
    }

    pthread_mutex_lock(&network->lock);
    entry = network_find_entry_locked(network, origin_realm);
    if (entry != NULL) {
        entry->status = ALLIANCE_PENDING_IN;
        entry->sigil_verified = false;
        network_store_pending_origin(entry, frame->origin);
        ok = !network->inbound.active &&
             network_begin_inbound_transfer(network, TRANSFER_SIGIL, origin_realm, frame->origin,
                                            sigil_name, (size_t) size_value, md5);
    }
    pthread_mutex_unlock(&network->lock);

    network_send_blank_reply_with_realm_fallback(network, frame->origin, origin_realm, FRAME_TYPE_ACK,
                                                 ok ? "OK" : "KO", network->config->realm_name);
    if (ok) {
        char *line = NULL;
        if (asprintf(&line, "Alliance request received from %s.", origin_realm) >= 0 && line != NULL) {
            network_log_line(line);
            free(line);
        }
    }

    free(copy);
}

static void network_handle_pledge_response(NetworkContext *network, const NetworkFrame *frame) {
    char *data = frame_data_to_text(frame);
    char *copy = NULL;
    char *decision = NULL;
    char *realm_name = NULL;
    AllianceEntry *entry = NULL;
    bool accepted = false;
    bool stale = false;
    char *ack_payload = NULL;

    if (data == NULL) {
        return;
    }

    copy = utils_strdup_safe(data);
    free(data);
    if (copy == NULL) {
        return;
    }

    decision = strtok(copy, "&");
    realm_name = strtok(NULL, "&");
    if (decision == NULL || realm_name == NULL) {
        free(copy);
        return;
    }

    accepted = utils_equals_ignore_case(decision, "ACCEPT");

    pthread_mutex_lock(&network->lock);
    entry = network_find_entry_locked(network, realm_name);
    if (entry == NULL || entry->status != ALLIANCE_PENDING_OUT || entry->deadline == 0 ||
        time(NULL) >= entry->deadline) {
        stale = true;
    } else {
        entry->deadline = 0;
        if (accepted) {
            entry->status = ALLIANCE_ALLIED;
            network_set_entry_endpoint(entry, frame->origin);
        } else {
            entry->status = ALLIANCE_REJECTED;
        }
    }
    pthread_mutex_unlock(&network->lock);

    if (stale) {
        network_send_blank_payload(frame->origin, FRAME_TYPE_NACK, network->config->realm_name);
    } else {
        if (asprintf(&ack_payload, "OK&%s", network->config->realm_name) >= 0 && ack_payload != NULL) {
            network_send_blank_payload(frame->origin, FRAME_TYPE_ACK, ack_payload);
        }
    }

    if (!stale) {
        char *line = NULL;
        if (asprintf(&line, "Alliance with %s %s.", realm_name,
                     accepted ? "forged successfully" : "was rejected") >= 0 && line != NULL) {
            network_log_line(line);
            free(line);
        }
    }

    free(ack_payload);
    free(copy);
}

static void network_handle_products_request(NetworkContext *network, const NetworkFrame *frame) {
    char *origin_realm = frame_data_to_text(frame);
    AllianceEntry *entry = NULL;
    char *file_path = NULL;
    char *file_name = NULL;
    char md5[CITADEL_MD5_LENGTH + 1];
    size_t file_size = 0;
    NetworkFrame header;
    char *origin = NULL;
    char *data = NULL;
    bool allowed = false;

    if (origin_realm == NULL) {
        return;
    }

    pthread_mutex_lock(&network->lock);
    entry = network_find_entry_locked(network, origin_realm);
    allowed = (entry != NULL && entry->status == ALLIANCE_ALLIED && !network->outbound.active);
    pthread_mutex_unlock(&network->lock);

    if (!allowed) {
        network_send_auth_error(network, origin_realm, origin_realm);
        free(origin_realm);
        return;
    }

    if (!transfer_write_inventory_file(network->config, network->stock, &file_path, &file_name, &file_size, md5)) {
        free(origin_realm);
        return;
    }

    origin = network_build_self_endpoint(network->config);
    if (origin != NULL &&
        asprintf(&data, "%s&%zu&%s", file_name, file_size, md5) >= 0 &&
        frame_set(&header, FRAME_TYPE_PRODUCTS_RESPONSE, origin, origin_realm, data, strlen(data)) &&
        network_send_frame_to_realm(network, origin_realm, &header)) {
        pthread_mutex_lock(&network->lock);
        network_outbound_reset(network);
        network->outbound.active = true;
        network->outbound.kind = TRANSFER_PRODUCTS;
        network->outbound.realm_name = utils_strdup_safe(origin_realm);
        network->outbound.file_name = file_name;
        network->outbound.file_path = file_path;
        network->outbound.file_size = file_size;
        strncpy(network->outbound.md5, md5, CITADEL_MD5_LENGTH);
        network->outbound.md5[CITADEL_MD5_LENGTH] = '\0';
        network->outbound.data_type = FRAME_TYPE_PRODUCTS_DATA;
        network->outbound.waiting_header_ack = true;
        pthread_mutex_unlock(&network->lock);
        file_name = NULL;
        file_path = NULL;
        network_log_line("Sending product list.");
    }

    free(file_name);
    free(file_path);
    free(data);
    free(origin);
    free(origin_realm);
}

static void network_handle_products_header(NetworkContext *network, const NetworkFrame *frame) {
    char *realm_name = network_find_realm_by_endpoint(network, frame->origin);
    char *data = frame_data_to_text(frame);
    char *file_name = NULL;
    char md5[CITADEL_MD5_LENGTH + 1];
    size_t file_size = 0;
    bool ok = false;

    if (realm_name == NULL || data == NULL) {
        free(realm_name);
        free(data);
        return;
    }

    if (network_parse_header_triplet(data, &file_name, &file_size, md5)) {
        pthread_mutex_lock(&network->lock);
        ok = !network->inbound.active &&
             network_begin_inbound_transfer(network, TRANSFER_PRODUCTS, realm_name, frame->origin,
                                            file_name, file_size, md5);
        pthread_mutex_unlock(&network->lock);
    }

    network_send_blank_reply_with_realm_fallback(network, frame->origin, realm_name, FRAME_TYPE_ACK,
                                                 ok ? "OK" : "KO", network->config->realm_name);
    free(file_name);
    free(realm_name);
    free(data);
}

static void network_handle_trade_header(NetworkContext *network, const NetworkFrame *frame) {
    char *realm_name = network_find_realm_by_endpoint(network, frame->origin);
    char *data = frame_data_to_text(frame);
    char *file_name = NULL;
    char md5[CITADEL_MD5_LENGTH + 1];
    size_t file_size = 0;
    AllianceEntry *entry = NULL;
    bool allowed = false;
    bool ok = false;

    if (realm_name == NULL || data == NULL) {
        free(realm_name);
        free(data);
        return;
    }

    pthread_mutex_lock(&network->lock);
    entry = network_find_entry_locked(network, realm_name);
    allowed = (entry != NULL && entry->status == ALLIANCE_ALLIED);
    pthread_mutex_unlock(&network->lock);

    if (!allowed) {
        network_send_auth_error(network, realm_name, realm_name);
        free(file_name);
        free(realm_name);
        free(data);
        return;
    }

    if (network_parse_header_triplet(data, &file_name, &file_size, md5)) {
        pthread_mutex_lock(&network->lock);
        ok = !network->inbound.active &&
             network_begin_inbound_transfer(network, TRANSFER_ORDER, realm_name, frame->origin,
                                            file_name, file_size, md5);
        pthread_mutex_unlock(&network->lock);
    }

    network_send_blank_reply_with_realm_fallback(network, frame->origin, realm_name, FRAME_TYPE_ACK,
                                                 ok ? "OK" : "KO", network->config->realm_name);
    if (ok) {
        char *line = NULL;
        if (asprintf(&line, "Trade request received from %s.", realm_name) >= 0 && line != NULL) {
            network_log_line(line);
            free(line);
        }
    }

    free(file_name);
    free(realm_name);
    free(data);
}

static void network_handle_file_data(NetworkContext *network, const NetworkFrame *frame, TransferKind kind) {
    pthread_mutex_lock(&network->lock);
    if (!network->inbound.active || network->inbound.kind != kind || network->inbound.file_fd < 0) {
        pthread_mutex_unlock(&network->lock);
        return;
    }

    if (utils_write_all(network->inbound.file_fd, frame->data, frame->data_length) < 0) {
        pthread_mutex_unlock(&network->lock);
        network_inbound_reset(network);
        return;
    }

    network->inbound.bytes_received += frame->data_length;
    if (network->inbound.bytes_received < network->inbound.file_size) {
        pthread_mutex_unlock(&network->lock);
        return;
    }
    pthread_mutex_unlock(&network->lock);

    network_finalize_inbound_transfer(network);
}

static float network_find_catalog_weight(NetworkContext *network, const char *realm_name,
                                         const char *product_name, bool *found) {
    AllianceEntry *entry = NULL;
    size_t i = 0;
    float weight = 0.0f;

    if (found != NULL) {
        *found = false;
    }

    if (network == NULL || realm_name == NULL || product_name == NULL) {
        return 0.0f;
    }

    pthread_mutex_lock(&network->lock);
    entry = network_find_entry_locked(network, realm_name);
    if (entry != NULL && entry->catalog != NULL) {
        for (i = 0; i < entry->catalog_count; ++i) {
            if (utils_equals_ignore_case(entry->catalog[i].name, product_name)) {
                weight = entry->catalog[i].weight;
                if (found != NULL) {
                    *found = true;
                }
                break;
            }
        }
    }
    pthread_mutex_unlock(&network->lock);

    return weight;
}

static bool network_apply_successful_order_to_local_stock(NetworkContext *network,
                                                          const char *supplier_realm,
                                                          const char *order_file_path) {
    Product *items = NULL;
    size_t count = 0;
    size_t i = 0;

    if (network == NULL || supplier_realm == NULL || order_file_path == NULL) {
        return false;
    }

    if (!transfer_parse_order_file(order_file_path, &items, &count) || count == 0) {
        stock_free_products(items, count);
        return false;
    }

    for (i = 0; i < count; ++i) {
        Product *existing = stock_find_mutable(network->stock, items[i].name);
        if (existing != NULL) {
            existing->amount += items[i].amount;
            continue;
        }

        {
            Product *grown = (Product *) realloc(network->stock->products,
                                                 sizeof(Product) * (network->stock->count + 1));
            Product *slot = NULL;
            bool weight_found = false;

            if (grown == NULL) {
                stock_free_products(items, count);
                return false;
            }

            network->stock->products = grown;
            slot = &network->stock->products[network->stock->count];
            memset(slot, 0, sizeof(*slot));
            slot->name = utils_strdup_safe(items[i].name);
            if (slot->name == NULL) {
                stock_free_products(items, count);
                return false;
            }

            slot->amount = items[i].amount;
            slot->weight = network_find_catalog_weight(network, supplier_realm, items[i].name, &weight_found);
            if (!weight_found) {
                slot->weight = 0.0f;
            }
            network->stock->count++;
        }
    }

    stock_free_products(items, count);
    return stock_save(network->stock);
}

static void network_handle_trade_response(NetworkContext *network, const NetworkFrame *frame) {
    char *realm_name = network_find_realm_by_endpoint(network, frame->origin);
    char *data = frame_data_to_text(frame);
    AllianceEntry *entry = NULL;
    char *order_file_path = NULL;
    bool can_apply = false;

    if (realm_name == NULL || data == NULL) {
        free(realm_name);
        free(data);
        return;
    }

    pthread_mutex_lock(&network->lock);
    entry = network_find_entry_locked(network, realm_name);
    if (entry != NULL) {
        entry->waiting_trade_ack = false;
    }
    if (network->outbound.active &&
        network->outbound.kind == TRANSFER_ORDER &&
        network->outbound.waiting_order_response &&
        utils_equals_ignore_case(network->outbound.realm_name, realm_name)) {
        order_file_path = utils_strdup_safe(network->outbound.file_path);
        can_apply = order_file_path != NULL;
        network_outbound_reset(network);
    }
    pthread_mutex_unlock(&network->lock);

    if (strcmp(data, "OK") == 0) {
        char *line = NULL;
        bool applied = true;

        if (can_apply) {
            applied = network_apply_successful_order_to_local_stock(network, realm_name, order_file_path);
        }

        if (asprintf(&line, "Order accepted by %s.", realm_name) >= 0 && line != NULL) {
            network_log_line(line);
            free(line);
        }
        if (!applied) {
            network_log_line("Warning: order was accepted but local stock could not be updated.");
        }
    } else if (strncmp(data, "REJECT&", 7) == 0) {
        char *line = NULL;
        if (asprintf(&line, "Order rejected by %s (%s).", realm_name, data + 7) >= 0 && line != NULL) {
            network_log_line(line);
            free(line);
        }
    }

    free(order_file_path);
    free(realm_name);
    free(data);
}

static void network_handle_ack(NetworkContext *network, const NetworkFrame *frame) {
    char *data = frame_data_to_text(frame);
    char *copy = NULL;
    char *status = NULL;
    char *realm_name = NULL;
    bool send_data = false;
    bool reset = false;

    if (data == NULL) {
        return;
    }

    copy = utils_strdup_safe(data);
    free(data);
    if (copy == NULL) {
        return;
    }

    status = strtok(copy, "&");
    realm_name = strtok(NULL, "&");
    if (status == NULL || realm_name == NULL) {
        free(copy);
        return;
    }

    pthread_mutex_lock(&network->lock);
    if (network->outbound.active &&
        network->outbound.waiting_header_ack &&
        utils_equals_ignore_case(network->outbound.realm_name, realm_name)) {
        if (utils_equals_ignore_case(status, "OK")) {
            network->outbound.waiting_header_ack = false;
            network->outbound.waiting_md5_ack = true;
            send_data = true;
        } else {
            if (network->outbound.kind == TRANSFER_SIGIL) {
                AllianceEntry *entry = network_find_entry_locked(network, realm_name);
                if (entry != NULL) {
                    entry->status = ALLIANCE_FAILED;
                    entry->deadline = 0;
                }
            }
            reset = true;
        }
    }
    pthread_mutex_unlock(&network->lock);

    if (send_data) {
        if (!network_send_outbound_file_data(network)) {
            pthread_mutex_lock(&network->lock);
            network_outbound_reset(network);
            pthread_mutex_unlock(&network->lock);
        }
    } else if (reset) {
        pthread_mutex_lock(&network->lock);
        network_outbound_reset(network);
        pthread_mutex_unlock(&network->lock);
    }

    free(copy);
}

static void network_handle_md5_ack(NetworkContext *network, const NetworkFrame *frame) {
    char *data = frame_data_to_text(frame);
    char *copy = NULL;
    char *status = NULL;
    char *realm_name = NULL;

    if (data == NULL) {
        return;
    }

    copy = utils_strdup_safe(data);
    free(data);
    if (copy == NULL) {
        return;
    }

    status = strtok(copy, "&");
    realm_name = strtok(NULL, "&");
    if (status == NULL || realm_name == NULL) {
        free(copy);
        return;
    }

    pthread_mutex_lock(&network->lock);
    if (network->outbound.active &&
        network->outbound.waiting_md5_ack &&
        utils_equals_ignore_case(network->outbound.realm_name, realm_name)) {
        if (utils_equals_ignore_case(status, "CHECK_OK")) {
            if (network->outbound.kind == TRANSFER_ORDER) {
                network->outbound.waiting_md5_ack = false;
                network->outbound.waiting_order_response = true;
            } else {
                network_outbound_reset(network);
            }
        } else {
            if (network->outbound.kind == TRANSFER_SIGIL) {
                AllianceEntry *entry = network_find_entry_locked(network, realm_name);
                if (entry != NULL) {
                    entry->status = ALLIANCE_FAILED;
                    entry->deadline = 0;
                }
            }
            network_outbound_reset(network);
        }
    }
    pthread_mutex_unlock(&network->lock);

    if (utils_equals_ignore_case(status, "CHECK_OK")) {
        char *line = NULL;
        if (asprintf(&line, "Transfer verified by %s.", realm_name) >= 0 && line != NULL) {
            network_log_line(line);
            free(line);
        }
    } else {
        char *line = NULL;
        if (asprintf(&line, "Transfer rejected by %s (MD5 mismatch).", realm_name) >= 0 && line != NULL) {
            network_log_line(line);
            free(line);
        }
    }

    free(copy);
}

static void network_handle_nack(NetworkContext *network, const NetworkFrame *frame) {
    char *data = frame_data_to_text(frame);
    AllianceEntry *entry = NULL;
    char *line = NULL;

    if (data == NULL) {
        return;
    }

    utils_trim(data);
    if (*data != '\0') {
        pthread_mutex_lock(&network->lock);
        entry = network_find_entry_locked(network, data);
        if (entry != NULL) {
            entry->waiting_products = false;
            entry->waiting_trade_ack = false;
            if (entry->status == ALLIANCE_PENDING_OUT || entry->status == ALLIANCE_PENDING_IN) {
                entry->status = ALLIANCE_FAILED;
                entry->deadline = 0;
                entry->sigil_verified = false;
            }
        }
        pthread_mutex_unlock(&network->lock);

        if (asprintf(&line, "NACK received from %s.", data) >= 0 && line != NULL) {
            network_log_line(line);
            free(line);
        }
    } else {
        network_log_line("NACK received.");
    }

    (void) frame;
    free(data);
}

static void network_handle_unknown_realm(NetworkContext *network, const NetworkFrame *frame) {
    char *data = frame_data_to_text(frame);
    char *copy = NULL;
    char *kind = NULL;
    char *realm_name = NULL;
    char *line = NULL;

    if (network == NULL || data == NULL) {
        return;
    }

    if (asprintf(&line, "Routing error: %s", data) >= 0 && line != NULL) {
        network_log_line(line);
        free(line);
    }

    copy = utils_strdup_safe(data);
    if (copy != NULL) {
        kind = strtok(copy, "&");
        realm_name = strtok(NULL, "&");
        if (kind != NULL && realm_name != NULL && utils_equals_ignore_case(kind, "UNKNOWN_REALM")) {
            pthread_mutex_lock(&network->lock);
            {
                AllianceEntry *entry = network_find_entry_locked(network, realm_name);
                if (entry != NULL) {
                    entry->waiting_products = false;
                    entry->waiting_trade_ack = false;
                    if (entry->status == ALLIANCE_PENDING_OUT) {
                        entry->status = ALLIANCE_FAILED;
                        entry->deadline = 0;
                    }
                }
            }
            pthread_mutex_unlock(&network->lock);
        }
        free(copy);
    }

    free(data);
}

static void network_handle_auth_error(NetworkContext *network, const NetworkFrame *frame) {
    char *data = frame_data_to_text(frame);
    char *copy = NULL;
    char *token = NULL;
    char *realm_name = NULL;
    char *line = NULL;

    if (network == NULL || data == NULL) {
        return;
    }

    if (asprintf(&line, "Authorization error: %s", data) >= 0 && line != NULL) {
        network_log_line(line);
        free(line);
    }

    copy = utils_strdup_safe(data);
    if (copy != NULL) {
        token = strtok(copy, "&");
        realm_name = strtok(NULL, "&");
        if (token != NULL && realm_name != NULL) {
            pthread_mutex_lock(&network->lock);
            {
                AllianceEntry *entry = network_find_entry_locked(network, realm_name);
                if (entry != NULL) {
                    entry->waiting_products = false;
                    entry->waiting_trade_ack = false;
                }
            }
            pthread_mutex_unlock(&network->lock);
        }
        free(copy);
    }

    free(data);
}

static void network_handle_disconnect(NetworkContext *network, const NetworkFrame *frame) {
    char *realm_name = network_find_realm_by_endpoint(network, frame->origin);
    char *line = NULL;

    if (realm_name == NULL) {
        return;
    }

    pthread_mutex_lock(&network->lock);
    {
        AllianceEntry *entry = network_find_entry_locked(network, realm_name);
        if (entry != NULL) {
            entry->status = ALLIANCE_INACTIVE;
        }
    }
    pthread_mutex_unlock(&network->lock);

    if (asprintf(&line, "%s is now inactive.", realm_name) >= 0 && line != NULL) {
        network_log_line(line);
        free(line);
    }

    free(realm_name);
}

static void network_handle_ping(NetworkContext *network, const NetworkFrame *frame) {
    char *payload = NULL;
    char *origin = NULL;
    char *origin_realm = NULL;
    char *line = NULL;
    NetworkFrame response;

    if (network == NULL || frame == NULL) {
        return;
    }

    payload = frame_data_to_text(frame);
    if (payload == NULL) {
        return;
    }

    if (utils_equals_ignore_case(payload, "PONG")) {
        origin_realm = network_find_realm_by_endpoint(network, frame->origin);
        if (origin_realm == NULL) {
            origin_realm = utils_strdup_safe(frame->origin);
        }
        if (origin_realm != NULL &&
            asprintf(&line, "PONG received from %s.", origin_realm) >= 0 && line != NULL) {
            network_log_line(line);
            free(line);
        }
        free(origin_realm);
        free(payload);
        return;
    }

    origin = network_build_self_endpoint(network->config);
    if (origin == NULL) {
        free(payload);
        return;
    }

    origin_realm = network_find_realm_by_endpoint(network, frame->origin);
    if (origin_realm != NULL &&
        frame_set(&response, FRAME_TYPE_PING, origin, origin_realm, "PONG", strlen("PONG"))) {
        network_send_frame_to_realm(network, origin_realm, &response);
    } else if (frame_set(&response, FRAME_TYPE_PING, origin, frame->origin, "PONG", strlen("PONG"))) {
        network_send_frame_to_endpoint(frame->origin, &response);
    }

    free(origin_realm);
    free(origin);
    free(payload);
}

static void network_process_local_frame(NetworkContext *network, const NetworkFrame *frame) {
    switch (frame->type) {
        case FRAME_TYPE_PLEDGE:
            network_handle_pledge(network, frame);
            break;
        case FRAME_TYPE_SIGIL_DATA:
            network_handle_file_data(network, frame, TRANSFER_SIGIL);
            break;
        case FRAME_TYPE_PLEDGE_RESPONSE:
            network_handle_pledge_response(network, frame);
            break;
        case FRAME_TYPE_PRODUCTS_REQUEST:
            network_handle_products_request(network, frame);
            break;
        case FRAME_TYPE_PRODUCTS_RESPONSE:
            network_handle_products_header(network, frame);
            break;
        case FRAME_TYPE_PRODUCTS_DATA:
            network_handle_file_data(network, frame, TRANSFER_PRODUCTS);
            break;
        case FRAME_TYPE_TRADE_HEADER:
            network_handle_trade_header(network, frame);
            break;
        case FRAME_TYPE_TRADE_DATA:
            network_handle_file_data(network, frame, TRANSFER_ORDER);
            break;
        case FRAME_TYPE_TRADE_RESPONSE:
            network_handle_trade_response(network, frame);
            break;
        case FRAME_TYPE_ACK:
            network_handle_ack(network, frame);
            break;
        case FRAME_TYPE_MD5_ACK:
            network_handle_md5_ack(network, frame);
            break;
        case FRAME_TYPE_NACK:
            network_handle_nack(network, frame);
            break;
        case FRAME_TYPE_UNKNOWN_REALM:
            network_handle_unknown_realm(network, frame);
            break;
        case FRAME_TYPE_AUTH_ERROR:
            network_handle_auth_error(network, frame);
            break;
        case FRAME_TYPE_DISCONNECT:
            network_handle_disconnect(network, frame);
            break;
        case FRAME_TYPE_PING:
            network_handle_ping(network, frame);
            break;
        default:
            break;
    }
}

static void network_forward_or_discard(NetworkContext *network, const NetworkFrame *frame) {
    char *origin_realm = NULL;
    char *line = NULL;

    if (network_send_frame_to_realm(network, frame->destination, frame)) {
        if (asprintf(&line, "Received hop for %s. Forwarding...", frame->destination) >= 0 && line != NULL) {
            network_log_line(line);
            free(line);
        }
        return;
    }

    origin_realm = network_derive_origin_realm(network, frame);
    if (origin_realm != NULL) {
        network_send_unknown_realm(network, origin_realm, frame->destination);
    }
    free(origin_realm);
}

static void network_handle_client(NetworkContext *network, citadel_socket_t client_fd) {
    unsigned char buffer[CITADEL_FRAME_SIZE];
    NetworkFrame frame;
    char *raw_origin = NULL;

    if (!network_recv_exact(client_fd, buffer, sizeof(buffer))) {
        return;
    }

    raw_origin = network_extract_origin_from_raw(buffer);
    if (!frame_deserialize(buffer, &frame)) {
        network_send_protocol_nack(network, raw_origin);
        free(raw_origin);
        return;
    }

    if (!frame_validate_checksum(&frame)) {
        network_send_protocol_nack(network, raw_origin != NULL ? raw_origin : frame.origin);
        free(raw_origin);
        return;
    }

    if (!network_is_known_type(frame.type)) {
        network_send_protocol_nack(network, frame.origin);
        free(raw_origin);
        return;
    }

    if (frame.destination[0] == '\0') {
        if (!network_is_blank_destination_type(frame.type)) {
            network_send_protocol_nack(network, frame.origin);
            free(raw_origin);
            return;
        }
        network_process_local_frame(network, &frame);
    } else if (utils_equals_ignore_case(frame.destination, network->config->realm_name)) {
        network_process_local_frame(network, &frame);
    } else {
        network_forward_or_discard(network, &frame);
    }

    free(raw_origin);
}

static void *network_server_main(void *arg) {
    NetworkContext *network = (NetworkContext *) arg;

    while (network->running) {
        fd_set readfds;
        struct timeval timeout;
        int result = 0;

        FD_ZERO(&readfds);
        FD_SET(network->server_fd, &readfds);

        timeout.tv_sec = 1;
        timeout.tv_usec = 0;
        result = select((int) network->server_fd + 1, &readfds, NULL, NULL, &timeout);
        if (result > 0 && FD_ISSET(network->server_fd, &readfds)) {
            struct sockaddr_in client_addr;
            socklen_t client_size = (socklen_t) sizeof(client_addr);
            citadel_socket_t client_fd = accept(network->server_fd, (struct sockaddr *) &client_addr, &client_size);
            if (client_fd != CITADEL_INVALID_SOCKET) {
                network_handle_client(network, client_fd);
                CITADEL_SOCKET_CLOSE(client_fd);
            }
        }

        network_check_timeouts(network);
    }

    return NULL;
}

static bool network_init_alliances(NetworkContext *network) {
    size_t i = 0;
    size_t count = 0;

    for (i = 0; i < network->config->route_count; ++i) {
        if (!utils_equals_ignore_case(network->config->routes[i].realm_name, "DEFAULT")) {
            count++;
        }
    }

    network->alliances = (AllianceEntry *) calloc(count, sizeof(AllianceEntry));
    if (network->alliances == NULL) {
        return false;
    }

    network->alliance_count = count;
    count = 0;
    for (i = 0; i < network->config->route_count; ++i) {
        if (utils_equals_ignore_case(network->config->routes[i].realm_name, "DEFAULT")) {
            continue;
        }
        network->alliances[count].realm_name = utils_strdup_safe(network->config->routes[i].realm_name);
        if (network->alliances[count].realm_name == NULL) {
            return false;
        }
        count++;
    }

    return true;
}

bool network_init(NetworkContext *network, CitadelConfig *config, Stock *stock) {
    if (network == NULL || config == NULL || stock == NULL) {
        return false;
    }

    memset(network, 0, sizeof(*network));
    network->config = config;
    network->stock = stock;
    network->server_fd = CITADEL_INVALID_SOCKET;
    network->inbound.file_fd = -1;

    if (!network_socket_init()) {
        return false;
    }

    if (pthread_mutex_init(&network->lock, NULL) != 0) {
        network_socket_cleanup();
        return false;
    }

    if (!network_init_alliances(network)) {
        pthread_mutex_destroy(&network->lock);
        network_socket_cleanup();
        return false;
    }

    network->server_fd = network_create_listener(config);
    if (network->server_fd == CITADEL_INVALID_SOCKET) {
        pthread_mutex_destroy(&network->lock);
        network_socket_cleanup();
        return false;
    }

    network->running = true;
    if (pthread_create(&network->server_thread, NULL, network_server_main, network) != 0) {
        CITADEL_SOCKET_CLOSE(network->server_fd);
        pthread_mutex_destroy(&network->lock);
        network_socket_cleanup();
        return false;
    }

    network->initialized = true;
    return true;
}

static void network_send_disconnects(NetworkContext *network) {
    size_t i = 0;
    char *origin = NULL;

    if (network == NULL || !network->initialized) {
        return;
    }

    origin = network_build_self_endpoint(network->config);
    if (origin == NULL) {
        return;
    }

    pthread_mutex_lock(&network->lock);
    for (i = 0; i < network->alliance_count; ++i) {
        if (network->alliances[i].status == ALLIANCE_ALLIED && network->alliances[i].known_endpoint != NULL) {
            NetworkFrame frame;
            if (frame_set(&frame, FRAME_TYPE_DISCONNECT, origin, network->alliances[i].realm_name,
                          "DISCONNECT", strlen("DISCONNECT"))) {
                network_send_frame_to_endpoint(network->alliances[i].known_endpoint, &frame);
            }
        }
    }
    pthread_mutex_unlock(&network->lock);

    free(origin);
}

void network_shutdown(NetworkContext *network) {
    size_t i = 0;

    if (network == NULL || !network->initialized) {
        return;
    }

    network_send_disconnects(network);
    network->running = false;

    if (network->server_fd != CITADEL_INVALID_SOCKET) {
        shutdown(network->server_fd, SHUT_RDWR);
        CITADEL_SOCKET_CLOSE(network->server_fd);
        network->server_fd = CITADEL_INVALID_SOCKET;
    }

    pthread_join(network->server_thread, NULL);

    network_outbound_reset(network);
    network_inbound_reset(network);
    for (i = 0; i < network->alliance_count; ++i) {
        network_alliance_free(&network->alliances[i]);
    }

    free(network->alliances);
    pthread_mutex_destroy(&network->lock);
    network_socket_cleanup();
    memset(network, 0, sizeof(*network));
}

bool network_realm_exists(NetworkContext *network, const char *realm_name) {
    bool exists = false;

    if (network == NULL || realm_name == NULL) {
        return false;
    }

    pthread_mutex_lock(&network->lock);
    exists = network_find_entry_locked(network, realm_name) != NULL;
    pthread_mutex_unlock(&network->lock);
    return exists;
}

bool network_has_active_alliance(NetworkContext *network, const char *realm_name) {
    bool active = false;
    AllianceEntry *entry = NULL;

    if (network == NULL || realm_name == NULL) {
        return false;
    }

    pthread_mutex_lock(&network->lock);
    entry = network_find_entry_locked(network, realm_name);
    active = (entry != NULL && entry->status == ALLIANCE_ALLIED);
    pthread_mutex_unlock(&network->lock);
    return active;
}

bool network_send_pledge(NetworkContext *network, const char *realm_name, const char *sigil_name) {
    AllianceEntry *entry = NULL;
    char *sigil_path = NULL;
    char *file_name = NULL;
    char *origin = NULL;
    char *data = NULL;
    char md5[CITADEL_MD5_LENGTH + 1];
    size_t file_size = 0;
    NetworkFrame frame;
    bool sent = false;

    if (network == NULL || realm_name == NULL || sigil_name == NULL) {
        return false;
    }

    sigil_path = transfer_resolve_sigil_path(network->config, sigil_name);
    if (sigil_path == NULL || !transfer_get_file_info(sigil_path, &file_name, &file_size, md5)) {
        free(sigil_path);
        return false;
    }

    pthread_mutex_lock(&network->lock);
    entry = network_find_entry_locked(network, realm_name);
    if (entry == NULL || entry->status == ALLIANCE_ALLIED || entry->status == ALLIANCE_PENDING_OUT ||
        network->outbound.active) {
        pthread_mutex_unlock(&network->lock);
        free(sigil_path);
        free(file_name);
        return false;
    }
    pthread_mutex_unlock(&network->lock);

    origin = network_build_self_endpoint(network->config);
    if (origin == NULL ||
        asprintf(&data, "%s&%s&%zu&%s", network->config->realm_name, file_name, file_size, md5) < 0 ||
        !frame_set(&frame, FRAME_TYPE_PLEDGE, origin, realm_name, data, strlen(data))) {
        free(sigil_path);
        free(file_name);
        free(origin);
        free(data);
        return false;
    }

    sent = network_send_frame_to_realm(network, realm_name, &frame);
    if (sent) {
        pthread_mutex_lock(&network->lock);
        entry = network_find_entry_locked(network, realm_name);
        if (entry != NULL) {
            entry->status = ALLIANCE_PENDING_OUT;
            entry->deadline = time(NULL) + CITADEL_PLEDGE_TIMEOUT_SECONDS;
        }
        network_outbound_reset(network);
        network->outbound.active = true;
        network->outbound.kind = TRANSFER_SIGIL;
        network->outbound.realm_name = utils_strdup_safe(realm_name);
        network->outbound.file_name = file_name;
        network->outbound.file_path = sigil_path;
        network->outbound.file_size = file_size;
        strncpy(network->outbound.md5, md5, CITADEL_MD5_LENGTH);
        network->outbound.md5[CITADEL_MD5_LENGTH] = '\0';
        network->outbound.data_type = FRAME_TYPE_SIGIL_DATA;
        network->outbound.waiting_header_ack = true;
        pthread_mutex_unlock(&network->lock);
        file_name = NULL;
        sigil_path = NULL;
        {
            char *line = NULL;
            if (asprintf(&line, "Pledge sent to %s.", realm_name) >= 0 && line != NULL) {
                network_log_line(line);
                free(line);
            }
        }
    }

    free(sigil_path);
    free(file_name);
    free(origin);
    free(data);
    return sent;
}

bool network_send_pledge_response(NetworkContext *network, const char *realm_name, bool accepted) {
    AllianceEntry *entry = NULL;
    NetworkFrame frame;
    char *origin = NULL;
    char *data = NULL;
    bool sent = false;

    if (network == NULL || realm_name == NULL) {
        return false;
    }

    pthread_mutex_lock(&network->lock);
    entry = network_find_entry_locked(network, realm_name);
    if (entry == NULL || entry->status != ALLIANCE_PENDING_IN || !entry->sigil_verified) {
        pthread_mutex_unlock(&network->lock);
        return false;
    }
    pthread_mutex_unlock(&network->lock);

    origin = network_build_self_endpoint(network->config);
    if (origin == NULL ||
        asprintf(&data, "%s&%s", accepted ? "ACCEPT" : "REJECT", network->config->realm_name) < 0 ||
        !frame_set(&frame, FRAME_TYPE_PLEDGE_RESPONSE, origin, realm_name, data, strlen(data))) {
        free(origin);
        free(data);
        return false;
    }

    sent = network_send_frame_to_realm(network, realm_name, &frame);
    if (sent) {
        pthread_mutex_lock(&network->lock);
        entry = network_find_entry_locked(network, realm_name);
        if (entry != NULL) {
            entry->deadline = 0;
            if (accepted) {
                entry->status = ALLIANCE_ALLIED;
                if (entry->pending_origin_endpoint != NULL) {
                    network_set_entry_endpoint(entry, entry->pending_origin_endpoint);
                }
            } else {
                entry->status = ALLIANCE_REJECTED;
            }
            entry->sigil_verified = false;
            free(entry->pending_origin_endpoint);
            entry->pending_origin_endpoint = NULL;
        }
        pthread_mutex_unlock(&network->lock);

        {
            char *line = NULL;
            if (asprintf(&line, "Alliance with %s %s.", realm_name,
                         accepted ? "established" : "rejected") >= 0 && line != NULL) {
                network_log_line(line);
                free(line);
            }
        }
    }

    free(origin);
    free(data);
    return sent;
}

bool network_request_remote_products(NetworkContext *network, const char *realm_name) {
    NetworkFrame frame;
    char *origin = NULL;
    AllianceEntry *entry = NULL;
    bool sent = false;

    if (network == NULL || realm_name == NULL) {
        return false;
    }

    pthread_mutex_lock(&network->lock);
    entry = network_find_entry_locked(network, realm_name);
    if (entry == NULL || entry->status != ALLIANCE_ALLIED) {
        pthread_mutex_unlock(&network->lock);
        return false;
    }
    entry->waiting_products = true;
    pthread_mutex_unlock(&network->lock);

    origin = network_build_self_endpoint(network->config);
    if (origin == NULL ||
        !frame_set(&frame, FRAME_TYPE_PRODUCTS_REQUEST, origin, realm_name,
                   network->config->realm_name, strlen(network->config->realm_name))) {
        pthread_mutex_lock(&network->lock);
        entry = network_find_entry_locked(network, realm_name);
        if (entry != NULL) {
            entry->waiting_products = false;
        }
        pthread_mutex_unlock(&network->lock);
        free(origin);
        return false;
    }

    sent = network_send_frame_to_realm(network, realm_name, &frame);
    if (sent) {
        network_log_line("Remote product request sent.");
    } else {
        pthread_mutex_lock(&network->lock);
        entry = network_find_entry_locked(network, realm_name);
        if (entry != NULL) {
            entry->waiting_products = false;
        }
        pthread_mutex_unlock(&network->lock);
    }
    free(origin);
    return sent;
}

bool network_send_trade_offer(NetworkContext *network, const char *realm_name, const char *file_path) {
    AllianceEntry *entry = NULL;
    NetworkFrame frame;
    char *origin = NULL;
    char *data = NULL;
    char *file_name = NULL;
    char md5[CITADEL_MD5_LENGTH + 1];
    size_t file_size = 0;
    bool sent = false;

    if (network == NULL || realm_name == NULL || file_path == NULL) {
        return false;
    }

    if (!transfer_get_file_info(file_path, &file_name, &file_size, md5)) {
        return false;
    }

    pthread_mutex_lock(&network->lock);
    entry = network_find_entry_locked(network, realm_name);
    if (entry == NULL || entry->status != ALLIANCE_ALLIED || network->outbound.active) {
        pthread_mutex_unlock(&network->lock);
        free(file_name);
        return false;
    }
    entry->waiting_trade_ack = true;
    pthread_mutex_unlock(&network->lock);

    origin = network_build_self_endpoint(network->config);
    if (origin == NULL ||
        asprintf(&data, "%s&%zu&%s", file_name, file_size, md5) < 0 ||
        !frame_set(&frame, FRAME_TYPE_TRADE_HEADER, origin, realm_name, data, strlen(data))) {
        pthread_mutex_lock(&network->lock);
        entry = network_find_entry_locked(network, realm_name);
        if (entry != NULL) {
            entry->waiting_trade_ack = false;
        }
        pthread_mutex_unlock(&network->lock);
        free(file_name);
        free(origin);
        free(data);
        return false;
    }

    sent = network_send_frame_to_realm(network, realm_name, &frame);
    if (sent) {
        pthread_mutex_lock(&network->lock);
        network_outbound_reset(network);
        network->outbound.active = true;
        network->outbound.kind = TRANSFER_ORDER;
        network->outbound.realm_name = utils_strdup_safe(realm_name);
        network->outbound.file_name = file_name;
        network->outbound.file_path = utils_strdup_safe(file_path);
        network->outbound.file_size = file_size;
        strncpy(network->outbound.md5, md5, CITADEL_MD5_LENGTH);
        network->outbound.md5[CITADEL_MD5_LENGTH] = '\0';
        network->outbound.data_type = FRAME_TYPE_TRADE_DATA;
        network->outbound.waiting_header_ack = true;
        pthread_mutex_unlock(&network->lock);
        file_name = NULL;
    } else {
        pthread_mutex_lock(&network->lock);
        entry = network_find_entry_locked(network, realm_name);
        if (entry != NULL) {
            entry->waiting_trade_ack = false;
        }
        pthread_mutex_unlock(&network->lock);
    }

    free(file_name);
    free(origin);
    free(data);
    return sent;
}

bool network_send_ping(NetworkContext *network, const char *realm_name) {
    NetworkFrame frame;
    char *origin = NULL;
    bool sent = false;

    if (network == NULL || realm_name == NULL || *realm_name == '\0') {
        return false;
    }

    origin = network_build_self_endpoint(network->config);
    if (origin == NULL ||
        !frame_set(&frame, FRAME_TYPE_PING, origin, realm_name, "PING", strlen("PING"))) {
        free(origin);
        return false;
    }

    sent = network_send_frame_to_realm(network, realm_name, &frame);
    free(origin);
    return sent;
}

bool network_get_remote_products_copy(NetworkContext *network, const char *realm_name,
                                      Product **products_out, size_t *count_out) {
    AllianceEntry *entry = NULL;
    Product *copy = NULL;

    if (network == NULL || realm_name == NULL || products_out == NULL || count_out == NULL) {
        return false;
    }

    *products_out = NULL;
    *count_out = 0;

    pthread_mutex_lock(&network->lock);
    entry = network_find_entry_locked(network, realm_name);
    if (entry != NULL && entry->catalog_count > 0) {
        copy = stock_clone_products(entry->catalog, entry->catalog_count);
        if (copy != NULL) {
            *products_out = copy;
            *count_out = entry->catalog_count;
        }
    }
    pthread_mutex_unlock(&network->lock);

    return *products_out != NULL;
}

bool network_get_alliance_status(NetworkContext *network, const char *realm_name, AllianceStatus *status_out) {
    AllianceEntry *entry = NULL;

    if (network == NULL || realm_name == NULL || status_out == NULL) {
        return false;
    }

    pthread_mutex_lock(&network->lock);
    entry = network_find_entry_locked(network, realm_name);
    if (entry == NULL) {
        pthread_mutex_unlock(&network->lock);
        return false;
    }
    *status_out = entry->status;
    pthread_mutex_unlock(&network->lock);
    return true;
}

bool network_is_waiting_products(NetworkContext *network, const char *realm_name, bool *waiting_out) {
    AllianceEntry *entry = NULL;

    if (network == NULL || realm_name == NULL || waiting_out == NULL) {
        return false;
    }

    pthread_mutex_lock(&network->lock);
    entry = network_find_entry_locked(network, realm_name);
    if (entry == NULL) {
        pthread_mutex_unlock(&network->lock);
        return false;
    }
    *waiting_out = entry->waiting_products;
    pthread_mutex_unlock(&network->lock);
    return true;
}

bool network_is_waiting_trade(NetworkContext *network, const char *realm_name, bool *waiting_out) {
    AllianceEntry *entry = NULL;

    if (network == NULL || realm_name == NULL || waiting_out == NULL) {
        return false;
    }

    pthread_mutex_lock(&network->lock);
    entry = network_find_entry_locked(network, realm_name);
    if (entry == NULL) {
        pthread_mutex_unlock(&network->lock);
        return false;
    }
    *waiting_out = entry->waiting_trade_ack;
    pthread_mutex_unlock(&network->lock);
    return true;
}

void network_print_pledge_status(NetworkContext *network) {
    size_t i = 0;

    if (network == NULL) {
        return;
    }

    pthread_mutex_lock(&network->lock);
    for (i = 0; i < network->alliance_count; ++i) {
        char *line = NULL;
        if (asprintf(&line, "- %s: %s\n", network->alliances[i].realm_name,
                     network_status_text(network->alliances[i].status)) >= 0 && line != NULL) {
            utils_print(line);
            free(line);
        }
    }
    pthread_mutex_unlock(&network->lock);
}
