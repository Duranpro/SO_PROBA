#include "envoy.h"

<<<<<<< HEAD
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
=======
#include "../realm/maester.h"
#include "../utils/utils.h"

static ssize_t envoy_read_exact_local(int fd, void *buffer, size_t size) {
    unsigned char *cursor = (unsigned char *) buffer;
    size_t total = 0;

    while (total < size) {
        ssize_t bytes = read(fd, cursor + total, size - total);
        if (bytes == 0) {
            return total == 0 ? 0 : -1;
>>>>>>> 795777dd2388f27e94bd3af97a531f1a701d767c
        }
        if (bytes < 0) {
            if (errno == EINTR) {
                continue;
            }
<<<<<<< HEAD
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return -2;
            }
=======
>>>>>>> 795777dd2388f27e94bd3af97a531f1a701d767c
            return -1;
        }
        total += (size_t) bytes;
    }

    return (ssize_t) total;
}

<<<<<<< HEAD
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
=======
static const char *envoy_mission_text(EnvoyMissionType mission_type) {
    switch (mission_type) {
        case ENVOY_MISSION_PLEDGE:
            return "PLEDGE";
        case ENVOY_MISSION_PRODUCTS:
            return "PRODUCTS";
>>>>>>> 795777dd2388f27e94bd3af97a531f1a701d767c
        case ENVOY_MISSION_TRADE:
            return "TRADE";
        case ENVOY_MISSION_NONE:
        default:
<<<<<<< HEAD
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
=======
            return "NONE";
    }
}

static const char *envoy_result_text(EnvoyResultStatus result_status) {
    switch (result_status) {
        case ENVOY_RESULT_OK:
            return "OK";
        case ENVOY_RESULT_REJECTED:
            return "REJECTED";
        case ENVOY_RESULT_TIMEOUT:
            return "TIMEOUT";
        case ENVOY_RESULT_FAILED:
        default:
            return "FAILED";
    }
}

static const char *envoy_mission_exec_text(EnvoyMissionType mission_type) {
    switch (mission_type) {
        case ENVOY_MISSION_PLEDGE:
            return "pledge";
        case ENVOY_MISSION_PRODUCTS:
            return "products";
        case ENVOY_MISSION_TRADE:
            return "trade";
        case ENVOY_MISSION_NONE:
        default:
            return "none";
    }
}

static void envoy_slot_reset(EnvoySlot *slot) {
    if (slot == NULL) {
        return;
    }

    if (slot->read_fd >= 0) {
        close(slot->read_fd);
    }

    free(slot->target_realm);
    free(slot->file_path);

    slot->pid = 0;
    slot->read_fd = -1;
    slot->status = ENVOY_SLOT_FREE;
    slot->mission_type = ENVOY_MISSION_NONE;
    slot->target_realm = NULL;
    slot->file_path = NULL;
    slot->started_at = 0;
}

static EnvoySlot *envoy_find_slot_by_pid_locked(EnvoyManager *manager, pid_t pid) {
    int i = 0;

    if (manager == NULL || manager->slots == NULL || pid <= 0) {
        return NULL;
    }

    for (i = 0; i < manager->count; ++i) {
        if (manager->slots[i].pid == pid) {
            return &manager->slots[i];
        }
    }

    return NULL;
}

bool envoy_result_write(int fd, const EnvoyResultHeader *header, const void *payload) {
    if (fd < 0 || header == NULL) {
        return false;
    }

    if (utils_write_all(fd, header, sizeof(*header)) != (ssize_t) sizeof(*header)) {
        return false;
    }

    if (header->payload_size > 0) {
        if (payload == NULL) {
            return false;
        }
        if (utils_write_all(fd, payload, header->payload_size) != (ssize_t) header->payload_size) {
            return false;
        }
    }

    return true;
}

