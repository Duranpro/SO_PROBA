#include "envoy.h"

#include "../utils/utils.h"

typedef enum {
    ENVOY_IPC_CMD_RUN = 1,
    ENVOY_IPC_CMD_COMPLETE = 2,
    ENVOY_IPC_CMD_SHUTDOWN = 3,
    ENVOY_EVT_DISPATCHED = 11,
    ENVOY_EVT_COMPLETED = 12
} EnvoyIpcKind;

typedef struct {
    uint8_t kind;
    uint8_t mission;
    uint8_t success;
    uint8_t reserved;
    char realm[64];
    char arg[160];
} EnvoyIpcMessage;

static ssize_t envoy_pipe_read_full_blocking(int fd, void *buffer, size_t size) {
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
            return -1;
        }
        total += (size_t) bytes;
    }

    return (ssize_t) total;
}

static ssize_t envoy_pipe_read_full_nonblocking(int fd, void *buffer, size_t size) {
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
                return total == 0 ? -2 : -1;
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

static void envoy_child_loop(int read_fd, int write_fd, EnvoyActionRunner runner, void *runner_context) {
    for (;;) {
        EnvoyIpcMessage incoming;
        ssize_t rc = envoy_pipe_read_full_blocking(read_fd, &incoming, sizeof(incoming));
        if (rc <= 0) {
            break;
        }

        if (incoming.kind == ENVOY_IPC_CMD_SHUTDOWN) {
            break;
        }

        if (incoming.kind == ENVOY_IPC_CMD_RUN) {
            EnvoyIpcMessage outgoing;
            bool launched = false;

            if (runner != NULL) {
                launched = runner((EnvoyMissionType) incoming.mission, incoming.realm, incoming.arg, runner_context);
            }

            memset(&outgoing, 0, sizeof(outgoing));
            outgoing.kind = ENVOY_EVT_DISPATCHED;
            outgoing.mission = incoming.mission;
            outgoing.success = launched ? 1 : 0;
            strncpy(outgoing.realm, incoming.realm, sizeof(outgoing.realm) - 1);
            strncpy(outgoing.arg, incoming.arg, sizeof(outgoing.arg) - 1);
            if (!envoy_pipe_write_full(write_fd, &outgoing, sizeof(outgoing))) {
                break;
            }

            if (!launched || incoming.mission == ENVOY_MISSION_PLEDGE_RESPOND) {
                outgoing.kind = ENVOY_EVT_COMPLETED;
                if (!envoy_pipe_write_full(write_fd, &outgoing, sizeof(outgoing))) {
                    break;
                }
                continue;
            }

            for (;;) {
                rc = envoy_pipe_read_full_blocking(read_fd, &incoming, sizeof(incoming));
                if (rc <= 0) {
                    close(read_fd);
                    close(write_fd);
                    _exit(EXIT_FAILURE);
                }

                if (incoming.kind == ENVOY_IPC_CMD_SHUTDOWN) {
                    close(read_fd);
                    close(write_fd);
                    _exit(EXIT_SUCCESS);
                }

                if (incoming.kind == ENVOY_IPC_CMD_COMPLETE) {
                    memset(&outgoing, 0, sizeof(outgoing));
                    outgoing.kind = ENVOY_EVT_COMPLETED;
                    outgoing.mission = incoming.mission;
                    outgoing.success = incoming.success;
                    strncpy(outgoing.realm, incoming.realm, sizeof(outgoing.realm) - 1);
                    strncpy(outgoing.arg, incoming.arg, sizeof(outgoing.arg) - 1);
                    envoy_pipe_write_full(write_fd, &outgoing, sizeof(outgoing));
                    break;
                }
            }
        }
    }

    close(read_fd);
    close(write_fd);
    _exit(EXIT_SUCCESS);
}

static void envoy_reset_process_state(EnvoyProcess *envoy) {
    if (envoy == NULL) {
        return;
    }

    envoy->busy = false;
    envoy->started = false;
    envoy->launch_reported = false;
    envoy->launch_success = false;
    envoy->launch_handled = false;
    envoy->mission = ENVOY_MISSION_NONE;
    envoy->realm[0] = '\0';
    envoy->arg[0] = '\0';
    envoy->assigned_at = 0;
}

static void envoy_mark_idle(EnvoyProcess *envoy, bool success) {
    if (envoy == NULL) {
        return;
    }

    envoy->busy = false;
    envoy->started = false;
    envoy->last_success = success;
    envoy->assigned_at = 0;
}

static bool envoy_spawn_worker(EnvoyProcess *envoy, EnvoyActionRunner runner, void *runner_context) {
    int parent_to_child[2] = {-1, -1};
    int child_to_parent[2] = {-1, -1};
    pid_t pid = 0;

    if (envoy == NULL) {
        return false;
    }

    if (pipe(parent_to_child) != 0 || pipe(child_to_parent) != 0) {
        if (parent_to_child[0] >= 0) {
            close(parent_to_child[0]);
        }
        if (parent_to_child[1] >= 0) {
            close(parent_to_child[1]);
        }
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
        close(parent_to_child[0]);
        close(parent_to_child[1]);
        close(child_to_parent[0]);
        close(child_to_parent[1]);
        return false;
    }

    if (pid == 0) {
        close(parent_to_child[1]);
        close(child_to_parent[0]);
        envoy_child_loop(parent_to_child[0], child_to_parent[1], runner, runner_context);
    }

    close(parent_to_child[0]);
    close(child_to_parent[1]);

    memset(envoy, 0, sizeof(*envoy));
    envoy->pid = pid;
    envoy->to_child_fd = parent_to_child[1];
    envoy->from_child_fd = child_to_parent[0];
    envoy->alive = true;
    envoy->last_success = true;

    fcntl(envoy->from_child_fd, F_SETFL, O_NONBLOCK);
    return true;
}

bool envoy_manager_init(EnvoyManager *manager, int count, EnvoyActionRunner runner, void *runner_context) {
    int i = 0;

    if (manager == NULL || count < 0) {
        return false;
    }

    envoy_manager_init_empty(manager);
    if (pthread_mutex_init(&manager->lock, NULL) != 0) {
        return false;
    }

    manager->runner = runner;
    manager->runner_context = runner_context;
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
    for (i = 0; i < count; ++i) {
        if (!envoy_spawn_worker(&manager->envoys[i], manager->runner, manager->runner_context)) {
            envoy_manager_shutdown(manager);
            return false;
        }
    }

    return true;
}

static bool envoy_respawn_worker(EnvoyProcess *envoy, EnvoyActionRunner runner, void *runner_context) {
    if (envoy == NULL) {
        return false;
    }

    if (envoy->to_child_fd >= 0) {
        close(envoy->to_child_fd);
    }
    if (envoy->from_child_fd >= 0) {
        close(envoy->from_child_fd);
    }

    return envoy_spawn_worker(envoy, runner, runner_context);
}

void envoy_manager_shutdown(EnvoyManager *manager) {
    int i = 0;

    if (manager == NULL || !manager->initialized) {
        return;
    }

    pthread_mutex_lock(&manager->lock);
    for (i = 0; i < manager->count; ++i) {
        EnvoyIpcMessage message;
        EnvoyProcess *envoy = &manager->envoys[i];

        if (!envoy->alive) {
            continue;
        }

        memset(&message, 0, sizeof(message));
        message.kind = ENVOY_IPC_CMD_SHUTDOWN;
        envoy_pipe_write_full(envoy->to_child_fd, &message, sizeof(message));
    }
    pthread_mutex_unlock(&manager->lock);

    for (i = 0; i < manager->count; ++i) {
        EnvoyProcess *envoy = &manager->envoys[i];

        if (!envoy->alive) {
            continue;
        }

        waitpid(envoy->pid, NULL, 0);
        if (envoy->to_child_fd >= 0) {
            close(envoy->to_child_fd);
        }
        if (envoy->from_child_fd >= 0) {
            close(envoy->from_child_fd);
        }
        envoy->alive = false;
    }

    free(manager->envoys);
    manager->envoys = NULL;
    manager->count = 0;
    manager->initialized = false;
    pthread_mutex_destroy(&manager->lock);
}

int envoy_manager_assign(EnvoyManager *manager, EnvoyMissionType mission, const char *realm, const char *arg,
                         EnvoyActionRunner runner, void *runner_context) {
    int i = 0;

    (void) runner;
    (void) runner_context;

    if (manager == NULL || !manager->initialized || mission == ENVOY_MISSION_NONE) {
        return -1;
    }

    pthread_mutex_lock(&manager->lock);
    for (i = 0; i < manager->count; ++i) {
        EnvoyIpcMessage message;
        EnvoyProcess *envoy = &manager->envoys[i];
        const char *safe_realm = realm != NULL ? realm : "";
        const char *safe_arg = arg != NULL ? arg : "";

        if (!envoy->alive) {
            if (!envoy_respawn_worker(envoy, manager->runner, manager->runner_context)) {
                continue;
            }
        }

        if (envoy->busy) {
            continue;
        }

        memset(&message, 0, sizeof(message));
        message.kind = ENVOY_IPC_CMD_RUN;
        message.mission = (uint8_t) mission;
        strncpy(message.realm, safe_realm, sizeof(message.realm) - 1);
        strncpy(message.arg, safe_arg, sizeof(message.arg) - 1);
        if (!envoy_pipe_write_full(envoy->to_child_fd, &message, sizeof(message))) {
            envoy->alive = false;
            continue;
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
    EnvoyIpcMessage message;
    EnvoyProcess *envoy = NULL;

    if (manager == NULL || !manager->initialized || envoy_index < 0 || envoy_index >= manager->count) {
        return false;
    }

    pthread_mutex_lock(&manager->lock);
    envoy = &manager->envoys[envoy_index];
    if (!envoy->busy || !envoy->alive) {
        pthread_mutex_unlock(&manager->lock);
        return false;
    }

    memset(&message, 0, sizeof(message));
    message.kind = ENVOY_IPC_CMD_COMPLETE;
    message.mission = (uint8_t) envoy->mission;
    message.success = success ? 1 : 0;
    strncpy(message.realm, envoy->realm, sizeof(message.realm) - 1);
    strncpy(message.arg, envoy->arg, sizeof(message.arg) - 1);
    if (!envoy_pipe_write_full(envoy->to_child_fd, &message, sizeof(message))) {
        envoy->alive = false;
        pthread_mutex_unlock(&manager->lock);
        return false;
    }

    envoy->last_success = success;
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
            ssize_t rc = envoy_pipe_read_full_nonblocking(envoy->from_child_fd, &message, sizeof(message));
            if (rc == -2) {
                break;
            }
            if (rc <= 0) {
                envoy->alive = false;
                envoy->busy = false;
                break;
            }

            if (message.kind == ENVOY_EVT_DISPATCHED) {
                envoy->started = true;
                envoy->launch_reported = true;
                envoy->launch_success = message.success != 0;
                envoy->launch_handled = false;
            } else if (message.kind == ENVOY_EVT_COMPLETED) {
                envoy_mark_idle(envoy, message.success != 0);
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

        if (envoy->to_child_fd >= 0) {
            close(envoy->to_child_fd);
            envoy->to_child_fd = -1;
        }
        if (envoy->from_child_fd >= 0) {
            close(envoy->from_child_fd);
            envoy->from_child_fd = -1;
        }
        envoy->alive = false;
        envoy->pid = 0;
        envoy_reset_process_state(envoy);
    }
    pthread_mutex_unlock(&manager->lock);
}

bool envoy_manager_consume_launch_result(EnvoyManager *manager, int *envoy_index_out, EnvoyMissionType *mission_out,
                                         char *realm_out, size_t realm_out_size, char *arg_out, size_t arg_out_size,
                                         bool *success_out) {
    int i = 0;

    if (manager == NULL || !manager->initialized || envoy_index_out == NULL || mission_out == NULL ||
        realm_out == NULL || arg_out == NULL || success_out == NULL || realm_out_size == 0 || arg_out_size == 0) {
        return false;
    }

    pthread_mutex_lock(&manager->lock);
    for (i = 0; i < manager->count; ++i) {
        EnvoyProcess *envoy = &manager->envoys[i];
        if (!envoy->launch_reported || envoy->launch_handled) {
            continue;
        }

        envoy->launch_handled = true;
        *envoy_index_out = i;
        *mission_out = envoy->mission;
        *success_out = envoy->launch_success;
        strncpy(realm_out, envoy->realm, realm_out_size - 1);
        realm_out[realm_out_size - 1] = '\0';
        strncpy(arg_out, envoy->arg, arg_out_size - 1);
        arg_out[arg_out_size - 1] = '\0';
        pthread_mutex_unlock(&manager->lock);
        return true;
    }
    pthread_mutex_unlock(&manager->lock);
    return false;
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

        if (!envoy->alive) {
            if (asprintf(&line, "- Envoy %d: OFFLINE\n", i + 1) >= 0 && line != NULL) {
                utils_print(line);
                free(line);
            }
            continue;
        }

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

        if (asprintf(&line, "- Envoy %d: ON MISSION (%s to %s)\n", i + 1,
                     envoy_mission_text(envoy->mission), display_realm) >= 0 && line != NULL) {
            utils_print(line);
            free(line);
        }
    }
    pthread_mutex_unlock(&manager->lock);
}
