#include "stock.h"

#include "../utils/utils.h"

static void stock_reset_fields(Stock *stock) {
    if (stock == NULL) {
        return;
    }

    stock->products = NULL;
    stock->count = 0;
    stock->db_path = NULL;
}

static const Product *stock_find_unlocked_const(const Stock *stock, const char *name) {
    size_t i = 0;

    if (stock == NULL || name == NULL) {
        return NULL;
    }

    for (i = 0; i < stock->count; ++i) {
        if (utils_equals_ignore_case(stock->products[i].name, name)) {
            return &stock->products[i];
        }
    }

    return NULL;
}

static Product *stock_find_unlocked_mutable(Stock *stock, const char *name) {
    return (Product *) stock_find_unlocked_const((const Stock *) stock, name);
}

static bool stock_save_unlocked(const Stock *stock) {
    int fd = -1;
    size_t i = 0;

    if (stock == NULL || stock->db_path == NULL) {
        return false;
    }

    fd = open(stock->db_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        return false;
    }

    for (i = 0; i < stock->count; ++i) {
        StockRecordDisk record;
        memset(&record, 0, sizeof(record));
        strncpy(record.name, stock->products[i].name, sizeof(record.name) - 1);
        record.amount = stock->products[i].amount;
        record.weight = stock->products[i].weight;

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

    free(product->name);
    product->name = NULL;
    product->amount = 0;
    product->weight = 0.0f;
}

void stock_init(Stock *stock) {
    if (stock == NULL) {
        return;
    }

    stock_reset_fields(stock);
    stock->mutex_initialized = (pthread_mutex_init(&stock->mutex, NULL) == 0);
}

static bool stock_append_record(Stock *stock, const StockRecordDisk *disk_record) {
    Product *new_products = NULL;
    Product *product = NULL;
    size_t name_length = 0;
    char temp_name[101];

    memcpy(temp_name, disk_record->name, sizeof(disk_record->name));
    temp_name[100] = '\0';
    name_length = strnlen(temp_name, sizeof(temp_name));
    temp_name[name_length] = '\0';

    new_products = (Product *) realloc(stock->products, sizeof(Product) * (stock->count + 1));
    if (new_products == NULL) {
        return false;
    }

    stock->products = new_products;
    product = &stock->products[stock->count];
    product->name = utils_strdup_safe(temp_name);
    product->amount = disk_record->amount;
    product->weight = disk_record->weight;

    if (product->name == NULL) {
        return false;
    }

    stock->count++;
    return true;
}

bool stock_load(Stock *stock, const char *path) {
    int fd = -1;

    if (stock == NULL || path == NULL) {
        return false;
    }

    if (!stock_lock(stock)) {
        return false;
    }

    fd = open(path, O_RDONLY);
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
    stock->db_path = utils_strdup_safe(path);
    if (stock->db_path == NULL) {
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

    if (stock->mutex_initialized) {
        pthread_mutex_lock(&stock->mutex);
    }

    for (i = 0; i < stock->count; ++i) {
        stock_free_product(&stock->products[i]);
    }

    free(stock->products);
    free(stock->db_path);
    stock_reset_fields(stock);

    if (stock->mutex_initialized) {
        pthread_mutex_unlock(&stock->mutex);
        pthread_mutex_destroy(&stock->mutex);
    }
    stock->mutex_initialized = false;
}

bool stock_lock(Stock *stock) {
    if (stock == NULL || !stock->mutex_initialized) {
        return false;
    }

    return pthread_mutex_lock(&stock->mutex) == 0;
}

void stock_unlock(Stock *stock) {
    if (stock == NULL || !stock->mutex_initialized) {
        return;
    }

    pthread_mutex_unlock(&stock->mutex);
}

size_t stock_count(Stock *stock) {
    size_t count = 0;

    if (stock == NULL) {
        return 0;
    }

    if (!stock_lock(stock)) {
        return 0;
    }

    count = stock->count;
    stock_unlock(stock);
    return count;
}

char *stock_db_path_copy(Stock *stock) {
    char *copy = NULL;

    if (stock == NULL) {
        return NULL;
    }

    if (!stock_lock(stock)) {
        return NULL;
    }

    copy = utils_strdup_safe(stock->db_path);
    stock_unlock(stock);
    return copy;
}

const Product *stock_find(const Stock *stock, const char *name) {
    const Product *found = NULL;
    Stock *mutable_stock = (Stock *) stock;

    if (stock == NULL || name == NULL) {
        return NULL;
    }

    if (!stock_lock(mutable_stock)) {
        return NULL;
    }

    found = stock_find_unlocked_const(stock, name);
    stock_unlock(mutable_stock);
    return found;
}

Product *stock_find_mutable(Stock *stock, const char *name) {
    return stock_find_unlocked_mutable(stock, name);
}

Product *stock_clone_products(const Product *products, size_t count) {
    Product *copy = NULL;
    size_t i = 0;

    if (products == NULL || count == 0) {
        return NULL;
    }

    copy = (Product *) calloc(count, sizeof(Product));
    if (copy == NULL) {
        return NULL;
    }

    for (i = 0; i < count; ++i) {
        copy[i].name = utils_strdup_safe(products[i].name);
        copy[i].amount = products[i].amount;
        copy[i].weight = products[i].weight;
        if (copy[i].name == NULL) {
            stock_free_products(copy, count);
            return NULL;
        }
    }

    return copy;
}

void stock_free_products(Product *products, size_t count) {
    size_t i = 0;

    if (products == NULL) {
        return;
    }

    for (i = 0; i < count; ++i) {
        free(products[i].name);
        products[i].name = NULL;
    }

    free(products);
}

bool stock_apply_order(Stock *stock, const Product *items, size_t count, char **reason_out) {
    size_t i = 0;
    bool ok = false;

    if (reason_out != NULL) {
        *reason_out = NULL;
    }

    if (stock == NULL || items == NULL || count == 0) {
        return false;
    }

    if (!stock_lock(stock)) {
        return false;
    }

    for (i = 0; i < count; ++i) {
        Product *product = stock_find_unlocked_mutable(stock, items[i].name);
        if (product == NULL) {
            if (reason_out != NULL) {
                *reason_out = utils_strdup_safe("UNKNOWN_PRODUCT");
            }
            stock_unlock(stock);
            return false;
        }
        if (product->amount < items[i].amount) {
            if (reason_out != NULL) {
                *reason_out = utils_strdup_safe("OUT_OF_STOCK");
            }
            stock_unlock(stock);
            return false;
        }
    }

    for (i = 0; i < count; ++i) {
        Product *product = stock_find_unlocked_mutable(stock, items[i].name);
        if (product != NULL) {
            product->amount -= items[i].amount;
        }
    }

    ok = stock_save_unlocked(stock);
    if (!ok) {
        for (i = 0; i < count; ++i) {
            Product *product = stock_find_unlocked_mutable(stock, items[i].name);
            if (product != NULL) {
                product->amount += items[i].amount;
            }
        }
        if (reason_out != NULL) {
            *reason_out = utils_strdup_safe("SAVE_ERROR");
        }
        stock_unlock(stock);
        return false;
    }

    stock_unlock(stock);
    return true;
}

void stock_print_local(const Stock *stock) {
    Product *snapshot = NULL;
    size_t snapshot_count = 0;
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

    if (stock->count > 0) {
        snapshot = stock_clone_products(stock->products, stock->count);
        snapshot_count = stock->count;
    }
    stock_unlock(mutable_stock);

    if (snapshot_count == 0 || snapshot == NULL) {
        stock_free_products(snapshot, snapshot_count);
        if (snapshot_count == 0) {
            utils_println("No products available.");
        }
        return;
    }

    utils_println("--- Trade Ledger ---");
    utils_println("Item | Value (Gold) | Weight (Stone)");
    utils_println("--------------------------------------------------------");
    for (i = 0; i < snapshot_count; ++i) {
        char *line = NULL;
        int written = asprintf(&line, "%s | %d | %.1f\n",
                               snapshot[i].name,
                               snapshot[i].amount,
                               snapshot[i].weight);
        if (written >= 0 && line != NULL) {
            utils_print(line);
            free(line);
        }
    }
    utils_println("--------------------------------------------------------");

    {
        char *summary = NULL;
        int written = asprintf(&summary, "Total Entries: %zu\n", snapshot_count);
        if (written >= 0 && summary != NULL) {
            utils_print(summary);
            free(summary);
        }
    }

    stock_free_products(snapshot, snapshot_count);
}
