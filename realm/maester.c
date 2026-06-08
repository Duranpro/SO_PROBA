#include "maester.h"

#include "../envoy/envoy_worker.h"
#include "../terminal/terminal.h"
#include "../utils/utils.h"

volatile sig_atomic_t g_stop_requested = 0;
volatile sig_atomic_t g_sigchld_pending = 0;

typedef struct {
    char *config_path;
    char *stock_path;
} LaunchPaths;

static void maester_handle_signal(int signal_number) {
    if (signal_number == SIGINT) {
        g_stop_requested = 1;
    } else if (signal_number == SIGCHLD) {
        g_sigchld_pending = 1;
    }
}

void maester_context_init(MaesterContext *context) {
    if (context == NULL) {
        return;
    }

    config_init(&context->config);
    stock_init(&context->stock);
    memset(&context->network, 0, sizeof(context->network));
    context->program_path = NULL;
    context->config_path = NULL;
    context->stock_path = NULL;
    context->envoys.slots = NULL;
    context->envoys.count = -1;
}

void maester_context_destroy(MaesterContext *context) {
    if (context == NULL) {
        return;
    }

    stock_save(&context->stock);
    envoy_manager_destroy(&context->envoys);
    network_shutdown(&context->network);
    stock_free(&context->stock);
    config_free(&context->config);
    free(context->program_path);
    free(context->config_path);
    free(context->stock_path);
}

static bool maester_install_signals(void) {
    struct sigaction action;

    memset(&action, 0, sizeof(action));
    sigemptyset(&action.sa_mask);
    action.sa_handler = maester_handle_signal;

    if (sigaction(SIGINT, &action, NULL) != 0) {
        return false;
    }

    if (sigaction(SIGCHLD, &action, NULL) != 0) {
        return false;
    }

    return true;
}

static void maester_print_usage(const char *program_name) {
    char *message = NULL;
    int written = asprintf(&message, "Usage: %s <config.dat> <stock.db>\n", program_name);
    if (written >= 0 && message != NULL) {
        utils_print(message);
        free(message);
    }
}

static void maester_launch_paths_free(LaunchPaths *paths) {
    if (paths == NULL) {
        return;
    }

    free(paths->config_path);
    free(paths->stock_path);
    paths->config_path = NULL;
    paths->stock_path = NULL;
}

static bool maester_file_exists(const char *path) {
    return path != NULL && access(path, F_OK) == 0;
}

static char *maester_find_file_in_data(const char *filename) {
    DIR *data_dir = NULL;
    struct dirent *entry = NULL;
    char *candidate = NULL;

    if (filename == NULL || *filename == '\0') {
        return NULL;
    }

    if (maester_file_exists(filename)) {
        return utils_strdup_safe(filename);
    }

    candidate = utils_build_path("./data", filename);
    if (candidate != NULL && maester_file_exists(candidate)) {
        return candidate;
    }
    free(candidate);
    candidate = NULL;

    data_dir = opendir("./data");
    if (data_dir == NULL) {
        return NULL;
    }

    while ((entry = readdir(data_dir)) != NULL) {
        char *realm_dir = NULL;

        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        realm_dir = utils_build_path("./data", entry->d_name);
        if (realm_dir == NULL) {
            continue;
        }

        candidate = utils_build_path(realm_dir, filename);
        free(realm_dir);
        if (candidate != NULL && maester_file_exists(candidate)) {
            closedir(data_dir);
            return candidate;
        }
        free(candidate);
        candidate = NULL;
    }

    closedir(data_dir);
    return NULL;
}

static bool maester_resolve_explicit_paths(const char *config_arg, const char *stock_arg, LaunchPaths *paths) {
    if (config_arg == NULL || stock_arg == NULL || paths == NULL) {
        return false;
    }

    paths->config_path = maester_find_file_in_data(config_arg);
    paths->stock_path = maester_find_file_in_data(stock_arg);
    return paths->config_path != NULL && paths->stock_path != NULL;
}

static bool maester_prepare_launch_paths(int argc, char **argv, LaunchPaths *paths) {
    if (paths == NULL) {
        return false;
    }

    paths->config_path = NULL;
    paths->stock_path = NULL;

    if (argc == 3) {
        return maester_resolve_explicit_paths(argv[1], argv[2], paths);
    }

    return false;
}

int main(int argc, char **argv) {
    MaesterContext context;
    LaunchPaths paths;

    if (argc >= 2 && strcmp(argv[1], "--envoy-worker") == 0) {
        return envoy_worker_main(argc, argv);
    }

    maester_context_init(&context);
    paths.config_path = NULL;
    paths.stock_path = NULL;

    if (!maester_prepare_launch_paths(argc, argv, &paths)) {
        maester_print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    if (!maester_install_signals()) {
        utils_println("Could not install signal handlers.");
        return EXIT_FAILURE;
    }

    context.program_path = utils_strdup_safe(argv[0]);
    context.config_path = utils_strdup_safe(paths.config_path);
    context.stock_path = utils_strdup_safe(paths.stock_path);
    if (context.program_path == NULL || context.config_path == NULL || context.stock_path == NULL) {
        utils_println("Not enough memory to store launch paths.");
        maester_launch_paths_free(&paths);
        maester_context_destroy(&context);
        return EXIT_FAILURE;
    }

    if (!config_load(paths.config_path, &context.config)) {
        utils_println("Could not load config.dat.");
        maester_launch_paths_free(&paths);
        maester_context_destroy(&context);
        return EXIT_FAILURE;
    }

    if (!stock_load(&context.stock, paths.stock_path)) {
        utils_println("Could not load stock.db.");
        maester_launch_paths_free(&paths);
        maester_context_destroy(&context);
        return EXIT_FAILURE;
    }

    maester_launch_paths_free(&paths);
    if (!envoy_manager_init(&context.envoys, context.config.envoy_count)) {
        utils_println("Could not initialize the Envoy manager.");
        maester_context_destroy(&context);
        return EXIT_FAILURE;
    }

    if (!network_init(&context.network, &context.config, &context.stock)) {
        utils_println("Could not initialize the network.");
        maester_context_destroy(&context);
        return EXIT_FAILURE;
    }

    {
        char *line = NULL;
        if (asprintf(&line, "Maester of %s initialized. The board is set.\n", context.config.realm_name) >= 0 &&
            line != NULL) {
            utils_print(line);
            free(line);
        }
    }

    terminal_run(&context, &g_stop_requested);
    maester_context_destroy(&context);

    return EXIT_SUCCESS;
}
