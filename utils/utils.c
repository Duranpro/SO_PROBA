#include "utils.h"

ssize_t utils_write_all(int fd, const void *buffer, size_t size) {
    const char *cursor = (const char *) buffer;
    size_t total = 0;

    while (total < size) {
        ssize_t written = write(fd, cursor + total, size - total);
        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        total += (size_t) written;
    }

    return (ssize_t) total;
}

bool utils_print(const char *text) {
    if (text == NULL) {
        return false;
    }

    return utils_write_all(CITADEL_STDOUT, text, strlen(text)) >= 0;
}

bool utils_println(const char *text) {
    if (text == NULL) {
        return false;
    }

    return utils_print(text) && utils_write_all(CITADEL_STDOUT, "\n", 1) == 1;
}

char *utils_read_line_fd(int fd) {
    size_t capacity = 64;
    size_t length = 0;
    char *buffer = (char *) malloc(capacity);

    if (buffer == NULL) {
        return NULL;
    }

    while (true) {
        char ch = '\0';
        ssize_t bytes = read(fd, &ch, 1);

        if (bytes == 0) {
            if (length == 0) {
                free(buffer);
                return NULL;
            }
            break;
        }

        if (bytes < 0) {
            if (errno == EINTR) {
                free(buffer);
                return NULL;
            }
            free(buffer);
            return NULL;
        }

        if (ch == '\n') {
            break;
        }

        if (length + 1 >= capacity) {
            size_t new_capacity = capacity * 2;
            char *new_buffer = (char *) realloc(buffer, new_capacity);
            if (new_buffer == NULL) {
                free(buffer);
                return NULL;
            }
            buffer = new_buffer;
            capacity = new_capacity;
        }

        buffer[length++] = ch;
    }

    buffer[length] = '\0';
    if (length > 0 && buffer[length - 1] == '\r') {
        buffer[length - 1] = '\0';
    }

    return buffer;
}

char *utils_read_file(const char *path, size_t *size_out) {
    int fd = open(path, O_RDONLY);
    char temp[512];
    size_t capacity = 1024;
    size_t length = 0;
    char *buffer = NULL;

    if (fd < 0) {
        return NULL;
    }

    buffer = (char *) malloc(capacity + 1);
    if (buffer == NULL) {
        close(fd);
        return NULL;
    }

    while (true) {
        ssize_t bytes = read(fd, temp, sizeof(temp));
        if (bytes == 0) {
            break;
        }
        if (bytes < 0) {
            if (errno == EINTR) {
                continue;
            }
            free(buffer);
            close(fd);
            return NULL;
        }

        if (length + (size_t) bytes >= capacity) {
            size_t new_capacity = capacity;
            while (length + (size_t) bytes >= new_capacity) {
                new_capacity *= 2;
            }
            char *new_buffer = (char *) realloc(buffer, new_capacity + 1);
            if (new_buffer == NULL) {
                free(buffer);
                close(fd);
                return NULL;
            }
            buffer = new_buffer;
            capacity = new_capacity;
        }

        memcpy(buffer + length, temp, (size_t) bytes);
        length += (size_t) bytes;
    }

    buffer[length] = '\0';
    close(fd);

    if (size_out != NULL) {
        *size_out = length;
    }

    return buffer;
}

bool utils_write_file(const char *path, const char *text) {
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    bool ok = false;

    if (fd < 0) {
        return false;
    }

    if (text == NULL) {
        text = "";
    }

    ok = utils_write_all(fd, text, strlen(text)) >= 0;
    close(fd);
    return ok;
}

char *utils_strdup_safe(const char *text) {
    size_t length = 0;
    char *copy = NULL;

    if (text == NULL) {
        return NULL;
    }

    length = strlen(text);
    copy = (char *) malloc(length + 1);
    if (copy == NULL) {
        return NULL;
    }

    memcpy(copy, text, length + 1);
    return copy;
}

