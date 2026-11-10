#define _POSIX_C_SOURCE 200809L

#include <ninit/unit_file.h>

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum section {
    SECTION_NONE = 0,
    SECTION_UNIT,
    SECTION_SERVICE,
    SECTION_TARGET
};

struct string_list {
    char **items;
    size_t count;
};

struct unit_file_data {
    char *description;
    struct string_list requires;
    struct string_list wants;
    struct string_list after;
    struct string_list before;
    struct string_list exec_start;
    unsigned int seen;
};

enum {
    SEEN_DESCRIPTION = 1u << 0,
    SEEN_REQUIRES = 1u << 1,
    SEEN_WANTS = 1u << 2,
    SEEN_AFTER = 1u << 3,
    SEEN_BEFORE = 1u << 4,
    SEEN_EXEC_START = 1u << 5
};

static char *
trim(char *text)
{
    char *end;
    while (isspace((unsigned char) *text))
        ++text;
    end = text + strlen(text);
    while (end > text && isspace((unsigned char) end[-1]))
        --end;
    *end = '\0';
    return text;
}

static void
free_list(struct string_list *list)
{
    size_t i;
    for (i = 0; i < list->count; ++i)
        free(list->items[i]);
    free(list->items);
    list->items = NULL;
    list->count = 0;
}

static int
append_word(struct string_list *list, const char *word, size_t length)
{
    char **items;
    char *copy;

    if (list->count == (size_t) -1 / sizeof(*items)) {
        errno = EOVERFLOW;
        return -1;
    }
    copy = malloc(length + 1);
    if (copy == NULL)
        return -1;
    memcpy(copy, word, length);
    copy[length] = '\0';
    items = realloc(list->items, (list->count + 1) * sizeof(*items));
    if (items == NULL) {
        free(copy);
        return -1;
    }
    list->items = items;
    list->items[list->count++] = copy;
    return 0;
}

static int
parse_name_list(const char *value, struct string_list *list)
{
    const char *cursor = value;

    while (*cursor != '\0') {
        const char *start;
        while (isspace((unsigned char) *cursor))
            ++cursor;
        if (*cursor == '\0')
            break;
        start = cursor;
        while (*cursor != '\0' && !isspace((unsigned char) *cursor)) {
            if (*cursor == '\'' || *cursor == '"' || *cursor == '\\') {
                errno = EINVAL;
                return -1;
            }
            ++cursor;
        }
        if (append_word(list, start, (size_t) (cursor - start)) < 0)
            return -1;
    }
    return 0;
}

