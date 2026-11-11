#include <ninit/unit_manager.h>

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

struct ninit_unit_manager {
    struct ninit_unit **units;
    size_t count;
    size_t capacity;
};

static char *
copy_string(const char *value)
{
    size_t length;
    char *copy;

    if (value == NULL) {
        return NULL;
    }
    length = strlen(value);
    copy = malloc(length + 1);
    if (copy != NULL) {
        memcpy(copy, value, length + 1);
    }
    return copy;
}

static int
valid_name(const char *name)
{
    size_t i;
    size_t length;

    if (name == NULL || name[0] == '\0') {
        return 0;
    }
    length = strlen(name);
    if (length > 255) {
        return 0;
    }
    for (i = 0; i < length; ++i) {
        unsigned char ch = (unsigned char)name[i];
        if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
              (ch >= '0' && ch <= '9') || ch == '_' || ch == '-' ||
              ch == '.' || ch == '@' || ch == ':')) {
            return 0;
        }
    }
    return 1;
}

static void
free_string_array(char **items, size_t count)
{
    size_t i;
    if (items == NULL) {
        return;
    }
    for (i = 0; i < count; ++i) {
        free(items[i]);
    }
    free(items);
}

static void
unit_free(struct ninit_unit *unit)
{
    if (unit == NULL) {
        return;
    }
    free(unit->name);
    free(unit->description);
    free_string_array(unit->exec_start_argv, unit->exec_start_argc);
    free_string_array(unit->requires, unit->requires_count);
    free_string_array(unit->wants, unit->wants_count);
    free_string_array(unit->after, unit->after_count);
    free_string_array(unit->before, unit->before_count);
    free_string_array(unit->exec_stop_argv, unit->exec_stop_argc);
    free(unit);
}

static int
copy_string_array(char ***destination, const char *const *source,
                  size_t count, int validate_names)
{
    size_t i;
    char **items;

    *destination = NULL;
    if (count == 0) {
        return 0;
    }
    if (source == NULL || count >= SIZE_MAX / sizeof(*items)) {
        errno = EINVAL;
        return -1;
    }
    /* Keep argv-style arrays NULL-terminated for execvp(). */
    items = calloc(count + 1, sizeof(*items));
    if (items == NULL) {
        return -1;
    }
    for (i = 0; i < count; ++i) {
        if (source[i] == NULL || (validate_names && !valid_name(source[i]))) {
            free_string_array(items, i);
            errno = EINVAL;
            return -1;
        }
        items[i] = copy_string(source[i]);
        if (items[i] == NULL) {
            free_string_array(items, i);
            return -1;
        }
    }
    *destination = items;
    return 0;
}

static struct ninit_unit *
unit_copy(const struct ninit_unit_spec *spec)
{
    struct ninit_unit *unit = calloc(1, sizeof(*unit));

    if (unit == NULL) {
        return NULL;
    }
    unit->name = copy_string(spec->name);
    unit->description = copy_string(spec->description);
    unit->type = spec->type;
    unit->exec_start_argc = spec->exec_start_argc;
    unit->requires_count = spec->requires_count;
    unit->wants_count = spec->wants_count;
    unit->after_count = spec->after_count;
    unit->before_count = spec->before_count;
    unit->exec_stop_argc = spec->exec_stop_argc;
    unit->oneshot = spec->oneshot;
    unit->keyword_no_shutdown = spec->keyword_no_shutdown;
    unit->keyword_no_stop = spec->keyword_no_stop;
    if (unit->name == NULL ||
        (spec->description != NULL && unit->description == NULL) ||
        copy_string_array(&unit->exec_start_argv, spec->exec_start_argv,
                          spec->exec_start_argc, 0) != 0 ||
        copy_string_array(&unit->requires, spec->requires,
                          spec->requires_count, 1) != 0 ||
        copy_string_array(&unit->wants, spec->wants,
                          spec->wants_count, 1) != 0 ||
        copy_string_array(&unit->after, spec->after,
                          spec->after_count, 1) != 0 ||
        copy_string_array(&unit->before, spec->before,
                          spec->before_count, 1) != 0 ||
        copy_string_array(&unit->exec_stop_argv, spec->exec_stop_argv,
                          spec->exec_stop_argc, 0) != 0) {
        unit_free(unit);
        return NULL;
    }
    return unit;
}

