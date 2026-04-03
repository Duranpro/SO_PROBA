#ifndef COMMANDS_H
#define COMMANDS_H

#include "../realm/maester.h"

bool commands_dispatch(MaesterContext *context, const char *line);

#endif
