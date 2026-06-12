#include "transfer.h"

#include "../utils/utils.h"

static bool transfer_file_exists(const char *ruta) {
    return ruta != NULL && access(ruta, F_OK) == 0;
}

static char *transfer_basename_copy(const char *ruta) {
    const char *name = NULL;

    if (ruta == NULL) {
        return NULL;
    }

    name = strrchr(ruta, '/');
    if (name != NULL) {
        name++;
    } else {
        name = ruta;
    }

    return utils_strdup_safe(name);
}

bool transfer_compute_md5sum(const char *ruta, char md5_out[CITADEL_MD5_LENGTH + 1]) {
    int pipefd[2] = {-1, -1};
    pid_t pid = 0;
    char buffer[256];
    ssize_t bytes = 0;
    size_t total = 0;
    int status = 0;

    if (ruta == NULL || md5_out == NULL) {
        return false;
    }

    if (pipe(pipefd) != 0) {
        return false;
    }

    pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        return false;
    }

    if (pid == 0) {
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[0]);
        close(pipefd[1]);
        execlp("md5sum", "md5sum", ruta, (char *) NULL);
        _exit(1);
    }

    close(pipefd[1]);
    memset(buffer, 0, sizeof(buffer));
    while (total < sizeof(buffer) - 1) {
        bytes = read(pipefd[0], buffer + total, sizeof(buffer) - 1 - total);
        if (bytes == 0) {
            break;
        }
        if (bytes < 0) {
            if (errno == EINTR) {
                continue;
            }
            close(pipefd[0]);
            waitpid(pid, &status, 0);
            return false;
        }
        total += (size_t) bytes;
    }

    close(pipefd[0]);
    if (waitpid(pid, &status, 0) < 0 || !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        return false;
    }

    if (total < CITADEL_MD5_LENGTH) {
        return false;
    }

    memcpy(md5_out, buffer, CITADEL_MD5_LENGTH);
    md5_out[CITADEL_MD5_LENGTH] = '\0';
    return true;
}

bool transfer_obtenir_info_fitxer(const char *ruta, char **nom_fitxer_out, size_t *mida_out, char md5_out[CITADEL_MD5_LENGTH + 1]) {
    int fd = -1;
    off_t size = 0;

    if (ruta == NULL || nom_fitxer_out == NULL || mida_out == NULL || md5_out == NULL) {
        return false;
    }

    fd = open(ruta, O_RDONLY);
    if (fd < 0) {
        return false;
    }

    size = lseek(fd, 0, SEEK_END);
    close(fd);
    if (size < 0) {
        return false;
    }

    *nom_fitxer_out = transfer_basename_copy(ruta);
    if (*nom_fitxer_out == NULL) {
        return false;
    }

    if (!transfer_compute_md5sum(ruta, md5_out)) {
        free(*nom_fitxer_out);
        *nom_fitxer_out = NULL;
        return false;
    }

    *mida_out = (size_t) size;
    return true;
}

char *transfer_resolve_sigil_path(const CitadelConfig *config, const char *sigil_name) {
    char *candidate = NULL;

    if (sigil_name == NULL || *sigil_name == '\0') {
        return NULL;
    }

    if (transfer_file_exists(sigil_name)) {
        return utils_strdup_safe(sigil_name);
    }

    if (config != NULL && config->directori_carpeta != NULL) {
        candidate = utils_build_path(config->directori_carpeta, sigil_name);
        if (candidate != NULL && transfer_file_exists(candidate)) {
            return candidate;
        }
        free(candidate);
    }

    return NULL;
}

