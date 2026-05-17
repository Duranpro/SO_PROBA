#include "envoy.h"

#include "../utils/utils.h"

typedef enum {
    ENVOY_EVT_STARTED = 11
} EnvoyIpcKind;

typedef struct {
    uint8_t kind;
    uint8_t mission;
    uint8_t success;
    uint8_t reserved;
    char realm[64];
    char arg[160];
} EnvoyIpcMessage;

static ssize_t envoy_pipe_read_full(int fd, void *buffer, size_t size) {
    size_t total = 0;

    while (total < size) {
        ssize_t bytes = read(fd, (char *) buffer + total, size - total);
        if (bytes == 0) {
            return 0;
        }
        if (bytes < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return -2;
            }
            return -1;
        }
        total += (size_t) bytes;
    }

    return (ssize_t) total;
}

static bool envoy_pipe_write_full(int fd, const void *buffer, size_t size) {
    size_t total = 0;

    while (total < size) {
        ssize_t written = write(fd, (const char *) buffer + total, size - total);
        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        total += (size_t) written;
    }

    return true;
}

const char *envoy_mission_text(EnvoyMissionType mission) {
    switch (mission) {
        case ENVOY_MISSION_PLEDGE:
            return "PLEDGE";
        case ENVOY_MISSION_PLEDGE_RESPOND:
            return "PLEDGE_RESPOND";
        case ENVOY_MISSION_LIST_PRODUCTS:
            return "LIST_PRODUCTS";
        case ENVOY_MISSION_TRADE:
            return "TRADE";
        case ENVOY_MISSION_NONE:
        default:
            return "IDLE";
    }
}

void envoy_manager_init_empty(EnvoyManager *manager) {
    if (manager == NULL) {
        return;
    }

    memset(manager, 0, sizeof(*manager));
}

static void envoy_child_loop(int write_fd, EnvoyMissionType mission, const char *realm, const char *arg) {
    EnvoyIpcMessage outgoing;

    memset(&outgoing, 0, sizeof(outgoing));
    outgoing.kind = ENVOY_EVT_STARTED;
    outgoing.mission = (uint8_t) mission;
    if (realm != NULL) {
        strncpy(outgoing.realm, realm, sizeof(outgoing.realm) - 1);
    }
    if (arg != NULL) {
        strncpy(outgoing.arg, arg, sizeof(outgoing.arg) - 1);
    }
    envoy_pipe_write_full(write_fd, &outgoing, sizeof(outgoing));

    for (;;) {
        pause();
    }

    close(write_fd);
    _exit(0);
}

static bool envoy_spawn_one(EnvoyProcess *envoy, EnvoyMissionType mission, const char *realm, const char *arg) {
    int child_to_parent[2] = {-1, -1};
    pid_t pid = 0;

    if (envoy == NULL) {
        return false;
    }

    if (pipe(child_to_parent) != 0) {
        if (child_to_parent[0] >= 0) {
            close(child_to_parent[0]);
        }
        if (child_to_parent[1] >= 0) {
            close(child_to_parent[1]);
        }
        return false;
    }

    pid = fork();
    if (pid < 0) {
        close(child_to_parent[0]);
        close(child_to_parent[1]);
        return false;
    }

    if (pid == 0) {
        close(child_to_parent[0]);
        envoy_child_loop(child_to_parent[1], mission, realm, arg);
    }

    close(child_to_parent[1]);

    envoy->pid = pid;
    envoy->from_child_fd = child_to_parent[0];
    envoy->alive = true;
    envoy->busy = false;
    envoy->started = false;
    envoy->mission = ENVOY_MISSION_NONE;
    envoy->realm[0] = '\0';
    envoy->arg[0] = '\0';
    envoy->last_success = true;
    envoy->assigned_at = 0;

    fcntl(envoy->from_child_fd, F_SETFL, O_NONBLOCK);
    return true;
}

