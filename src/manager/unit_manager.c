#include <ninit/unit_manager.h>

#include <ctype.h>
#include <errno.h>
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
        if (!(isalnum(ch) || ch == '_' || ch == '-' || ch == '.' ||
              ch == '@' || ch == ':')) {
            return 0;
        }
    }
    return 1;
}

static void
free_string_array(char **items, size_t count)
{
    size_t i;
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
    free(unit->exec_start);
    free_string_array(unit->requires, unit->requires_count);
    free_string_array(unit->wants, unit->wants_count);
    free_string_array(unit->after, unit->after_count);
    free_string_array(unit->before, unit->before_count);
    free(unit);
}

static int
copy_string_array(char ***destination, const char *const *source,
                  size_t count)
{
    size_t i;
    char **items;

    *destination = NULL;
    if (count == 0) {
        return 0;
    }
    if (source == NULL || count > SIZE_MAX / sizeof(*items)) {
        errno = EINVAL;
        return -1;
    }
    items = calloc(count, sizeof(*items));
    if (items == NULL) {
        return -1;
    }
    for (i = 0; i < count; ++i) {
        if (!valid_name(source[i])) {
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
    unit->exec_start = copy_string(spec->exec_start);
    unit->type = spec->type;
    if (unit->name == NULL ||
        (spec->description != NULL && unit->description == NULL) ||
        (spec->exec_start != NULL && unit->exec_start == NULL) ||
        copy_string_array(&unit->requires, spec->requires,
                          spec->requires_count) != 0 ||
        copy_string_array(&unit->wants, spec->wants,
                          spec->wants_count) != 0 ||
        copy_string_array(&unit->after, spec->after,
                          spec->after_count) != 0 ||
        copy_string_array(&unit->before, spec->before,
                          spec->before_count) != 0) {
        unit_free(unit);
        return NULL;
    }
    unit->requires_count = spec->requires_count;
    unit->wants_count = spec->wants_count;
    unit->after_count = spec->after_count;
    unit->before_count = spec->before_count;
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
        (spec->type == NINIT_UNIT_TARGET && spec->exec_start != NULL)) {
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
