#ifndef TERMINAL_H
#define TERMINAL_H

#include "../realm/maester.h"
#include "../utils/system.h"

void terminal_run(MaesterContext *context, volatile sig_atomic_t *stop_requested);

#endif
