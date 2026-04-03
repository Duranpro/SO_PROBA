#include "stock.h"

#include "../utils/utils.h"

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

    stock->products = NULL;
    stock->count = 0;
    stock->db_path = NULL;
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

    fd = open(path, O_RDONLY);
    if (fd < 0) {
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
                return false;
            }
            total += (size_t) bytes;
        }

        if (total == 0) {
            break;
        }

        if (total != sizeof(record)) {
            close(fd);
            return false;
        }

        if (!stock_append_record(stock, &record)) {
            close(fd);
            return false;
        }
    }

    close(fd);
    stock->db_path = utils_strdup_safe(path);
    return stock->db_path != NULL;
}

bool stock_save(const Stock *stock) {
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

void stock_free(Stock *stock) {
    size_t i = 0;

    if (stock == NULL) {
        return;
    }

    for (i = 0; i < stock->count; ++i) {
        stock_free_product(&stock->products[i]);
    }

    free(stock->products);
    free(stock->db_path);
    stock_init(stock);
}

const Product *stock_find(const Stock *stock, const char *name) {
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

Product *stock_find_mutable(Stock *stock, const char *name) {
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

    if (reason_out != NULL) {
        *reason_out = NULL;
    }

    if (stock == NULL || items == NULL || count == 0) {
        return false;
    }

    for (i = 0; i < count; ++i) {
        Product *product = stock_find_mutable(stock, items[i].name);
        if (product == NULL) {
            if (reason_out != NULL) {
                *reason_out = utils_strdup_safe("UNKNOWN_PRODUCT");
            }
            return false;
        }
        if (product->amount < items[i].amount) {
            if (reason_out != NULL) {
                *reason_out = utils_strdup_safe("OUT_OF_STOCK");
            }
            return false;
        }
    }

    for (i = 0; i < count; ++i) {
        Product *product = stock_find_mutable(stock, items[i].name);
        if (product != NULL) {
            product->amount -= items[i].amount;
        }
    }

    if (!stock_save(stock)) {
        for (i = 0; i < count; ++i) {
            Product *product = stock_find_mutable(stock, items[i].name);
            if (product != NULL) {
                product->amount += items[i].amount;
            }
        }
        if (reason_out != NULL) {
            *reason_out = utils_strdup_safe("SAVE_ERROR");
        }
        return false;
    }

    return true;
}

void stock_print_local(const Stock *stock) {
    size_t i = 0;

    if (stock == NULL || stock->count == 0) {
        utils_println("No products available.");
        return;
    }

    utils_println("--- Trade Ledger ---");
    utils_println("Item | Value (Gold) | Weight (Stone)");
    for (i = 0; i < stock->count; ++i) {
        char *line = NULL;
        int written = asprintf(&line, "%s | %d | %.1f\n",
                               stock->products[i].name,
                               stock->products[i].amount,
                               stock->products[i].weight);
        if (written >= 0 && line != NULL) {
            utils_print(line);
            free(line);
        }
    }

    {
        char *summary = NULL;
        int written = asprintf(&summary, "Total Entries: %zu\n", stock->count);
        if (written >= 0 && summary != NULL) {
            utils_print(summary);
            free(summary);
        }
    }
}
