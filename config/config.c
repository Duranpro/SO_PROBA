#include "config.h"

#include "../utils/utils.h"

static void config_free_route(RouteInfo *route) {
    if (route == NULL) {
        return;
    }

    free(route->nom_regne);
    free(route->ip_regne);
    route->nom_regne = NULL;
    route->ip_regne = NULL;
    route->port_regne = 0;
}

void config_init(CitadelConfig *config) {
    if (config == NULL) {
        return;
    }

    config->nom_regne = NULL;
    config->directori_carpeta = NULL;
    config->num_envoys = 0;
    config->ip_regne = NULL;
    config->port_regne = 0;
    config->rutes = NULL;
    config->num_rutes = 0;
}

static bool config_append_route(CitadelConfig *config, const char *line) {
    char *copy = utils_strdup_safe(line);
    char *tokens[3] = {0};
    size_t num_items = 0;
    RouteInfo *new_routes = NULL;
    RouteInfo *route = NULL;

    if (copy == NULL) {
        return false;
    }

    utils_trim(copy);
    num_items = utils_tokenize(copy, tokens, 3);
    if (num_items != 3) {
        free(copy);
        return false;
    }

    new_routes = (RouteInfo *) realloc(config->rutes, sizeof(RouteInfo) * (config->num_rutes + 1));
    if (new_routes == NULL) {
        free(copy);
        return false;
    }

    config->rutes = new_routes;
    route = &config->rutes[config->num_rutes];
    route->nom_regne = utils_sanitize_realm_name(tokens[0]);
    route->ip_regne = utils_strdup_safe(tokens[1]);
    route->port_regne = 0;

    if (route->nom_regne == NULL || route->ip_regne == NULL || !utils_parse_int(tokens[2], &route->port_regne)) {
        config_free_route(route);
        free(copy);
        return false;
    }

    config->num_rutes++;
    free(copy);
    return true;
}

bool config_load(const char *ruta, CitadelConfig *config) {
    char *buffer = NULL;
    char *saveptr = NULL;
    char *line = NULL;
    int field_index = 0;
    bool routes_started = false;

    if (ruta == NULL || config == NULL) {
        return false;
    }

    buffer = utils_read_file(ruta, NULL);
    if (buffer == NULL) {
        return false;
    }

    for (line = strtok_r(buffer, "\n", &saveptr); line != NULL; line = strtok_r(NULL, "\n", &saveptr)) {
        utils_trim(line);
        if (*line == '\0') {
            continue;
        }

        if (!routes_started) {
            if (strcmp(line, "--- ROUTES ---") == 0) {
                routes_started = true;
                continue;
            }

            switch (field_index) {
                case 0:
                    config->nom_regne = utils_sanitize_realm_name(line);
                    break;
                case 1:
                    config->directori_carpeta = utils_strdup_safe(line);
                    break;
                case 2:
                    if (!utils_parse_int(line, &config->num_envoys)) {
                        free(buffer);
                        return false;
                    }
                    break;
                case 3:
                    config->ip_regne = utils_strdup_safe(line);
                    break;
                case 4:
                    if (!utils_parse_int(line, &config->port_regne)) {
                        free(buffer);
                        return false;
                    }
                    break;
                default:
                    free(buffer);
                    return false;
            }

            field_index++;
            continue;
        }

        if (!config_append_route(config, line)) {
            free(buffer);
            return false;
        }
    }

    free(buffer);

    if (field_index < 5 || !routes_started || config->nom_regne == NULL || config->directori_carpeta == NULL || config->ip_regne == NULL) {
        return false;
    }

    return true;
}

void config_free(CitadelConfig *config) {
    size_t i = 0;

    if (config == NULL) {
        return;
    }

    free(config->nom_regne);
    free(config->directori_carpeta);
    free(config->ip_regne);

    for (i = 0; i < config->num_rutes; ++i) {
        config_free_route(&config->rutes[i]);
    }

    free(config->rutes);
    config_init(config);
}

const RouteInfo *config_find_route(const CitadelConfig *config, const char *nom_regne) {
    size_t i = 0;

    if (config == NULL || nom_regne == NULL) {
        return NULL;
    }

    for (i = 0; i < config->num_rutes; ++i) {
        if (utils_equals_ignore_case(config->rutes[i].nom_regne, nom_regne)) {
            return &config->rutes[i];
        }
    }

    return NULL;
}

void config_print_realms(const CitadelConfig *config) {
    size_t i = 0;
    bool any = false;

    if (config == NULL) {
        return;
    }

    for (i = 0; i < config->num_rutes; ++i) {
        char *line = NULL;
        int written = 0;

        if (utils_equals_ignore_case(config->rutes[i].nom_regne, "DEFAULT")) {
            continue;
        }

        written = asprintf(&line, "- %s\n", config->rutes[i].nom_regne);
        if (written >= 0 && line != NULL) {
            utils_print(line);
            free(line);
        }
        any = true;
    }

    if (!any) {
        utils_println("No realms configured.");
    }
}
