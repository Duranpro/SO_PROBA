#ifndef TRADE_H
#define TRADE_H

#include "../config/config.h"
#include "../envoy/envoy.h"
#include "../network/network.h"
#include "../stock/stock.h"
#include "../utils/system.h"

bool trade_run_local(const CitadelConfig *config, const Stock *stock, NetworkContext *network,
                     EnvoyManager *envoys, const char *target_realm);

#endif
