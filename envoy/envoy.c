#include "envoy.h"

#include "../utils/utils.h"

typedef enum {
    ENVOY_CMD_NONE = 0,
    ENVOY_CMD_ASSIGN = 1,
    ENVOY_CMD_COMPLETE = 2,
    ENVOY_CMD_SHUTDOWN = 3,
    ENVOY_EVT_STARTED = 11,
    ENVOY_EVT_FINISHED = 12
} EnvoyIpcKind;

typedef struct {
    uint8_t kind;
    uint8_t mission;
    uint8_t success;
    uint8_t reserved;
    char realm[64];
    char arg[160];
} EnvoyIpcMessage;

#ifndef _WIN32
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
#endif

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

#ifndef _WIN32
static void envoy_child_loop(int read_fd, int write_fd) {
    bool running = true;
    bool mission_active = false;
    EnvoyIpcMessage incoming;
    EnvoyIpcMessage outgoing;

    while (running) {
        ssize_t rc = envoy_pipe_read_full(read_fd, &incoming, sizeof(incoming));
        if (rc <= 0) {
            break;
        }

        memset(&outgoing, 0, sizeof(outgoing));
        switch (incoming.kind) {
            case ENVOY_CMD_ASSIGN:
                mission_active = true;
                outgoing.kind = ENVOY_EVT_STARTED;
                outgoing.mission = incoming.mission;
                strncpy(outgoing.realm, incoming.realm, sizeof(outgoing.realm) - 1);
                strncpy(outgoing.arg, incoming.arg, sizeof(outgoing.arg) - 1);
                envoy_pipe_write_full(write_fd, &outgoing, sizeof(outgoing));
                break;
            case ENVOY_CMD_COMPLETE:
                if (mission_active) {
                    mission_active = false;
                    outgoing.kind = ENVOY_EVT_FINISHED;
                    outgoing.mission = incoming.mission;
                    outgoing.success = incoming.success;
                    strncpy(outgoing.realm, incoming.realm, sizeof(outgoing.realm) - 1);
                    strncpy(outgoing.arg, incoming.arg, sizeof(outgoing.arg) - 1);
                    envoy_pipe_write_full(write_fd, &outgoing, sizeof(outgoing));
                }
                break;
            case ENVOY_CMD_SHUTDOWN:
                running = false;
                break;
            default:
                break;
        }
    }

    close(read_fd);
    close(write_fd);
    _exit(0);
}

static bool envoy_spawn_one(EnvoyProcess *envoy) {
    int parent_to_child[2] = {-1, -1};
    int child_to_parent[2] = {-1, -1};
    pid_t pid = 0;

    if (envoy == NULL) {
        return false;
    }

    if (pipe(parent_to_child) != 0 || pipe(child_to_parent) != 0) {
        if (parent_to_child[0] >= 0) close(parent_to_child[0]);
        if (parent_to_child[1] >= 0) close(parent_to_child[1]);
        if (child_to_parent[0] >= 0) close(child_to_parent[0]);
        if (child_to_parent[1] >= 0) close(child_to_parent[1]);
        return false;
    }

    pid = fork();
    if (pid < 0) {
        close(parent_to_child[0]);
        close(parent_to_child[1]);
        close(child_to_parent[0]);
        close(child_to_parent[1]);
        return false;
    }

    if (pid == 0) {
        close(parent_to_child[1]);
        close(child_to_parent[0]);
        envoy_child_loop(parent_to_child[0], child_to_parent[1]);
    }

    close(parent_to_child[0]);
    close(child_to_parent[1]);

    envoy->pid = pid;
    envoy->to_child_fd = parent_to_child[1];
    envoy->from_child_fd = child_to_parent[0];
    envoy->alive = true;
    envoy->busy = false;
    envoy->started = false;
    envoy->completion_sent = false;
    envoy->mission = ENVOY_MISSION_NONE;
    envoy->realm[0] = '\0';
    envoy->arg[0] = '\0';
    envoy->last_success = true;
    envoy->assigned_at = 0;

    fcntl(envoy->from_child_fd, F_SETFL, O_NONBLOCK);
    return true;
}
#endif

bool envoy_manager_init(EnvoyManager *manager, int count) {
    int i = 0;

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

#ifndef _WIN32
    for (i = 0; i < count; ++i) {
        if (!envoy_spawn_one(&manager->envoys[i])) {
            manager->count = i;
            envoy_manager_shutdown(manager);
            return false;
        }
    }
#else
    for (i = 0; i < count; ++i) {
        manager->envoys[i].pid = 0;
        manager->envoys[i].to_child_fd = -1;
        manager->envoys[i].from_child_fd = -1;
        manager->envoys[i].alive = true;
        manager->envoys[i].busy = false;
        manager->envoys[i].mission = ENVOY_MISSION_NONE;
    }
#endif

    manager->initialized = true;
    return true;
}

