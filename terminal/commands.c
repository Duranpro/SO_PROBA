#include "commands.h"

#include "../config/config.h"
#include "../envoy/envoy.h"
#include "../network/network.h"
#include "../stock/stock.h"
#include "../trade/trade.h"
#include "../utils/utils.h"

static void commands_print_incomplete(const char *message) {
    utils_println(message);
}

static bool commands_realm_exists(const CitadelConfig *config, const char *nom_regne) {
    const RouteInfo *route = NULL;

    if (config == NULL || nom_regne == NULL) {
        return false;
    }

    route = config_find_route(config, nom_regne);
    if (route == NULL) {
        return false;
    }

    return !utils_equals_ignore_case(route->nom_regne, "DEFAULT");
}

static void commands_print_trade_authorization_error(const char *nom_regne) {
    char *line = NULL;

    if (nom_regne == NULL) {
        return;
    }

    if (asprintf(&line, "The gates of commerce with %s remain closed; no alliance binds you.", nom_regne) >= 0 && line != NULL) {
        utils_println(line);
        free(line);
    }
}

static bool commands_handle_list(MaesterContext *context, char **tokens, size_t num_items) {
    if (num_items == 1) {
        commands_print_incomplete("LIST needs a target. Try LIST REALMS or LIST PRODUCTS.");
        return true;
    }

    if (utils_equals_ignore_case(tokens[1], "REALMS")) {
        if (num_items == 2) {
            config_print_realms(&context->config);
            return true;
        }
        utils_println("Unknown command");
        return true;
    }

    if (utils_equals_ignore_case(tokens[1], "PRODUCTS")) {
        if (num_items == 2) {
            stock_print_local(&context->stock);
            return true;
        }
        if (num_items == 3) {
            if (!commands_realm_exists(&context->config, tokens[2])) {
                utils_println("Unknown realm. Use LIST REALMS to see the available kingdoms.");
                return true;
            }
            if (!network_can_request_products(&context->network, tokens[2])) {
                commands_print_trade_authorization_error(tokens[2]);
                return true;
            }
            if (!envoy_spawn_mission(context, ENVOY_MISSION_PRODUCTS, tokens[2], NULL)) {
                utils_println("All envoys are occupied. Your command must wait.");
                return true;
            }
            return true;
        }
        utils_println("Unknown command");
        return true;
    }

    utils_println("Unknown command");
    return true;
}

static bool commands_handle_pledge(MaesterContext *context, char **tokens, size_t num_items) {
    if (num_items == 1) {
        commands_print_incomplete("PLEDGE needs more arguments. Use PLEDGE <REALM> <sigil.jpg>, PLEDGE RESPOND <REALM> ACCEPT/REJECT or PLEDGE STATUS.");
        return true;
    }

    if (utils_equals_ignore_case(tokens[1], "STATUS")) {
        if (num_items == 2) {
            network_print_pledge_status(&context->network);
            return true;
        }
        utils_println("Unknown command");
        return true;
    }

    if (utils_equals_ignore_case(tokens[1], "RESPOND")) {
        if (num_items < 4) {
            commands_print_incomplete("PLEDGE RESPOND is incomplete. Use PLEDGE RESPOND <REALM> ACCEPT or PLEDGE RESPOND <REALM> REJECT.");
            return true;
        }
        if (num_items == 4 && (utils_equals_ignore_case(tokens[3], "ACCEPT") || utils_equals_ignore_case(tokens[3], "REJECT"))) {
            bool accepted = utils_equals_ignore_case(tokens[3], "ACCEPT");
            char endpoint_desti[128];
            char endpoint_estable_peer[128];

            memset(endpoint_desti, 0, sizeof(endpoint_desti));
            memset(endpoint_estable_peer, 0, sizeof(endpoint_estable_peer));

            if (!network_prepare_pledge_response_mission(&context->network, tokens[2], accepted, endpoint_desti, sizeof(endpoint_desti), endpoint_estable_peer, sizeof(endpoint_estable_peer))) {
                char *line = NULL;
                if (asprintf(&line, "No pending pledge from %s.", tokens[2]) >= 0 && line != NULL) {
                    utils_println(line);
                    free(line);
                }
                return true;
            }

            if (!envoy_spawn_pledge_response(context, tokens[2], accepted, endpoint_desti, endpoint_estable_peer)) {
                network_revert_pledge_response_mission(&context->network, tokens[2]);
                utils_println("All envoys are occupied. Your command must wait.");
                return true;
            }
            return true;
        }
        utils_println("Unknown command");
        return true;
    }

    if (num_items == 2) {
        commands_print_incomplete("PLEDGE is missing the sigil file. Use PLEDGE <REALM> <sigil.jpg>.");
        return true;
    }

    if (num_items == 3) {
        if (!commands_realm_exists(&context->config, tokens[1])) {
            utils_println("No such realm exists. The pledge is hereby withdrawn.");
            return true;
        }
        if (!network_can_launch_pledge(&context->network, tokens[1])) {
            utils_println("Could not send the pledge request.");
            return true;
        }
        if (!network_mark_pledge_pending(&context->network, tokens[1])) {
            utils_println("Could not send the pledge request.");
            return true;
        }
        if (!envoy_spawn_mission(context, ENVOY_MISSION_PLEDGE, tokens[1], tokens[2])) {
            network_revert_pledge_pending(&context->network, tokens[1]);
            utils_println("All envoys are occupied. Your command must wait.");
            return true;
        }
        char *line = NULL;
        if (asprintf(&line, "Pledge sent to %s.", tokens[1]) >= 0 && line != NULL) {
            utils_println(line);
            free(line);
        }
        return true;
    }

    utils_println("Unknown command");
    return true;
}

