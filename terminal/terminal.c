#include "terminal.h"

#include "../utils/utils.h"
#include "commands.h"

void terminal_run(MaesterContext *context, volatile sig_atomic_t *stop_requested) {
    bool keep_running = true;

    while (keep_running && (stop_requested == NULL || *stop_requested == 0)) {
        char *line = NULL;

        utils_print("$ ");
        line = utils_read_line_fd(STDIN_FILENO);
        if (line == NULL) {
            if (stop_requested != NULL && *stop_requested != 0) {
                break;
            }
            utils_println("");
            break;
        }

        utils_trim(line);
        if (*line == '\0') {
            free(line);
            continue;
        }

        keep_running = commands_dispatch(context, line);
        free(line);
    }

    if (stop_requested != NULL && *stop_requested != 0) {
        utils_println("\nClosing Maester cleanly after SIGINT.");
    }
}