bool transfer_write_inventory_file(const CitadelConfig *config, const Stock *stock, char **ruta_fitxer_out, char **nom_fitxer_out, size_t *mida_out, char md5_out[CITADEL_MD5_LENGTH + 1]) {
    char *nom_fitxer = NULL;
    char *ruta_fitxer = NULL;
    char *contingut = NULL;
    Product *copia_stock = NULL;
    size_t num_copia_stock = 0;
    size_t i = 0;
    Stock *mutable_stock = (Stock *) stock;

    if (config == NULL || stock == NULL || ruta_fitxer_out == NULL || nom_fitxer_out == NULL || mida_out == NULL || md5_out == NULL) {
        return false;
    }

    if (!utils_ensure_directory(config->directori_carpeta)) {
        return false;
    }

    if (asprintf(&nom_fitxer, "%s_products.txt", config->nom_regne) < 0 || nom_fitxer == NULL) {
        return false;
    }

    ruta_fitxer = utils_build_path(config->directori_carpeta, nom_fitxer);
    if (ruta_fitxer == NULL) {
        free(nom_fitxer);
        return false;
    }

    contingut = utils_strdup_safe("");
    if (contingut == NULL) {
        free(nom_fitxer);
        free(ruta_fitxer);
        return false;
    }

    if (!stock_lock(mutable_stock)) {
        free(contingut);
        free(nom_fitxer);
        free(ruta_fitxer);
        return false;
    }

    if (stock->num_productes > 0) {
        copia_stock = stock_clonar_productes(stock->productes, stock->num_productes);
        num_copia_stock = stock->num_productes;
    }

    stock_unlock(mutable_stock);

    if (num_copia_stock > 0 && copia_stock == NULL) {
        free(contingut);
        free(nom_fitxer);
        free(ruta_fitxer);
        return false;
    }

    for (i = 0; i < num_copia_stock; ++i) {
        char *linia = NULL;
        char *nou_contingut = NULL;

        if (asprintf(&linia, "%s|%d|%.2f\n", copia_stock[i].nom, copia_stock[i].quantitat, copia_stock[i].pes) < 0 || linia == NULL) {
            free(contingut);
            free(nom_fitxer);
            free(ruta_fitxer);
            stock_alliberar_productes(copia_stock, num_copia_stock);
            return false;
        }

        if (asprintf(&nou_contingut, "%s%s", contingut, linia) < 0 || nou_contingut == NULL) {
            free(linia);
            free(contingut);
            free(nom_fitxer);
            free(ruta_fitxer);
            stock_alliberar_productes(copia_stock, num_copia_stock);
            return false;
        }

        free(contingut);
        free(linia);
        contingut = nou_contingut;
    }

    if (!utils_write_file(ruta_fitxer, contingut)) {
        free(contingut);
        free(nom_fitxer);
        free(ruta_fitxer);
        stock_alliberar_productes(copia_stock, num_copia_stock);
        return false;
    }

    *mida_out = strlen(contingut);
    free(contingut);
    stock_alliberar_productes(copia_stock, num_copia_stock);

    if (!transfer_compute_md5sum(ruta_fitxer, md5_out)) {
        free(nom_fitxer);
        free(ruta_fitxer);
        return false;
    }

    *nom_fitxer_out = nom_fitxer;
    *ruta_fitxer_out = ruta_fitxer;
    return true;
}

static bool transfer_append_product(Product **productes, size_t *num_items, const char *name, int quantitat, float pes) {
    Product *new_products = NULL;
    Product *product = NULL;

    new_products = (Product *) realloc(*productes, sizeof(Product) * (*num_items + 1));
    if (new_products == NULL) {
        return false;
    }

    *productes = new_products;
    product = &(*productes)[*num_items];
    product->nom = utils_strdup_safe(name);
    product->quantitat = quantitat;
    product->pes = pes;
    if (product->nom == NULL) {
        return false;
    }

    (*num_items)++;
    return true;
}

