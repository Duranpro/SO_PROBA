#ifndef TRANSFER_H
#define TRANSFER_H

#include "../config/config.h"
#include "../stock/stock.h"
#include "../utils/system.h"

#define CITADEL_MD5_LENGTH 32

bool transfer_compute_md5sum(const char *path, char md5_out[CITADEL_MD5_LENGTH + 1]);
bool transfer_get_file_info(const char *path, char **file_name_out, size_t *size_out,
                            char md5_out[CITADEL_MD5_LENGTH + 1]);
char *transfer_resolve_sigil_path(const CitadelConfig *config, const char *sigil_name);

bool transfer_write_inventory_file(const CitadelConfig *config, const Stock *stock,
                                   char **file_path_out, char **file_name_out,
                                   size_t *size_out, char md5_out[CITADEL_MD5_LENGTH + 1]);

bool transfer_parse_catalog_file(const char *path, Product **products_out, size_t *count_out);
bool transfer_parse_order_file(const char *path, Product **products_out, size_t *count_out);

#endif
