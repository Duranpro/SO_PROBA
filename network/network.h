#ifndef NETWORK_H
#define NETWORK_H

#include "../config/config.h"
#include "../stock/stock.h"
#include "../transfer/transfer.h"
#include "../utils/system.h"
#include "frame.h"

#define CITADEL_PLEDGE_TIMEOUT_SECONDS 120

typedef enum {
    ALLIANCE_NONE = 0,
    ALLIANCE_PENDING_OUT,
    ALLIANCE_PENDING_IN,
    ALLIANCE_ALLIED,
    ALLIANCE_INACTIVE,
    ALLIANCE_REJECTED,
    ALLIANCE_FAILED
} AllianceStatus;

typedef enum {
    TRANSFER_NONE = 0,
    TRANSFER_SIGIL,
    TRANSFER_PRODUCTS,
    TRANSFER_ORDER
} TransferKind;

typedef struct {
    char *realm_name;
    AllianceStatus status;
    char *known_endpoint;
    char *pending_origin_endpoint;
    time_t deadline;
    bool waiting_products;
    bool waiting_trade_ack;
    bool sigil_verified;
    Product *catalog;
    size_t catalog_count;
} AllianceEntry;

typedef struct {
    bool active;
    TransferKind kind;
    char *realm_name;
    char *file_name;
    char *file_path;
    size_t file_size;
    char md5[CITADEL_MD5_LENGTH + 1];
    uint8_t data_type;
    bool waiting_header_ack;
    bool waiting_md5_ack;
    bool waiting_order_response;
} OutboundTransferState;

typedef struct {
    bool active;
    TransferKind kind;
    char *realm_name;
    char *origin_endpoint;
    char *file_name;
    char *file_path;
    int file_fd;
    size_t file_size;
    size_t bytes_received;
    char md5[CITADEL_MD5_LENGTH + 1];
} InboundTransferState;

typedef struct {
    bool initialized;
    bool running;
    citadel_socket_t server_fd;
    pthread_t server_thread;
    pthread_mutex_t lock;
    CitadelConfig *config;
    Stock *stock;
    AllianceEntry *alliances;
    size_t alliance_count;
    OutboundTransferState outbound;
    InboundTransferState inbound;
} NetworkContext;

bool network_init(NetworkContext *network, CitadelConfig *config, Stock *stock);
void network_shutdown(NetworkContext *network);

bool network_realm_exists(NetworkContext *network, const char *realm_name);
bool network_has_active_alliance(NetworkContext *network, const char *realm_name);

bool network_send_pledge(NetworkContext *network, const char *realm_name, const char *sigil_name);
bool network_send_pledge_response(NetworkContext *network, const char *realm_name, bool accepted);
bool network_request_remote_products(NetworkContext *network, const char *realm_name);
bool network_send_trade_offer(NetworkContext *network, const char *realm_name, const char *file_path);
bool network_get_remote_products_copy(NetworkContext *network, const char *realm_name,
                                      Product **products_out, size_t *count_out);
bool network_get_alliance_status(NetworkContext *network, const char *realm_name, AllianceStatus *status_out);
bool network_is_waiting_products(NetworkContext *network, const char *realm_name, bool *waiting_out);
bool network_is_waiting_trade(NetworkContext *network, const char *realm_name, bool *waiting_out);

void network_print_pledge_status(NetworkContext *network);

#endif
