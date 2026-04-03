#ifndef UTILS_H
#define UTILS_H

#include "system.h"

ssize_t utils_write_all(int fd, const void *buffer, size_t size);
bool utils_print(const char *text);
bool utils_println(const char *text);
char *utils_read_line_fd(int fd);
char *utils_read_file(const char *path, size_t *size_out);
bool utils_write_file(const char *path, const char *text);
char *utils_strdup_safe(const char *text);
void utils_trim(char *text);
bool utils_equals_ignore_case(const char *left, const char *right);
bool utils_parse_int(const char *text, int *value);
bool utils_parse_float(const char *text, float *value);
size_t utils_tokenize(char *text, char **tokens, size_t max_tokens);
char *utils_sanitize_realm_name(const char *input);
bool utils_ensure_directory(const char *path);
char *utils_build_path(const char *left, const char *right);

#endif
