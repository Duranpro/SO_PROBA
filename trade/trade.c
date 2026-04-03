#include "trade.h"

#include "../utils/utils.h"

typedef struct {
    char *name;
    int amount;
} TradeItem;

typedef struct {
    char *target_realm;
    const CitadelConfig *config;
    const Stock *stock;
    NetworkContext *network;
    Product *available_products;
    size_t available_count;
    TradeItem *items;
    size_t count;
} TradeSession;

static void trade_session_free(TradeSession *session) {
    size_t i = 0;

    if (session == NULL) {
        return;
    }

    for (i = 0; i < session->count; ++i) {
        free(session->items[i].name);
    }

    free(session->items);
    free(session->target_realm);
    stock_free_products(session->available_products, session->available_count);
    session->items = NULL;
    session->target_realm = NULL;
    session->available_products = NULL;
    session->available_count = 0;
    session->count = 0;
}

static bool trade_parse_item_command(char *rest, char **product_name, int *amount) {
    char *last_space = NULL;

    if (rest == NULL || product_name == NULL || amount == NULL) {
        return false;
    }

    utils_trim(rest);
    if (*rest == '\0') {
        return false;
    }

    last_space = strrchr(rest, ' ');
    if (last_space == NULL) {
        return false;
    }

    *last_space = '\0';
    last_space++;
    utils_trim(rest);
    utils_trim(last_space);

    if (*rest == '\0' || *last_space == '\0') {
        return false;
    }

    if (!utils_parse_int(last_space, amount) || *amount <= 0) {
        return false;
    }

    *product_name = rest;
    return true;
}

static bool trade_add_item(TradeSession *session, const char *product_name, int amount) {
    size_t i = 0;
    TradeItem *new_items = NULL;

    if (session->available_count == 0) {
        return false;
    }

    for (i = 0; i < session->available_count; ++i) {
        if (utils_equals_ignore_case(session->available_products[i].name, product_name)) {
            break;
        }
    }

    if (i == session->available_count) {
        return false;
    }

    for (i = 0; i < session->count; ++i) {
        if (utils_equals_ignore_case(session->items[i].name, product_name)) {
            session->items[i].amount += amount;
            return true;
        }
    }

    new_items = (TradeItem *) realloc(session->items, sizeof(TradeItem) * (session->count + 1));
    if (new_items == NULL) {
        return false;
    }

    session->items = new_items;
    session->items[session->count].name = utils_strdup_safe(product_name);
    session->items[session->count].amount = amount;

    if (session->items[session->count].name == NULL) {
        return false;
    }

    session->count++;
    return true;
}

static bool trade_remove_item(TradeSession *session, const char *product_name, int amount) {
    size_t i = 0;

    for (i = 0; i < session->count; ++i) {
        if (utils_equals_ignore_case(session->items[i].name, product_name)) {
            if (session->items[i].amount <= amount) {
                free(session->items[i].name);
                if (i + 1 < session->count) {
                    memmove(&session->items[i], &session->items[i + 1],
                            sizeof(TradeItem) * (session->count - i - 1));
                }
                session->count--;
            } else {
                session->items[i].amount -= amount;
            }
            return true;
        }
    }

    return false;
}

static bool trade_append_text(char **content, const char *suffix) {
    char *new_content = NULL;
    int written = 0;

    if (content == NULL || suffix == NULL) {
        return false;
    }

    if (*content == NULL) {
        *content = utils_strdup_safe(suffix);
        return *content != NULL;
    }

    written = asprintf(&new_content, "%s%s", *content, suffix);
    if (written < 0 || new_content == NULL) {
        return false;
    }

    free(*content);
    *content = new_content;
    return true;
}

static bool trade_write_shopping_list(const TradeSession *session, char **file_path_out,
                                      char **file_name_out, size_t *file_size_out) {
    char *file_name = NULL;
    char *path = NULL;
    char *content = NULL;
    size_t i = 0;
    int written = 0;

    if (session == NULL || session->count == 0) {
        return false;
    }

    if (!utils_ensure_directory(session->config->workdir)) {
        return false;
    }

    written = asprintf(&file_name, "shopping_list_%s.txt", session->target_realm);
    if (written < 0 || file_name == NULL) {
        return false;
    }

    path = utils_build_path(session->config->workdir, file_name);
    free(file_name);
    if (path == NULL) {
        return false;
    }

    written = asprintf(&content, "Requester: %s\nTarget: %s\nItems:\n",
                       session->config->realm_name,
                       session->target_realm);
    if (written < 0 || content == NULL) {
        free(path);
        return false;
    }

    for (i = 0; i < session->count; ++i) {
        char *line = NULL;

        written = asprintf(&line, "- %s x%d\n", session->items[i].name, session->items[i].amount);
        if (written < 0 || line == NULL) {
            free(path);
            free(content);
            return false;
        }

        if (!trade_append_text(&content, line)) {
            free(line);
            free(path);
            free(content);
            return false;
        }

        free(line);
    }

    {
        char *summary = NULL;
        written = asprintf(&summary, "Local stock loaded: %zu products\n", session->stock->count);
        if (written < 0 || summary == NULL) {
            free(path);
            free(content);
            return false;
        }

        if (!trade_append_text(&content, summary)) {
            free(summary);
            free(path);
            free(content);
            return false;
        }

        free(summary);
    }

    if (!utils_write_file(path, content)) {
        free(path);
        free(content);
        return false;
    }

    if (file_path_out != NULL) {
        *file_path_out = utils_strdup_safe(path);
    }

    if (file_name_out != NULL) {
        *file_name_out = utils_strdup_safe(strrchr(path, '/') != NULL ? strrchr(path, '/') + 1 : path);
    }

    if (file_size_out != NULL) {
        *file_size_out = strlen(content);
    }

    free(path);
    free(content);
    return true;
}

