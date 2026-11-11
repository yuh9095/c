#include <ninit/job_manager.h>
#include <assert.h>
#include <errno.h>
#include <string.h>

static void add_target(struct ninit_unit_manager *m, const char *name,
 const char *const *req, size_t nr, const char *const *after, size_t na)
{
    struct ninit_unit_spec s = { name, NULL, NINIT_UNIT_TARGET, NULL, 0,
        req, nr, NULL, 0, after, na, NULL, 0, NULL, 0 };
    assert(ninit_unit_manager_add(m, &s) == 0);
}
int main(void)
{
    struct ninit_unit_manager *units = ninit_unit_manager_create();
    struct ninit_job_manager *jobs = ninit_job_manager_create();
    struct ninit_dependency_plan *plan = NULL;
    struct ninit_job_info info;
    const struct ninit_unit *unit;
    const char *requires[] = { "a.target", "b.target" };
    const char *after_both[] = { "a.target", "b.target" };
    size_t index;
    uint64_t id;
    assert(units && jobs);
    add_target(units, "a.target", NULL, 0, NULL, 0);
    add_target(units, "b.target", NULL, 0, NULL, 0);
    /* Both prerequisites must finish before root can be dispatched. */
    add_target(units, "root.target", requires, 2, after_both, 2);
    assert(ninit_dependency_plan_build(units, "root.target", &plan) == 0);
    assert(ninit_job_manager_submit_start(jobs, plan, 2, &id) == 0);
    assert(ninit_job_manager_dispatch(jobs, id, &unit, &index) == 1);
    assert(strcmp(unit->name, "a.target") == 0);
    {
        const struct ninit_unit *second;
        size_t second_index;
        assert(ninit_job_manager_dispatch(jobs, id, &second, &second_index) == 1);
        assert(strcmp(second->name, "b.target") == 0);
        assert(ninit_job_manager_dispatch(jobs, id, &second, &second_index) == 0);
        assert(ninit_job_manager_complete(jobs, id, index, 1, 0) == 0);
        assert(ninit_job_manager_dispatch(jobs, id, &second, &second_index) == 0);
        assert(ninit_job_manager_complete(jobs, id, second_index, 1, 0) == 0);
        assert(ninit_job_manager_dispatch(jobs, id, &second, &second_index) == 1);
        assert(strcmp(second->name, "root.target") == 0);
        assert(ninit_job_manager_complete(jobs, id, second_index, 1, 0) == 0);
        assert(ninit_job_manager_dispatch(jobs, id, &second, &second_index) == 0);
    }
    assert(ninit_job_manager_get_info(jobs, id, &info) == 0);
    assert(info.state == NINIT_JOB_SUCCEEDED && info.completed_count == 3);

    /* Stop order is the exact reverse of the dependency start plan. */
    assert(ninit_job_manager_submit_stop(jobs, plan, 2, &id) == 0);
    assert(ninit_job_manager_dispatch(jobs, id, &unit, &index) == 1);
    assert(strcmp(unit->name, "root.target") == 0);
    assert(ninit_job_manager_complete(jobs, id, index, 1, 0) == 0);
    assert(ninit_job_manager_dispatch(jobs, id, &unit, &index) == 1);
    assert(strcmp(unit->name, "b.target") == 0);
    {
        const struct ninit_unit *last;
        size_t last_index;
        assert(ninit_job_manager_dispatch(jobs, id, &last, &last_index) == 1);
        assert(strcmp(last->name, "a.target") == 0);
        assert(ninit_job_manager_complete(jobs, id, index, 1, 0) == 0);
        assert(ninit_job_manager_complete(jobs, id, last_index, 1, 0) == 0);
        assert(ninit_job_manager_dispatch(jobs, id, &last, &last_index) == 0);
    }
    assert(ninit_job_manager_get_info(jobs, id, &info) == 0);
    assert(info.state == NINIT_JOB_SUCCEEDED && info.completed_count == 3);

    /* OpenRC -shutdown excludes a service from ordered stop, but does not
     * remove it from the plan's completion accounting. */
    {
        struct ninit_unit_spec skipped = { 0 };
        const char *skip_requires[] = { "skip.service" };
        const char *skip_argv[] = { "/bin/true", NULL };
        skipped.name = "skip.service";
        skipped.type = NINIT_UNIT_SERVICE;
        skipped.exec_start_argv = skip_argv;
        skipped.exec_start_argc = 1;
        skipped.keyword_no_shutdown = 1;
        assert(ninit_unit_manager_add(units, &skipped) == 0);
        add_target(units, "skip.target", skip_requires, 1, NULL, 0);
        ninit_dependency_plan_destroy(plan);
        plan = NULL;
        assert(ninit_dependency_plan_build(units, "skip.target", &plan) == 0);
        assert(ninit_job_manager_submit_stop(jobs, plan, 1, &id) == 0);
        assert(ninit_job_manager_dispatch(jobs, id, &unit, &index) == 1);
        assert(strcmp(unit->name, "skip.target") == 0);
        assert(ninit_job_manager_complete(jobs, id, index, 1, 0) == 0);
        assert(ninit_job_manager_dispatch(jobs, id, &unit, &index) == 0);
        assert(ninit_job_manager_get_info(jobs, id, &info) == 0);
        assert(info.state == NINIT_JOB_SUCCEEDED && info.completed_count == 2);
    }

    /* OpenRC -stop is also excluded from the shutdown stop transaction. */
    {
        struct ninit_unit_spec skipped = { 0 };
        const char *skip_requires[] = { "skip-runlevel.service" };
        const char *skip_argv[] = { "/bin/true", NULL };
        skipped.name = "skip-runlevel.service";
        skipped.type = NINIT_UNIT_SERVICE;
        skipped.exec_start_argv = skip_argv;
        skipped.exec_start_argc = 1;
        skipped.keyword_no_stop = 1;
        assert(ninit_unit_manager_add(units, &skipped) == 0);
        add_target(units, "skip-runlevel.target", skip_requires, 1, NULL, 0);
        ninit_dependency_plan_destroy(plan);
        plan = NULL;
        assert(ninit_dependency_plan_build(units, "skip-runlevel.target", &plan) == 0);
        assert(ninit_job_manager_submit_stop(jobs, plan, 1, &id) == 0);
        assert(ninit_job_manager_dispatch(jobs, id, &unit, &index) == 1);
        assert(strcmp(unit->name, "skip-runlevel.target") == 0);
        assert(ninit_job_manager_complete(jobs, id, index, 1, 0) == 0);
        assert(ninit_job_manager_dispatch(jobs, id, &unit, &index) == 0);
        assert(ninit_job_manager_get_info(jobs, id, &info) == 0);
        assert(info.state == NINIT_JOB_SUCCEEDED && info.completed_count == 2);
    }

    ninit_dependency_plan_destroy(plan);
    ninit_job_manager_destroy(jobs);
    ninit_unit_manager_destroy(units);
    return 0;
}
