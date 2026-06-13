#include "stock.h"

#include "../utils/utils.h"

static void stock_reset_fields(Stock *stock) {
    if (stock == NULL) {
        return;
    }

    stock->productes = NULL;
    stock->num_productes = 0;
    stock->ruta_db = NULL;
}

static const Product *stock_buscar_unlocked_const(const Stock *stock, const char *name) {
    size_t i = 0;

    if (stock == NULL || name == NULL) {
        return NULL;
    }

    for (i = 0; i < stock->num_productes; ++i) {
        if (utils_equals_ignore_case(stock->productes[i].nom, name)) {
            return &stock->productes[i];
        }
    }

    return NULL;
}

static Product *stock_buscar_unlocked_mutable(Stock *stock, const char *name) {
    return (Product *) stock_buscar_unlocked_const((const Stock *) stock, name);
}

static bool stock_save_unlocked(const Stock *stock) {
    int fd = -1;
    size_t i = 0;

    if (stock == NULL || stock->ruta_db == NULL) {
        return false;
    }

    fd = open(stock->ruta_db, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        return false;
    }

    for (i = 0; i < stock->num_productes; ++i) {
        StockRecordDisk record;
        memset(&record, 0, sizeof(record));
        strncpy(record.nom, stock->productes[i].nom, sizeof(record.nom) - 1);
        record.quantitat = stock->productes[i].quantitat;
        record.pes = stock->productes[i].pes;

        if (utils_write_all(fd, &record, sizeof(record)) < 0) {
            close(fd);
            return false;
        }
    }

    close(fd);
    return true;
}

static void stock_free_product(Product *product) {
    if (product == NULL) {
        return;
    }

    free(product->nom);
    product->nom = NULL;
    product->quantitat = 0;
    product->pes = 0.0f;
}

void stock_init(Stock *stock) {
    if (stock == NULL) {
        return;
    }

    stock_reset_fields(stock);
    stock->mutex_inicialitzat = (pthread_mutex_init(&stock->mutex, NULL) == 0);
}

static bool stock_append_record(Stock *stock, const StockRecordDisk *disk_record) {
    Product *new_products = NULL;
    Product *product = NULL;
    size_t name_length = 0;
    char temp_name[101];

    memcpy(temp_name, disk_record->nom, sizeof(disk_record->nom));
    temp_name[100] = '\0';
    name_length = strnlen(temp_name, sizeof(temp_name));
    temp_name[name_length] = '\0';

    new_products = (Product *) realloc(stock->productes, sizeof(Product) * (stock->num_productes + 1));
    if (new_products == NULL) {
        return false;
    }

    stock->productes = new_products;
    product = &stock->productes[stock->num_productes];
    product->nom = utils_strdup_safe(temp_name);
    product->quantitat = disk_record->quantitat;
    product->pes = disk_record->pes;

    if (product->nom == NULL) {
        return false;
    }

    stock->num_productes++;
    return true;
}

bool stock_load(Stock *stock, const char *ruta) {
    int fd = -1;

    if (stock == NULL || ruta == NULL) {
        return false;
    }

    if (!stock_lock(stock)) {
        return false;
    }

    fd = open(ruta, O_RDONLY);
    if (fd < 0) {
        stock_unlock(stock);
        return false;
    }

    while (true) {
        StockRecordDisk record;
        unsigned char *cursor = (unsigned char *) &record;
        size_t total = 0;

        memset(&record, 0, sizeof(record));

        while (total < sizeof(record)) {
            ssize_t bytes = read(fd, cursor + total, sizeof(record) - total);
            if (bytes == 0) {
                break;
            }
            if (bytes < 0) {
                if (errno == EINTR) {
                    continue;
                }
                close(fd);
                stock_unlock(stock);
                return false;
            }
            total += (size_t) bytes;
        }

        if (total == 0) {
            break;
        }

        if (total != sizeof(record)) {
            close(fd);
            stock_unlock(stock);
            return false;
        }

        if (!stock_append_record(stock, &record)) {
            close(fd);
            stock_unlock(stock);
            return false;
        }
    }

    close(fd);
    stock->ruta_db = utils_strdup_safe(ruta);
    if (stock->ruta_db == NULL) {
        stock_unlock(stock);
        return false;
    }

    stock_unlock(stock);
    return true;
}

bool stock_save(const Stock *stock) {
    bool ok = false;
    Stock *mutable_stock = (Stock *) stock;

    if (stock == NULL) {
        return false;
    }

    if (!stock_lock(mutable_stock)) {
        return false;
    }

    ok = stock_save_unlocked(stock);
    stock_unlock(mutable_stock);
    return ok;
}

bool stock_save_locked(const Stock *stock) {
    return stock_save_unlocked(stock);
}

void stock_free(Stock *stock) {
    size_t i = 0;

    if (stock == NULL) {
        return;
    }

    if (stock->mutex_inicialitzat) {
        pthread_mutex_lock(&stock->mutex);
    }

    for (i = 0; i < stock->num_productes; ++i) {
        stock_free_product(&stock->productes[i]);
    }

    free(stock->productes);
    free(stock->ruta_db);
    stock_reset_fields(stock);

    if (stock->mutex_inicialitzat) {
        pthread_mutex_unlock(&stock->mutex);
        pthread_mutex_destroy(&stock->mutex);
    }
    stock->mutex_inicialitzat = false;
}

bool stock_lock(Stock *stock) {
    if (stock == NULL || !stock->mutex_inicialitzat) {
        return false;
    }

    return pthread_mutex_lock(&stock->mutex) == 0;
}

