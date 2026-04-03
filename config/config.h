#ifndef CONFIG_H
#define CONFIG_H

#include "../utils/system.h"

typedef struct {
    char *realm_name;
    char *ip;
    int port;
} RouteInfo;

typedef struct {
    char *realm_name;
    char *workdir;
    int envoy_count;
    char *ip;
    int port;
    RouteInfo *routes;
    size_t route_count;
} CitadelConfig;

void config_init(CitadelConfig *config);
bool config_load(const char *path, CitadelConfig *config);
void config_free(CitadelConfig *config);
const RouteInfo *config_find_route(const CitadelConfig *config, const char *realm_name);
void config_print_realms(const CitadelConfig *config);

#endif
