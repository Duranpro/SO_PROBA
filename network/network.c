#include "network.h"

#include "../utils/utils.h"

typedef struct {
    char ip_regne[64];
    int port_regne;
} ParsedEndpoint;

static const char *network_frame_type_text(uint8_t type);

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

static void network_println(const char *text) {
    if (text != NULL) {
        utils_println(text);
    }
}

static char *network_build_self_endpoint(const CitadelConfig *config) {
    char *endpoint = NULL;

    if (config == NULL || config->ip_regne == NULL) {
        return NULL;
    }

    if (asprintf(&endpoint, "%s:%d", config->ip_regne, config->port_regne) < 0) {
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

    if (!utils_parse_int(separator, &endpoint->port_regne) || endpoint->port_regne <= 0) {
        free(copy);
        return false;
    }

    strncpy(endpoint->ip_regne, copy, sizeof(endpoint->ip_regne) - 1);
    endpoint->ip_regne[sizeof(endpoint->ip_regne) - 1] = '\0';
    free(copy);
    return endpoint->ip_regne[0] != '\0';
}

static bool network_route_has_address(const RouteInfo *route) {
    if (route == NULL || route->ip_regne == NULL || route->port_regne <= 0) {
        return false;
    }

    return strcmp(route->ip_regne, "*.*.*.*") != 0;
}

static const char *network_status_text(AllianceStatus status) {
    switch (status) {
        case ALLIANCE_PENDING_OUT:
        case ALLIANCE_PENDING_IN:
            return "PENDING";
        case ALLIANCE_ALLIED:
            return "ACCEPTED";
        case ALLIANCE_REJECTED:
            return "REJECTED";
        case ALLIANCE_FAILED:
            return "FAILED";
        case ALLIANCE_INACTIVE:
            return "INACTIVE";
        case ALLIANCE_NONE:
        default:
            return "NONE";
    }
}

static void network_free_catalog(AllianceEntry *entry) {
    if (entry == NULL) {
        return;
    }

    stock_alliberar_productes(entry->cataleg, entry->num_cataleg);
    entry->cataleg = NULL;
    entry->num_cataleg = 0;
}

static void network_alliance_free(AllianceEntry *entry) {
    if (entry == NULL) {
        return;
    }

    free(entry->nom_regne);
    free(entry->endpoint_conegut);
    free(entry->endpoint_origen_pendent);
    free(entry->endpoint_estable_pendent);
    network_free_catalog(entry);
    memset(entry, 0, sizeof(*entry));
}

static AllianceEntry *network_buscar_entrada_locked(NetworkContext *network, const char *nom_regne) {
    size_t i = 0;

    if (network == NULL || nom_regne == NULL) {
        return NULL;
    }

    for (i = 0; i < network->num_aliances; ++i) {
        if (utils_equals_ignore_case(network->aliances[i].nom_regne, nom_regne)) {
            return &network->aliances[i];
        }
    }

    return NULL;
}

static AllianceEntry *network_find_entry_by_endpoint_locked(NetworkContext *network, const char *endpoint) {
    size_t i = 0;

    if (network == NULL || endpoint == NULL) {
        return NULL;
    }

    for (i = 0; i < network->num_aliances; ++i) {
        if (network->aliances[i].endpoint_conegut != NULL && strcmp(network->aliances[i].endpoint_conegut, endpoint) == 0) {
            return &network->aliances[i];
        }
        if (network->aliances[i].endpoint_origen_pendent != NULL && strcmp(network->aliances[i].endpoint_origen_pendent, endpoint) == 0) {
            return &network->aliances[i];
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

    for (i = 0; i < network->config->num_rutes; ++i) {
        const RouteInfo *route = &network->config->rutes[i];
        if (utils_equals_ignore_case(route->nom_regne, "DEFAULT")) {
            continue;
        }
        if (route->ip_regne != NULL && route->port_regne == parsed.port_regne && strcmp(route->ip_regne, parsed.ip_regne) == 0) {
            return utils_strdup_safe(route->nom_regne);
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
        realm = utils_strdup_safe(entry->nom_regne);
    }
    pthread_mutex_unlock(&network->lock);

    if (realm != NULL) {
        return realm;
    }

    return network_find_realm_from_route(network, endpoint);
}

static bool network_set_entry_endpoint(AllianceEntry *entry, const char *endpoint) {
    char *copy = NULL;
    ParsedEndpoint parsed;

    if (entry == NULL || endpoint == NULL) {
        return false;
    }

    if (!network_parse_endpoint(endpoint, &parsed)) {
        return false;
    }

    copy = utils_strdup_safe(endpoint);
    if (copy == NULL) {
        return false;
    }

    free(entry->endpoint_conegut);
    entry->endpoint_conegut = copy;
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

    free(entry->endpoint_origen_pendent);
    entry->endpoint_origen_pendent = copy;
    return true;
}

static bool network_store_pending_peer_stable_endpoint(AllianceEntry *entry, const char *endpoint) {
    char *copy = NULL;
    ParsedEndpoint parsed;

    if (entry == NULL) {
        return false;
    }

    if (endpoint == NULL || endpoint[0] == '\0') {
        free(entry->endpoint_estable_pendent);
        entry->endpoint_estable_pendent = NULL;
        return true;
    }

    if (!network_parse_endpoint(endpoint, &parsed)) {
        return false;
    }

    copy = utils_strdup_safe(endpoint);
    if (copy == NULL) {
        return false;
    }

    free(entry->endpoint_estable_pendent);
    entry->endpoint_estable_pendent = copy;
    return true;
}

static bool network_copy_route_endpoint(const CitadelConfig *config, const char *nom_regne, char *endpoint_out, size_t endpoint_size) {
    const RouteInfo *route = NULL;
    int written = 0;

    if (config == NULL || nom_regne == NULL || endpoint_out == NULL || endpoint_size == 0) {
        return false;
    }

    route = config_find_route(config, nom_regne);
    if (route == NULL || route->ip_regne == NULL || route->port_regne <= 0 || strcmp(route->ip_regne, "*.*.*.*") == 0) {
        return false;
    }

    written = snprintf(endpoint_out, endpoint_size, "%s:%d", route->ip_regne, route->port_regne);
    return written >= 0 && (size_t) written < endpoint_size;
}

static bool network_set_catalog(AllianceEntry *entry, Product *productes, size_t num_items) {
    if (entry == NULL) {
        return false;
    }

    network_free_catalog(entry);
    entry->cataleg = productes;
    entry->num_cataleg = num_items;
    return true;
}

static void network_outbound_reset(NetworkContext *network) {
    if (network == NULL) {
        return;
    }

    free(network->sortint.nom_regne);
    free(network->sortint.endpoint_desti);
    free(network->sortint.nom_fitxer);
    free(network->sortint.ruta_fitxer);
    memset(&network->sortint, 0, sizeof(network->sortint));
}

static void network_inbound_reset(NetworkContext *network) {
    if (network == NULL) {
        return;
    }

    if (network->entrant.fd_fitxer >= 0) {
        close(network->entrant.fd_fitxer);
    }
    free(network->entrant.nom_regne);
    free(network->entrant.endpoint_origen);
    free(network->entrant.nom_fitxer);
    free(network->entrant.ruta_fitxer);
    memset(&network->entrant, 0, sizeof(network->entrant));
    network->entrant.fd_fitxer = -1;
}

static bool network_socket_init(void) {
    return true;
}

static void network_socket_cleanup(void) {
    return;
}

static citadel_socket_t network_create_listener(const CitadelConfig *config) {
    citadel_socket_t server_fd = CITADEL_INVALID_SOCKET;
    struct sockaddr_in address;
    int option = 1;

    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons((uint16_t) config->port_regne);

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == CITADEL_INVALID_SOCKET) {
        return CITADEL_INVALID_SOCKET;
    }

    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, (const char *) &option, sizeof(option));

    if (inet_pton(AF_INET, config->ip_regne, &address.sin_addr) <= 0) {
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

    bytes = recv(socket_fd, (char *) buffer, (int) size, MSG_WAITALL);
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

static bool network_resolve_next_endpoint(NetworkContext *network, const char *nom_regne, char **endpoint_out) {
    AllianceEntry *entry = NULL;
    const RouteInfo *route = NULL;

    if (network == NULL || nom_regne == NULL || endpoint_out == NULL) {
        return false;
    }

    *endpoint_out = NULL;

    pthread_mutex_lock(&network->lock);
    entry = network_buscar_entrada_locked(network, nom_regne);
    if (entry != NULL && entry->estat == ALLIANCE_ALLIED && entry->endpoint_conegut != NULL) {
        *endpoint_out = utils_strdup_safe(entry->endpoint_conegut);
        pthread_mutex_unlock(&network->lock);
        return *endpoint_out != NULL;
    }
    pthread_mutex_unlock(&network->lock);

    route = config_find_route(network->config, nom_regne);
    if (network_route_has_address(route)) {
        return asprintf(endpoint_out, "%s:%d", route->ip_regne, route->port_regne) >= 0;
    }

    route = config_find_route(network->config, "DEFAULT");
    if (network_route_has_address(route)) {
        return asprintf(endpoint_out, "%s:%d", route->ip_regne, route->port_regne) >= 0;
    }

    return false;
}

static bool network_send_serialized_to_endpoint(const char *endpoint_text, const unsigned char buffer[CITADEL_FRAME_SIZE]) {
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
    address.sin_port = htons((uint16_t) endpoint.port_regne);
    if (inet_pton(AF_INET, endpoint.ip_regne, &address.sin_addr) <= 0) {
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

static bool network_send_frame_to_realm(NetworkContext *network, const char *nom_regne, const NetworkFrame *frame) {
    unsigned char buffer[CITADEL_FRAME_SIZE];
    char *endpoint = NULL;
    char *line = NULL;
    bool ok = false;

    if (!network_resolve_next_endpoint(network, nom_regne, &endpoint)) {
        (void) line;
        network_println("Route not found.");
        return false;
    }

    frame_serialize(frame, buffer);
    ok = network_send_serialized_to_endpoint(endpoint, buffer);
    if (!ok) {
        (void) nom_regne;
        network_println("Connection failed.");
    }

    free(endpoint);
    return ok;
}

static bool network_send_frame_to_runtime_target(NetworkContext *network, const NetworkFrame *frame) {
    if (network == NULL || frame == NULL) {
        return false;
    }

    if (network->sortint.endpoint_desti != NULL) {
        return network_send_frame_to_endpoint(network->sortint.endpoint_desti, frame);
    }

    return network_send_frame_to_realm(network, network->sortint.nom_regne, frame);
}

static bool network_send_blank_reply(const char *endpoint, uint8_t type, const char *status, const char *nom_regne) {
    NetworkFrame frame;
    char *data = NULL;
    bool ok = false;

    if (endpoint == NULL || status == NULL || nom_regne == NULL) {
        return false;
    }

    if (asprintf(&data, "%s&%s", status, nom_regne) < 0 || data == NULL) {
        return false;
    }

    ok = frame_set(&frame, type, "", "", data, strlen(data)) && network_send_frame_to_endpoint(endpoint, &frame);
    free(data);
    return ok;
}

static bool network_send_blank_reply_with_realm_fallback(NetworkContext *network, const char *endpoint, const char *nom_regne, uint8_t type, const char *status, const char *ack_realm) {
    NetworkFrame frame;
    char *data = NULL;
    char *fallback_endpoint = NULL;
    bool ok = false;

    if (network == NULL || status == NULL || ack_realm == NULL) {
        return false;
    }

    ok = network_send_blank_reply(endpoint, type, status, ack_realm);

    if (!ok && nom_regne != NULL && nom_regne[0] != '\0' &&
        network_resolve_next_endpoint(network, nom_regne, &fallback_endpoint)) {
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

    return network_send_blank_payload(endpoint, FRAME_TYPE_NACK, network->config->nom_regne);
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

    if (frame->tipus == FRAME_TYPE_PLEDGE || frame->tipus == FRAME_TYPE_PRODUCTS_REQUEST) {
        data = frame_data_to_text(frame);
        realm = network_extract_first_token(data);
        free(data);
        return realm;
    }

    return network_find_realm_by_endpoint(network, frame->origen);
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

static bool network_send_auth_error(NetworkContext *network, const char *origin_realm, const char *nom_regne) {
    NetworkFrame frame;
    char *origin = NULL;
    char *data = NULL;
    bool ok = false;

    if (network == NULL || origin_realm == NULL || nom_regne == NULL) {
        return false;
    }

    origin = network_build_self_endpoint(network->config);
    if (origin == NULL) {
        return false;
    }

    if (asprintf(&data, "AUTH&%s", nom_regne) < 0 || data == NULL) {
        free(origin);
        return false;
    }

    ok = frame_set(&frame, FRAME_TYPE_AUTH_ERROR, origin, origin_realm, data, strlen(data)) &&
         network_send_frame_to_realm(network, origin_realm, &frame);
    free(origin);
    free(data);
    return ok;
}

static bool network_parse_header_triplet(const char *data_text, char **nom_fitxer_out, size_t *mida_out, char md5_out[CITADEL_MD5_LENGTH + 1]) {
    char *copy = NULL;
    char *nom_fitxer = NULL;
    char *size_text = NULL;
    char *md5 = NULL;
    int size_value = 0;

    if (data_text == NULL || nom_fitxer_out == NULL || mida_out == NULL || md5_out == NULL) {
        return false;
    }

    copy = utils_strdup_safe(data_text);
    if (copy == NULL) {
        return false;
    }

    nom_fitxer = strtok(copy, "&");
    size_text = strtok(NULL, "&");
    md5 = strtok(NULL, "&");

    if (nom_fitxer == NULL || size_text == NULL || md5 == NULL || strlen(md5) != CITADEL_MD5_LENGTH || !utils_parse_int(size_text, &size_value) || size_value < 0) {
        free(copy);
        return false;
    }

    *nom_fitxer_out = utils_strdup_safe(nom_fitxer);
    if (*nom_fitxer_out == NULL) {
        free(copy);
        return false;
    }

    *mida_out = (size_t) size_value;
    memcpy(md5_out, md5, CITADEL_MD5_LENGTH);
    md5_out[CITADEL_MD5_LENGTH] = '\0';
    free(copy);
    return true;
}

static bool network_parse_trade_header_payload(const char *data_text, char **origin_realm_out, char **nom_fitxer_out, size_t *mida_out, char md5_out[CITADEL_MD5_LENGTH + 1]) {
    char *copy = NULL;
    char *first = NULL;
    char *second = NULL;
    char *third = NULL;
    char *fourth = NULL;
    int size_value = 0;

    if (data_text == NULL || nom_fitxer_out == NULL || mida_out == NULL || md5_out == NULL) {
        return false;
    }

    if (origin_realm_out != NULL) {
        *origin_realm_out = NULL;
    }

    copy = utils_strdup_safe(data_text);
    if (copy == NULL) {
        return false;
    }

    first = strtok(copy, "&");
    second = strtok(NULL, "&");
    third = strtok(NULL, "&");
    fourth = strtok(NULL, "&");

    if (first != NULL && second != NULL && third != NULL && fourth != NULL && strlen(fourth) == CITADEL_MD5_LENGTH && utils_parse_int(third, &size_value) && size_value >= 0) {
        if (origin_realm_out != NULL) {
            *origin_realm_out = utils_strdup_safe(first);
            if (*origin_realm_out == NULL) {
                free(copy);
                return false;
            }
        }

        *nom_fitxer_out = utils_strdup_safe(second);
        if (*nom_fitxer_out == NULL) {
            if (origin_realm_out != NULL) {
                free(*origin_realm_out);
                *origin_realm_out = NULL;
            }
            free(copy);
            return false;
        }

        *mida_out = (size_t) size_value;
        memcpy(md5_out, fourth, CITADEL_MD5_LENGTH);
        md5_out[CITADEL_MD5_LENGTH] = '\0';
        free(copy);
        return true;
    }

    free(copy);
    return network_parse_header_triplet(data_text, nom_fitxer_out, mida_out, md5_out);
}

static bool network_iniciar_recepcio_transfer(NetworkContext *network, TransferKind kind, const char *nom_regne, const char *endpoint_origen, const char *nom_fitxer, size_t mida_fitxer, const char *md5_text) {
    char *ruta_fitxer = NULL;
    int file_fd = -1;
    bool ok = false;

    if (network == NULL || nom_regne == NULL || endpoint_origen == NULL || nom_fitxer == NULL || md5_text == NULL) {
        return false;
    }

    if (!utils_ensure_directory(network->config->directori_carpeta)) {
        return false;
    }

    ruta_fitxer = utils_build_path(network->config->directori_carpeta, nom_fitxer);
    if (ruta_fitxer == NULL) {
        return false;
    }

    file_fd = open(ruta_fitxer, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (file_fd < 0) {
        free(ruta_fitxer);
        return false;
    }

    network_inbound_reset(network);
    network->entrant.actiu = true;
    network->entrant.tipus_transfer = kind;
    network->entrant.nom_regne = utils_strdup_safe(nom_regne);
    network->entrant.endpoint_origen = utils_strdup_safe(endpoint_origen);
    network->entrant.nom_fitxer = utils_strdup_safe(nom_fitxer);
    network->entrant.ruta_fitxer = ruta_fitxer;
    network->entrant.fd_fitxer = file_fd;
    network->entrant.mida_fitxer = mida_fitxer;
    network->entrant.bytes_rebuts = 0;
    strncpy(network->entrant.md5, md5_text, CITADEL_MD5_LENGTH);
    network->entrant.md5[CITADEL_MD5_LENGTH] = '\0';

    ok = network->entrant.nom_regne != NULL &&
         network->entrant.endpoint_origen != NULL &&
         network->entrant.nom_fitxer != NULL;
    if (!ok) {
        network_inbound_reset(network);
    }

    return ok;
}

static bool network_send_outbound_file_data(NetworkContext *network) {
    int fd = -1;
    unsigned char block[CITADEL_FRAME_DATA_SIZE];
    char *origin = NULL;
    bool ok = true;

    if (network == NULL || !network->sortint.actiu || network->sortint.ruta_fitxer == NULL || network->sortint.nom_regne == NULL) {
        return false;
    }

    fd = open(network->sortint.ruta_fitxer, O_RDONLY);
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

        if (!frame_set(&frame, network->sortint.tipus_data, origin, network->sortint.nom_regne, block, (size_t) bytes) || !network_send_frame_to_runtime_target(network, &frame)) {
            ok = false;
            break;
        }
    }

    free(origin);
    close(fd);
    return ok;
}

static bool network_send_file_data_to_endpoint(NetworkContext *network, const char *endpoint, uint8_t frame_type, const char *regne_desti, const char *ruta_fitxer) {
    int fd = -1;
    unsigned char block[CITADEL_FRAME_DATA_SIZE];
    char *origin = NULL;
    bool ok = true;

    if (network == NULL || endpoint == NULL || regne_desti == NULL || ruta_fitxer == NULL) {
        return false;
    }

    fd = open(ruta_fitxer, O_RDONLY);
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

        if (!frame_set(&frame, frame_type, origin, regne_desti, block, (size_t) bytes) || !network_send_frame_to_endpoint(endpoint, &frame)) {
            ok = false;
            break;
        }
    }

    free(origin);
    close(fd);
    return ok;
}

static void network_print_catalog(const char *nom_regne, const Product *productes, size_t num_items) {
    size_t i = 0;
    char *line = NULL;

    if (nom_regne == NULL) {
        return;
    }

    if (asprintf(&line, "Listing products from %s:", nom_regne) >= 0 && line != NULL) {
        utils_println(line);
        free(line);
    }

    for (i = 0; i < num_items; ++i) {
        if (asprintf(&line, "%zu. %s (%d units)\n", i + 1, productes[i].nom, productes[i].quantitat) >= 0 &&
            line != NULL) {
            utils_print(line);
            free(line);
        }
    }
}

static bool network_finalitzar_recepcio_transfer(NetworkContext *network) {
    char md5[CITADEL_MD5_LENGTH + 1];
    bool ok = false;
    Product *productes = NULL;
    size_t num_items = 0;
    char *reason = NULL;
    AllianceEntry *entry = NULL;

    if (network == NULL || !network->entrant.actiu || network->entrant.ruta_fitxer == NULL || network->entrant.endpoint_origen == NULL || network->entrant.nom_regne == NULL) {
        return false;
    }

    if (network->entrant.fd_fitxer >= 0) {
        close(network->entrant.fd_fitxer);
        network->entrant.fd_fitxer = -1;
    }

    ok = transfer_compute_md5sum(network->entrant.ruta_fitxer, md5) &&
         strcmp(md5, network->entrant.md5) == 0;

    {
        const char *md5_status = NULL;

        if (ok) {
            md5_status = "CHECK_OK";
        } else {
            md5_status = "CHECK_KO";
        }

        if (!network_send_blank_reply_with_realm_fallback(network, network->entrant.endpoint_origen, network->entrant.nom_regne, FRAME_TYPE_MD5_ACK, md5_status, network->config->nom_regne)) {
            ok = false;
        }
    }

    if (!ok) {
        if (network->entrant.tipus_transfer == TRANSFER_SIGIL) {
            pthread_mutex_lock(&network->lock);
            entry = network_buscar_entrada_locked(network, network->entrant.nom_regne);
            if (entry != NULL) {
                entry->sigil_verificat = false;
                entry->estat = ALLIANCE_FAILED;
                entry->resposta_pledge_en_curs = false;
                free(entry->endpoint_origen_pendent);
                entry->endpoint_origen_pendent = NULL;
                free(entry->endpoint_estable_pendent);
                entry->endpoint_estable_pendent = NULL;
            }
            pthread_mutex_unlock(&network->lock);
        }
        network_inbound_reset(network);
        return false;
    }

    if (network->entrant.tipus_transfer == TRANSFER_SIGIL) {
        pthread_mutex_lock(&network->lock);
        entry = network_buscar_entrada_locked(network, network->entrant.nom_regne);
        if (entry != NULL) {
            entry->sigil_verificat = true;
        }
        pthread_mutex_unlock(&network->lock);
    } else if (network->entrant.tipus_transfer == TRANSFER_PRODUCTS) {
        if (transfer_parse_catalog_file(network->entrant.ruta_fitxer, &productes, &num_items)) {
            pthread_mutex_lock(&network->lock);
            entry = network_buscar_entrada_locked(network, network->entrant.nom_regne);
            if (entry != NULL) {
                network_set_catalog(entry, productes, num_items);
                entry->esperant_productes = false;
                productes = NULL;
                num_items = 0;
            }
            pthread_mutex_unlock(&network->lock);
            if (entry != NULL) {
                network_print_catalog(entry->nom_regne, entry->cataleg, entry->num_cataleg);
            }
        }
        stock_alliberar_productes(productes, num_items);
    } else if (network->entrant.tipus_transfer == TRANSFER_ORDER) {
        if (transfer_parse_order_file(network->entrant.ruta_fitxer, &productes, &num_items)) {
            NetworkFrame response;
            char *origin = network_build_self_endpoint(network->config);
            const char *payload = NULL;

            if (stock_aplicar_order(network->stock, productes, num_items, &reason)) {
                payload = "OK";
                network_log_line("Order processed successfully. Stock updated.");
            } else {
                if (reason != NULL && strcmp(reason, "OUT_OF_STOCK") == 0) {
                    network_println("The vaults stand empty; the order cannot be fulfilled");
                }
                if (reason != NULL) {
                    payload = reason;
                } else {
                    payload = "REJECT";
                }
            }

            if (origin != NULL) {
                char *data = NULL;
                if (strcmp(payload, "OK") == 0) {
                    data = utils_strdup_safe("OK");
                } else if (asprintf(&data, "REJECT&%s", payload) < 0) {
                    data = NULL;
                }

                if (data != NULL && frame_set(&response, FRAME_TYPE_TRADE_RESPONSE, origin, network->entrant.nom_regne, data, strlen(data))) {
                    if (network->entrant.endpoint_origen != NULL) {
                        network_send_frame_to_endpoint(network->entrant.endpoint_origen, &response);
                    } else {
                        network_send_frame_to_realm(network, network->entrant.nom_regne, &response);
                    }
                }

                free(data);
                free(origin);
            }
            free(reason);
        }
        stock_alliberar_productes(productes, num_items);
    }

    network_inbound_reset(network);
    return true;
}

static void network_mark_timeout(AllianceEntry *entry) {
    if (entry == NULL) {
        return;
    }

    entry->estat = ALLIANCE_FAILED;
    entry->limit_temps = 0;
    entry->resposta_pledge_en_curs = false;
}

static void network_check_timeouts(NetworkContext *network) {
    size_t i = 0;
    time_t now = time(NULL);

    pthread_mutex_lock(&network->lock);
    for (i = 0; i < network->num_aliances; ++i) {
        if (network->aliances[i].estat == ALLIANCE_PENDING_OUT && network->aliances[i].limit_temps > 0 && now >= network->aliances[i].limit_temps) {
            network_mark_timeout(&network->aliances[i]);
            {
                char *line = NULL;
                if (asprintf(&line, "Pledge to %s has failed (TIMEOUT).", network->aliances[i].nom_regne) >= 0 && line != NULL) {
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
    char *origin_stable_endpoint = NULL;
    int size_value = 0;
    AllianceEntry *entry = NULL;
    bool ok = false;
    char route_endpoint[128];

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
    origin_stable_endpoint = strtok(NULL, "&");
    if (origin_realm == NULL || sigil_name == NULL || size_text == NULL || md5 == NULL || !utils_parse_int(size_text, &size_value) || size_value < 0) {
        free(copy);
        return;
    }

    pthread_mutex_lock(&network->lock);
    entry = network_buscar_entrada_locked(network, origin_realm);
    if (entry != NULL) {
        entry->estat = ALLIANCE_PENDING_IN;
        entry->sigil_verificat = false;
        entry->resposta_pledge_en_curs = false;
        ok = network_store_pending_origin(entry, frame->origen);
        if (ok) {
            memset(route_endpoint, 0, sizeof(route_endpoint));
            if (origin_stable_endpoint != NULL && origin_stable_endpoint[0] != '\0') {
                ok = network_store_pending_peer_stable_endpoint(entry, origin_stable_endpoint);
            } else if (network_copy_route_endpoint(network->config, origin_realm, route_endpoint, sizeof(route_endpoint))) {
                ok = network_store_pending_peer_stable_endpoint(entry, route_endpoint);
            } else {
                ok = network_store_pending_peer_stable_endpoint(entry, NULL);
            }
        }
        if (ok) {
            ok = !network->entrant.actiu &&
                 network_iniciar_recepcio_transfer(network, TRANSFER_SIGIL, origin_realm, frame->origen, sigil_name, (size_t) size_value, md5);
        }
    }
    pthread_mutex_unlock(&network->lock);

    {
        const char *ack_status = NULL;

        if (ok) {
            ack_status = "OK";
        } else {
            ack_status = "KO";
        }

        network_send_blank_reply_with_realm_fallback(network, frame->origen, origin_realm, FRAME_TYPE_ACK, ack_status, network->config->nom_regne);
    }
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
    char *nom_regne = NULL;
    char *stable_endpoint = NULL;
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
    nom_regne = strtok(NULL, "&");
    stable_endpoint = strtok(NULL, "&");
    if (decision == NULL || nom_regne == NULL) {
        free(copy);
        return;
    }

    accepted = utils_equals_ignore_case(decision, "ACCEPT");

    pthread_mutex_lock(&network->lock);
    entry = network_buscar_entrada_locked(network, nom_regne);
    if (entry == NULL || entry->estat != ALLIANCE_PENDING_OUT || entry->limit_temps == 0 || time(NULL) >= entry->limit_temps) {
        stale = true;
    } else {
        entry->limit_temps = 0;
        if (accepted) {
            entry->estat = ALLIANCE_ALLIED;
            if (stable_endpoint != NULL && stable_endpoint[0] != '\0') {
                if (!network_set_entry_endpoint(entry, stable_endpoint)) {
                    (void) network_set_entry_endpoint(entry, frame->origen);
                }
            } else {
                (void) network_set_entry_endpoint(entry, frame->origen);
            }
        } else {
            entry->estat = ALLIANCE_REJECTED;
        }
    }
    pthread_mutex_unlock(&network->lock);

    if (stale) {
        network_send_blank_payload(frame->origen, FRAME_TYPE_NACK, network->config->nom_regne);
    } else {
        if (asprintf(&ack_payload, "OK&%s", network->config->nom_regne) >= 0 && ack_payload != NULL) {
            network_send_blank_payload(frame->origen, FRAME_TYPE_ACK, ack_payload);
        }
    }

    if (!stale) {
        char *line = NULL;
        const char *alliance_text = NULL;

        if (accepted) {
            alliance_text = "established";
        } else {
            alliance_text = "rejected";
        }

        if (asprintf(&line, "Alliance with %s %s.", nom_regne, alliance_text) >= 0 && line != NULL) {
            utils_println(line);
            free(line);
        }
    }

    free(ack_payload);
    free(copy);
}

static void network_handle_products_request(NetworkContext *network, const NetworkFrame *frame) {
    char *origin_realm = frame_data_to_text(frame);
    AllianceEntry *entry = NULL;
    char *ruta_fitxer = NULL;
    char *nom_fitxer = NULL;
    char md5[CITADEL_MD5_LENGTH + 1];
    size_t mida_fitxer = 0;
    NetworkFrame header;
    char *origin = NULL;
    char *data = NULL;
    bool allowed = false;
    bool sent = false;

    if (origin_realm == NULL) {
        return;
    }

    pthread_mutex_lock(&network->lock);
    entry = network_buscar_entrada_locked(network, origin_realm);
    allowed = (entry != NULL && entry->estat == ALLIANCE_ALLIED);
    pthread_mutex_unlock(&network->lock);

    if (!allowed) {
        network_send_auth_error(network, origin_realm, origin_realm);
        free(origin_realm);
        return;
    }

    if (!transfer_write_inventory_file(network->config, network->stock, &ruta_fitxer, &nom_fitxer, &mida_fitxer, md5)) {
        free(origin_realm);
        return;
    }

    origin = network_build_self_endpoint(network->config);
    if (origin != NULL && asprintf(&data, "%s&%zu&%s", nom_fitxer, mida_fitxer, md5) >= 0 && frame_set(&header, FRAME_TYPE_PRODUCTS_RESPONSE, origin, origin_realm, data, strlen(data)) && network_send_frame_to_endpoint(frame->origen, &header)) {
        sent = network_send_file_data_to_endpoint(network, frame->origen, FRAME_TYPE_PRODUCTS_DATA, origin_realm, ruta_fitxer);
        if (sent) {
            char *line = NULL;
            if (asprintf(&line, ">>>LIST PRODUCTS request from %s.", origin_realm) >= 0 && line != NULL) {
                utils_println(line);
                free(line);
            }
            utils_println("Unveiling the vault of goods...");
            utils_println("The list has been sealed and sent forth");
        }
    }

    free(nom_fitxer);
    free(ruta_fitxer);
    free(data);
    free(origin);
    free(origin_realm);
}

static void network_handle_products_header(NetworkContext *network, const NetworkFrame *frame) {
    char *nom_regne = network_find_realm_by_endpoint(network, frame->origen);
    char *data = frame_data_to_text(frame);
    char *nom_fitxer = NULL;
    char md5[CITADEL_MD5_LENGTH + 1];
    size_t mida_fitxer = 0;
    bool ok = false;

    if (nom_regne == NULL || data == NULL) {
        free(nom_regne);
        free(data);
        return;
    }

    if (network_parse_header_triplet(data, &nom_fitxer, &mida_fitxer, md5)) {
        pthread_mutex_lock(&network->lock);
        ok = !network->entrant.actiu &&
             network_iniciar_recepcio_transfer(network, TRANSFER_PRODUCTS, nom_regne, frame->origen, nom_fitxer, mida_fitxer, md5);
        pthread_mutex_unlock(&network->lock);
    }

    {
        const char *ack_status = NULL;

        if (ok) {
            ack_status = "OK";
        } else {
            ack_status = "KO";
        }

        network_send_blank_reply_with_realm_fallback(network, frame->origen, nom_regne, FRAME_TYPE_ACK, ack_status, network->config->nom_regne);
    }
    free(nom_fitxer);
    free(nom_regne);
    free(data);
}

static void network_handle_trade_header(NetworkContext *network, const NetworkFrame *frame) {
    char *nom_regne = network_find_realm_by_endpoint(network, frame->origen);
    char *origin_realm = NULL;
    char *data = frame_data_to_text(frame);
    char *nom_fitxer = NULL;
    char md5[CITADEL_MD5_LENGTH + 1];
    size_t mida_fitxer = 0;
    AllianceEntry *entry = NULL;
    bool allowed = false;
    bool ok = false;

    if (data == NULL) {
        free(data);
        free(nom_regne);
        return;
    }

    if (network_parse_trade_header_payload(data, &origin_realm, &nom_fitxer, &mida_fitxer, md5)) {
        if (origin_realm != NULL) {
            free(nom_regne);
            nom_regne = origin_realm;
            origin_realm = NULL;
        }
    }

    if (nom_regne == NULL) {
        free(nom_fitxer);
        free(origin_realm);
        free(data);
        return;
    }

    pthread_mutex_lock(&network->lock);
    entry = network_buscar_entrada_locked(network, nom_regne);
    allowed = (entry != NULL && entry->estat == ALLIANCE_ALLIED);
    pthread_mutex_unlock(&network->lock);

    if (!allowed) {
        network_send_auth_error(network, nom_regne, nom_regne);
        free(nom_fitxer);
        free(origin_realm);
        free(nom_regne);
        free(data);
        return;
    }

    if (nom_fitxer != NULL) {
        pthread_mutex_lock(&network->lock);
        ok = !network->entrant.actiu &&
             network_iniciar_recepcio_transfer(network, TRANSFER_ORDER, nom_regne, frame->origen, nom_fitxer, mida_fitxer, md5);
        pthread_mutex_unlock(&network->lock);
    }

    {
        const char *ack_status = NULL;

        if (ok) {
            ack_status = "OK";
        } else {
            ack_status = "KO";
        }

        network_send_blank_reply_with_realm_fallback(network, frame->origen, nom_regne, FRAME_TYPE_ACK, ack_status, network->config->nom_regne);
    }
    if (ok) {
        char *line = NULL;
        if (asprintf(&line, "Trade request received from %s.", nom_regne) >= 0 && line != NULL) {
            network_log_line(line);
            free(line);
        }
    }

    free(nom_fitxer);
    free(origin_realm);
    free(nom_regne);
    free(data);
}

static void network_handle_file_data(NetworkContext *network, const NetworkFrame *frame, TransferKind kind) {
    pthread_mutex_lock(&network->lock);
    if (!network->entrant.actiu || network->entrant.tipus_transfer != kind || network->entrant.fd_fitxer < 0) {
        pthread_mutex_unlock(&network->lock);
        return;
    }

    if (utils_write_all(network->entrant.fd_fitxer, frame->data, frame->mida_data) < 0) {
        pthread_mutex_unlock(&network->lock);
        network_inbound_reset(network);
        return;
    }

    network->entrant.bytes_rebuts += frame->mida_data;
    if (network->entrant.bytes_rebuts < network->entrant.mida_fitxer) {
        pthread_mutex_unlock(&network->lock);
        return;
    }
    pthread_mutex_unlock(&network->lock);

    network_finalitzar_recepcio_transfer(network);
}

static float network_find_catalog_weight(NetworkContext *network, const char *nom_regne, const char *nom_producte, bool *trobat) {
    AllianceEntry *entry = NULL;
    size_t i = 0;
    float pes = 0.0f;

    if (trobat != NULL) {
        *trobat = false;
    }

    if (network == NULL || nom_regne == NULL || nom_producte == NULL) {
        return 0.0f;
    }

    pthread_mutex_lock(&network->lock);
    entry = network_buscar_entrada_locked(network, nom_regne);
    if (entry != NULL && entry->cataleg != NULL) {
        for (i = 0; i < entry->num_cataleg; ++i) {
            if (utils_equals_ignore_case(entry->cataleg[i].nom, nom_producte)) {
                pes = entry->cataleg[i].pes;
                if (trobat != NULL) {
                    *trobat = true;
                }
                break;
            }
        }
    }
    pthread_mutex_unlock(&network->lock);

    return pes;
}

static void network_restore_stock_snapshot_locked(Stock *stock, Product *copia_stock, size_t num_copia_stock, char *ruta_db_copia) {
    size_t i = 0;

    if (stock == NULL) {
        return;
    }

    for (i = 0; i < stock->num_productes; ++i) {
        free(stock->productes[i].nom);
        stock->productes[i].nom = NULL;
    }

    free(stock->productes);
    stock->productes = copia_stock;
    stock->num_productes = num_copia_stock;
    free(stock->ruta_db);
    stock->ruta_db = ruta_db_copia;
}

static bool network_apply_successful_order_to_local_stock(NetworkContext *network, const char *supplier_realm, const char *order_file_path) {
    char *order_text = NULL;
    char *stock_path_copy = NULL;
    bool applied = false;

    if (network == NULL || supplier_realm == NULL || order_file_path == NULL) {
        return false;
    }

    order_text = utils_read_file(order_file_path, NULL);
    if (order_text == NULL) {
        return false;
    }

    stock_path_copy = stock_copiar_ruta_db(network->stock);
    applied = network_apply_envoy_trade_result(network, network->stock, stock_path_copy, supplier_realm, ENVOY_RESULT_OK, order_text);
    free(stock_path_copy);
    free(order_text);
    return applied;
}

bool network_apply_envoy_trade_result(NetworkContext *network, Stock *stock, const char *stock_path, const char *regne, EnvoyResultStatus status, const char *payload) {
    Product *items = NULL;
    Product *copia_stock = NULL;
    size_t num_items = 0;
    size_t num_copia_stock = 0;
    size_t i = 0;
    bool ok = false;
    char *ruta_db_copia = NULL;
    char *new_db_path = NULL;

    if (network == NULL || stock == NULL || regne == NULL || payload == NULL) {
        return false;
    }

    if (status != ENVOY_RESULT_OK) {
        return false;
    }

    if (!transfer_parse_order_text(payload, &items, &num_items) || num_items == 0) {
        stock_alliberar_productes(items, num_items);
        return false;
    }

    for (i = 0; i < num_items; ++i) {
        bool pes_trobat = false;
        items[i].pes = network_find_catalog_weight(network, regne, items[i].nom, &pes_trobat);
        if (!pes_trobat) {
            items[i].pes = 0.0f;
        }
    }

    if (!stock_lock(stock)) {
        stock_alliberar_productes(items, num_items);
        return false;
    }

    num_copia_stock = stock->num_productes;
    if (stock->num_productes > 0) {
        copia_stock = stock_clonar_productes(stock->productes, stock->num_productes);
        if (copia_stock == NULL) {
            stock_unlock(stock);
            stock_alliberar_productes(items, num_items);
            return false;
        }
    }

    ruta_db_copia = utils_strdup_safe(stock->ruta_db);
    if (stock->ruta_db != NULL && ruta_db_copia == NULL) {
        stock_unlock(stock);
        stock_alliberar_productes(copia_stock, num_copia_stock);
        stock_alliberar_productes(items, num_items);
        return false;
    }

    if (stock->ruta_db == NULL && stock_path != NULL) {
        new_db_path = utils_strdup_safe(stock_path);
        if (new_db_path == NULL) {
            stock_unlock(stock);
            free(ruta_db_copia);
            stock_alliberar_productes(copia_stock, num_copia_stock);
            stock_alliberar_productes(items, num_items);
            return false;
        }
    }

    for (i = 0; i < num_items; ++i) {
        Product *existing = stock_buscar_mutable(stock, items[i].nom);
        if (existing != NULL) {
            existing->quantitat += items[i].quantitat;
            continue;
        }

        {
            Product *grown = (Product *) realloc(stock->productes, sizeof(Product) * (stock->num_productes + 1));
            Product *slot = NULL;

            if (grown == NULL) {
                network_restore_stock_snapshot_locked(stock, copia_stock, num_copia_stock, ruta_db_copia);
                copia_stock = NULL;
                ruta_db_copia = NULL;
                stock_unlock(stock);
                free(new_db_path);
                stock_alliberar_productes(items, num_items);
                return false;
            }

            stock->productes = grown;
            slot = &stock->productes[stock->num_productes];
            memset(slot, 0, sizeof(*slot));
            slot->nom = utils_strdup_safe(items[i].nom);
            if (slot->nom == NULL) {
                network_restore_stock_snapshot_locked(stock, copia_stock, num_copia_stock, ruta_db_copia);
                copia_stock = NULL;
                ruta_db_copia = NULL;
                stock_unlock(stock);
                free(new_db_path);
                stock_alliberar_productes(items, num_items);
                return false;
            }

            slot->quantitat = items[i].quantitat;
            slot->pes = items[i].pes;
            stock->num_productes++;
        }
    }

    if (stock_path != NULL && stock->ruta_db == NULL) {
        stock->ruta_db = new_db_path;
        new_db_path = NULL;
    }

    ok = stock_save_locked(stock);
    if (!ok) {
        network_restore_stock_snapshot_locked(stock, copia_stock, num_copia_stock, ruta_db_copia);
        copia_stock = NULL;
        ruta_db_copia = NULL;
    }
    stock_unlock(stock);
    stock_alliberar_productes(copia_stock, num_copia_stock);
    free(ruta_db_copia);
    free(new_db_path);
    stock_alliberar_productes(items, num_items);
    return ok;
}

static void network_handle_trade_response(NetworkContext *network, const NetworkFrame *frame) {
    char *nom_regne = network_find_realm_by_endpoint(network, frame->origen);
    char *data = frame_data_to_text(frame);
    AllianceEntry *entry = NULL;
    char *order_file_path = NULL;
    bool can_apply = false;

    if (nom_regne == NULL || data == NULL) {
        free(nom_regne);
        free(data);
        return;
    }

    pthread_mutex_lock(&network->lock);
    entry = network_buscar_entrada_locked(network, nom_regne);
    if (entry != NULL) {
        entry->esperant_ack_trade = false;
    }
    if (network->sortint.actiu && network->sortint.tipus_transfer == TRANSFER_ORDER && network->sortint.esperant_resposta_order && utils_equals_ignore_case(network->sortint.nom_regne, nom_regne)) {
        order_file_path = utils_strdup_safe(network->sortint.ruta_fitxer);
        can_apply = order_file_path != NULL;
        network_outbound_reset(network);
    }
    pthread_mutex_unlock(&network->lock);

    if (strcmp(data, "OK") == 0) {
        char *line = NULL;
        bool applied = true;

        if (can_apply) {
            applied = network_apply_successful_order_to_local_stock(network, nom_regne, order_file_path);
        }

        if (asprintf(&line, "Order accepted by %s. Stock updated.", nom_regne) >= 0 && line != NULL) {
            network_log_line(line);
            free(line);
        }
        if (!applied) {
            network_log_line("Order accepted.");
        }
    } else if (strncmp(data, "REJECT&", 7) == 0) {
        char *line = NULL;
        if (asprintf(&line, "Order rejected by %s.", nom_regne) >= 0 && line != NULL) {
            network_log_line(line);
            free(line);
        }
    }

    free(order_file_path);
    free(nom_regne);
    free(data);
}

static void network_handle_ack(NetworkContext *network, const NetworkFrame *frame) {
    char *data = frame_data_to_text(frame);
    char *copy = NULL;
    char *status = NULL;
    char *nom_regne = NULL;
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
    nom_regne = strtok(NULL, "&");
    if (status == NULL || nom_regne == NULL) {
        free(copy);
        return;
    }

    pthread_mutex_lock(&network->lock);
    if (network->sortint.actiu && network->sortint.esperant_ack_header && utils_equals_ignore_case(network->sortint.nom_regne, nom_regne)) {
        if (utils_equals_ignore_case(status, "OK")) {
            network->sortint.esperant_ack_header = false;
            network->sortint.esperant_ack_md5 = true;
            send_data = true;
        } else {
            if (network->sortint.tipus_transfer == TRANSFER_SIGIL) {
                AllianceEntry *entry = network_buscar_entrada_locked(network, nom_regne);
                if (entry != NULL) {
                    entry->estat = ALLIANCE_FAILED;
                    entry->limit_temps = 0;
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
    char *nom_regne = NULL;

    if (data == NULL) {
        return;
    }

    copy = utils_strdup_safe(data);
    free(data);
    if (copy == NULL) {
        return;
    }

    status = strtok(copy, "&");
    nom_regne = strtok(NULL, "&");
    if (status == NULL || nom_regne == NULL) {
        free(copy);
        return;
    }

    pthread_mutex_lock(&network->lock);
    if (network->sortint.actiu && network->sortint.esperant_ack_md5 && utils_equals_ignore_case(network->sortint.nom_regne, nom_regne)) {
        if (utils_equals_ignore_case(status, "CHECK_OK")) {
            if (network->sortint.tipus_transfer == TRANSFER_ORDER) {
                network->sortint.esperant_ack_md5 = false;
                network->sortint.esperant_resposta_order = true;
            } else {
                network_outbound_reset(network);
            }
        } else {
            if (network->sortint.tipus_transfer == TRANSFER_SIGIL) {
                AllianceEntry *entry = network_buscar_entrada_locked(network, nom_regne);
                if (entry != NULL) {
                    entry->estat = ALLIANCE_FAILED;
                    entry->limit_temps = 0;
                }
            }
            network_outbound_reset(network);
        }
    }
    pthread_mutex_unlock(&network->lock);

    if (!utils_equals_ignore_case(status, "CHECK_OK")) {
        network_println("Invalid checksum.");
    }

    free(copy);
}

static void network_handle_nack(NetworkContext *network, const NetworkFrame *frame) {
    char *data = frame_data_to_text(frame);
    AllianceEntry *entry = NULL;

    if (data == NULL) {
        return;
    }

    utils_trim(data);
    if (*data != '\0') {
        pthread_mutex_lock(&network->lock);
        entry = network_buscar_entrada_locked(network, data);
        if (entry != NULL && (entry->estat == ALLIANCE_PENDING_OUT || entry->estat == ALLIANCE_PENDING_IN)) {
            entry->estat = ALLIANCE_FAILED;
            entry->limit_temps = 0;
            entry->sigil_verificat = false;
            entry->resposta_pledge_en_curs = false;
            free(entry->endpoint_origen_pendent);
            entry->endpoint_origen_pendent = NULL;
            free(entry->endpoint_estable_pendent);
            entry->endpoint_estable_pendent = NULL;
        }
        pthread_mutex_unlock(&network->lock);

        network_println("Frame discarded.");
    } else {
        network_println("Frame discarded.");
    }

    (void) frame;
    free(data);
}

static void network_handle_unknown_realm(const NetworkFrame *frame) {
    char *data = frame_data_to_text(frame);

    if (data == NULL) {
        return;
    }

    network_println("Route not found.");

    free(data);
}

static void network_handle_auth_error(const NetworkFrame *frame) {
    char *data = frame_data_to_text(frame);

    if (data == NULL) {
        return;
    }

    network_println("Connection failed.");

    free(data);
}

static void network_handle_disconnect(NetworkContext *network, const NetworkFrame *frame) {
    char *nom_regne = network_find_realm_by_endpoint(network, frame->origen);

    if (nom_regne == NULL) {
        return;
    }

    pthread_mutex_lock(&network->lock);
    {
        AllianceEntry *entry = network_buscar_entrada_locked(network, nom_regne);
        if (entry != NULL) {
            entry->estat = ALLIANCE_INACTIVE;
        }
    }
    pthread_mutex_unlock(&network->lock);

    free(nom_regne);
}

static void network_process_local_frame(NetworkContext *network, const NetworkFrame *frame) {
    switch (frame->tipus) {
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
            network_handle_unknown_realm(frame);
            break;
        case FRAME_TYPE_AUTH_ERROR:
            network_handle_auth_error(frame);
            break;
        case FRAME_TYPE_DISCONNECT:
            network_handle_disconnect(network, frame);
            break;
        default:
            break;
    }
}

static bool network_should_log_forwarded_hop(uint8_t frame_type) {
    switch (frame_type) {
        case FRAME_TYPE_SIGIL_DATA:
        case FRAME_TYPE_PRODUCTS_DATA:
        case FRAME_TYPE_TRADE_DATA:
        case FRAME_TYPE_ACK:
        case FRAME_TYPE_MD5_ACK:
            return false;
        default:
            return true;
    }
}

static void network_forward_or_discard(NetworkContext *network, const NetworkFrame *frame) {
    char *origin_realm = NULL;
    char *next_endpoint = NULL;
    char *origin_name = NULL;
    char *line = NULL;

    if (network_resolve_next_endpoint(network, frame->destination, &next_endpoint) && network_send_frame_to_endpoint(next_endpoint, frame)) {
        if (network_should_log_forwarded_hop(frame->tipus)) {
            const char *hop_origin = NULL;

            origin_name = network_derive_origin_realm(network, frame);
            if (origin_name != NULL) {
                hop_origin = origin_name;
            } else {
                hop_origin = frame->origen;
            }

            if (asprintf(&line, ">>> Received hop: %s -> %s (%s)", hop_origin, frame->destination, network_frame_type_text(frame->tipus)) >= 0 && line != NULL) {
                utils_println(line);
                free(line);
            }
            if (asprintf(&line, "Found route: %s -> %s", frame->destination, next_endpoint) >= 0 && line != NULL) {
                utils_println(line);
                free(line);
            }
            utils_println("Forwarding...");
        }
        free(next_endpoint);
        free(origin_name);
        return;
    }

    origin_realm = network_derive_origin_realm(network, frame);
    if (origin_realm != NULL) {
        network_send_unknown_realm(network, origin_realm, frame->destination);
    }
    free(origin_realm);
    free(next_endpoint);
    free(origin_name);
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

    if (!frame_validar_checksum(&frame)) {
        {
            const char *nack_origin = NULL;

            if (raw_origin != NULL) {
                nack_origin = raw_origin;
            } else {
                nack_origin = frame.origen;
            }

            network_send_protocol_nack(network, nack_origin);
        }
        free(raw_origin);
        return;
    }

    if (!network_is_known_type(frame.tipus)) {
        network_send_protocol_nack(network, frame.origen);
        free(raw_origin);
        return;
    }

    if (frame.destination[0] == '\0') {
        if (!network_is_blank_destination_type(frame.tipus)) {
            network_send_protocol_nack(network, frame.origen);
            free(raw_origin);
            return;
        }
        network_process_local_frame(network, &frame);
    } else if (utils_equals_ignore_case(frame.destination, network->config->nom_regne)) {
        network_process_local_frame(network, &frame);
    } else {
        network_forward_or_discard(network, &frame);
    }

    free(raw_origin);
}

static void *network_server_main(void *arg) {
    NetworkContext *network = (NetworkContext *) arg;

    while (network->en_marxa) {
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
    size_t num_items = 0;

    for (i = 0; i < network->config->num_rutes; ++i) {
        if (!utils_equals_ignore_case(network->config->rutes[i].nom_regne, "DEFAULT")) {
            num_items++;
        }
    }

    network->aliances = (AllianceEntry *) calloc(num_items, sizeof(AllianceEntry));
    if (network->aliances == NULL) {
        return false;
    }

    network->num_aliances = num_items;
    num_items = 0;
    for (i = 0; i < network->config->num_rutes; ++i) {
        if (utils_equals_ignore_case(network->config->rutes[i].nom_regne, "DEFAULT")) {
            continue;
        }
        network->aliances[num_items].nom_regne = utils_strdup_safe(network->config->rutes[i].nom_regne);
        if (network->aliances[num_items].nom_regne == NULL) {
            size_t j = 0;
            for (j = 0; j < num_items; ++j) {
                free(network->aliances[j].nom_regne);
                network->aliances[j].nom_regne = NULL;
            }
            free(network->aliances);
            network->aliances = NULL;
            network->num_aliances = 0;
            return false;
        }
        num_items++;
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
    network->entrant.fd_fitxer = -1;

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
        for (size_t i = 0; i < network->num_aliances; ++i) {
            network_alliance_free(&network->aliances[i]);
        }
        free(network->aliances);
        network->aliances = NULL;
        network->num_aliances = 0;
        pthread_mutex_destroy(&network->lock);
        network_socket_cleanup();
        return false;
    }

    network->en_marxa = true;
    if (pthread_create(&network->fil_servidor, NULL, network_server_main, network) != 0) {
        CITADEL_SOCKET_CLOSE(network->server_fd);
        network->server_fd = CITADEL_INVALID_SOCKET;
        for (size_t i = 0; i < network->num_aliances; ++i) {
            network_alliance_free(&network->aliances[i]);
        }
        free(network->aliances);
        network->aliances = NULL;
        network->num_aliances = 0;
        pthread_mutex_destroy(&network->lock);
        network_socket_cleanup();
        return false;
    }

    network->inicialitzat = true;
    return true;
}

static void network_send_disconnects(NetworkContext *network) {
    size_t i = 0;
    char *origin = NULL;

    if (network == NULL || !network->inicialitzat) {
        return;
    }

    origin = network_build_self_endpoint(network->config);
    if (origin == NULL) {
        return;
    }

    pthread_mutex_lock(&network->lock);
    for (i = 0; i < network->num_aliances; ++i) {
        if (network->aliances[i].estat == ALLIANCE_ALLIED && network->aliances[i].endpoint_conegut != NULL) {
            NetworkFrame frame;
            if (frame_set(&frame, FRAME_TYPE_DISCONNECT, origin, network->aliances[i].nom_regne, "DISCONNECT", strlen("DISCONNECT"))) {
                network_send_frame_to_endpoint(network->aliances[i].endpoint_conegut, &frame);
            }
        }
    }
    pthread_mutex_unlock(&network->lock);

    free(origin);
}

void network_shutdown(NetworkContext *network) {
    size_t i = 0;

    if (network == NULL || !network->inicialitzat) {
        return;
    }

    network_send_disconnects(network);
    network->en_marxa = false;

    if (network->server_fd != CITADEL_INVALID_SOCKET) {
        shutdown(network->server_fd, SHUT_RDWR);
        CITADEL_SOCKET_CLOSE(network->server_fd);
        network->server_fd = CITADEL_INVALID_SOCKET;
    }

    pthread_join(network->fil_servidor, NULL);

    network_outbound_reset(network);
    network_inbound_reset(network);
    for (i = 0; i < network->num_aliances; ++i) {
        network_alliance_free(&network->aliances[i]);
    }

    free(network->aliances);
    pthread_mutex_destroy(&network->lock);
    network_socket_cleanup();
    memset(network, 0, sizeof(*network));
}

bool network_realm_exists(NetworkContext *network, const char *nom_regne) {
    bool exists = false;

    if (network == NULL || nom_regne == NULL) {
        return false;
    }

    pthread_mutex_lock(&network->lock);
    exists = network_buscar_entrada_locked(network, nom_regne) != NULL;
    pthread_mutex_unlock(&network->lock);
    return exists;
}

bool network_has_active_alliance(NetworkContext *network, const char *nom_regne) {
    bool active = false;
    AllianceEntry *entry = NULL;

    if (network == NULL || nom_regne == NULL) {
        return false;
    }

    pthread_mutex_lock(&network->lock);
    entry = network_buscar_entrada_locked(network, nom_regne);
    active = (entry != NULL && entry->estat == ALLIANCE_ALLIED);
    pthread_mutex_unlock(&network->lock);
    return active;
}

bool network_send_pledge(NetworkContext *network, const char *nom_regne, const char *sigil_name) {
    AllianceEntry *entry = NULL;
    char *sigil_path = NULL;
    char *nom_fitxer = NULL;
    char *origin = NULL;
    char *data = NULL;
    char md5[CITADEL_MD5_LENGTH + 1];
    size_t mida_fitxer = 0;
    NetworkFrame frame;
    bool sent = false;

    if (network == NULL || nom_regne == NULL || sigil_name == NULL) {
        return false;
    }

    sigil_path = transfer_resolve_sigil_path(network->config, sigil_name);
    if (sigil_path == NULL || !transfer_obtenir_info_fitxer(sigil_path, &nom_fitxer, &mida_fitxer, md5)) {
        free(sigil_path);
        return false;
    }

    pthread_mutex_lock(&network->lock);
    entry = network_buscar_entrada_locked(network, nom_regne);
    if (entry == NULL || entry->estat == ALLIANCE_ALLIED || entry->estat == ALLIANCE_PENDING_OUT || network->sortint.actiu) {
        pthread_mutex_unlock(&network->lock);
        free(sigil_path);
        free(nom_fitxer);
        return false;
    }
    pthread_mutex_unlock(&network->lock);

    origin = network_build_self_endpoint(network->config);
    if (origin == NULL || asprintf(&data, "%s&%s&%zu&%s", network->config->nom_regne, nom_fitxer, mida_fitxer, md5) < 0 || !frame_set(&frame, FRAME_TYPE_PLEDGE, origin, nom_regne, data, strlen(data))) {
        free(sigil_path);
        free(nom_fitxer);
        free(origin);
        free(data);
        return false;
    }

    sent = network_send_frame_to_realm(network, nom_regne, &frame);
    if (sent) {
        pthread_mutex_lock(&network->lock);
        entry = network_buscar_entrada_locked(network, nom_regne);
        if (entry != NULL) {
            entry->estat = ALLIANCE_PENDING_OUT;
            entry->limit_temps = time(NULL) + CITADEL_PLEDGE_TIMEOUT_SECONDS;
        }
        network_outbound_reset(network);
        network->sortint.actiu = true;
        network->sortint.tipus_transfer = TRANSFER_SIGIL;
        network->sortint.nom_regne = utils_strdup_safe(nom_regne);
        network->sortint.nom_fitxer = nom_fitxer;
        network->sortint.ruta_fitxer = sigil_path;
        network->sortint.mida_fitxer = mida_fitxer;
        strncpy(network->sortint.md5, md5, CITADEL_MD5_LENGTH);
        network->sortint.md5[CITADEL_MD5_LENGTH] = '\0';
        network->sortint.tipus_data = FRAME_TYPE_SIGIL_DATA;
        network->sortint.esperant_ack_header = true;
        pthread_mutex_unlock(&network->lock);
        nom_fitxer = NULL;
        sigil_path = NULL;
        {
            char *line = NULL;
            if (asprintf(&line, "Pledge sent to %s.", nom_regne) >= 0 && line != NULL) {
                utils_println(line);
                free(line);
            }
        }
    }

    free(sigil_path);
    free(nom_fitxer);
    free(origin);
    free(data);
    return sent;
}

bool network_send_pledge_response(NetworkContext *network, const char *nom_regne, bool accepted) {
    AllianceEntry *entry = NULL;
    NetworkFrame frame;
    char *origin = NULL;
    char *data = NULL;
    char *response_endpoint = NULL;
    char *endpoint_estable_peer = NULL;
    bool sent = false;

    if (network == NULL || nom_regne == NULL) {
        return false;
    }

    pthread_mutex_lock(&network->lock);
    entry = network_buscar_entrada_locked(network, nom_regne);
    if (entry == NULL || entry->estat != ALLIANCE_PENDING_IN || !entry->sigil_verificat) {
        pthread_mutex_unlock(&network->lock);
        return false;
    }
    response_endpoint = utils_strdup_safe(entry->endpoint_origen_pendent);
    endpoint_estable_peer = utils_strdup_safe(entry->endpoint_estable_pendent);
    pthread_mutex_unlock(&network->lock);

    origin = network_build_self_endpoint(network->config);
    if (origin == NULL) {
        free(origin);
        free(data);
        free(endpoint_estable_peer);
        return false;
    }

    if (accepted) {
        if (asprintf(&data, "%s&%s&%s", "ACCEPT", network->config->nom_regne, origin) < 0) {
            free(origin);
            free(data);
            free(endpoint_estable_peer);
            return false;
        }
    } else {
        if (asprintf(&data, "%s&%s", "REJECT", network->config->nom_regne) < 0) {
            free(origin);
            free(data);
            free(endpoint_estable_peer);
            return false;
        }
    }

    if (!frame_set(&frame, FRAME_TYPE_PLEDGE_RESPONSE, origin, nom_regne, data, strlen(data))) {
        free(origin);
        free(data);
        free(endpoint_estable_peer);
        return false;
    }

    if (response_endpoint != NULL) {
        sent = network_send_frame_to_endpoint(response_endpoint, &frame);
    } else {
        sent = network_send_frame_to_realm(network, nom_regne, &frame);
    }
    if (sent) {
        pthread_mutex_lock(&network->lock);
        entry = network_buscar_entrada_locked(network, nom_regne);
        if (entry != NULL) {
            entry->limit_temps = 0;
            if (accepted) {
                entry->estat = ALLIANCE_ALLIED;
                if (endpoint_estable_peer != NULL && endpoint_estable_peer[0] != '\0') {
                    (void) network_set_entry_endpoint(entry, endpoint_estable_peer);
                } else if (entry->endpoint_estable_pendent != NULL) {
                    (void) network_set_entry_endpoint(entry, entry->endpoint_estable_pendent);
                }
            } else {
                entry->estat = ALLIANCE_REJECTED;
            }
            entry->sigil_verificat = false;
            entry->resposta_pledge_en_curs = false;
            free(entry->endpoint_origen_pendent);
            entry->endpoint_origen_pendent = NULL;
            free(entry->endpoint_estable_pendent);
            entry->endpoint_estable_pendent = NULL;
        }
        pthread_mutex_unlock(&network->lock);

        {
            char *line = NULL;
            const char *alliance_text = NULL;

            if (accepted) {
                alliance_text = "established";
            } else {
                alliance_text = "rejected";
            }

            if (asprintf(&line, "Alliance with %s %s.", nom_regne, alliance_text) >= 0 && line != NULL) {
                utils_println(line);
                free(line);
            }
        }
    }

    free(origin);
    free(data);
    free(response_endpoint);
    free(endpoint_estable_peer);
    return sent;
}

bool network_request_remote_products(NetworkContext *network, const char *nom_regne) {
    NetworkFrame frame;
    char *origin = NULL;
    AllianceEntry *entry = NULL;
    bool sent = false;

    if (network == NULL || nom_regne == NULL) {
        return false;
    }

    pthread_mutex_lock(&network->lock);
    entry = network_buscar_entrada_locked(network, nom_regne);
    if (entry == NULL || entry->estat != ALLIANCE_ALLIED) {
        pthread_mutex_unlock(&network->lock);
        return false;
    }
    entry->esperant_productes = true;
    pthread_mutex_unlock(&network->lock);

    origin = network_build_self_endpoint(network->config);
    if (origin == NULL || !frame_set(&frame, FRAME_TYPE_PRODUCTS_REQUEST, origin, nom_regne, network->config->nom_regne, strlen(network->config->nom_regne))) {
        pthread_mutex_lock(&network->lock);
        entry = network_buscar_entrada_locked(network, nom_regne);
        if (entry != NULL) {
            entry->esperant_productes = false;
        }
        pthread_mutex_unlock(&network->lock);
        free(origin);
        return false;
    }

    sent = network_send_frame_to_realm(network, nom_regne, &frame);
    if (!sent) {
        pthread_mutex_lock(&network->lock);
        entry = network_buscar_entrada_locked(network, nom_regne);
        if (entry != NULL) {
            entry->esperant_productes = false;
        }
        pthread_mutex_unlock(&network->lock);
    }
    free(origin);
    return sent;
}

bool network_send_trade_offer(NetworkContext *network, const char *nom_regne, const char *ruta_fitxer) {
    AllianceEntry *entry = NULL;
    NetworkFrame frame;
    char *origin = NULL;
    char *data = NULL;
    char *nom_fitxer = NULL;
    char md5[CITADEL_MD5_LENGTH + 1];
    size_t mida_fitxer = 0;
    bool sent = false;

    if (network == NULL || nom_regne == NULL || ruta_fitxer == NULL) {
        return false;
    }

    if (!transfer_obtenir_info_fitxer(ruta_fitxer, &nom_fitxer, &mida_fitxer, md5)) {
        return false;
    }

    pthread_mutex_lock(&network->lock);
    entry = network_buscar_entrada_locked(network, nom_regne);
    if (entry == NULL || entry->estat != ALLIANCE_ALLIED || network->sortint.actiu) {
        pthread_mutex_unlock(&network->lock);
        free(nom_fitxer);
        return false;
    }
    entry->esperant_ack_trade = true;
    pthread_mutex_unlock(&network->lock);

    origin = network_build_self_endpoint(network->config);
    if (origin == NULL || asprintf(&data, "%s&%zu&%s", nom_fitxer, mida_fitxer, md5) < 0 || !frame_set(&frame, FRAME_TYPE_TRADE_HEADER, origin, nom_regne, data, strlen(data))) {
        pthread_mutex_lock(&network->lock);
        entry = network_buscar_entrada_locked(network, nom_regne);
        if (entry != NULL) {
            entry->esperant_ack_trade = false;
        }
        pthread_mutex_unlock(&network->lock);
        free(nom_fitxer);
        free(origin);
        free(data);
        return false;
    }

    sent = network_send_frame_to_realm(network, nom_regne, &frame);
    if (sent) {
        pthread_mutex_lock(&network->lock);
        network_outbound_reset(network);
        network->sortint.actiu = true;
        network->sortint.tipus_transfer = TRANSFER_ORDER;
        network->sortint.nom_regne = utils_strdup_safe(nom_regne);
        network->sortint.nom_fitxer = nom_fitxer;
        network->sortint.ruta_fitxer = utils_strdup_safe(ruta_fitxer);
        network->sortint.mida_fitxer = mida_fitxer;
        strncpy(network->sortint.md5, md5, CITADEL_MD5_LENGTH);
        network->sortint.md5[CITADEL_MD5_LENGTH] = '\0';
        network->sortint.tipus_data = FRAME_TYPE_TRADE_DATA;
        network->sortint.esperant_ack_header = true;
        pthread_mutex_unlock(&network->lock);
        nom_fitxer = NULL;
    } else {
        pthread_mutex_lock(&network->lock);
        entry = network_buscar_entrada_locked(network, nom_regne);
        if (entry != NULL) {
            entry->esperant_ack_trade = false;
        }
        pthread_mutex_unlock(&network->lock);
    }

    free(nom_fitxer);
    free(origin);
    free(data);
    return sent;
}

bool network_get_remote_products_copy(NetworkContext *network, const char *nom_regne, Product **productes_out, size_t *num_productes_out) {
    AllianceEntry *entry = NULL;
    Product *copy = NULL;

    if (network == NULL || nom_regne == NULL || productes_out == NULL || num_productes_out == NULL) {
        return false;
    }

    *productes_out = NULL;
    *num_productes_out = 0;

    pthread_mutex_lock(&network->lock);
    entry = network_buscar_entrada_locked(network, nom_regne);
    if (entry != NULL && entry->num_cataleg > 0) {
        copy = stock_clonar_productes(entry->cataleg, entry->num_cataleg);
        if (copy != NULL) {
            *productes_out = copy;
            *num_productes_out = entry->num_cataleg;
        }
    }
    pthread_mutex_unlock(&network->lock);

    return *productes_out != NULL;
}

bool network_get_direct_endpoint_for_realm(NetworkContext *network, const char *regne, char *endpoint_out, size_t endpoint_size) {
    AllianceEntry *entry = NULL;
    ParsedEndpoint parsed;
    bool ok = false;

    if (network == NULL || regne == NULL || endpoint_out == NULL || endpoint_size == 0) {
        return false;
    }

    endpoint_out[0] = '\0';

    pthread_mutex_lock(&network->lock);
    entry = network_buscar_entrada_locked(network, regne);
    if (entry != NULL && entry->estat == ALLIANCE_ALLIED && entry->endpoint_conegut != NULL && network_parse_endpoint(entry->endpoint_conegut, &parsed)) {
        ok = snprintf(endpoint_out, endpoint_size, "%s", entry->endpoint_conegut) >= 0 &&
             strlen(entry->endpoint_conegut) < endpoint_size;
    }
    pthread_mutex_unlock(&network->lock);

    return ok;
}

bool network_can_launch_pledge(NetworkContext *network, const char *nom_regne) {
    AllianceEntry *entry = NULL;
    bool allowed = false;

    if (network == NULL || nom_regne == NULL) {
        return false;
    }

    pthread_mutex_lock(&network->lock);
    entry = network_buscar_entrada_locked(network, nom_regne);
    allowed = (entry != NULL && entry->estat != ALLIANCE_ALLIED && entry->estat != ALLIANCE_PENDING_OUT && entry->estat != ALLIANCE_PENDING_IN);
    pthread_mutex_unlock(&network->lock);
    return allowed;
}

bool network_mark_pledge_pending(NetworkContext *network, const char *nom_regne) {
    AllianceEntry *entry = NULL;
    bool marked = false;

    if (network == NULL || nom_regne == NULL) {
        return false;
    }

    pthread_mutex_lock(&network->lock);
    entry = network_buscar_entrada_locked(network, nom_regne);
    if (entry != NULL && entry->estat != ALLIANCE_ALLIED && entry->estat != ALLIANCE_PENDING_OUT && entry->estat != ALLIANCE_PENDING_IN) {
        entry->estat = ALLIANCE_PENDING_OUT;
        entry->limit_temps = 0;
        marked = true;
    }
    pthread_mutex_unlock(&network->lock);
    return marked;
}

void network_revert_pledge_pending(NetworkContext *network, const char *nom_regne) {
    AllianceEntry *entry = NULL;

    if (network == NULL || nom_regne == NULL) {
        return;
    }

    pthread_mutex_lock(&network->lock);
    entry = network_buscar_entrada_locked(network, nom_regne);
    if (entry != NULL && entry->estat == ALLIANCE_PENDING_OUT) {
        entry->estat = ALLIANCE_NONE;
        entry->limit_temps = 0;
    }
    pthread_mutex_unlock(&network->lock);
}

bool network_prepare_pledge_response_mission(NetworkContext *network, const char *regne, bool accepted, char *endpoint_desti_out, size_t mida_endpoint_desti, char *endpoint_estable_peer_out, size_t mida_endpoint_estable_peer) {
    AllianceEntry *entry = NULL;
    bool prepared = false;

    (void) accepted;

    if (network == NULL || regne == NULL || endpoint_desti_out == NULL || mida_endpoint_desti == 0 || endpoint_estable_peer_out == NULL || mida_endpoint_estable_peer == 0) {
        return false;
    }

    endpoint_desti_out[0] = '\0';
    endpoint_estable_peer_out[0] = '\0';

    pthread_mutex_lock(&network->lock);
    entry = network_buscar_entrada_locked(network, regne);
    if (entry != NULL && entry->estat == ALLIANCE_PENDING_IN && entry->sigil_verificat && !entry->resposta_pledge_en_curs && entry->endpoint_origen_pendent != NULL && entry->endpoint_origen_pendent[0] != '\0') {
        int written_target = snprintf(endpoint_desti_out, mida_endpoint_desti, "%s", entry->endpoint_origen_pendent);
        int written_peer = 0;

        if (written_target >= 0 && (size_t) written_target < mida_endpoint_desti) {
            prepared = true;
            if (entry->endpoint_estable_pendent != NULL && entry->endpoint_estable_pendent[0] != '\0') {
                written_peer = snprintf(endpoint_estable_peer_out, mida_endpoint_estable_peer, "%s", entry->endpoint_estable_pendent);
                if (written_peer < 0 || (size_t) written_peer >= mida_endpoint_estable_peer) {
                    prepared = false;
                }
            }
        }

        if (prepared) {
            entry->resposta_pledge_en_curs = true;
        } else {
            endpoint_desti_out[0] = '\0';
            endpoint_estable_peer_out[0] = '\0';
        }
    }
    pthread_mutex_unlock(&network->lock);

    return prepared;
}

void network_revert_pledge_response_mission(NetworkContext *network, const char *regne) {
    AllianceEntry *entry = NULL;

    if (network == NULL || regne == NULL) {
        return;
    }

    pthread_mutex_lock(&network->lock);
    entry = network_buscar_entrada_locked(network, regne);
    if (entry != NULL && entry->estat == ALLIANCE_PENDING_IN) {
        entry->resposta_pledge_en_curs = false;
    }
    pthread_mutex_unlock(&network->lock);
}

void network_apply_envoy_pledge_result(NetworkContext *network, const char *nom_regne, EnvoyResultStatus status, const char *endpoint_remot) {
    AllianceEntry *entry = NULL;
    ParsedEndpoint parsed;

    if (network == NULL || nom_regne == NULL) {
        return;
    }

    pthread_mutex_lock(&network->lock);
    entry = network_buscar_entrada_locked(network, nom_regne);
    if (entry != NULL) {
        entry->limit_temps = 0;
        switch (status) {
            case ENVOY_RESULT_OK:
                entry->estat = ALLIANCE_ALLIED;
                if (endpoint_remot != NULL && endpoint_remot[0] != '\0' &&
                    network_parse_endpoint(endpoint_remot, &parsed)) {
                    (void) network_set_entry_endpoint(entry, endpoint_remot);
                }
                break;
            case ENVOY_RESULT_REJECTED:
                entry->estat = ALLIANCE_REJECTED;
                break;
            case ENVOY_RESULT_TIMEOUT:
            case ENVOY_RESULT_FAILED:
            default:
                entry->estat = ALLIANCE_FAILED;
                break;
        }
    }
    pthread_mutex_unlock(&network->lock);
}

void network_apply_envoy_pledge_response_result(NetworkContext *network, const char *regne, bool accepted, EnvoyResultStatus status, const char *endpoint_estable_peer) {
    AllianceEntry *entry = NULL;
    ParsedEndpoint parsed;

    if (network == NULL || regne == NULL) {
        return;
    }

    pthread_mutex_lock(&network->lock);
    entry = network_buscar_entrada_locked(network, regne);
    if (entry != NULL) {
        entry->resposta_pledge_en_curs = false;
        if (status == ENVOY_RESULT_OK) {
            entry->limit_temps = 0;
            entry->sigil_verificat = false;
            if (accepted) {
                entry->estat = ALLIANCE_ALLIED;
                if (endpoint_estable_peer != NULL && endpoint_estable_peer[0] != '\0' &&
                    network_parse_endpoint(endpoint_estable_peer, &parsed)) {
                    (void) network_set_entry_endpoint(entry, endpoint_estable_peer);
                } else if (entry->endpoint_estable_pendent != NULL && network_parse_endpoint(entry->endpoint_estable_pendent, &parsed)) {
                    (void) network_set_entry_endpoint(entry, entry->endpoint_estable_pendent);
                }
            } else {
                entry->estat = ALLIANCE_REJECTED;
            }
            free(entry->endpoint_origen_pendent);
            entry->endpoint_origen_pendent = NULL;
            free(entry->endpoint_estable_pendent);
            entry->endpoint_estable_pendent = NULL;
        }
    }
    pthread_mutex_unlock(&network->lock);
}

bool network_can_request_products(NetworkContext *network, const char *nom_regne) {
    AllianceEntry *entry = NULL;
    bool allowed = false;

    if (network == NULL || nom_regne == NULL) {
        return false;
    }

    pthread_mutex_lock(&network->lock);
    entry = network_buscar_entrada_locked(network, nom_regne);
    allowed = (entry != NULL && entry->estat == ALLIANCE_ALLIED);
    pthread_mutex_unlock(&network->lock);
    return allowed;
}

void network_apply_envoy_products_result(NetworkContext *network, const char *nom_regne, const char *payload) {
    AllianceEntry *entry = NULL;
    Product *productes = NULL;
    size_t num_items = 0;
    Product *print_products = NULL;
    size_t print_count = 0;

    if (network == NULL || nom_regne == NULL || payload == NULL) {
        return;
    }

    if (!transfer_parse_catalog_text(payload, &productes, &num_items)) {
        pthread_mutex_lock(&network->lock);
        entry = network_buscar_entrada_locked(network, nom_regne);
        if (entry != NULL) {
            entry->esperant_productes = false;
        }
        pthread_mutex_unlock(&network->lock);
        return;
    }

    pthread_mutex_lock(&network->lock);
    entry = network_buscar_entrada_locked(network, nom_regne);
    if (entry != NULL) {
        network_set_catalog(entry, productes, num_items);
        entry->esperant_productes = false;
        print_products = stock_clonar_productes(entry->cataleg, entry->num_cataleg);
        print_count = entry->num_cataleg;
        productes = NULL;
        num_items = 0;
    }
    pthread_mutex_unlock(&network->lock);

    if (print_products != NULL) {
        network_print_catalog(nom_regne, print_products, print_count);
    }

    stock_alliberar_productes(print_products, print_count);
    stock_alliberar_productes(productes, num_items);
}

void network_print_pledge_status(NetworkContext *network) {
    size_t i = 0;
    bool any = false;

    if (network == NULL) {
        return;
    }

    pthread_mutex_lock(&network->lock);
    for (i = 0; i < network->num_aliances; ++i) {
        char *line = NULL;
        const char *status_text = network_status_text(network->aliances[i].estat);

        if (strcmp(status_text, "NONE") == 0) {
            continue;
        }

        any = true;
        if (asprintf(&line, "- %s: %s\n", network->aliances[i].nom_regne, status_text) >= 0 && line != NULL) {
            utils_print(line);
            free(line);
        }
    }
    pthread_mutex_unlock(&network->lock);

    if (!any) {
        utils_println("You have no pledges awaiting or accepted");
    }
}

static const char *network_frame_type_text(uint8_t type) {
    switch (type) {
        case FRAME_TYPE_PLEDGE:
            return "PLEDGE";
        case FRAME_TYPE_SIGIL_DATA:
            return "SIGIL_DATA";
        case FRAME_TYPE_PLEDGE_RESPONSE:
            return "PLEDGE_RESPONSE";
        case FRAME_TYPE_PRODUCTS_REQUEST:
            return "PRODUCTS_REQUEST";
        case FRAME_TYPE_PRODUCTS_RESPONSE:
            return "PRODUCTS_RESPONSE";
        case FRAME_TYPE_PRODUCTS_DATA:
            return "PRODUCTS_DATA";
        case FRAME_TYPE_TRADE_HEADER:
        case FRAME_TYPE_TRADE_DATA:
        case FRAME_TYPE_TRADE_RESPONSE:
            return "TRADE";
        case FRAME_TYPE_UNKNOWN_REALM:
            return "UNKNOWN_REALM";
        case FRAME_TYPE_AUTH_ERROR:
            return "AUTH_ERROR";
        case FRAME_TYPE_ACK:
            return "ACK";
        case FRAME_TYPE_MD5_ACK:
            return "MD5_ACK";
        case FRAME_TYPE_NACK:
            return "NACK";
        case FRAME_TYPE_DISCONNECT:
            return "DISCONNECT";
        default:
            return "UNKNOWN";
    }
}
