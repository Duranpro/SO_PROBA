#ifndef STOCK_H
#define STOCK_H

#include "../utils/system.h"

typedef struct {
    char *nom;
    int quantitat;
    float pes;
} Product;

typedef struct {
    Product *productes;
    size_t num_productes;
    char *ruta_db;
    pthread_mutex_t mutex;
    bool mutex_inicialitzat;
} Stock;

typedef struct {
    char nom[100];
    int quantitat;
    float pes;
} StockRecordDisk;

void stock_init(Stock *stock);
bool stock_load(Stock *stock, const char *ruta);
bool stock_save(const Stock *stock);
bool stock_save_locked(const Stock *stock);
void stock_free(Stock *stock);
bool stock_lock(Stock *stock);
void stock_unlock(Stock *stock);
size_t stock_num_productes(Stock *stock);
char *stock_copiar_ruta_db(Stock *stock);
const Product *stock_buscar(const Stock *stock, const char *nom_producte);
Product *stock_buscar_mutable(Stock *stock, const char *nom_producte);
bool stock_aplicar_order(Stock *stock, const Product *items, size_t num_items, char **motiu_out);
Product *stock_clonar_productes(const Product *productes, size_t num_items);
void stock_alliberar_productes(Product *productes, size_t num_items);
void stock_print_local(const Stock *stock);

#endif