bool transfer_parse_catalog_text(const char *text, Product **productes_out, size_t *num_productes_out) {
    char *contingut = NULL;
    char *linia = NULL;
    char *saveptr = NULL;
    Product *productes = NULL;
    size_t num_items = 0;

    if (text == NULL || productes_out == NULL || num_productes_out == NULL) {
        return false;
    }

    *productes_out = NULL;
    *num_productes_out = 0;

    contingut = utils_strdup_safe(text);
    if (contingut == NULL) {
        return false;
    }

    linia = strtok_r(contingut, "\n", &saveptr);
    while (linia != NULL) {
        char *copy = utils_strdup_safe(linia);
        char *name = NULL;
        char *amount_text = NULL;
        char *weight_text = NULL;
        int quantitat = 0;
        float pes = 0.0f;

        if (copy == NULL) {
            free(contingut);
            stock_alliberar_productes(productes, num_items);
            return false;
        }

        name = strtok(copy, "|");
        amount_text = strtok(NULL, "|");
        weight_text = strtok(NULL, "|");
        if (name != NULL && amount_text != NULL && weight_text != NULL && utils_parse_int(amount_text, &quantitat) && utils_parse_float(weight_text, &pes)) {
            if (!transfer_append_product(&productes, &num_items, name, quantitat, pes)) {
                free(copy);
                free(contingut);
                stock_alliberar_productes(productes, num_items);
                return false;
            }
        }

        free(copy);
        linia = strtok_r(NULL, "\n", &saveptr);
    }

    free(contingut);
    *productes_out = productes;
    *num_productes_out = num_items;
    return true;
}

bool transfer_parse_catalog_file(const char *ruta, Product **productes_out, size_t *num_productes_out) {
    char *contingut = NULL;
    bool ok = false;

    if (ruta == NULL || productes_out == NULL || num_productes_out == NULL) {
        return false;
    }

    contingut = utils_read_file(ruta, NULL);
    if (contingut == NULL) {
        return false;
    }

    ok = transfer_parse_catalog_text(contingut, productes_out, num_productes_out);
    free(contingut);
    return ok;
}

bool transfer_parse_order_text(const char *text, Product **productes_out, size_t *num_productes_out) {
    char *contingut = NULL;
    char *linia = NULL;
    char *saveptr = NULL;
    Product *productes = NULL;
    size_t num_items = 0;

    if (text == NULL || productes_out == NULL || num_productes_out == NULL) {
        return false;
    }

    *productes_out = NULL;
    *num_productes_out = 0;

    contingut = utils_strdup_safe(text);
    if (contingut == NULL) {
        return false;
    }

    linia = strtok_r(contingut, "\n", &saveptr);
    while (linia != NULL) {
        if (strncmp(linia, "- ", 2) == 0) {
            char *copy = utils_strdup_safe(linia + 2);
            char *marker = NULL;
            int quantitat = 0;

            if (copy == NULL) {
                free(contingut);
                stock_alliberar_productes(productes, num_items);
                return false;
            }

            marker = strrchr(copy, 'x');
            if (marker != NULL) {
                *marker = '\0';
                marker++;
                utils_trim(copy);
                utils_trim(marker);
                if (utils_parse_int(marker, &quantitat) && quantitat > 0) {
                    if (!transfer_append_product(&productes, &num_items, copy, quantitat, 0.0f)) {
                        free(copy);
                        free(contingut);
                        stock_alliberar_productes(productes, num_items);
                        return false;
                    }
                }
            }

            free(copy);
        }

        linia = strtok_r(NULL, "\n", &saveptr);
    }

    free(contingut);
    *productes_out = productes;
    *num_productes_out = num_items;
    return true;
}

bool transfer_parse_order_file(const char *ruta, Product **productes_out, size_t *num_productes_out) {
    char *contingut = NULL;
    bool ok = false;

    if (ruta == NULL || productes_out == NULL || num_productes_out == NULL) {
        return false;
    }

    contingut = utils_read_file(ruta, NULL);
    if (contingut == NULL) {
        return false;
    }

    ok = transfer_parse_order_text(contingut, productes_out, num_productes_out);
    free(contingut);
    return ok;
}
