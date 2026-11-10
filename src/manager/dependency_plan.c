#include <ninit/dependency_plan.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

struct ninit_dependency_plan {
    const struct ninit_unit **units;
    size_t *waves;
    size_t count;
    size_t wave_count;
};

static size_t
unit_index(const struct ninit_unit_manager *manager, const char *name)
{
    size_t i, count = ninit_unit_manager_count(manager);
    for (i = 0; i < count; ++i) {
        const struct ninit_unit *unit = ninit_unit_manager_at(manager, i);
        if (unit != NULL && strcmp(unit->name, name) == 0) return i;
    }
    return SIZE_MAX;
}

void
ninit_dependency_plan_destroy(struct ninit_dependency_plan *plan)
{
    if (plan == NULL) return;
    free(plan->units);
    free(plan->waves);
    free(plan);
}

int
ninit_dependency_plan_build(const struct ninit_unit_manager *manager,
                            const char *target,
                            struct ninit_dependency_plan **result)
{
    size_t n, root, head = 0, tail = 0, i, j, active_count, matrix_size;
    size_t *queue = NULL, *indegree = NULL;
    unsigned char *active = NULL, *edges = NULL, *done = NULL;
    struct ninit_dependency_plan *plan = NULL;

    if (result == NULL || manager == NULL || target == NULL) {
        errno = EINVAL;
        return -1;
    }
    *result = NULL;
    n = ninit_unit_manager_count(manager);
    root = unit_index(manager, target);
    if (root == SIZE_MAX) { errno = ENOENT; return -1; }
    if (n == 0 || n > SIZE_MAX / n || n > SIZE_MAX / sizeof(*queue)) {
        errno = ENOMEM;
        return -1;
    }
    matrix_size = n * n;
    active = calloc(n, sizeof(*active));
    done = calloc(n, sizeof(*done));
    indegree = calloc(n, sizeof(*indegree));
    queue = malloc(n * sizeof(*queue));
    edges = calloc(matrix_size, sizeof(*edges));
    plan = calloc(1, sizeof(*plan));
    if (!active || !done || !indegree || !queue || !edges || !plan) {
        errno = ENOMEM;
        goto fail;
    }

    /* Iterative Requires/Wants closure avoids recursive stack growth. */
    active[root] = 1;
    queue[tail++] = root;
    while (head < tail) {
        const struct ninit_unit *unit = ninit_unit_manager_at(manager, queue[head++]);
        const char *const *lists[2] = {
            (const char *const *)unit->requires, (const char *const *)unit->wants
        };
        size_t lengths[2] = { unit->requires_count, unit->wants_count };
        int kind;
        for (kind = 0; kind < 2; ++kind) {
            for (j = 0; j < lengths[kind]; ++j) {
                size_t dep = unit_index(manager, lists[kind][j]);
                if (dep == SIZE_MAX) {
                    if (kind == 0) { errno = ENOENT; goto fail; }
                    continue;
                }
                if (!active[dep]) {
                    active[dep] = 1;
                    queue[tail++] = dep;
                }
            }
        }
    }
    active_count = tail;

    /* Ordering constraints never activate otherwise-unrequested units. */
    for (i = 0; i < n; ++i) {
        const struct ninit_unit *unit;
        if (!active[i]) continue;
        unit = ninit_unit_manager_at(manager, i);
        for (j = 0; j < unit->after_count; ++j) {
            size_t dep = unit_index(manager, unit->after[j]);
            if (dep != SIZE_MAX && active[dep]) edges[dep * n + i] = 1;
        }
        for (j = 0; j < unit->before_count; ++j) {
            size_t dep = unit_index(manager, unit->before[j]);
            if (dep != SIZE_MAX && active[dep]) edges[i * n + dep] = 1;
        }
    }
    for (i = 0; i < n; ++i) if (active[i])
        for (j = 0; j < n; ++j)
            if (active[j] && edges[i * n + j]) ++indegree[j];

    plan->units = calloc(active_count, sizeof(*plan->units));
    plan->waves = calloc(active_count, sizeof(*plan->waves));
    if (!plan->units || !plan->waves) { errno = ENOMEM; goto fail; }

    while (plan->count < active_count) {
        size_t added = 0, first;
        /* Registration order provides deterministic ordering within each wave. */
        first = plan->count;
        for (i = 0; i < n; ++i) {
            if (active[i] && !done[i] && indegree[i] == 0) {
                done[i] = 1;
                plan->units[plan->count] = ninit_unit_manager_at(manager, i);
                plan->waves[plan->count] = plan->wave_count;
                ++plan->count;
                ++added;
            }
        }
        if (added == 0) { errno = ELOOP; goto fail; }
        for (i = first; i < plan->count; ++i) {
            size_t completed = unit_index(manager, plan->units[i]->name);
            for (j = 0; j < n; ++j) {
                if (active[j] && edges[completed * n + j] && indegree[j] > 0)
                    --indegree[j];
            }
        }
        ++plan->wave_count;
    }

    free(active); free(done); free(indegree); free(queue); free(edges);
    *result = plan;
    return 0;

fail:
    free(active); free(done); free(indegree); free(queue); free(edges);
    ninit_dependency_plan_destroy(plan);
    return -1;
}

size_t ninit_dependency_plan_count(const struct ninit_dependency_plan *p)
{ return p == NULL ? 0 : p->count; }

const struct ninit_unit *
ninit_dependency_plan_unit(const struct ninit_dependency_plan *p, size_t i)
{ return p == NULL || i >= p->count ? NULL : p->units[i]; }

size_t ninit_dependency_plan_wave(const struct ninit_dependency_plan *p, size_t i)
{ return p == NULL || i >= p->count ? SIZE_MAX : p->waves[i]; }

size_t ninit_dependency_plan_wave_count(const struct ninit_dependency_plan *p)
{ return p == NULL ? 0 : p->wave_count; }
