#include "config.h"

#include "../utils/utils.h"

static void config_free_route(RouteInfo *route) {
    if (route == NULL) {
        return;
    }

    free(route->realm_name);
    free(route->ip);
    route->realm_name = NULL;
    route->ip = NULL;
    route->port = 0;
}

void config_init(CitadelConfig *config) {
    if (config == NULL) {
        return;
    }

    config->realm_name = NULL;
    config->workdir = NULL;
    config->envoy_count = 0;
    config->ip = NULL;
    config->port = 0;
    config->routes = NULL;
    config->route_count = 0;
}

static bool config_append_route(CitadelConfig *config, const char *line) {
    char *copy = utils_strdup_safe(line);
    char *tokens[3] = {0};
    size_t count = 0;
    RouteInfo *new_routes = NULL;
    RouteInfo *route = NULL;

    if (copy == NULL) {
        return false;
    }

    utils_trim(copy);
    count = utils_tokenize(copy, tokens, 3);
    if (count != 3) {
        free(copy);
        return false;
    }

    new_routes = (RouteInfo *) realloc(config->routes, sizeof(RouteInfo) * (config->route_count + 1));
    if (new_routes == NULL) {
        free(copy);
        return false;
    }

    config->routes = new_routes;
    route = &config->routes[config->route_count];
    route->realm_name = utils_sanitize_realm_name(tokens[0]);
    route->ip = utils_strdup_safe(tokens[1]);
    route->port = 0;

    if (route->realm_name == NULL || route->ip == NULL || !utils_parse_int(tokens[2], &route->port)) {
        config_free_route(route);
        free(copy);
        return false;
    }

    config->route_count++;
    free(copy);
    return true;
}

bool config_load(const char *path, CitadelConfig *config) {
    char *buffer = NULL;
    char *saveptr = NULL;
    char *line = NULL;
    int field_index = 0;
    bool routes_started = false;

    if (path == NULL || config == NULL) {
        return false;
    }

    buffer = utils_read_file(path, NULL);
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
                    config->realm_name = utils_sanitize_realm_name(line);
                    break;
                case 1:
                    config->workdir = utils_strdup_safe(line);
                    break;
                case 2:
                    if (!utils_parse_int(line, &config->envoy_count)) {
                        free(buffer);
                        return false;
                    }
                    break;
                case 3:
                    config->ip = utils_strdup_safe(line);
                    break;
                case 4:
                    if (!utils_parse_int(line, &config->port)) {
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

    if (field_index < 5 || !routes_started || config->realm_name == NULL || config->workdir == NULL ||
        config->ip == NULL) {
        return false;
    }

    return true;
}

void config_free(CitadelConfig *config) {
    size_t i = 0;

    if (config == NULL) {
        return;
    }

    free(config->realm_name);
    free(config->workdir);
    free(config->ip);

    for (i = 0; i < config->route_count; ++i) {
        config_free_route(&config->routes[i]);
    }

    free(config->routes);
    config_init(config);
}

const RouteInfo *config_find_route(const CitadelConfig *config, const char *realm_name) {
    size_t i = 0;

    if (config == NULL || realm_name == NULL) {
        return NULL;
    }

    for (i = 0; i < config->route_count; ++i) {
        if (utils_equals_ignore_case(config->routes[i].realm_name, realm_name)) {
            return &config->routes[i];
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

    for (i = 0; i < config->route_count; ++i) {
        char *line = NULL;
        int written = 0;

        if (utils_equals_ignore_case(config->routes[i].realm_name, "DEFAULT")) {
            continue;
        }

        written = asprintf(&line, "- %s\n", config->routes[i].realm_name);
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