static void trade_help(const char *command) {
    char *line = NULL;
    int written = 0;

    written = asprintf(&line,
                       "Incomplete %s command. Use: add <product> <amount>, remove <product> <amount>, send or cancel.",
                       command);
    if (written >= 0 && line != NULL) {
        utils_println(line);
        free(line);
    }
}

bool trade_run_local(const CitadelConfig *config, const Stock *stock, NetworkContext *network, const char *target_realm) {
    TradeSession session;
    bool keep_running = true;
    char *line = NULL;

    memset(&session, 0, sizeof(session));
    session.target_realm = utils_sanitize_realm_name(target_realm);
    session.config = config;
    session.stock = stock;
    session.network = network;

    if (session.target_realm == NULL) {
        return false;
    }

    {
        char *message = NULL;
        int written = asprintf(&message, "Entering trade mode with %s.\n", session.target_realm);
        if (written >= 0 && message != NULL) {
            utils_print(message);
            free(message);
        }
    }

    if (network != NULL &&
        network_get_remote_products_copy(network, session.target_realm,
                                         &session.available_products, &session.available_count)) {
        size_t i = 0;
        char *line2 = utils_strdup_safe("Available products: ");
        if (line2 != NULL) {
            for (i = 0; i < session.available_count; ++i) {
                char *new_line = NULL;
                const char *separator = (i + 1 < session.available_count) ? ", " : ".";
                if (asprintf(&new_line, "%s%s%s", line2, session.available_products[i].name, separator) >= 0 &&
                    new_line != NULL) {
                    free(line2);
                    line2 = new_line;
                }
            }
            utils_println(line2);
            free(line2);
        }
    } else {
        utils_println("No products available. Use LIST PRODUCTS first.");
    }

    while (keep_running) {
        char *copy = NULL;
        char *tokens[CITADEL_MAX_TOKENS] = {0};
        size_t count = 0;

        utils_print("(trade)> ");
        line = utils_read_line_fd(STDIN_FILENO);
        if (line == NULL) {
            break;
        }

        utils_trim(line);
        if (*line == '\0') {
            free(line);
            line = NULL;
            continue;
        }

        copy = utils_strdup_safe(line);
        if (copy == NULL) {
            free(line);
            break;
        }

        count = utils_tokenize(copy, tokens, CITADEL_MAX_TOKENS);
        if (count == 0) {
            free(copy);
            free(line);
            line = NULL;
            continue;
        }

        if (utils_equals_ignore_case(tokens[0], "send")) {
            if (count != 1) {
                utils_println("Unknown command");
            } else if (session.count == 0) {
                utils_println("Trade is empty. Add at least one product before sending.");
            } else {
                char *file_path = NULL;
                char *file_name = NULL;
                size_t file_size = 0;
                bool wrote = trade_write_shopping_list(&session, &file_path, &file_name, &file_size);

                if (!wrote) {
                    utils_println("Could not write the shopping list. Please try again.");
                    free(file_path);
                    free(file_name);
                } else if (session.network != NULL &&
                           !network_send_trade_offer(session.network, session.target_realm, file_path)) {
                    utils_println("Trade list saved locally, but the ally could not be notified.");
                    free(file_path);
                    free(file_name);
                } else {
                    char *message = NULL;
                    int written = asprintf(&message, "Trade list sent to %s.\n", session.target_realm);
                    free(file_path);
                    free(file_name);
                    if (written >= 0 && message != NULL) {
                        utils_print(message);
                        free(message);
                    }
                    keep_running = false;
                }
            }
        } else if (utils_equals_ignore_case(tokens[0], "cancel") || utils_equals_ignore_case(tokens[0], "exit")) {
            if (count != 1) {
                utils_println("Unknown command");
            } else {
                utils_println("Trade cancelled.");
                keep_running = false;
            }
        } else if (utils_equals_ignore_case(tokens[0], "add") || utils_equals_ignore_case(tokens[0], "remove")) {
            char *rest = line + strlen(tokens[0]);
            char *product_name = NULL;
            int amount = 0;
            bool parsed_ok = trade_parse_item_command(rest, &product_name, &amount);

            if (!parsed_ok) {
                trade_help(tokens[0]);
            } else if (utils_equals_ignore_case(tokens[0], "add")) {
                if (trade_add_item(&session, product_name, amount)) {
                    utils_println("Product added to trade list.");
                } else {
                    utils_println("That product is not available from the remote catalog.");
                }
            } else {
                if (trade_remove_item(&session, product_name, amount)) {
                    utils_println("Product removed from trade list.");
                } else {
                    utils_println("That product is not currently in the trade list.");
                }
            }
        } else {
            utils_println("Unknown command");
        }

        free(copy);
        free(line);
        line = NULL;
    }

    free(line);
    trade_session_free(&session);
    return true;
}
