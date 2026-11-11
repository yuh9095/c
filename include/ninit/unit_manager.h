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
int ninit_unit_manager_add(struct ninit_unit_manager *manager, const struct ninit_unit_spec *spec);
const struct ninit_unit *ninit_unit_manager_find(const struct ninit_unit_manager *manager, const char *name);
const struct ninit_unit *ninit_unit_manager_at(const struct ninit_unit_manager *manager, size_t index);
size_t ninit_unit_manager_count(const struct ninit_unit_manager *manager);
#ifdef __cplusplus
}
#endif
#endif
