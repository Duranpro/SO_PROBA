#ifndef ENVOY_H
#define ENVOY_H

#include "../utils/system.h"

<<<<<<< HEAD
typedef enum {
    ENVOY_MISSION_NONE = 0,
    ENVOY_MISSION_PLEDGE,
    ENVOY_MISSION_PLEDGE_RESPOND,
    ENVOY_MISSION_LIST_PRODUCTS,
    ENVOY_MISSION_TRADE
} EnvoyMissionType;

typedef struct {
    pid_t pid;
    int to_child_fd;
    int from_child_fd;
    bool alive;
    bool busy;
    bool started;
    bool completion_sent;
    EnvoyMissionType mission;
    char realm[64];
    char arg[160];
    bool last_success;
    time_t assigned_at;
} EnvoyProcess;

typedef struct {
    bool initialized;
    int count;
    EnvoyProcess *envoys;
    pthread_mutex_t lock;
} EnvoyManager;

void envoy_manager_init_empty(EnvoyManager *manager);
bool envoy_manager_init(EnvoyManager *manager, int count);
void envoy_manager_shutdown(EnvoyManager *manager);

int envoy_manager_assign(EnvoyManager *manager, EnvoyMissionType mission,
                         const char *realm, const char *arg);
bool envoy_manager_complete(EnvoyManager *manager, int envoy_index, bool success);
int envoy_manager_find_busy(EnvoyManager *manager, EnvoyMissionType mission, const char *realm);
bool envoy_manager_has_free(EnvoyManager *manager);
void envoy_manager_poll_events(EnvoyManager *manager);
void envoy_manager_print_status(EnvoyManager *manager);

const char *envoy_mission_text(EnvoyMissionType mission);
=======
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
>>>>>>> 795777dd2388f27e94bd3af97a531f1a701d767c

#endif
