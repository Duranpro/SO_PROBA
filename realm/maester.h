#ifndef MAESTER_H
#define MAESTER_H

#include "../config/config.h"
#include "../network/network.h"
#include "../stock/stock.h"
#include "../utils/system.h"

typedef struct {
    CitadelConfig config;
    Stock stock;
    NetworkContext network;
} MaesterContext;

void maester_context_init(MaesterContext *context);
void maester_context_destroy(MaesterContext *context);

#endif