bool envoy_manager_init(EnvoyManager *manager, int count) {
    if (manager == NULL || count < 0) {
        return false;
    }

    envoy_manager_init_empty(manager);
    if (pthread_mutex_init(&manager->lock, NULL) != 0) {
        return false;
    }

    manager->count = count;
    if (count == 0) {
        manager->initialized = true;
        return true;
    }

    manager->envoys = (EnvoyProcess *) calloc((size_t) count, sizeof(EnvoyProcess));
    if (manager->envoys == NULL) {
        pthread_mutex_destroy(&manager->lock);
        return false;
    }

    manager->initialized = true;
    return true;
}

static void envoy_reset_process(EnvoyProcess *envoy) {
    if (envoy == NULL) {
        return;
    }

    envoy->busy = false;
    envoy->started = false;
    envoy->mission = ENVOY_MISSION_NONE;
    envoy->realm[0] = '\0';
    envoy->arg[0] = '\0';
    envoy->assigned_at = 0;
}

void envoy_manager_shutdown(EnvoyManager *manager) {
    if (manager == NULL || !manager->initialized) {
        return;
    }

    pthread_mutex_lock(&manager->lock);
    int i = 0;
    for (i = 0; i < manager->count; ++i) {
        if (!manager->envoys[i].alive) {
            continue;
        }

        kill(manager->envoys[i].pid, SIGTERM);
    }
    pthread_mutex_unlock(&manager->lock);

    envoy_manager_reap_children(manager);

    pthread_mutex_lock(&manager->lock);
    for (i = 0; i < manager->count; ++i) {
        if (!manager->envoys[i].alive) {
            continue;
        }
        waitpid(manager->envoys[i].pid, NULL, 0);
        if (manager->envoys[i].from_child_fd >= 0) {
            close(manager->envoys[i].from_child_fd);
            manager->envoys[i].from_child_fd = -1;
        }
        manager->envoys[i].alive = false;
    }
    pthread_mutex_unlock(&manager->lock);

    free(manager->envoys);
    manager->envoys = NULL;
    manager->count = 0;
    manager->initialized = false;
    pthread_mutex_destroy(&manager->lock);
}

int envoy_manager_assign(EnvoyManager *manager, EnvoyMissionType mission, const char *realm, const char *arg) {
    int i = 0;

    if (manager == NULL || !manager->initialized || mission == ENVOY_MISSION_NONE) {
        return -1;
    }

    pthread_mutex_lock(&manager->lock);
    for (i = 0; i < manager->count; ++i) {
        EnvoyProcess *envoy = &manager->envoys[i];
        const char *safe_realm = "";
        const char *safe_arg = "";

        if (envoy->busy) {
            continue;
        }

        if (realm != NULL) {
            safe_realm = realm;
        }
        if (arg != NULL) {
            safe_arg = arg;
        }
        if (!envoy_spawn_one(envoy, mission, safe_realm, safe_arg)) {
            pthread_mutex_unlock(&manager->lock);
            return -1;
        }

        envoy->busy = true;
        envoy->started = false;
        envoy->mission = mission;
        envoy->last_success = true;
        envoy->assigned_at = time(NULL);
        strncpy(envoy->realm, safe_realm, sizeof(envoy->realm) - 1);
        envoy->realm[sizeof(envoy->realm) - 1] = '\0';
        strncpy(envoy->arg, safe_arg, sizeof(envoy->arg) - 1);
        envoy->arg[sizeof(envoy->arg) - 1] = '\0';
        pthread_mutex_unlock(&manager->lock);
        return i;
    }
    pthread_mutex_unlock(&manager->lock);
    return -1;
}

bool envoy_manager_complete(EnvoyManager *manager, int envoy_index, bool success) {
    if (manager == NULL || !manager->initialized || envoy_index < 0 || envoy_index >= manager->count) {
        return false;
    }

    pthread_mutex_lock(&manager->lock);
    if (!manager->envoys[envoy_index].busy || !manager->envoys[envoy_index].alive) {
        pthread_mutex_unlock(&manager->lock);
        return false;
    }

    manager->envoys[envoy_index].last_success = success;
    kill(manager->envoys[envoy_index].pid, SIGTERM);
    pthread_mutex_unlock(&manager->lock);
    return true;
}