static int
parse_exec_start(const char *value, struct string_list *list)
{
    size_t length = strlen(value);
    char *word = malloc(length + 1);
    size_t used = 0;
    size_t i;
    int started = 0;
    char quote = '\0';

    if (word == NULL)
        return -1;

    for (i = 0; i <= length; ++i) {
        char ch = value[i];

        if (ch == '\0' || (isspace((unsigned char) ch) && quote == '\0')) {
            if (started) {
                if (append_word(list, word, used) < 0) {
                    free(word);
                    return -1;
                }
                used = 0;
                started = 0;
            }
            continue;
        }
        if (ch == '\\' && quote != '\'') {
            if (i == length) {
                free(word);
                errno = EINVAL;
                return -1;
            }
            word[used++] = value[++i];
            started = 1;
            continue;
        }
        if (ch == '\'' || ch == '"') {
            if (quote == '\0') {
                quote = ch;
                started = 1;
                continue;
            }
            if (quote == ch) {
                quote = '\0';
                continue;
            }
        }
        word[used++] = ch;
        started = 1;
    }

    free(word);
    if (quote != '\0') {
        errno = EINVAL;
        return -1;
    }
    if (list->count == 0) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

static int
mark_seen(struct unit_file_data *data, unsigned int bit)
{
    if ((data->seen & bit) != 0) {
        errno = EINVAL;
        return -1;
    }
    data->seen |= bit;
    return 0;
}

static void
free_data(struct unit_file_data *data)
{
    free(data->description);
    free_list(&data->requires);
    free_list(&data->wants);
    free_list(&data->after);
    free_list(&data->before);
    free_list(&data->exec_start);
}

static int
parse_property(struct unit_file_data *data, enum section section,
               char *key, char *value)
{
    unsigned int bit;
    struct string_list *list = NULL;

    if (section == SECTION_UNIT) {
        if (strcmp(key, "Description") == 0) {
            bit = SEEN_DESCRIPTION;
            if (mark_seen(data, bit) < 0)
                return -1;
            data->description = strdup(value);
            return data->description == NULL ? -1 : 0;
        }
        if (strcmp(key, "Requires") == 0) {
            bit = SEEN_REQUIRES;
            list = &data->requires;
        } else if (strcmp(key, "Wants") == 0) {
            bit = SEEN_WANTS;
            list = &data->wants;
        } else if (strcmp(key, "After") == 0) {
            bit = SEEN_AFTER;
            list = &data->after;
        } else if (strcmp(key, "Before") == 0) {
            bit = SEEN_BEFORE;
            list = &data->before;
        } else {
            errno = EINVAL;
            return -1;
        }
        if (mark_seen(data, bit) < 0)
            return -1;
        return parse_name_list(value, list);
    }

    if (section == SECTION_SERVICE && strcmp(key, "ExecStart") == 0) {
        if (mark_seen(data, SEEN_EXEC_START) < 0)
            return -1;
        return parse_exec_start(value, &data->exec_start);
    }

    errno = EINVAL;
    return -1;
}

static int
unit_name_from_path(const char *path, char **name, enum ninit_unit_type *type)
{
    const char *base = strrchr(path, '/');
    const char *dot;
    size_t length;

    base = base == NULL ? path : base + 1;
    dot = strrchr(base, '.');
    if (dot == NULL || dot == base || dot[1] == '\0') {
        errno = EINVAL;
        return -1;
    }
    if (strcmp(dot, ".service") == 0)
        *type = NINIT_UNIT_SERVICE;
    else if (strcmp(dot, ".target") == 0)
        *type = NINIT_UNIT_TARGET;
    else {
        errno = EINVAL;
        return -1;
    }

    length = (size_t) (dot - base);
    *name = malloc(length + strlen(dot) + 1);
    if (*name == NULL)
        return -1;
    memcpy(*name, base, length);
    strcpy(*name + length, dot);
    return 0;
}

int
ninit_unit_file_load(struct ninit_unit_manager *manager, const char *path)
{
    FILE *file;
    char *line = NULL;
    size_t capacity = 0;
    ssize_t length;
    size_t line_number = 0;
    enum section section = SECTION_NONE;
    enum ninit_unit_type type;
    struct unit_file_data data = { 0 };
    struct ninit_unit_spec spec;
    char *name = NULL;
    int result = -1;

    if (manager == NULL || path == NULL || path[0] == '\0') {
        errno = EINVAL;
        return -1;
    }
    if (unit_name_from_path(path, &name, &type) < 0)
        return -1;
    file = fopen(path, "r");
    if (file == NULL) {
        free(name);
        return -1;
    }

    while ((length = getline(&line, &capacity, file)) >= 0) {
        char *text;
        char *equals;
        (void) length;
        ++line_number;
        text = trim(line);
        if (*text == '\0' || *text == '#' || *text == ';')
            continue;
        if (*text == '[') {
            size_t n = strlen(text);
            if (n < 3 || text[n - 1] != ']') {
                errno = EINVAL;
                goto done;
            }
            text[n - 1] = '\0';
            text = trim(text + 1);
            if (strcmp(text, "Unit") == 0)
                section = SECTION_UNIT;
            else if (strcmp(text, "Service") == 0 &&
                     type == NINIT_UNIT_SERVICE)
                section = SECTION_SERVICE;
            else if (strcmp(text, "Target") == 0 &&
                     type == NINIT_UNIT_TARGET)
                section = SECTION_TARGET;
            else {
                errno = EINVAL;
                goto done;
            }
            continue;
        }
        if (section == SECTION_NONE || section == SECTION_TARGET) {
            errno = EINVAL;
            goto done;
        }
        equals = strchr(text, '=');
        if (equals == NULL) {
            errno = EINVAL;
            goto done;
        }
        *equals = '\0';
        if (parse_property(&data, section, trim(text), trim(equals + 1)) < 0)
            goto done;
    }

    if (ferror(file))
        goto done;
    if (type == NINIT_UNIT_SERVICE &&
        ((data.seen & SEEN_EXEC_START) == 0 || data.exec_start.count == 0)) {
        errno = EINVAL;
        goto done;
    }
    if (type == NINIT_UNIT_TARGET &&
        (data.seen & SEEN_EXEC_START) != 0) {
        errno = EINVAL;
        goto done;
    }

    memset(&spec, 0, sizeof(spec));
    spec.name = name;
    spec.description = data.description;
    spec.type = type;
    spec.exec_start_argv = (const char *const *) data.exec_start.items;
    spec.exec_start_argc = data.exec_start.count;
    spec.requires = (const char *const *) data.requires.items;
    spec.requires_count = data.requires.count;
    spec.wants = (const char *const *) data.wants.items;
    spec.wants_count = data.wants.count;
    spec.after = (const char *const *) data.after.items;
    spec.after_count = data.after.count;
    spec.before = (const char *const *) data.before.items;
    spec.before_count = data.before.count;
    result = ninit_unit_manager_add(manager, &spec);

done:
    if (result < 0 && errno == 0)
        errno = EINVAL;
    free(line);
    fclose(file);
    free(name);
    free_data(&data);
    (void) line_number;
    return result;
}
