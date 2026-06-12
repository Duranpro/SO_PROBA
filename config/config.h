#ifndef CONFIG_H
#define CONFIG_H

#include "../utils/system.h"

typedef struct {
    char *nom_regne;
    char *ip_regne;
    int port_regne;
} RouteInfo;

typedef struct {
    char *nom_regne;
    char *directori_carpeta;
    int num_envoys;
    char *ip_regne;
    int port_regne;
    RouteInfo *rutes;
    size_t num_rutes;
} CitadelConfig;

void config_init(CitadelConfig *config);
bool config_load(const char *ruta, CitadelConfig *config);
void config_free(CitadelConfig *config);
const RouteInfo *config_find_route(const CitadelConfig *config, const char *nom_regne);
void config_print_realms(const CitadelConfig *config);

#endif