int envoy_manager_find_busy(EnvoyManager *manager, EnvoyMissionType mission, const char *realm) {
    int i = 0;

    if (manager == NULL || !manager->initialized || mission == ENVOY_MISSION_NONE || realm == NULL) {
        return -1;
    }

    pthread_mutex_lock(&manager->lock);
    for (i = 0; i < manager->count; ++i) {
        EnvoyProcess *envoy = &manager->envoys[i];
        if (envoy->busy && envoy->mission == mission && utils_equals_ignore_case(envoy->realm, realm)) {
            pthread_mutex_unlock(&manager->lock);
            return i;
        }
    }
    pthread_mutex_unlock(&manager->lock);
    return -1;
}

bool envoy_manager_has_free(EnvoyManager *manager) {
    int i = 0;
    bool free_found = false;

    if (manager == NULL || !manager->initialized) {
        return false;
    }

    pthread_mutex_lock(&manager->lock);
    for (i = 0; i < manager->count; ++i) {
        if (!manager->envoys[i].busy) {
            free_found = true;
            break;
        }
    }
    pthread_mutex_unlock(&manager->lock);
    return free_found;
}

void envoy_manager_poll_events(EnvoyManager *manager) {
    int i = 0;

    if (manager == NULL || !manager->initialized) {
        return;
    }

    pthread_mutex_lock(&manager->lock);
    for (i = 0; i < manager->count; ++i) {
        EnvoyIpcMessage message;
        EnvoyProcess *envoy = &manager->envoys[i];

        if (!envoy->alive || envoy->from_child_fd < 0) {
            continue;
        }

        while (true) {
            ssize_t rc = envoy_pipe_read_full(envoy->from_child_fd, &message, sizeof(message));
            if (rc == -2) {
                break;
            }
            if (rc <= 0) {
                break;
            }

            if (message.kind == ENVOY_EVT_STARTED) {
                envoy->started = true;
            }
        }
    }
    pthread_mutex_unlock(&manager->lock);
}

void envoy_manager_reap_children(EnvoyManager *manager) {
    int i = 0;

    if (manager == NULL || !manager->initialized) {
        return;
    }

    pthread_mutex_lock(&manager->lock);
    for (i = 0; i < manager->count; ++i) {
        int status = 0;
        pid_t rc = 0;
        EnvoyProcess *envoy = &manager->envoys[i];

        if (!envoy->alive || envoy->pid <= 0) {
            continue;
        }

        rc = waitpid(envoy->pid, &status, WNOHANG);
        if (rc <= 0) {
            continue;
        }

        if (envoy->from_child_fd >= 0) {
            close(envoy->from_child_fd);
            envoy->from_child_fd = -1;
        }
        envoy->alive = false;
        envoy->pid = 0;
        envoy_reset_process(envoy);
    }
    pthread_mutex_unlock(&manager->lock);
}

void envoy_manager_print_status(EnvoyManager *manager) {
    int i = 0;

    if (manager == NULL || !manager->initialized) {
        utils_println("No Envoys initialized.");
        return;
    }

    pthread_mutex_lock(&manager->lock);
    for (i = 0; i < manager->count; ++i) {
        char *line = NULL;
        EnvoyProcess *envoy = &manager->envoys[i];
        const char *display_realm = "-";

        if (!envoy->busy) {
            if (asprintf(&line, "- Envoy %d: FREE\n", i + 1) >= 0 && line != NULL) {
                utils_print(line);
                free(line);
            }
            continue;
        }

        if (envoy->realm[0] != '\0') {
            display_realm = envoy->realm;
        }

        if (asprintf(&line, "- Envoy %d: ON MISSION (%s to %s)\n", i + 1, envoy_mission_text(envoy->mission), display_realm) >= 0 && line != NULL) {
            utils_print(line);
            free(line);
        }
    }
    pthread_mutex_unlock(&manager->lock);
}