bool envoy_result_read(int fd, EnvoyResultHeader *header, char **payload_out) {
    char *payload = NULL;

    if (fd < 0 || header == NULL || payload_out == NULL) {
        return false;
    }

    *payload_out = NULL;
    memset(header, 0, sizeof(*header));

    if (envoy_read_exact_local(fd, header, sizeof(*header)) != (ssize_t) sizeof(*header)) {
        return false;
    }

    if (header->magic != ENVOY_RESULT_MAGIC) {
        return false;
    }

    if (header->payload_size == 0) {
        return true;
    }

    payload = (char *) malloc((size_t) header->payload_size + 1);
    if (payload == NULL) {
        return false;
    }

    if (envoy_read_exact_local(fd, payload, header->payload_size) != (ssize_t) header->payload_size) {
        free(payload);
        return false;
    }

    payload[header->payload_size] = '\0';
    *payload_out = payload;
    return true;
}
>>>>>>> 795777dd2388f27e94bd3af97a531f1a701d767c

bool envoy_manager_init(EnvoyManager *manager, int count) {
    int i = 0;

    if (manager == NULL || count < 0) {
        return false;
    }

<<<<<<< HEAD
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
=======
    manager->slots = NULL;
    manager->count = -1;

    if (pthread_mutex_init(&manager->mutex, NULL) != 0) {
        return false;
    }

    if (count > 0) {
        manager->slots = (EnvoySlot *) calloc((size_t) count, sizeof(EnvoySlot));
        if (manager->slots == NULL) {
            pthread_mutex_destroy(&manager->mutex);
            return false;
        }
    }

    manager->count = count;

    for (i = 0; i < count; ++i) {
        manager->slots[i].id = i + 1;
        manager->slots[i].pid = 0;
        manager->slots[i].read_fd = -1;
        manager->slots[i].status = ENVOY_SLOT_FREE;
        manager->slots[i].mission_type = ENVOY_MISSION_NONE;
        manager->slots[i].target_realm = NULL;
        manager->slots[i].file_path = NULL;
        manager->slots[i].started_at = 0;
    }

    return true;
}

bool envoy_spawn_mission(struct MaesterContext *context,
                         EnvoyMissionType type,
                         const char *realm,
                         const char *file_path) {
    EnvoySlot *slot = NULL;
    int i = 0;
    int pipe_fd[2] = {-1, -1};
    char pipe_fd_text[32];
    char envoy_id_text[32];
    const char *mission_text = NULL;
    const char *file_arg = NULL;
    pid_t pid = 0;

    if (context == NULL || context->program_path == NULL || context->config_path == NULL ||
        context->stock_path == NULL) {
        return false;
    }

    if ((type == ENVOY_MISSION_PLEDGE || type == ENVOY_MISSION_PRODUCTS || type == ENVOY_MISSION_TRADE) &&
        realm == NULL) {
        return false;
    }

    mission_text = envoy_mission_exec_text(type);
    file_arg = (file_path != NULL) ? file_path : "";

    pthread_mutex_lock(&context->envoys.mutex);
    for (i = 0; i < context->envoys.count; ++i) {
        if (context->envoys.slots[i].status == ENVOY_SLOT_FREE) {
            slot = &context->envoys.slots[i];
            break;
        }
    }

    if (slot == NULL) {
        pthread_mutex_unlock(&context->envoys.mutex);
        utils_println("No free Envoy available.");
        return false;
    }

    if (pipe(pipe_fd) != 0) {
        pthread_mutex_unlock(&context->envoys.mutex);
        return false;
    }

    snprintf(pipe_fd_text, sizeof(pipe_fd_text), "%d", pipe_fd[1]);
    snprintf(envoy_id_text, sizeof(envoy_id_text), "%d", slot->id);

    pid = fork();
    if (pid < 0) {
        close(pipe_fd[0]);
        close(pipe_fd[1]);
        pthread_mutex_unlock(&context->envoys.mutex);
        return false;
    }

    if (pid == 0) {
        char *const argv_worker[] = {
            context->program_path,
            "--envoy-worker",
            "--pipe-fd",
            pipe_fd_text,
            "--config",
            context->config_path,
            "--stock",
            context->stock_path,
            "--mission",
            (char *) mission_text,
            "--realm",
            (char *) realm,
            "--file",
            (char *) file_arg,
            "--envoy-id",
            envoy_id_text,
            NULL
        };
        EnvoyResultHeader header;
        static const char payload[] = "execv failed";

        close(pipe_fd[0]);
        execv(context->program_path, argv_worker);

        memset(&header, 0, sizeof(header));
        header.magic = ENVOY_RESULT_MAGIC;
        header.envoy_id = slot->id;
        header.mission_type = (int) type;
        header.result_status = ENVOY_RESULT_FAILED;
        strncpy(header.realm, realm != NULL ? realm : "", sizeof(header.realm) - 1);
        header.realm[sizeof(header.realm) - 1] = '\0';
        header.remote_endpoint[0] = '\0';
        header.payload_size = (uint32_t) (sizeof(payload) - 1);
        (void) envoy_result_write(pipe_fd[1], &header, payload);
        close(pipe_fd[1]);
        _exit(127);
    }

    close(pipe_fd[1]);
    slot->pid = pid;
    slot->read_fd = pipe_fd[0];
    slot->status = ENVOY_SLOT_ON_MISSION;
    slot->mission_type = type;
    slot->target_realm = utils_strdup_safe(realm);
    slot->file_path = file_path != NULL ? utils_strdup_safe(file_path) : NULL;
    slot->started_at = time(NULL);

    if (slot->target_realm == NULL || (file_path != NULL && slot->file_path == NULL)) {
        close(slot->read_fd);
        slot->read_fd = -1;
        kill(pid, SIGTERM);
        while (waitpid(pid, NULL, 0) < 0 && errno == EINTR) {
        }
        envoy_slot_reset(slot);
        pthread_mutex_unlock(&context->envoys.mutex);
        return false;
    }

    {
        char *line = NULL;
        if (asprintf(&line, "Envoy %d launched with pid %ld", slot->id, (long) pid) >= 0 && line != NULL) {
            utils_println(line);
            free(line);
        }
    }

    pthread_mutex_unlock(&context->envoys.mutex);
    return true;
}

