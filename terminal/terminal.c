#include "terminal.h"

#include "../utils/utils.h"
#include "commands.h"

static char *terminal_read_line_with_background(MaesterContext *context, volatile sig_atomic_t *stop_requested) {
#ifdef _WIN32
    (void) context;
    (void) stop_requested;
    return utils_read_line_fd(STDIN_FILENO);
#else
    size_t capacity = 128;
    size_t length = 0;
    char *buffer = (char *) malloc(capacity);

    if (buffer == NULL) {
        return NULL;
    }

    while (stop_requested == NULL || *stop_requested == 0) {
        fd_set readfds;
        struct timeval timeout;
        int rc = 0;

        FD_ZERO(&readfds);
        FD_SET(STDIN_FILENO, &readfds);
        timeout.tv_sec = 0;
        timeout.tv_usec = 200000;

        rc = select(STDIN_FILENO + 1, &readfds, NULL, NULL, &timeout);
        commands_poll_background(context);

        if (rc < 0) {
            if (errno == EINTR) {
                continue;
            }
            free(buffer);
            return NULL;
        }
        if (rc == 0 || !FD_ISSET(STDIN_FILENO, &readfds)) {
            continue;
        }

        while (true) {
            char ch = '\0';
            ssize_t bytes = read(STDIN_FILENO, &ch, 1);

            if (bytes == 0) {
                if (length == 0) {
                    free(buffer);
                    return NULL;
                }
                buffer[length] = '\0';
                return buffer;
            }
            if (bytes < 0) {
                if (errno == EINTR) {
                    continue;
                }
                free(buffer);
                return NULL;
            }

            if (ch == '\n') {
                buffer[length] = '\0';
                if (length > 0 && buffer[length - 1] == '\r') {
                    buffer[length - 1] = '\0';
                }
                return buffer;
            }

            if (length + 1 >= capacity) {
                size_t new_capacity = capacity * 2;
                char *new_buffer = (char *) realloc(buffer, new_capacity);
                if (new_buffer == NULL) {
                    free(buffer);
                    return NULL;
                }
                buffer = new_buffer;
                capacity = new_capacity;
            }

            buffer[length++] = ch;

            if (length >= CITADEL_MAX_INPUT) {
                buffer[length] = '\0';
                return buffer;
            }
        }
    }

    free(buffer);
    return NULL;
#endif
}

void terminal_run(MaesterContext *context, volatile sig_atomic_t *stop_requested) {
    bool keep_running = true;

    while (keep_running && (stop_requested == NULL || *stop_requested == 0)) {
        char *line = NULL;

        utils_print("$ ");
        line = terminal_read_line_with_background(context, stop_requested);
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
