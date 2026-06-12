#ifndef ENVOY_H
#define ENVOY_H

#include "../utils/system.h"

struct MaesterContext;

#define ENVOY_RESULT_MAGIC 0x454E5659u

typedef enum {
    ENVOY_MISSION_NONE = 0,
    ENVOY_MISSION_PLEDGE,
    ENVOY_MISSION_PRODUCTS,
    ENVOY_MISSION_TRADE,
    ENVOY_MISSION_PLEDGE_RESPONSE
} EnvoyMissionType;

typedef enum {
    ENVOY_SLOT_FREE = 0,
    ENVOY_SLOT_ON_MISSION
} EnvoySlotStatus;

typedef enum {
    ENVOY_RESULT_OK = 0,
    ENVOY_RESULT_REJECTED,
    ENVOY_RESULT_FAILED,
    ENVOY_RESULT_TIMEOUT
} EnvoyResultStatus;

typedef struct {
    uint32_t magic;
    int id_envoy;
    int tipus_missio;
    int estat_resultat;
    char regne[64];
    char endpoint_remot[128];
    uint32_t mida_payload;
} EnvoyResultHeader;

typedef struct {
    int id;
    pid_t pid;
    int read_fd;
    EnvoySlotStatus estat;
    EnvoyMissionType tipus_missio;
    bool resposta_acceptada;
    char *regne_desti;
    char *ruta_fitxer;
    time_t iniciat_a;
} EnvoySlot;

typedef struct {
    EnvoySlot *slots;
    int num_envoys;
    pthread_mutex_t mutex;
} EnvoyManager;

bool envoy_manager_init(EnvoyManager *manager, int num_envoys);
void envoy_manager_destroy(EnvoyManager *manager);
void envoy_print_status(EnvoyManager *manager);
void envoy_reap_finished(struct MaesterContext *context);
void envoy_kill_all(EnvoyManager *manager);
bool envoy_spawn_mission(struct MaesterContext *context, EnvoyMissionType tipus, const char *regne, const char *ruta_fitxer);
bool envoy_spawn_pledge_response(struct MaesterContext *context, const char *regne, bool accepted, const char *endpoint_desti, const char *endpoint_estable_peer);
bool envoy_result_write(int fd, const EnvoyResultHeader *header, const void *payload);
bool envoy_result_read(int fd, EnvoyResultHeader *header, char **payload_out);

#endif
