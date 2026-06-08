#ifndef MAESTER_H
#define MAESTER_H

#include "../config/config.h"
#include "../envoy/envoy.h"
#include "../network/network.h"
#include "../stock/stock.h"
#include "../utils/system.h"

typedef struct MaesterContext {
    CitadelConfig config;
    Stock stock;
    NetworkContext network;
    char *program_path;
    char *config_path;
    char *stock_path;
    EnvoyManager envoys;
} MaesterContext;

extern volatile sig_atomic_t g_stop_requested;
extern volatile sig_atomic_t g_sigchld_pending;

void maester_context_init(MaesterContext *context);
void maester_context_destroy(MaesterContext *context);

#endif
