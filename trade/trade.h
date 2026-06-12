#ifndef TRADE_H
#define TRADE_H

#include "../utils/system.h"

struct MaesterContext;

bool trade_run_local(struct MaesterContext *context, const char *regne_desti);

#endif
