#ifndef TRANSFER_H
#define TRANSFER_H

#include "../config/config.h"
#include "../stock/stock.h"
#include "../utils/system.h"

#define CITADEL_MD5_LENGTH 32

bool transfer_compute_md5sum(const char *ruta, char md5_out[CITADEL_MD5_LENGTH + 1]);
bool transfer_obtenir_info_fitxer(const char *ruta, char **nom_fitxer_out, size_t *mida_out, char md5_out[CITADEL_MD5_LENGTH + 1]);
char *transfer_resolve_sigil_path(const CitadelConfig *config, const char *sigil_name);

bool transfer_write_inventory_file(const CitadelConfig *config, const Stock *stock, char **ruta_fitxer_out, char **nom_fitxer_out, size_t *mida_out, char md5_out[CITADEL_MD5_LENGTH + 1]);

bool transfer_parse_catalog_text(const char *text, Product **productes_out, size_t *num_productes_out);
bool transfer_parse_catalog_file(const char *ruta, Product **productes_out, size_t *num_productes_out);
bool transfer_parse_order_text(const char *text, Product **productes_out, size_t *num_productes_out);
bool transfer_parse_order_file(const char *ruta, Product **productes_out, size_t *num_productes_out);

#endif