struct ninit_unit_manager *
ninit_unit_manager_create(void)
{
    return calloc(1, sizeof(struct ninit_unit_manager));
}

void
ninit_unit_manager_destroy(struct ninit_unit_manager *manager)
{
    size_t i;
    if (manager == NULL) {
        return;
    }
    for (i = 0; i < manager->count; ++i) {
        unit_free(manager->units[i]);
    }
    free(manager->units);
    free(manager);
}

const struct ninit_unit *
ninit_unit_manager_find(const struct ninit_unit_manager *manager,
                        const char *name)
{
    size_t i;
    if (manager == NULL || name == NULL) {
        return NULL;
    }
    for (i = 0; i < manager->count; ++i) {
        if (strcmp(manager->units[i]->name, name) == 0) {
            return manager->units[i];
        }
    }
    return NULL;
}

int
ninit_unit_manager_add(struct ninit_unit_manager *manager,
                       const struct ninit_unit_spec *spec)
{
    struct ninit_unit *unit;
    struct ninit_unit **grown;
    size_t capacity;

    if (manager == NULL || spec == NULL || !valid_name(spec->name) ||
        (spec->type != NINIT_UNIT_SERVICE && spec->type != NINIT_UNIT_TARGET) ||
        (spec->oneshot != 0 && spec->oneshot != 1) ||
        (spec->keyword_no_shutdown != 0 && spec->keyword_no_shutdown != 1) ||
        (spec->keyword_no_stop != 0 && spec->keyword_no_stop != 1) ||
        (spec->oneshot && spec->type != NINIT_UNIT_SERVICE) ||
        (spec->type == NINIT_UNIT_TARGET &&
         (spec->exec_start_argc != 0 || spec->exec_stop_argc != 0)) ||
        (spec->type == NINIT_UNIT_SERVICE &&
         (spec->exec_start_argv == NULL || spec->exec_start_argc == 0 ||
          spec->exec_start_argv[0] == NULL ||
          spec->exec_start_argv[0][0] == '\0')) ||
        (spec->exec_stop_argc != 0 &&
         (spec->exec_stop_argv == NULL || spec->exec_stop_argv[0] == NULL ||
          spec->exec_stop_argv[0][0] == '\0'))) {
        errno = EINVAL;
        return -1;
    }
    if (ninit_unit_manager_find(manager, spec->name) != NULL) {
        errno = EEXIST;
        return -1;
    }

    unit = unit_copy(spec);
    if (unit == NULL) {
        return -1;
    }
    if (manager->count == manager->capacity) {
        capacity = manager->capacity == 0 ? 8 : manager->capacity * 2;
        if (capacity < manager->capacity ||
            capacity > SIZE_MAX / sizeof(*grown)) {
            unit_free(unit);
            errno = ENOMEM;
            return -1;
        }
        grown = realloc(manager->units, capacity * sizeof(*grown));
        if (grown == NULL) {
            unit_free(unit);
            return -1;
        }
        manager->units = grown;
        manager->capacity = capacity;
    }
    manager->units[manager->count++] = unit;
    return 0;
}

size_t
ninit_unit_manager_count(const struct ninit_unit_manager *manager)
{
    return manager == NULL ? 0 : manager->count;
}

const struct ninit_unit *
ninit_unit_manager_at(const struct ninit_unit_manager *manager, size_t index)
{
    if (manager == NULL || index >= manager->count) return NULL;
    return manager->units[index];
}