void envoy_kill_all(EnvoyManager *manager) {
    int i = 0;

    if (manager == NULL || manager->count < 0) {
        return;
    }

    pthread_mutex_lock(&manager->mutex);
    for (i = 0; i < manager->count; ++i) {
        if (manager->slots[i].status == ENVOY_SLOT_ON_MISSION && manager->slots[i].pid > 0) {
            kill(manager->slots[i].pid, SIGTERM);
        }
    }
    pthread_mutex_unlock(&manager->mutex);

    for (i = 0; i < manager->count; ++i) {
        int attempt = 0;
        int status = 0;
        pid_t pid = 0;
        bool finished = false;

        pthread_mutex_lock(&manager->mutex);
        pid = manager->slots[i].pid;
        pthread_mutex_unlock(&manager->mutex);

        if (pid <= 0) {
            continue;
        }

        for (attempt = 0; attempt < 5; ++attempt) {
            pid_t waited = waitpid(pid, &status, WNOHANG);
            if (waited == pid) {
                finished = true;
                break;
            }
            if (waited < 0 && errno == ECHILD) {
                finished = true;
                break;
            }
            usleep(50000);
        }

        if (!finished) {
            kill(pid, SIGKILL);
            while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
            }
        }

        pthread_mutex_lock(&manager->mutex);
        envoy_slot_reset(&manager->slots[i]);
        pthread_mutex_unlock(&manager->mutex);
    }
}

void envoy_manager_destroy(EnvoyManager *manager) {
    int i = 0;

    if (manager == NULL || manager->count < 0) {
        return;
    }

    envoy_kill_all(manager);

    pthread_mutex_lock(&manager->mutex);
    for (i = 0; i < manager->count; ++i) {
        envoy_slot_reset(&manager->slots[i]);
    }
    free(manager->slots);
    manager->slots = NULL;
    manager->count = -1;
    pthread_mutex_unlock(&manager->mutex);

    pthread_mutex_destroy(&manager->mutex);
}

void envoy_print_status(EnvoyManager *manager) {
    int i = 0;

    if (manager == NULL || manager->count < 0 || manager->count == 0) {
        utils_println("No Envoys configured.");
        return;
    }

    pthread_mutex_lock(&manager->mutex);
    for (i = 0; i < manager->count; ++i) {
        EnvoySlot *slot = &manager->slots[i];
        char *line = NULL;

        if (slot->status == ENVOY_SLOT_FREE) {
            if (asprintf(&line, "Envoy %d: FREE", slot->id) >= 0 && line != NULL) {
                utils_println(line);
                free(line);
            }
            continue;
        }

        if (asprintf(&line, "Envoy %d: ON_MISSION (%s to %s) pid=%ld",
                     slot->id,
                     envoy_mission_text(slot->mission_type),
                     slot->target_realm != NULL ? slot->target_realm : "?",
                     (long) slot->pid) >= 0 && line != NULL) {
            utils_println(line);
            free(line);
        }
    }
    pthread_mutex_unlock(&manager->mutex);
}

