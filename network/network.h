#ifndef NETWORK_H
#define NETWORK_H

#include "../config/config.h"
#include "../envoy/envoy.h"
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
    char *nom_regne;
    AllianceStatus estat;
    char *endpoint_conegut;
    char *endpoint_origen_pendent;
    char *endpoint_estable_pendent;
    time_t limit_temps;
    bool esperant_productes;
    bool esperant_ack_trade;
    bool sigil_verificat;
    bool resposta_pledge_en_curs;
    Product *cataleg;
    size_t num_cataleg;
} AllianceEntry;

typedef struct {
    bool actiu;
    TransferKind tipus_transfer;
    char *nom_regne;
    char *endpoint_desti;
    char *nom_fitxer;
    char *ruta_fitxer;
    size_t mida_fitxer;
    char md5[CITADEL_MD5_LENGTH + 1];
    uint8_t tipus_data;
    bool esperant_ack_header;
    bool esperant_ack_md5;
    bool esperant_resposta_order;
} OutboundTransferState;

typedef struct {
    bool actiu;
    TransferKind tipus_transfer;
    char *nom_regne;
    char *endpoint_origen;
    char *nom_fitxer;
    char *ruta_fitxer;
    int fd_fitxer;
    size_t mida_fitxer;
    size_t bytes_rebuts;
    char md5[CITADEL_MD5_LENGTH + 1];
} InboundTransferState;

typedef struct {
    bool inicialitzat;
    bool en_marxa;
    citadel_socket_t server_fd;
    pthread_t fil_servidor;
    pthread_mutex_t lock;
    CitadelConfig *config;
    Stock *stock;
    AllianceEntry *aliances;
    size_t num_aliances;
    OutboundTransferState sortint;
    InboundTransferState entrant;
} NetworkContext;

bool network_init(NetworkContext *network, CitadelConfig *config, Stock *stock);
void network_shutdown(NetworkContext *network);

bool network_realm_exists(NetworkContext *network, const char *nom_regne);
bool network_has_active_alliance(NetworkContext *network, const char *nom_regne);

bool network_send_pledge(NetworkContext *network, const char *nom_regne, const char *sigil_name);
bool network_send_pledge_response(NetworkContext *network, const char *nom_regne, bool accepted);
bool network_request_remote_products(NetworkContext *network, const char *nom_regne);
bool network_send_trade_offer(NetworkContext *network, const char *nom_regne, const char *ruta_fitxer);
bool network_get_remote_products_copy(NetworkContext *network, const char *nom_regne, Product **productes_out, size_t *num_productes_out);
bool network_can_launch_pledge(NetworkContext *network, const char *nom_regne);
bool network_mark_pledge_pending(NetworkContext *network, const char *nom_regne);
void network_revert_pledge_pending(NetworkContext *network, const char *nom_regne);
bool network_prepare_pledge_response_mission(NetworkContext *network, const char *regne, bool accepted, char *endpoint_desti_out, size_t mida_endpoint_desti, char *endpoint_estable_peer_out, size_t mida_endpoint_estable_peer);
void network_revert_pledge_response_mission(NetworkContext *network, const char *regne);
bool network_can_request_products(NetworkContext *network, const char *nom_regne);
bool network_get_direct_endpoint_for_realm(NetworkContext *network, const char *regne, char *endpoint_out, size_t endpoint_size);
void network_apply_envoy_pledge_result(NetworkContext *network, const char *nom_regne, EnvoyResultStatus status, const char *endpoint_remot);
void network_apply_envoy_pledge_response_result(NetworkContext *network, const char *regne, bool accepted, EnvoyResultStatus status, const char *endpoint_estable_peer);
void network_apply_envoy_products_result(NetworkContext *network, const char *nom_regne, const char *payload);
bool network_apply_envoy_trade_result(NetworkContext *network, Stock *stock, const char *stock_path, const char *regne, EnvoyResultStatus status, const char *payload);

void network_print_pledge_status(NetworkContext *network);

#endif
