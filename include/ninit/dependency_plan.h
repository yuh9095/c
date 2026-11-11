#ifndef NINIT_DEPENDENCY_PLAN_H
#define NINIT_DEPENDENCY_PLAN_H
#include <stddef.h>
#include <ninit/unit_manager.h>
#ifdef __cplusplus
extern "C" {
#endif
struct ninit_dependency_plan;
/* Missing Requires returns ENOENT; missing Wants and ordering-only references are ignored. */
int ninit_dependency_plan_build(const struct ninit_unit_manager *, const char *, struct ninit_dependency_plan **);
void ninit_dependency_plan_destroy(struct ninit_dependency_plan *);
size_t ninit_dependency_plan_count(const struct ninit_dependency_plan *);
const struct ninit_unit *ninit_dependency_plan_unit(const struct ninit_dependency_plan *, size_t);
size_t ninit_dependency_plan_wave(const struct ninit_dependency_plan *, size_t);
size_t ninit_dependency_plan_wave_count(const struct ninit_dependency_plan *);
#ifdef __cplusplus
}
#endif
#endif