void envoy_reap_finished(struct MaesterContext *context) {
    int status = 0;
    pid_t pid = 0;

    if (context == NULL || context->envoys.count < 0) {
        return;
    }

    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        EnvoySlot *slot = NULL;
        int envoy_id = 0;
        int read_fd = -1;
        EnvoyMissionType mission_type = ENVOY_MISSION_NONE;
        char *realm = NULL;
        char *payload = NULL;
        EnvoyResultHeader header;
        bool read_ok = false;

        pthread_mutex_lock(&context->envoys.mutex);
        slot = envoy_find_slot_by_pid_locked(&context->envoys, pid);
        if (slot != NULL) {
            envoy_id = slot->id;
            read_fd = slot->read_fd;
            mission_type = slot->mission_type;
            realm = utils_strdup_safe(slot->target_realm != NULL ? slot->target_realm : "");
        }
        pthread_mutex_unlock(&context->envoys.mutex);

        if (slot == NULL) {
            free(realm);
            continue;
        }

        memset(&header, 0, sizeof(header));
        read_ok = envoy_result_read(read_fd, &header, &payload);
        if (read_fd >= 0) {
            close(read_fd);
        }

        if (read_ok) {
            if (header.mission_type == ENVOY_MISSION_PLEDGE) {
                network_apply_envoy_pledge_result(&context->network,
                                                  header.realm[0] != '\0' ? header.realm : (realm != NULL ? realm : ""),
                                                  (EnvoyResultStatus) header.result_status,
                                                  header.remote_endpoint);
                if ((EnvoyResultStatus) header.result_status == ENVOY_RESULT_OK) {
                    char *line = NULL;
                    if (asprintf(&line, ">>> Alliance with %s forged successfully!",
                                 header.realm[0] != '\0' ? header.realm : (realm != NULL ? realm : "")) >= 0 &&
                        line != NULL) {
                        utils_println(line);
                        free(line);
                    }
                } else if ((EnvoyResultStatus) header.result_status == ENVOY_RESULT_REJECTED) {
                    char *line = NULL;
                    if (asprintf(&line, ">>> Alliance with %s was rejected.",
                                 header.realm[0] != '\0' ? header.realm : (realm != NULL ? realm : "")) >= 0 &&
                        line != NULL) {
                        utils_println(line);
                        free(line);
                    }
                } else if ((EnvoyResultStatus) header.result_status == ENVOY_RESULT_TIMEOUT) {
                    char *line = NULL;
                    if (asprintf(&line, ">>> Pledge to %s has failed (TIMEOUT).",
                                 header.realm[0] != '\0' ? header.realm : (realm != NULL ? realm : "")) >= 0 &&
                        line != NULL) {
                        utils_println(line);
                        free(line);
                    }
                } else {
                    char *line = NULL;
                    if (asprintf(&line, ">>> Pledge to %s has failed.",
                                 header.realm[0] != '\0' ? header.realm : (realm != NULL ? realm : "")) >= 0 &&
                        line != NULL) {
                        utils_println(line);
                        free(line);
                    }
                }
            } else if (header.mission_type == ENVOY_MISSION_PRODUCTS) {
                const char *result_realm = header.realm[0] != '\0' ? header.realm : (realm != NULL ? realm : "");

                if ((EnvoyResultStatus) header.result_status == ENVOY_RESULT_OK) {
                    network_apply_envoy_products_result(&context->network, result_realm, payload != NULL ? payload : "");
                } else if ((EnvoyResultStatus) header.result_status == ENVOY_RESULT_TIMEOUT) {
                    char *line = NULL;
                    if (asprintf(&line, ">>> Could not retrieve products from %s (TIMEOUT).", result_realm) >= 0 &&
                        line != NULL) {
                        utils_println(line);
                        free(line);
                    }
                } else if ((EnvoyResultStatus) header.result_status == ENVOY_RESULT_REJECTED) {
                    char *line = NULL;
                    if (asprintf(&line, ">>> Products request to %s was rejected.", result_realm) >= 0 &&
                        line != NULL) {
                        utils_println(line);
                        free(line);
                    }
                } else {
                    char *line = NULL;
                    if (asprintf(&line, ">>> Could not retrieve products from %s.", result_realm) >= 0 &&
                        line != NULL) {
                        utils_println(line);
                        free(line);
                    }
                }
            } else if (header.mission_type == ENVOY_MISSION_TRADE) {
                const char *result_realm = header.realm[0] != '\0' ? header.realm : (realm != NULL ? realm : "");

                if ((EnvoyResultStatus) header.result_status == ENVOY_RESULT_OK) {
                    if (network_apply_envoy_trade_result(&context->network,
                                                         &context->stock,
                                                         context->stock_path,
                                                         result_realm,
                                                         (EnvoyResultStatus) header.result_status,
                                                         payload != NULL ? payload : "")) {
                        char *line = NULL;
                        if (asprintf(&line, ">>> Order accepted by %s.", result_realm) >= 0 && line != NULL) {
                            utils_println(line);
                            free(line);
                        }
                    } else {
                        char *line = NULL;
                        if (asprintf(&line, ">>> Order accepted by %s, but local stock could not be updated.",
                                     result_realm) >= 0 && line != NULL) {
                            utils_println(line);
                            free(line);
                        }
                    }
                } else if ((EnvoyResultStatus) header.result_status == ENVOY_RESULT_REJECTED) {
                    char *line = NULL;
                    if (payload != NULL && payload[0] != '\0') {
                        if (asprintf(&line, ">>> Order rejected by %s (%s).", result_realm, payload) >= 0 &&
                            line != NULL) {
                            utils_println(line);
                            free(line);
                        }
                    } else if (asprintf(&line, ">>> Order rejected by %s.", result_realm) >= 0 && line != NULL) {
                        utils_println(line);
                        free(line);
                    }
                } else if ((EnvoyResultStatus) header.result_status == ENVOY_RESULT_TIMEOUT) {
                    char *line = NULL;
                    if (asprintf(&line, ">>> Trade with %s failed (TIMEOUT).", result_realm) >= 0 &&
                        line != NULL) {
                        utils_println(line);
                        free(line);
                    }
                } else {
                    char *line = NULL;
                    if (asprintf(&line, ">>> Trade with %s failed.", result_realm) >= 0 && line != NULL) {
                        utils_println(line);
                        free(line);
                    }
                }
            } else {
                char *line = NULL;
                if (asprintf(&line, "Envoy %d finished mission %s to %s: %s",
                             envoy_id,
                             envoy_mission_text((EnvoyMissionType) header.mission_type),
                             header.realm[0] != '\0' ? header.realm : (realm != NULL ? realm : ""),
                             envoy_result_text((EnvoyResultStatus) header.result_status)) >= 0 &&
                    line != NULL) {
                    utils_println(line);
                    free(line);
                }
                if (payload != NULL) {
                    utils_println(payload);
                }
            }
        } else {
            if (mission_type == ENVOY_MISSION_PLEDGE) {
                network_apply_envoy_pledge_result(&context->network,
                                                  realm != NULL ? realm : "",
                                                  ENVOY_RESULT_FAILED,
                                                  NULL);
                {
                    char *line = NULL;
                    if (asprintf(&line, ">>> Pledge to %s has failed.", realm != NULL ? realm : "") >= 0 &&
                        line != NULL) {
                        utils_println(line);
                        free(line);
                    }
                }
            } else {
                char *line = NULL;
                if (asprintf(&line, "Envoy %d finished but no valid result was received.", envoy_id) >= 0 &&
                    line != NULL) {
                    utils_println(line);
                    free(line);
                }
            }
        }

        free(payload);
        free(realm);

        pthread_mutex_lock(&context->envoys.mutex);
        slot = envoy_find_slot_by_pid_locked(&context->envoys, pid);
        if (slot != NULL) {
            slot->read_fd = -1;
            slot->mission_type = mission_type;
            envoy_slot_reset(slot);
        }
        pthread_mutex_unlock(&context->envoys.mutex);
    }
>>>>>>> 795777dd2388f27e94bd3af97a531f1a701d767c
}
