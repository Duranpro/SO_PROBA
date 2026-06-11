#include "transfer.h"

#include "../utils/utils.h"

static bool transfer_file_exists(const char *path) {
    return path != NULL && access(path, F_OK) == 0;
}

static char *transfer_basename_copy(const char *path) {
    const char *name = NULL;

    if (path == NULL) {
        return NULL;
    }

    name = strrchr(path, '/');
#ifdef _WIN32
    if (name == NULL) {
        name = strrchr(path, '\\');
    }
#endif
    if (name != NULL) {
        name++;
    } else {
        name = path;
    }

    return utils_strdup_safe(name);
}

bool transfer_compute_md5sum(const char *path, char md5_out[CITADEL_MD5_LENGTH + 1]) {
#ifdef _WIN32
    (void) path;
    if (md5_out != NULL) {
        md5_out[0] = '\0';
    }
    return false;
#else
    int pipefd[2] = {-1, -1};
    pid_t pid = 0;
    char buffer[256];
    ssize_t bytes = 0;
    size_t total = 0;
    int status = 0;

    if (path == NULL || md5_out == NULL) {
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
        execlp("md5sum", "md5sum", path, (char *) NULL);
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
#endif
}

bool transfer_get_file_info(const char *path, char **file_name_out, size_t *size_out,
                            char md5_out[CITADEL_MD5_LENGTH + 1]) {
    int fd = -1;
    off_t size = 0;

    if (path == NULL || file_name_out == NULL || size_out == NULL || md5_out == NULL) {
        return false;
    }

    fd = open(path, O_RDONLY);
    if (fd < 0) {
        return false;
    }

    size = lseek(fd, 0, SEEK_END);
    close(fd);
    if (size < 0) {
        return false;
    }

    *file_name_out = transfer_basename_copy(path);
    if (*file_name_out == NULL) {
        return false;
    }

    if (!transfer_compute_md5sum(path, md5_out)) {
        free(*file_name_out);
        *file_name_out = NULL;
        return false;
    }

    *size_out = (size_t) size;
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

    if (config != NULL && config->workdir != NULL) {
        candidate = utils_build_path(config->workdir, sigil_name);
        if (candidate != NULL && transfer_file_exists(candidate)) {
            return candidate;
        }
        free(candidate);
    }

    return NULL;
}

bool transfer_write_inventory_file(const CitadelConfig *config, const Stock *stock,
                                   char **file_path_out, char **file_name_out,
                                   size_t *size_out, char md5_out[CITADEL_MD5_LENGTH + 1]) {
    char *file_name = NULL;
    char *file_path = NULL;
    char *content = NULL;
    size_t i = 0;

    if (config == NULL || stock == NULL || file_path_out == NULL || file_name_out == NULL ||
        size_out == NULL || md5_out == NULL) {
        return false;
    }

    if (!utils_ensure_directory(config->workdir)) {
        return false;
    }

    if (asprintf(&file_name, "%s_products.txt", config->realm_name) < 0 || file_name == NULL) {
        return false;
    }

    file_path = utils_build_path(config->workdir, file_name);
    if (file_path == NULL) {
        free(file_name);
        return false;
    }

    content = utils_strdup_safe("");
    if (content == NULL) {
        free(file_name);
        free(file_path);
        return false;
    }

    for (i = 0; i < stock->count; ++i) {
        char *line = NULL;
        char *new_content = NULL;

        if (asprintf(&line, "%s|%d|%.2f\n", stock->products[i].name, stock->products[i].amount,
                     stock->products[i].weight) < 0 || line == NULL) {
            free(content);
            free(file_name);
            free(file_path);
            return false;
        }

        if (asprintf(&new_content, "%s%s", content, line) < 0 || new_content == NULL) {
            free(line);
            free(content);
            free(file_name);
            free(file_path);
            return false;
        }

        free(content);
        free(line);
        content = new_content;
    }

    if (!utils_write_file(file_path, content)) {
        free(content);
        free(file_name);
        free(file_path);
        return false;
    }

    *size_out = strlen(content);
    free(content);

    if (!transfer_compute_md5sum(file_path, md5_out)) {
        free(file_name);
        free(file_path);
        return false;
    }

    *file_name_out = file_name;
    *file_path_out = file_path;
    return true;
}

static bool transfer_append_product(Product **products, size_t *count, const char *name, int amount, float weight) {
    Product *new_products = NULL;
    Product *product = NULL;

    new_products = (Product *) realloc(*products, sizeof(Product) * (*count + 1));
    if (new_products == NULL) {
        return false;
    }

    *products = new_products;
    product = &(*products)[*count];
    product->name = utils_strdup_safe(name);
    product->amount = amount;
    product->weight = weight;
    if (product->name == NULL) {
        return false;
    }

    (*count)++;
    return true;
}

bool transfer_parse_catalog_file(const char *path, Product **products_out, size_t *count_out) {
    char *content = NULL;
    char *line = NULL;
    char *saveptr = NULL;
    Product *products = NULL;
    size_t count = 0;

    if (path == NULL || products_out == NULL || count_out == NULL) {
        return false;
    }

    content = utils_read_file(path, NULL);
    if (content == NULL) {
        return false;
    }

    line = strtok_r(content, "\n", &saveptr);
    while (line != NULL) {
        char *copy = utils_strdup_safe(line);
        char *name = NULL;
        char *amount_text = NULL;
        char *weight_text = NULL;
        int amount = 0;
        float weight = 0.0f;

        if (copy == NULL) {
            free(content);
            return false;
        }

        name = strtok(copy, "|");
        amount_text = strtok(NULL, "|");
        weight_text = strtok(NULL, "|");
        if (name != NULL && amount_text != NULL && weight_text != NULL &&
            utils_parse_int(amount_text, &amount) && utils_parse_float(weight_text, &weight)) {
            if (!transfer_append_product(&products, &count, name, amount, weight)) {
                free(copy);
                free(content);
                return false;
            }
        }

        free(copy);
        line = strtok_r(NULL, "\n", &saveptr);
    }

    free(content);
    *products_out = products;
    *count_out = count;
    return true;
}

bool transfer_parse_order_file(const char *path, Product **products_out, size_t *count_out) {
    char *content = NULL;
    char *line = NULL;
    char *saveptr = NULL;
    Product *products = NULL;
    size_t count = 0;

    if (path == NULL || products_out == NULL || count_out == NULL) {
        return false;
    }

    content = utils_read_file(path, NULL);
    if (content == NULL) {
        return false;
    }

    line = strtok_r(content, "\n", &saveptr);
    while (line != NULL) {
        if (strncmp(line, "- ", 2) == 0) {
            char *copy = utils_strdup_safe(line + 2);
            char *marker = NULL;
            int amount = 0;

            if (copy == NULL) {
                free(content);
                return false;
            }

            marker = strrchr(copy, 'x');
            if (marker != NULL) {
                *marker = '\0';
                marker++;
                utils_trim(copy);
                utils_trim(marker);
                if (utils_parse_int(marker, &amount) && amount > 0) {
                    if (!transfer_append_product(&products, &count, copy, amount, 0.0f)) {
                        free(copy);
                        free(content);
                        return false;
                    }
                }
            }

            free(copy);
        }

        line = strtok_r(NULL, "\n", &saveptr);
    }

    free(content);
    *products_out = products;
    *count_out = count;
    return true;
}