void stock_unlock(Stock *stock) {
    if (stock == NULL || !stock->mutex_inicialitzat) {
        return;
    }

    pthread_mutex_unlock(&stock->mutex);
}

size_t stock_num_productes(Stock *stock) {
    size_t num_items = 0;

    if (stock == NULL) {
        return 0;
    }

    if (!stock_lock(stock)) {
        return 0;
    }

    num_items = stock->num_productes;
    stock_unlock(stock);
    return num_items;
}

char *stock_copiar_ruta_db(Stock *stock) {
    char *copy = NULL;

    if (stock == NULL) {
        return NULL;
    }

    if (!stock_lock(stock)) {
        return NULL;
    }

    copy = utils_strdup_safe(stock->ruta_db);
    stock_unlock(stock);
    return copy;
}

const Product *stock_buscar(const Stock *stock, const char *name) {
    const Product *trobat = NULL;
    Stock *mutable_stock = (Stock *) stock;

    if (stock == NULL || name == NULL) {
        return NULL;
    }

    if (!stock_lock(mutable_stock)) {
        return NULL;
    }

    trobat = stock_buscar_unlocked_const(stock, name);
    stock_unlock(mutable_stock);
    return trobat;
}

Product *stock_buscar_mutable(Stock *stock, const char *name) {
    return stock_buscar_unlocked_mutable(stock, name);
}

Product *stock_clonar_productes(const Product *productes, size_t num_items) {
    Product *copy = NULL;
    size_t i = 0;

    if (productes == NULL || num_items == 0) {
        return NULL;
    }

    copy = (Product *) calloc(num_items, sizeof(Product));
    if (copy == NULL) {
        return NULL;
    }

    for (i = 0; i < num_items; ++i) {
        copy[i].nom = utils_strdup_safe(productes[i].nom);
        copy[i].quantitat = productes[i].quantitat;
        copy[i].pes = productes[i].pes;
        if (copy[i].nom == NULL) {
            stock_alliberar_productes(copy, num_items);
            return NULL;
        }
    }

    return copy;
}

void stock_alliberar_productes(Product *productes, size_t num_items) {
    size_t i = 0;

    if (productes == NULL) {
        return;
    }

    for (i = 0; i < num_items; ++i) {
        free(productes[i].nom);
        productes[i].nom = NULL;
    }

    free(productes);
}

bool stock_aplicar_order(Stock *stock, const Product *items, size_t num_items, char **motiu_out) {
    size_t i = 0;
    bool ok = false;

    if (motiu_out != NULL) {
        *motiu_out = NULL;
    }

    if (stock == NULL || items == NULL || num_items == 0) {
        return false;
    }

    if (!stock_lock(stock)) {
        return false;
    }

    for (i = 0; i < num_items; ++i) {
        Product *product = stock_buscar_unlocked_mutable(stock, items[i].nom);
        if (product == NULL) {
            if (motiu_out != NULL) {
                *motiu_out = utils_strdup_safe("UNKNOWN_PRODUCT");
            }
            stock_unlock(stock);
            return false;
        }
        if (product->quantitat < items[i].quantitat) {
            if (motiu_out != NULL) {
                *motiu_out = utils_strdup_safe("OUT_OF_STOCK");
            }
            stock_unlock(stock);
            return false;
        }
    }

    for (i = 0; i < num_items; ++i) {
        Product *product = stock_buscar_unlocked_mutable(stock, items[i].nom);
        if (product != NULL) {
            product->quantitat -= items[i].quantitat;
        }
    }

    ok = stock_save_unlocked(stock);
    if (!ok) {
        for (i = 0; i < num_items; ++i) {
            Product *product = stock_buscar_unlocked_mutable(stock, items[i].nom);
            if (product != NULL) {
                product->quantitat += items[i].quantitat;
            }
        }
        if (motiu_out != NULL) {
            *motiu_out = utils_strdup_safe("SAVE_ERROR");
        }
        stock_unlock(stock);
        return false;
    }

    stock_unlock(stock);
    return true;
}

void stock_print_local(const Stock *stock) {
    Product *copia_stock = NULL;
    size_t num_copia_stock = 0;
    size_t i = 0;
    Stock *mutable_stock = (Stock *) stock;

    if (stock == NULL) {
        utils_println("No products available.");
        return;
    }

    if (!stock_lock(mutable_stock)) {
        utils_println("No products available.");
        return;
    }

    if (stock->num_productes > 0) {
        copia_stock = stock_clonar_productes(stock->productes, stock->num_productes);
        num_copia_stock = stock->num_productes;
    }
    stock_unlock(mutable_stock);

    if (num_copia_stock == 0 || copia_stock == NULL) {
        stock_alliberar_productes(copia_stock, num_copia_stock);
        if (num_copia_stock == 0) {
            utils_println("No products available.");
        }
        return;
    }

    utils_println("--- Trade Ledger ---");
    utils_println("Item | Value (Gold) | Weight (Stone)");
    utils_println("--------------------------------------------------------");
    for (i = 0; i < num_copia_stock; ++i) {
        char *line = NULL;
        int written = asprintf(&line, "%s | %d | %.1f\n", copia_stock[i].nom, copia_stock[i].quantitat, copia_stock[i].pes);
        if (written >= 0 && line != NULL) {
            utils_print(line);
            free(line);
        }
    }
    utils_println("--------------------------------------------------------");

    char *summary = NULL;
    int written = asprintf(&summary, "Total Entries: %zu\n", num_copia_stock);
    if (written >= 0 && summary != NULL) {
        utils_print(summary);
        free(summary);
    }

    stock_alliberar_productes(copia_stock, num_copia_stock);
}
