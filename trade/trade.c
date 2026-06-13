#include "trade.h"

#include "../envoy/envoy.h"
#include "../realm/maester.h"
#include "../utils/utils.h"

typedef struct {
    char *nom;
    int quantitat;
} TradeItem;

typedef struct {
    char *regne_desti;
    struct MaesterContext *context;
    Product *available_products;
    size_t available_count;
    TradeItem *items;
    size_t num_productes;
} TradeSession;

static void trade_session_free(TradeSession *session) {
    size_t i = 0;

    if (session == NULL) {
        return;
    }

    for (i = 0; i < session->num_productes; ++i) {
        free(session->items[i].nom);
    }

    free(session->items);
    free(session->regne_desti);
    stock_alliberar_productes(session->available_products, session->available_count);
    session->items = NULL;
    session->regne_desti = NULL;
    session->available_products = NULL;
    session->available_count = 0;
    session->num_productes = 0;
}

static bool trade_parse_item_command(char *rest, char **nom_producte, int *quantitat) {
    char *last_space = NULL;

    if (rest == NULL || nom_producte == NULL || quantitat == NULL) {
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

    if (!utils_parse_int(last_space, quantitat) || *quantitat <= 0) {
        return false;
    }

    *nom_producte = rest;
    return true;
}

static bool trade_afegir_item(TradeSession *session, const char *nom_producte, int quantitat) {
    size_t i = 0;
    TradeItem *new_items = NULL;

    if (session->available_count == 0) {
        return false;
    }

    for (i = 0; i < session->available_count; ++i) {
        if (utils_equals_ignore_case(session->available_products[i].nom, nom_producte)) {
            break;
        }
    }

    if (i == session->available_count) {
        return false;
    }

    for (i = 0; i < session->num_productes; ++i) {
        if (utils_equals_ignore_case(session->items[i].nom, nom_producte)) {
            session->items[i].quantitat += quantitat;
            return true;
        }
    }

    new_items = (TradeItem *) realloc(session->items, sizeof(TradeItem) * (session->num_productes + 1));
    if (new_items == NULL) {
        return false;
    }

    session->items = new_items;
    session->items[session->num_productes].nom = utils_strdup_safe(nom_producte);
    session->items[session->num_productes].quantitat = quantitat;

    if (session->items[session->num_productes].nom == NULL) {
        return false;
    }

    session->num_productes++;
    return true;
}

static bool trade_treure_item(TradeSession *session, const char *nom_producte, int quantitat) {
    size_t i = 0;

    for (i = 0; i < session->num_productes; ++i) {
        if (utils_equals_ignore_case(session->items[i].nom, nom_producte)) {
            if (session->items[i].quantitat <= quantitat) {
                free(session->items[i].nom);
                if (i + 1 < session->num_productes) {
                    memmove(&session->items[i], &session->items[i + 1], sizeof(TradeItem) * (session->num_productes - i - 1));
                }
                session->num_productes--;
            } else {
                session->items[i].quantitat -= quantitat;
            }
            return true;
        }
    }

    return false;
}

static bool trade_append_text(char **contingut, const char *suffix) {
    char *nou_contingut = NULL;
    int written = 0;

    if (contingut == NULL || suffix == NULL) {
        return false;
    }

    if (*contingut == NULL) {
        *contingut = utils_strdup_safe(suffix);
        return *contingut != NULL;
    }

    written = asprintf(&nou_contingut, "%s%s", *contingut, suffix);
    if (written < 0 || nou_contingut == NULL) {
        return false;
    }

    free(*contingut);
    *contingut = nou_contingut;
    return true;
}

static bool trade_write_shopping_list(const TradeSession *session, char **ruta_fitxer_out, char **nom_fitxer_out, size_t *mida_fitxer_out) {
    char *nom_fitxer = NULL;
    char *ruta = NULL;
    char *contingut = NULL;
    size_t i = 0;
    int written = 0;

    if (session == NULL || session->num_productes == 0) {
        return false;
    }

    if (!utils_ensure_directory(session->context->config.directori_carpeta)) {
        return false;
    }

    written = asprintf(&nom_fitxer, "shopping_list_%s.txt", session->regne_desti);
    if (written < 0 || nom_fitxer == NULL) {
        return false;
    }

    ruta = utils_build_path(session->context->config.directori_carpeta, nom_fitxer);
    free(nom_fitxer);
    if (ruta == NULL) {
        return false;
    }

    written = asprintf(&contingut, "Requester: %s\nTarget: %s\nItems:\n", session->context->config.nom_regne, session->regne_desti);
    if (written < 0 || contingut == NULL) {
        free(ruta);
        return false;
    }

    for (i = 0; i < session->num_productes; ++i) {
        char *line = NULL;

        written = asprintf(&line, "- %s x%d\n", session->items[i].nom, session->items[i].quantitat);
        if (written < 0 || line == NULL) {
            free(ruta);
            free(contingut);
            return false;
        }

        if (!trade_append_text(&contingut, line)) {
            free(line);
            free(ruta);
            free(contingut);
            return false;
        }

        free(line);
    }

    {
        char *summary = NULL;
        written = asprintf(&summary, "Local stock loaded: %zu products\n", stock_num_productes(&session->context->stock));
        if (written < 0 || summary == NULL) {
            free(ruta);
            free(contingut);
            return false;
        }

        if (!trade_append_text(&contingut, summary)) {
            free(summary);
            free(ruta);
            free(contingut);
            return false;
        }

        free(summary);
    }

    if (!utils_write_file(ruta, contingut)) {
        free(ruta);
        free(contingut);
        return false;
    }

    if (ruta_fitxer_out != NULL) {
        *ruta_fitxer_out = utils_strdup_safe(ruta);
    }

    if (nom_fitxer_out != NULL) {
        const char *base_name = NULL;

        if (strrchr(ruta, '/') != NULL) {
            base_name = strrchr(ruta, '/') + 1;
        } else {
            base_name = ruta;
        }

        *nom_fitxer_out = utils_strdup_safe(base_name);
    }

    if (mida_fitxer_out != NULL) {
        *mida_fitxer_out = strlen(contingut);
    }

    free(ruta);
    free(contingut);
    return true;
}

static void trade_process_sigchld(struct MaesterContext *context) {
    if (context != NULL && g_sigchld_pending != 0) {
        g_sigchld_pending = 0;
        envoy_reap_finished(context);
    }
}

bool trade_run_local(struct MaesterContext *context, const char *regne_desti) {
    TradeSession session;
    bool keep_running = true;
    char *line = NULL;

    if (context == NULL || regne_desti == NULL) {
        return false;
    }

    memset(&session, 0, sizeof(session));
    session.regne_desti = utils_sanitize_realm_name(regne_desti);
    session.context = context;

    if (session.regne_desti == NULL) {
        return false;
    }

    char *start_message = NULL;
    int written = asprintf(&start_message, "Trade with %s begins.\n" "A direct path is open; your houses are allied, and no intermediaries stand in between.\n", session.regne_desti);
    if (written >= 0 && start_message != NULL) {
        utils_print(start_message);
        free(start_message);
    }

    if (context->network.inicialitzat && network_get_remote_products_copy(&context->network, session.regne_desti, &session.available_products, &session.available_count)) {
        size_t i = 0;
        char *line2 = utils_strdup_safe("Available products: ");
        if (line2 != NULL) {
            for (i = 0; i < session.available_count; ++i) {
                char *new_line = NULL;
                const char *separator = NULL;

                if (i + 1 < session.available_count) {
                    separator = ", ";
                } else {
                    separator = ".";
                }

                if (asprintf(&new_line, "%s%s%s", line2, session.available_products[i].nom, separator) >= 0 && new_line != NULL) {
                    free(line2);
                    line2 = new_line;
                }
            }
            utils_println(line2);
            free(line2);
        }
    } else {
        utils_println("No products available. Use LIST PRODUCTS\nfirst.");
    }

    while (keep_running) {
        char *copy = NULL;
        char *tokens[CITADEL_MAX_TOKENS] = {0};
        size_t num_items = 0;

        trade_process_sigchld(context);
        utils_print("(trade)> ");
        line = utils_read_line_fd(STDIN_FILENO);
        if (line == NULL) {
            if (g_stop_requested != 0) {
                break;
            }
            if (errno == EINTR) {
                trade_process_sigchld(context);
                continue;
            }
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

        num_items = utils_tokenize(copy, tokens, CITADEL_MAX_TOKENS);
        if (num_items == 0) {
            free(copy);
            free(line);
            line = NULL;
            continue;
        }

        if (utils_equals_ignore_case(tokens[0], "send")) {
            if (num_items != 1) {
                utils_println("Unknown command");
            } else if (session.num_productes == 0) {
                utils_println("Trade list is empty.");
            } else {
                char *ruta_fitxer = NULL;
                char *nom_fitxer = NULL;
                size_t mida_fitxer = 0;
                bool wrote = trade_write_shopping_list(&session, &ruta_fitxer, &nom_fitxer, &mida_fitxer);

                if (!wrote) {
                    utils_println("Could not write the shopping list. Please try again.");
                    free(ruta_fitxer);
                    free(nom_fitxer);
                } else {
                    if (!envoy_spawn_mission(session.context, ENVOY_MISSION_TRADE, session.regne_desti, ruta_fitxer)) {
                        utils_println("All envoys are occupied. Your command must wait.");
                        free(ruta_fitxer);
                        free(nom_fitxer);
                    } else {
                        char *message = NULL;
                        int written = asprintf(&message, "Trade list has been dispatched to %s.", session.regne_desti);
                        free(ruta_fitxer);
                        free(nom_fitxer);
                        if (written >= 0 && message != NULL) {
                            utils_println(message);
                            free(message);
                        }
                        keep_running = false;
                    }
                }
            }
        } else if (utils_equals_ignore_case(tokens[0], "cancel") || utils_equals_ignore_case(tokens[0], "exit")) {
            if (num_items != 1) {
                utils_println("Unknown command");
            } else {
                utils_println("Trade cancelled.");
                keep_running = false;
            }
        } else if (utils_equals_ignore_case(tokens[0], "add") || utils_equals_ignore_case(tokens[0], "remove")) {
            char *rest = line + strlen(tokens[0]);
            char *nom_producte = NULL;
            int quantitat = 0;
            bool parsed_ok = trade_parse_item_command(rest, &nom_producte, &quantitat);

            if (!parsed_ok) {
                utils_println("Invalid amount.");
            } else if (utils_equals_ignore_case(tokens[0], "add")) {
                if (trade_afegir_item(&session, nom_producte, quantitat)) {
                    utils_println("Product added to trade list.");
                } else {
                    utils_println("Product not available.");
                }
            } else {
                if (trade_treure_item(&session, nom_producte, quantitat)) {
                    utils_println("Product removed from trade list.");
                } else {
                    utils_println("Product not available.");
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
