#ifndef STOCK_H
#define STOCK_H

#include "../utils/system.h"

typedef struct {
    char *name;
    int amount;
    float weight;
} Product;

typedef struct {
    Product *products;
    size_t count;
    char *db_path;
    pthread_mutex_t mutex;
    bool mutex_initialized;
} Stock;

typedef struct {
    char name[100];
    int amount;
    float weight;
} StockRecordDisk;

void stock_init(Stock *stock);
bool stock_load(Stock *stock, const char *path);
bool stock_save(const Stock *stock);
bool stock_save_locked(const Stock *stock);
void stock_free(Stock *stock);
bool stock_lock(Stock *stock);
void stock_unlock(Stock *stock);
size_t stock_count(Stock *stock);
char *stock_db_path_copy(Stock *stock);
const Product *stock_find(const Stock *stock, const char *name);
Product *stock_find_mutable(Stock *stock, const char *name);
bool stock_apply_order(Stock *stock, const Product *items, size_t count, char **reason_out);
Product *stock_clone_products(const Product *products, size_t count);
void stock_free_products(Product *products, size_t count);
void stock_print_local(const Stock *stock);

#endif
