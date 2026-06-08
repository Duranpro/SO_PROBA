#include "terminal.h"

#include <errno.h>

#include "../envoy/envoy.h"
#include "../utils/utils.h"
#include "commands.h"

static void terminal_process_sigchld(MaesterContext *context) {
    if (g_sigchld_pending != 0) {
        g_sigchld_pending = 0;
        envoy_reap_finished(context);
    }
}

void terminal_run(MaesterContext *context, volatile sig_atomic_t *stop_requested) {
    bool keep_running = true;

    while (keep_running && (stop_requested == NULL || *stop_requested == 0)) {
        char *line = NULL;

        terminal_process_sigchld(context);
        utils_print("$ ");
        line = utils_read_line_fd(STDIN_FILENO);
        if (line == NULL) {
            if (stop_requested != NULL && *stop_requested != 0) {
                break;
            }
            if (errno == EINTR) {
                terminal_process_sigchld(context);
                continue;
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
