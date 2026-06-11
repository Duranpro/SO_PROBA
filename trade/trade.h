#ifndef TRADE_H
#define TRADE_H

<<<<<<< HEAD
#include "../config/config.h"
#include "../envoy/envoy.h"
#include "../network/network.h"
#include "../stock/stock.h"
#include "../utils/system.h"

bool trade_run_local(const CitadelConfig *config, const Stock *stock, NetworkContext *network,
                     EnvoyManager *envoys, const char *target_realm);
=======
#include "../utils/system.h"

struct MaesterContext;

bool trade_run_local(struct MaesterContext *context, const char *target_realm);
>>>>>>> 795777dd2388f27e94bd3af97a531f1a701d767c

#endif
