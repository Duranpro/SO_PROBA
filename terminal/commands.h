#ifndef COMMANDS_H
#define COMMANDS_H

#include "../realm/maester.h"

bool commands_dispatch(MaesterContext *context, const char *line);
void commands_poll_background(MaesterContext *context);

#endif
