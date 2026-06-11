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
<<<<<<< HEAD
=======
    char *program_path;
    char *config_path;
    char *stock_path;
>>>>>>> 795777dd2388f27e94bd3af97a531f1a701d767c
    EnvoyManager envoys;
} MaesterContext;

extern volatile sig_atomic_t g_stop_requested;
extern volatile sig_atomic_t g_sigchld_pending;

void maester_context_init(MaesterContext *context);
void maester_context_destroy(MaesterContext *context);

#endif
