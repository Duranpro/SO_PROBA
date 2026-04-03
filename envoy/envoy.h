#ifndef ENVOY_H
#define ENVOY_H

#include "../utils/system.h"

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

#endif
