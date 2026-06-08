#ifndef ENVOY_H
#define ENVOY_H

#include "../utils/system.h"

struct MaesterContext;

#define ENVOY_RESULT_MAGIC 0x454E5659u

typedef enum {
    ENVOY_MISSION_NONE = 0,
    ENVOY_MISSION_PLEDGE,
    ENVOY_MISSION_PRODUCTS,
    ENVOY_MISSION_TRADE
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
    int envoy_id;
    int mission_type;
    int result_status;
    char realm[64];
    char remote_endpoint[128];
    uint32_t payload_size;
} EnvoyResultHeader;

typedef struct {
    int id;
    pid_t pid;
    int read_fd;
    EnvoySlotStatus status;
    EnvoyMissionType mission_type;
    char *target_realm;
    char *file_path;
    time_t started_at;
} EnvoySlot;

typedef struct {
    EnvoySlot *slots;
    int count;
    pthread_mutex_t mutex;
} EnvoyManager;

bool envoy_manager_init(EnvoyManager *manager, int count);
void envoy_manager_destroy(EnvoyManager *manager);
void envoy_print_status(EnvoyManager *manager);
void envoy_reap_finished(struct MaesterContext *context);
void envoy_kill_all(EnvoyManager *manager);
bool envoy_spawn_mission(struct MaesterContext *context,
                         EnvoyMissionType type,
                         const char *realm,
                         const char *file_path);
bool envoy_result_write(int fd,
                        const EnvoyResultHeader *header,
                        const void *payload);
bool envoy_result_read(int fd,
                       EnvoyResultHeader *header,
                       char **payload_out);

#endif
