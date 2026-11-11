#include <ninit/dependency_plan.h>
#include <assert.h>
#include <errno.h>
#include <string.h>

static void add_target(struct ninit_unit_manager *m, const char *name,
 const char *const *req, size_t nr, const char *const *wants, size_t nw,
 const char *const *after, size_t na, const char *const *before, size_t nb)
{
    struct ninit_unit_spec s = { name, NULL, NINIT_UNIT_TARGET, NULL, 0,
        req, nr, wants, nw, after, na, before, nb, NULL, 0 };
    assert(ninit_unit_manager_add(m, &s) == 0);
}

int main(void)
{
    struct ninit_unit_manager *m = ninit_unit_manager_create();
    struct ninit_dependency_plan *p = NULL;
    const char *req[] = { "a.target" }, *missing_want[] = { "not-installed.target" };
    const char *after_a[] = { "a.target" };
    assert(m != NULL);
    add_target(m, "a.target", NULL, 0, NULL, 0, NULL, 0, NULL, 0);
    add_target(m, "b.target", req, 1, missing_want, 1, after_a, 1, NULL, 0);
    assert(ninit_dependency_plan_build(m, "b.target", &p) == 0);
    assert(ninit_dependency_plan_count(p) == 2);
    assert(strcmp(ninit_dependency_plan_unit(p, 0)->name, "a.target") == 0);
    assert(strcmp(ninit_dependency_plan_unit(p, 1)->name, "b.target") == 0);
    assert(ninit_dependency_plan_wave(p, 0) == 0);
    assert(ninit_dependency_plan_wave(p, 1) == 1);
    assert(ninit_dependency_plan_wave_count(p) == 2);
    ninit_dependency_plan_destroy(p); p = NULL;

    {
        const char *bad_req[] = { "absent.target" };
        add_target(m, "broken.target", bad_req, 1, NULL, 0, NULL, 0, NULL, 0);
        errno = 0;
        assert(ninit_dependency_plan_build(m, "broken.target", &p) == -1);
        assert(errno == ENOENT);
    }
    {
        const char *a_after_b[] = { "cycle-b.target" };
        const char *b_after_a[] = { "cycle-a.target" };
        const char *activate_b[] = { "cycle-b.target" };
        add_target(m, "cycle-a.target", activate_b, 1, NULL, 0, a_after_b, 1, NULL, 0);
        add_target(m, "cycle-b.target", NULL, 0, NULL, 0, b_after_a, 1, NULL, 0);
        errno = 0;
        assert(ninit_dependency_plan_build(m, "cycle-a.target", &p) == -1);
        assert(errno == ELOOP);
    }
    ninit_unit_manager_destroy(m);
    return 0;
}