void utils_trim(char *text) {
    size_t length = 0;
    size_t start = 0;
    size_t end = 0;

    if (text == NULL) {
        return;
    }

    length = strlen(text);

    while (start < length && isspace((unsigned char) text[start])) {
        start++;
    }

    end = length;
    while (end > start && isspace((unsigned char) text[end - 1])) {
        end--;
    }

    if (start > 0) {
        memmove(text, text + start, end - start);
    }

    text[end - start] = '\0';
}

bool utils_equals_ignore_case(const char *left, const char *right) {
    if (left == NULL || right == NULL) {
        return false;
    }
    return strcasecmp(left, right) == 0;
}

bool utils_parse_int(const char *text, int *value) {
    char *end = NULL;
    long parsed = 0;

    if (text == NULL || value == NULL || *text == '\0') {
        return false;
    }

    errno = 0;
    parsed = strtol(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0') {
        return false;
    }

    if (parsed < INT_MIN || parsed > INT_MAX) {
        return false;
    }

    *value = (int) parsed;
    return true;
}

bool utils_parse_float(const char *text, float *value) {
    char *end = NULL;
    float parsed = 0.0f;

    if (text == NULL || value == NULL || *text == '\0') {
        return false;
    }

    errno = 0;
    parsed = strtof(text, &end);
    if (errno != 0 || end == text || *end != '\0') {
        return false;
    }

    *value = parsed;
    return true;
}

size_t utils_tokenize(char *text, char **tokens, size_t max_tokens) {
    char *cursor = text;
    size_t count = 0;

    if (text == NULL || tokens == NULL || max_tokens == 0) {
        return 0;
    }

    while (*cursor != '\0' && count < max_tokens) {
        while (*cursor != '\0' && isspace((unsigned char) *cursor)) {
            cursor++;
        }

        if (*cursor == '\0') {
            break;
        }

        tokens[count++] = cursor;

        while (*cursor != '\0' && !isspace((unsigned char) *cursor)) {
            cursor++;
        }

        if (*cursor == '\0') {
            break;
        }

        *cursor = '\0';
        cursor++;
    }

    return count;
}

char *utils_sanitize_realm_name(const char *input) {
    size_t length = 0;
    size_t i = 0;
    size_t j = 0;
    char *copy = NULL;

    if (input == NULL) {
        return NULL;
    }

    length = strlen(input);
    copy = (char *) malloc(length + 1);
    if (copy == NULL) {
        return NULL;
    }

    for (i = 0; i < length; ++i) {
        if (input[i] != '&') {
            copy[j++] = input[i];
        }
    }

    copy[j] = '\0';
    utils_trim(copy);
    return copy;
}

static int utils_create_dir(const char *path) {
    if (CITADEL_MKDIR(path) == 0) {
        return 0;
    }
    if (errno == EEXIST) {
        return 0;
    }
    return -1;
}

bool utils_ensure_directory(const char *path) {
    char buffer[PATH_MAX];
    size_t i = 0;
    size_t length = 0;

    if (path == NULL || *path == '\0') {
        return false;
    }

    length = strlen(path);
    if (length >= sizeof(buffer)) {
        return false;
    }

    memcpy(buffer, path, length + 1);

    for (i = 0; i < length; ++i) {
        if (buffer[i] == '\\') {
            buffer[i] = '/';
        }
    }

    for (i = 1; i < length; ++i) {
        if (buffer[i] == '/') {
            char saved = buffer[i];
            buffer[i] = '\0';
            if (buffer[0] != '\0' && utils_create_dir(buffer) != 0) {
                return false;
            }
            buffer[i] = saved;
        }
    }

    return utils_create_dir(buffer) == 0;
}

char *utils_build_path(const char *left, const char *right) {
    char *result = NULL;
    int written = 0;
    size_t left_length = 0;

    if (left == NULL || right == NULL) {
        return NULL;
    }

    if (*left == '\0') {
        return utils_strdup_safe(right);
    }

    left_length = strlen(left);
    if (left[left_length - 1] == '/' || left[left_length - 1] == '\\') {
        written = asprintf(&result, "%s%s", left, right);
    } else {
        written = asprintf(&result, "%s/%s", left, right);
    }

    if (written < 0) {
        return NULL;
    }

    return result;
}
