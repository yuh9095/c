#ifndef NINIT_UNIT_MANAGER_H
#define NINIT_UNIT_MANAGER_H

#include <stddef.h>
#include <ninit/unit.h>

#ifdef __cplusplus
extern "C" {
#endif

struct ninit_unit_manager;

struct ninit_unit_manager *ninit_unit_manager_create(void);
void ninit_unit_manager_destroy(struct ninit_unit_manager *manager);

/* Returns 0 on success; -1 with errno set on invalid or duplicate definitions. */
int ninit_unit_manager_add(struct ninit_unit_manager *manager,
                           const struct ninit_unit_spec *spec);

/* Returned unit is borrowed and must not be freed by the caller. */
const struct ninit_unit *ninit_unit_manager_find(
    const struct ninit_unit_manager *manager, const char *name);

size_t ninit_unit_manager_count(const struct ninit_unit_manager *manager);

#ifdef __cplusplus
}
#endif

#endif
