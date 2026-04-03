#include "commands.h"

#include "../config/config.h"
#include "../network/network.h"
#include "../stock/stock.h"
#include "../trade/trade.h"
#include "../utils/utils.h"

static void commands_print_incomplete(const char *message) {
    utils_println(message);
}

static bool commands_realm_exists(const CitadelConfig *config, const char *realm_name) {
    const RouteInfo *route = NULL;

    if (config == NULL || realm_name == NULL) {
        return false;
    }

    route = config_find_route(config, realm_name);
    if (route == NULL) {
        return false;
    }

    return !utils_equals_ignore_case(route->realm_name, "DEFAULT");
}

static void commands_print_trade_authorization_error(const char *realm_name) {
    char *line = NULL;

    if (realm_name == NULL) {
        return;
    }

    if (asprintf(&line, "ERROR: You must have an alliance with %s to trade.", realm_name) >= 0 && line != NULL) {
        utils_println(line);
        free(line);
    }
}

static bool commands_handle_list(MaesterContext *context, char **tokens, size_t count) {
    if (count == 1) {
        commands_print_incomplete("LIST needs a target. Try LIST REALMS or LIST PRODUCTS.");
        return true;
    }

    if (utils_equals_ignore_case(tokens[1], "REALMS")) {
        if (count == 2) {
            config_print_realms(&context->config);
            return true;
        }
        utils_println("Unknown command");
        return true;
    }

    if (utils_equals_ignore_case(tokens[1], "PRODUCTS")) {
        if (count == 2) {
            stock_print_local(&context->stock);
            return true;
        }
        if (count == 3) {
            if (!commands_realm_exists(&context->config, tokens[2])) {
                utils_println("Unknown realm. Use LIST REALMS to see the available kingdoms.");
                return true;
            }
            if (!network_has_active_alliance(&context->network, tokens[2])) {
                commands_print_trade_authorization_error(tokens[2]);
                return true;
            }
            if (!network_request_remote_products(&context->network, tokens[2])) {
                utils_println("Could not contact the allied realm.");
            }
            return true;
        }
        utils_println("Unknown command");
        return true;
    }

    utils_println("Unknown command");
    return true;
}

static bool commands_handle_pledge(MaesterContext *context, char **tokens, size_t count) {
    if (count == 1) {
        commands_print_incomplete("PLEDGE needs more arguments. Use PLEDGE <REALM> <sigil.jpg>, PLEDGE RESPOND <REALM> ACCEPT/REJECT or PLEDGE STATUS.");
        return true;
    }

    if (utils_equals_ignore_case(tokens[1], "STATUS")) {
        if (count == 2) {
            network_print_pledge_status(&context->network);
            return true;
        }
        utils_println("Unknown command");
        return true;
    }

    if (utils_equals_ignore_case(tokens[1], "RESPOND")) {
        if (count < 4) {
            commands_print_incomplete("PLEDGE RESPOND is incomplete. Use PLEDGE RESPOND <REALM> ACCEPT or PLEDGE RESPOND <REALM> REJECT.");
            return true;
        }
        if (count == 4 &&
            (utils_equals_ignore_case(tokens[3], "ACCEPT") || utils_equals_ignore_case(tokens[3], "REJECT"))) {
            if (!network_send_pledge_response(&context->network, tokens[2],
                                              utils_equals_ignore_case(tokens[3], "ACCEPT"))) {
                utils_println("There is no pending pledge from that realm.");
            }
            return true;
        }
        utils_println("Unknown command");
        return true;
    }

    if (count == 2) {
        commands_print_incomplete("PLEDGE is missing the sigil file. Use PLEDGE <REALM> <sigil.jpg>.");
        return true;
    }

    if (count == 3) {
        if (!commands_realm_exists(&context->config, tokens[1])) {
            utils_println("Unknown realm. Use LIST REALMS to see the available kingdoms.");
            return true;
        }
        if (!network_send_pledge(&context->network, tokens[1], tokens[2])) {
            utils_println("Could not send the pledge request.");
        }
        return true;
    }

    utils_println("Unknown command");
    return true;
}

static bool commands_handle_start(MaesterContext *context, char **tokens, size_t count) {
    if (count == 1) {
        commands_print_incomplete("START needs a subcommand. For this phase, use START TRADE <REALM>.");
        return true;
    }

    if (!utils_equals_ignore_case(tokens[1], "TRADE")) {
        utils_println("Unknown command");
        return true;
    }

    if (count == 2) {
        commands_print_incomplete("Missing arguments, can't start a trade. Please review the syntax.");
        return true;
    }

    if (count == 3) {
        if (!commands_realm_exists(&context->config, tokens[2])) {
            utils_println("Unknown realm. Use LIST REALMS to see the available kingdoms.");
            return true;
        }
        if (!network_has_active_alliance(&context->network, tokens[2])) {
            commands_print_trade_authorization_error(tokens[2]);
            return true;
        }
        trade_run_local(&context->config, &context->stock, &context->network, tokens[2]);
        return true;
    }

    utils_println("Unknown command");
    return true;
}

static bool commands_handle_envoy(char **tokens, size_t count) {
    if (count == 1) {
        commands_print_incomplete("ENVOY needs a subcommand. Use ENVOY STATUS.");
        return true;
    }

    if (count == 2 && utils_equals_ignore_case(tokens[1], "STATUS")) {
        utils_println("Command OK");
        return true;
    }

    utils_println("Unknown command");
    return true;
}

bool commands_dispatch(MaesterContext *context, const char *line) {
    char *copy = NULL;
    char *tokens[CITADEL_MAX_TOKENS] = {0};
    size_t count = 0;
    bool keep_running = true;

    if (context == NULL || line == NULL) {
        return true;
    }

    copy = utils_strdup_safe(line);
    if (copy == NULL) {
        utils_println("Not enough memory to process the command.");
        return true;
    }

    count = utils_tokenize(copy, tokens, CITADEL_MAX_TOKENS);
    if (count == 0) {
        free(copy);
        return true;
    }

    if (utils_equals_ignore_case(tokens[0], "LIST")) {
        keep_running = commands_handle_list(context, tokens, count);
    } else if (utils_equals_ignore_case(tokens[0], "PLEDGE")) {
        keep_running = commands_handle_pledge(context, tokens, count);
    } else if (utils_equals_ignore_case(tokens[0], "START")) {
        keep_running = commands_handle_start(context, tokens, count);
    } else if (utils_equals_ignore_case(tokens[0], "ENVOY")) {
        keep_running = commands_handle_envoy(tokens, count);
    } else if (utils_equals_ignore_case(tokens[0], "EXIT")) {
        if (count == 1) {
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
