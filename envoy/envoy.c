#include "envoy.h"

#include "../realm/maester.h"
#include "../utils/utils.h"

static ssize_t envoy_read_exact_local(int fd, void *buffer, size_t size) {
    unsigned char *cursor = (unsigned char *) buffer;
    size_t total = 0;

    while (total < size) {
        ssize_t bytes = read(fd, cursor + total, size - total);
        if (bytes == 0) {
            if (total == 0) {
                return 0;
            }

            return -1;
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

static const char *envoy_mission_text(EnvoyMissionType mission_type) {
    switch (mission_type) {
        case ENVOY_MISSION_PLEDGE:
            return "PLEDGE";
        case ENVOY_MISSION_PRODUCTS:
            return "PRODUCTS";
        case ENVOY_MISSION_TRADE:
            return "TRADE";
        case ENVOY_MISSION_PLEDGE_RESPONSE:
            return "PLEDGE_RESPONSE";
        case ENVOY_MISSION_NONE:
        default:
            return "NONE";
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
        case ENVOY_MISSION_PLEDGE_RESPONSE:
            return "pledge-response";
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

    free(slot->regne_desti);
    free(slot->ruta_fitxer);

    slot->pid = 0;
    slot->read_fd = -1;
    slot->estat = ENVOY_SLOT_FREE;
    slot->tipus_missio = ENVOY_MISSION_NONE;
    slot->resposta_acceptada = false;
    slot->regne_desti = NULL;
    slot->ruta_fitxer = NULL;
    slot->iniciat_a = 0;
}

static EnvoySlot *envoy_find_slot_by_pid_locked(EnvoyManager *manager, pid_t pid) {
    int i = 0;

    if (manager == NULL || manager->slots == NULL || pid <= 0) {
        return NULL;
    }

    for (i = 0; i < manager->num_envoys; ++i) {
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

    if (header->mida_payload > 0) {
        if (payload == NULL) {
            return false;
        }
        if (utils_write_all(fd, payload, header->mida_payload) != (ssize_t) header->mida_payload) {
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

    if (header->mida_payload == 0) {
        return true;
    }

    payload = (char *) malloc((size_t) header->mida_payload + 1);
    if (payload == NULL) {
        return false;
    }

    if (envoy_read_exact_local(fd, payload, header->mida_payload) != (ssize_t) header->mida_payload) {
        free(payload);
        return false;
    }

    payload[header->mida_payload] = '\0';
    *payload_out = payload;
    return true;
}

bool envoy_manager_init(EnvoyManager *manager, int num_items) {
    int i = 0;

    if (manager == NULL || num_items < 0) {
        return false;
    }

    manager->slots = NULL;
    manager->num_envoys = -1;

    if (pthread_mutex_init(&manager->mutex, NULL) != 0) {
        return false;
    }

    if (num_items > 0) {
        manager->slots = (EnvoySlot *) calloc((size_t) num_items, sizeof(EnvoySlot));
        if (manager->slots == NULL) {
            pthread_mutex_destroy(&manager->mutex);
            return false;
        }
    }

    manager->num_envoys = num_items;

    for (i = 0; i < num_items; ++i) {
        manager->slots[i].id = i + 1;
        manager->slots[i].pid = 0;
        manager->slots[i].read_fd = -1;
        manager->slots[i].estat = ENVOY_SLOT_FREE;
        manager->slots[i].tipus_missio = ENVOY_MISSION_NONE;
        manager->slots[i].resposta_acceptada = false;
        manager->slots[i].regne_desti = NULL;
        manager->slots[i].ruta_fitxer = NULL;
        manager->slots[i].iniciat_a = 0;
    }

    return true;
}

bool envoy_spawn_pledge_response(struct MaesterContext *context, const char *regne, bool accepted, const char *endpoint_desti, const char *endpoint_estable_peer) {
    EnvoySlot *slot = NULL;
    int i = 0;
    int pipe_fd[2] = {-1, -1};
    char pipe_fd_text[32];
    char envoy_id_text[32];
    const char *response_text = NULL;
    const char *stable_endpoint_arg = NULL;
    pid_t pid = 0;

    if (accepted) {
        response_text = "ACCEPT";
    } else {
        response_text = "REJECT";
    }

    if (endpoint_estable_peer != NULL) {
        stable_endpoint_arg = endpoint_estable_peer;
    } else {
        stable_endpoint_arg = "";
    }

    if (context == NULL || context->program_path == NULL || context->config_path == NULL || context->stock_path == NULL || regne == NULL || endpoint_desti == NULL || endpoint_desti[0] == '\0') {
        return false;
    }

    pthread_mutex_lock(&context->envoys.mutex);
    for (i = 0; i < context->envoys.num_envoys; ++i) {
        if (context->envoys.slots[i].estat == ENVOY_SLOT_FREE) {
            slot = &context->envoys.slots[i];
            break;
        }
    }

    if (slot == NULL) {
        pthread_mutex_unlock(&context->envoys.mutex);
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
            "pledge-response",
            "--realm",
            (char *) regne,
            "--file",
            "",
            "--envoy-id",
            envoy_id_text,
            "--response",
            (char *) response_text,
            "--target-endpoint",
            (char *) endpoint_desti,
            "--peer-stable-endpoint",
            (char *) stable_endpoint_arg,
            NULL
        };
        EnvoyResultHeader header;
        static const char payload[] = "execv failed";

        close(pipe_fd[0]);
        execv(context->program_path, argv_worker);

        memset(&header, 0, sizeof(header));
        header.magic = ENVOY_RESULT_MAGIC;
        header.id_envoy = slot->id;
        header.tipus_missio = ENVOY_MISSION_PLEDGE_RESPONSE;
        header.estat_resultat = ENVOY_RESULT_FAILED;
        strncpy(header.regne, regne, sizeof(header.regne) - 1);
        header.regne[sizeof(header.regne) - 1] = '\0';
        header.mida_payload = (uint32_t) (sizeof(payload) - 1);
        (void) envoy_result_write(pipe_fd[1], &header, payload);
        close(pipe_fd[1]);
        _exit(127);
    }

    close(pipe_fd[1]);
    slot->pid = pid;
    slot->read_fd = pipe_fd[0];
    slot->estat = ENVOY_SLOT_ON_MISSION;
    slot->tipus_missio = ENVOY_MISSION_PLEDGE_RESPONSE;
    slot->resposta_acceptada = accepted;
    slot->regne_desti = utils_strdup_safe(regne);
    slot->ruta_fitxer = NULL;
    slot->iniciat_a = time(NULL);

    if (slot->regne_desti == NULL) {
        close(slot->read_fd);
        slot->read_fd = -1;
        kill(pid, SIGTERM);
        while (waitpid(pid, NULL, 0) < 0 && errno == EINTR) {
        }
        envoy_slot_reset(slot);
        pthread_mutex_unlock(&context->envoys.mutex);
        return false;
    }

    pthread_mutex_unlock(&context->envoys.mutex);
    return true;
}

bool envoy_spawn_mission(struct MaesterContext *context, EnvoyMissionType tipus, const char *regne, const char *ruta_fitxer) {
    EnvoySlot *slot = NULL;
    int i = 0;
    int pipe_fd[2] = {-1, -1};
    char pipe_fd_text[32];
    char envoy_id_text[32];
    char direct_endpoint[128];
    const char *mission_text = NULL;
    const char *file_arg = NULL;
    bool has_direct_endpoint = false;
    pid_t pid = 0;

    if (context == NULL || context->program_path == NULL || context->config_path == NULL || context->stock_path == NULL) {
        return false;
    }

    if ((tipus == ENVOY_MISSION_PLEDGE || tipus == ENVOY_MISSION_PRODUCTS || tipus == ENVOY_MISSION_TRADE) && regne == NULL) {
        return false;
    }

    mission_text = envoy_mission_exec_text(tipus);
    if (ruta_fitxer != NULL) {
        file_arg = ruta_fitxer;
    } else {
        file_arg = "";
    }
    memset(direct_endpoint, 0, sizeof(direct_endpoint));
    if (regne != NULL) {
        has_direct_endpoint = network_get_direct_endpoint_for_realm(&context->network, regne, direct_endpoint, sizeof(direct_endpoint));
    }

    pthread_mutex_lock(&context->envoys.mutex);
    for (i = 0; i < context->envoys.num_envoys; ++i) {
        if (context->envoys.slots[i].estat == ENVOY_SLOT_FREE) {
            slot = &context->envoys.slots[i];
            break;
        }
    }

    if (slot == NULL) {
        pthread_mutex_unlock(&context->envoys.mutex);
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
        char *direct_endpoint_flag = NULL;
        char *direct_endpoint_value = NULL;
        char *valor_regne = NULL;

        if (has_direct_endpoint) {
            direct_endpoint_flag = "--direct-endpoint";
            direct_endpoint_value = direct_endpoint;
        }

        valor_regne = (char *) regne;

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
            valor_regne,
            "--file",
            (char *) file_arg,
            "--envoy-id",
            envoy_id_text,
            direct_endpoint_flag,
            direct_endpoint_value,
            NULL
        };
        EnvoyResultHeader header;
        static const char payload[] = "execv failed";

        close(pipe_fd[0]);
        execv(context->program_path, argv_worker);

        memset(&header, 0, sizeof(header));
        header.magic = ENVOY_RESULT_MAGIC;
        header.id_envoy = slot->id;
        header.tipus_missio = (int) tipus;
        header.estat_resultat = ENVOY_RESULT_FAILED;
        {
            const char *valor_regne = NULL;

            if (regne != NULL) {
                valor_regne = regne;
            } else {
                valor_regne = "";
            }

            strncpy(header.regne, valor_regne, sizeof(header.regne) - 1);
        }
        header.regne[sizeof(header.regne) - 1] = '\0';
        header.endpoint_remot[0] = '\0';
        header.mida_payload = (uint32_t) (sizeof(payload) - 1);
        (void) envoy_result_write(pipe_fd[1], &header, payload);
        close(pipe_fd[1]);
        _exit(127);
    }

    close(pipe_fd[1]);
    slot->pid = pid;
    slot->read_fd = pipe_fd[0];
    slot->estat = ENVOY_SLOT_ON_MISSION;
    slot->tipus_missio = tipus;
    slot->regne_desti = utils_strdup_safe(regne);
    if (ruta_fitxer != NULL) {
        slot->ruta_fitxer = utils_strdup_safe(ruta_fitxer);
    } else {
        slot->ruta_fitxer = NULL;
    }
    slot->iniciat_a = time(NULL);

    if (slot->regne_desti == NULL || (ruta_fitxer != NULL && slot->ruta_fitxer == NULL)) {
        close(slot->read_fd);
        slot->read_fd = -1;
        kill(pid, SIGTERM);
        while (waitpid(pid, NULL, 0) < 0 && errno == EINTR) {
        }
        envoy_slot_reset(slot);
        pthread_mutex_unlock(&context->envoys.mutex);
        return false;
    }

    pthread_mutex_unlock(&context->envoys.mutex);
    return true;
}

void envoy_kill_all(EnvoyManager *manager) {
    int i = 0;

    if (manager == NULL || manager->num_envoys < 0) {
        return;
    }

    pthread_mutex_lock(&manager->mutex);
    for (i = 0; i < manager->num_envoys; ++i) {
        if (manager->slots[i].estat == ENVOY_SLOT_ON_MISSION && manager->slots[i].pid > 0) {
            kill(manager->slots[i].pid, SIGTERM);
        }
    }
    pthread_mutex_unlock(&manager->mutex);

    for (i = 0; i < manager->num_envoys; ++i) {
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

    if (manager == NULL || manager->num_envoys < 0) {
        return;
    }

    envoy_kill_all(manager);

    pthread_mutex_lock(&manager->mutex);
    for (i = 0; i < manager->num_envoys; ++i) {
        envoy_slot_reset(&manager->slots[i]);
    }
    free(manager->slots);
    manager->slots = NULL;
    manager->num_envoys = -1;
    pthread_mutex_unlock(&manager->mutex);

    pthread_mutex_destroy(&manager->mutex);
}

void envoy_print_status(EnvoyManager *manager) {
    int i = 0;

    if (manager == NULL || manager->num_envoys < 0 || manager->num_envoys == 0) {
        utils_println("No Envoys configured.");
        return;
    }

    pthread_mutex_lock(&manager->mutex);
    for (i = 0; i < manager->num_envoys; ++i) {
        EnvoySlot *slot = &manager->slots[i];
        char *line = NULL;

        if (slot->estat == ENVOY_SLOT_FREE) {
            if (asprintf(&line, "- Envoy %d: FREE", slot->id) >= 0 && line != NULL) {
                utils_println(line);
                free(line);
            }
            continue;
        }

        {
            const char *regne_desti = NULL;

            if (slot->regne_desti != NULL) {
                regne_desti = slot->regne_desti;
            } else {
                regne_desti = "?";
            }

            if (asprintf(&line, "- Envoy %d: ON MISSION (%s to %s)", slot->id, envoy_mission_text(slot->tipus_missio), regne_desti) >= 0 && line != NULL) {
                utils_println(line);
                free(line);
            }
        }
    }
    pthread_mutex_unlock(&manager->mutex);
}

void envoy_reap_finished(struct MaesterContext *context) {
    int status = 0;
    pid_t pid = 0;

    if (context == NULL || context->envoys.num_envoys < 0) {
        return;
    }

    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        EnvoySlot *slot = NULL;
        int read_fd = -1;
        EnvoyMissionType mission_type = ENVOY_MISSION_NONE;
        bool response_accepted = false;
        char *realm = NULL;
        char *payload = NULL;
        EnvoyResultHeader header;
        bool read_ok = false;

        pthread_mutex_lock(&context->envoys.mutex);
        slot = envoy_find_slot_by_pid_locked(&context->envoys, pid);
        if (slot != NULL) {
            read_fd = slot->read_fd;
            mission_type = slot->tipus_missio;
            response_accepted = slot->resposta_acceptada;
            {
                const char *regne_desti = NULL;

                if (slot->regne_desti != NULL) {
                    regne_desti = slot->regne_desti;
                } else {
                    regne_desti = "";
                }

                realm = utils_strdup_safe(regne_desti);
            }
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
            if (header.tipus_missio == ENVOY_MISSION_PLEDGE) {
                {
                    const char *result_realm = NULL;

                    if (header.regne[0] != '\0') {
                        result_realm = header.regne;
                    } else if (realm != NULL) {
                        result_realm = realm;
                    } else {
                        result_realm = "";
                    }

                    network_apply_envoy_pledge_result(&context->network, result_realm, (EnvoyResultStatus) header.estat_resultat, header.endpoint_remot);
                }
                if ((EnvoyResultStatus) header.estat_resultat == ENVOY_RESULT_OK) {
                    char *line = NULL;
                    const char *result_realm = NULL;

                    if (header.regne[0] != '\0') {
                        result_realm = header.regne;
                    } else if (realm != NULL) {
                        result_realm = realm;
                    } else {
                        result_realm = "";
                    }

                    if (asprintf(&line, ">>> Alliance with %s forged successfully!", result_realm) >= 0 && line != NULL) {
                        utils_println(line);
                        free(line);
                    }
                } else if ((EnvoyResultStatus) header.estat_resultat == ENVOY_RESULT_REJECTED) {
                    char *line = NULL;
                    const char *result_realm = NULL;

                    if (header.regne[0] != '\0') {
                        result_realm = header.regne;
                    } else if (realm != NULL) {
                        result_realm = realm;
                    } else {
                        result_realm = "";
                    }

                    if (asprintf(&line, ">>> Alliance with %s was refused!", result_realm) >= 0 && line != NULL) {
                        utils_println(line);
                        free(line);
                    }
                } else if ((EnvoyResultStatus) header.estat_resultat == ENVOY_RESULT_TIMEOUT) {
                    char *line = NULL;
                    const char *result_realm = NULL;

                    if (header.regne[0] != '\0') {
                        result_realm = header.regne;
                    } else if (realm != NULL) {
                        result_realm = realm;
                    } else {
                        result_realm = "";
                    }

                    if (asprintf(&line, ">>> Pledge to %s has failed (TIMEOUT).", result_realm) >= 0 && line != NULL) {
                        utils_println(line);
                        free(line);
                    }
                } else {
                    char *line = NULL;
                    const char *result_realm = NULL;

                    if (header.regne[0] != '\0') {
                        result_realm = header.regne;
                    } else if (realm != NULL) {
                        result_realm = realm;
                    } else {
                        result_realm = "";
                    }

                    if (asprintf(&line, ">>> Pledge to %s has failed.", result_realm) >= 0 && line != NULL) {
                        utils_println(line);
                        free(line);
                    }
                }
            } else if (header.tipus_missio == ENVOY_MISSION_PLEDGE_RESPONSE) {
                const char *result_realm = NULL;
                const char *payload_text = NULL;
                const char *alliance_text = NULL;

                if (header.regne[0] != '\0') {
                    result_realm = header.regne;
                } else if (realm != NULL) {
                    result_realm = realm;
                } else {
                    result_realm = "";
                }

                if (payload != NULL) {
                    payload_text = payload;
                } else {
                    payload_text = "";
                }

                network_apply_envoy_pledge_response_result(&context->network, result_realm, response_accepted, (EnvoyResultStatus) header.estat_resultat, payload_text);
                if ((EnvoyResultStatus) header.estat_resultat == ENVOY_RESULT_OK) {
                    char *line = NULL;

                    if (response_accepted) {
                        alliance_text = "established";
                    } else {
                        alliance_text = "rejected";
                    }

                    if (asprintf(&line, "Alliance with %s %s.", result_realm, alliance_text) >= 0 && line != NULL) {
                        utils_println(line);
                        free(line);
                    }
                } else {
                    char *line = NULL;
                    if (asprintf(&line, "Alliance with %s failed.", result_realm) >= 0 && line != NULL) {
                        utils_println(line);
                        free(line);
                    }
                }
            } else if (header.tipus_missio == ENVOY_MISSION_PRODUCTS) {
                const char *result_realm = NULL;
                const char *payload_text = NULL;

                if (header.regne[0] != '\0') {
                    result_realm = header.regne;
                } else if (realm != NULL) {
                    result_realm = realm;
                } else {
                    result_realm = "";
                }

                if ((EnvoyResultStatus) header.estat_resultat == ENVOY_RESULT_OK) {
                    if (payload != NULL) {
                        payload_text = payload;
                    } else {
                        payload_text = "";
                    }

                    network_apply_envoy_products_result(&context->network, result_realm, payload_text);
                } else {
                    (void) result_realm;
                    utils_println("Connection failed.");
                }
            } else if (header.tipus_missio == ENVOY_MISSION_TRADE) {
                const char *result_realm = NULL;
                const char *payload_text = NULL;

                if (header.regne[0] != '\0') {
                    result_realm = header.regne;
                } else if (realm != NULL) {
                    result_realm = realm;
                } else {
                    result_realm = "";
                }

                if ((EnvoyResultStatus) header.estat_resultat == ENVOY_RESULT_OK) {
                    if (payload != NULL) {
                        payload_text = payload;
                    } else {
                        payload_text = "";
                    }

                    if (network_apply_envoy_trade_result(&context->network, &context->stock, context->stock_path, result_realm, (EnvoyResultStatus) header.estat_resultat, payload_text)) {
                        char *line = NULL;
                        if (asprintf(&line, ">>> Order accepted by %s. Stock updated.", result_realm) >= 0 && line != NULL) {
                            utils_println(line);
                            free(line);
                        }
                    } else {
                        char *line = NULL;
                        if (asprintf(&line, ">>> Order accepted by %s.", result_realm) >= 0 && line != NULL) {
                            utils_println(line);
                            free(line);
                        }
                    }
                } else if ((EnvoyResultStatus) header.estat_resultat == ENVOY_RESULT_REJECTED) {
                    char *line = NULL;
                    if (asprintf(&line, ">>> Order rejected by %s.", result_realm) >= 0 && line != NULL) {
                        utils_println(line);
                        free(line);
                    }
                } else if ((EnvoyResultStatus) header.estat_resultat == ENVOY_RESULT_TIMEOUT) {
                    char *line = NULL;
                    if (asprintf(&line, ">>> Trade with %s failed (TIMEOUT).", result_realm) >= 0 && line != NULL) {
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
            }
        } else {
            if (mission_type == ENVOY_MISSION_PLEDGE) {
                const char *result_realm = NULL;

                if (realm != NULL) {
                    result_realm = realm;
                } else {
                    result_realm = "";
                }

                network_apply_envoy_pledge_result(&context->network, result_realm, ENVOY_RESULT_FAILED, NULL);
                char *line = NULL;
                if (asprintf(&line, ">>> Pledge to %s has failed.", result_realm) >= 0 && line != NULL) {
                    utils_println(line);
                    free(line);
                }
            } else if (mission_type == ENVOY_MISSION_PLEDGE_RESPONSE) {
                const char *result_realm = NULL;

                if (realm != NULL) {
                    result_realm = realm;
                } else {
                    result_realm = "";
                }

                network_apply_envoy_pledge_response_result(&context->network, result_realm, response_accepted, ENVOY_RESULT_FAILED, "");
                char *line = NULL;
                if (asprintf(&line, "Alliance with %s failed.", result_realm) >= 0 && line != NULL) {
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
            slot->tipus_missio = mission_type;
            envoy_slot_reset(slot);
        }
        pthread_mutex_unlock(&context->envoys.mutex);
    }
}