static void envoy_reset_process(EnvoyProcess *envoy) {
    if (envoy == NULL) {
        return;
    }

    envoy->busy = false;
    envoy->started = false;
    envoy->completion_sent = false;
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
#ifndef _WIN32
    int i = 0;
    for (i = 0; i < manager->count; ++i) {
        EnvoyIpcMessage message;
        if (!manager->envoys[i].alive) {
            continue;
        }

        memset(&message, 0, sizeof(message));
        message.kind = ENVOY_CMD_SHUTDOWN;
        envoy_pipe_write_full(manager->envoys[i].to_child_fd, &message, sizeof(message));
        close(manager->envoys[i].to_child_fd);
        manager->envoys[i].to_child_fd = -1;
    }

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
#endif
    pthread_mutex_unlock(&manager->lock);

    free(manager->envoys);
    manager->envoys = NULL;
    manager->count = 0;
    manager->initialized = false;
    pthread_mutex_destroy(&manager->lock);
}

int envoy_manager_assign(EnvoyManager *manager, EnvoyMissionType mission,
                         const char *realm, const char *arg) {
    int i = 0;

    if (manager == NULL || !manager->initialized || mission == ENVOY_MISSION_NONE) {
        return -1;
    }

    pthread_mutex_lock(&manager->lock);
    for (i = 0; i < manager->count; ++i) {
        EnvoyProcess *envoy = &manager->envoys[i];
        if (!envoy->alive || envoy->busy) {
            continue;
        }

        envoy->busy = true;
        envoy->started = false;
        envoy->completion_sent = false;
        envoy->mission = mission;
        envoy->last_success = true;
        envoy->assigned_at = time(NULL);
        strncpy(envoy->realm, realm != NULL ? realm : "", sizeof(envoy->realm) - 1);
        envoy->realm[sizeof(envoy->realm) - 1] = '\0';
        strncpy(envoy->arg, arg != NULL ? arg : "", sizeof(envoy->arg) - 1);
        envoy->arg[sizeof(envoy->arg) - 1] = '\0';

#ifndef _WIN32
        {
            EnvoyIpcMessage message;
            memset(&message, 0, sizeof(message));
            message.kind = ENVOY_CMD_ASSIGN;
            message.mission = (uint8_t) mission;
            strncpy(message.realm, envoy->realm, sizeof(message.realm) - 1);
            strncpy(message.arg, envoy->arg, sizeof(message.arg) - 1);
            if (!envoy_pipe_write_full(envoy->to_child_fd, &message, sizeof(message))) {
                envoy_reset_process(envoy);
                pthread_mutex_unlock(&manager->lock);
                return -1;
            }
        }
#endif
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
    if (!manager->envoys[envoy_index].busy || manager->envoys[envoy_index].completion_sent) {
        pthread_mutex_unlock(&manager->lock);
        return false;
    }

    manager->envoys[envoy_index].last_success = success;
    manager->envoys[envoy_index].completion_sent = true;

#ifndef _WIN32
    {
        EnvoyIpcMessage message;
        memset(&message, 0, sizeof(message));
        message.kind = ENVOY_CMD_COMPLETE;
        message.mission = (uint8_t) manager->envoys[envoy_index].mission;
        message.success = success ? 1 : 0;
        strncpy(message.realm, manager->envoys[envoy_index].realm, sizeof(message.realm) - 1);
        strncpy(message.arg, manager->envoys[envoy_index].arg, sizeof(message.arg) - 1);
        if (!envoy_pipe_write_full(manager->envoys[envoy_index].to_child_fd, &message, sizeof(message))) {
            envoy_reset_process(&manager->envoys[envoy_index]);
            pthread_mutex_unlock(&manager->lock);
            return false;
        }
    }
#else
    envoy_reset_process(&manager->envoys[envoy_index]);
#endif

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
        if (manager->envoys[i].alive && !manager->envoys[i].busy) {
            free_found = true;
            break;
        }
    }
    pthread_mutex_unlock(&manager->lock);
    return free_found;
}

void envoy_manager_poll_events(EnvoyManager *manager) {
#ifndef _WIN32
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
                envoy->alive = false;
                envoy_reset_process(envoy);
                break;
            }

            if (message.kind == ENVOY_EVT_STARTED) {
                envoy->started = true;
            } else if (message.kind == ENVOY_EVT_FINISHED) {
                char *line = NULL;
                bool success = message.success != 0;
                if (asprintf(&line, "Envoy %d completed %s for %s (%s).",
                             i,
                             envoy_mission_text(envoy->mission),
                             envoy->realm[0] != '\0' ? envoy->realm : "-",
                             success ? "OK" : "FAILED") >= 0 && line != NULL) {
                    utils_println(line);
                    free(line);
                }
                envoy_reset_process(envoy);
            }
        }
    }
    pthread_mutex_unlock(&manager->lock);
#else
    (void) manager;
#endif
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
        const char *state = envoy->busy ? "BUSY" : "FREE";

        if (asprintf(&line, "Envoy %d | pid=%ld | %s | mission=%s | realm=%s\n",
                     i,
                     (long) envoy->pid,
                     state,
                     envoy_mission_text(envoy->mission),
                     envoy->realm[0] != '\0' ? envoy->realm : "-") >= 0 && line != NULL) {
            utils_print(line);
            free(line);
        }
    }
    pthread_mutex_unlock(&manager->lock);
}