static bool commands_handle_start(MaesterContext *context, char **tokens, size_t num_items) {
    if (num_items == 1) {
        commands_print_incomplete("START needs a subcommand. For this phase, use START TRADE <REALM>.");
        return true;
    }

    if (!utils_equals_ignore_case(tokens[1], "TRADE")) {
        utils_println("Unknown command");
        return true;
    }

    if (num_items == 2) {
        commands_print_incomplete("Missing arguments, can't start a trade. Please review the syntax.");
        return true;
    }

    if (num_items == 3) {
        if (!commands_realm_exists(&context->config, tokens[2])) {
            utils_println("Unknown realm. Use LIST REALMS to see the available kingdoms.");
            return true;
        }
        if (!network_has_active_alliance(&context->network, tokens[2])) {
            commands_print_trade_authorization_error(tokens[2]);
            return true;
        }
        if (!network_has_remote_products(&context->network, tokens[2])) {
            utils_println("No products available. Use LIST PRODUCTS\nfirst.");
            return true;
        }
        if (!envoy_manager_has_free_slot(&context->envoys)) {
            utils_println("All envoys are occupied. Your command must wait.");
            return true;
        }
        trade_run_local(context, tokens[2]);
        return true;
    }

    utils_println("Unknown command");
    return true;
}

static bool commands_handle_envoy(MaesterContext *context, char **tokens, size_t num_items) {
    if (num_items == 1) {
        commands_print_incomplete("ENVOY needs a subcommand. Use ENVOY STATUS.");
        return true;
    }

    if (num_items == 2 && utils_equals_ignore_case(tokens[1], "STATUS")) {
        envoy_print_status(&context->envoys);
        return true;
    }

    if (num_items >= 4 && utils_equals_ignore_case(tokens[1], "TEST")) {
        if (utils_equals_ignore_case(tokens[2], "PLEDGE") && num_items == 4) {
            (void) envoy_spawn_mission(context, ENVOY_MISSION_PLEDGE, tokens[3], "stub-sigil");
            return true;
        }

        if (utils_equals_ignore_case(tokens[2], "PRODUCTS") && num_items == 4) {
            (void) envoy_spawn_mission(context, ENVOY_MISSION_PRODUCTS, tokens[3], NULL);
            return true;
        }

        if (utils_equals_ignore_case(tokens[2], "TRADE") && num_items == 5) {
            (void) envoy_spawn_mission(context, ENVOY_MISSION_TRADE, tokens[3], tokens[4]);
            return true;
        }
    }

    utils_println("Unknown command");
    return true;
}

bool commands_dispatch(MaesterContext *context, const char *line) {
    char *copy = NULL;
    char *tokens[CITADEL_MAX_TOKENS] = {0};
    size_t num_items = 0;
    bool keep_running = true;

    if (context == NULL || line == NULL) {
        return true;
    }

    copy = utils_strdup_safe(line);
    if (copy == NULL) {
        utils_println("Not enough memory to process the command.");
        return true;
    }

    num_items = utils_tokenize(copy, tokens, CITADEL_MAX_TOKENS);
    if (num_items == 0) {
        free(copy);
        return true;
    }

    if (utils_equals_ignore_case(tokens[0], "LIST")) {
        keep_running = commands_handle_list(context, tokens, num_items);
    } else if (utils_equals_ignore_case(tokens[0], "PLEDGE")) {
        keep_running = commands_handle_pledge(context, tokens, num_items);
    } else if (utils_equals_ignore_case(tokens[0], "START")) {
        keep_running = commands_handle_start(context, tokens, num_items);
    } else if (utils_equals_ignore_case(tokens[0], "ENVOY")) {
        keep_running = commands_handle_envoy(context, tokens, num_items);
    } else if (utils_equals_ignore_case(tokens[0], "EXIT")) {
        if (num_items == 1) {
            keep_running = false;
        } else {
            utils_println("Unknown command");
        }
    } else {
        utils_println("Unknown command");
    }

    free(copy);
    return keep_running;
}
