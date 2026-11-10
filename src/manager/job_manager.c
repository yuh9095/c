#include <ninit/job_manager.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>

enum item_state { ITEM_PENDING = 0, ITEM_RUNNING, ITEM_DONE, ITEM_SKIPPED };
struct job_item {
    const struct ninit_unit *unit;
    size_t wave;
    enum item_state state;
};
struct job {
    uint64_t id;
    enum ninit_job_state state;
    struct job_item *items;
    size_t count, wave_count, current_wave, running, completed, max_parallel;
    int is_stop;
    int error_code;
    struct job *next;
};
struct ninit_job_manager {
    struct job *jobs;
    uint64_t next_id;
};

static struct job *
find_job(const struct ninit_job_manager *manager, uint64_t id)
{
    struct job *job;
    for (job = manager->jobs; job != NULL; job = job->next)
        if (job->id == id) return job;
    return NULL;
}

struct ninit_job_manager *
ninit_job_manager_create(void)
{
    struct ninit_job_manager *manager = calloc(1, sizeof(*manager));
    if (manager != NULL) manager->next_id = 1;
    return manager;
}

void
ninit_job_manager_destroy(struct ninit_job_manager *manager)
{
    struct job *job, *next;
    if (manager == NULL) return;
    for (job = manager->jobs; job != NULL; job = next) {
        next = job->next;
        free(job->items);
        free(job);
    }
    free(manager);
}

static int
submit_plan(struct ninit_job_manager *manager,
    const struct ninit_dependency_plan *plan, size_t max_parallel,
    uint64_t *job_id, int is_stop)
{
    struct job *job;
    size_t i, count;
    if (manager == NULL || plan == NULL || job_id == NULL || max_parallel == 0) {
        errno = EINVAL;
        return -1;
    }
    count = ninit_dependency_plan_count(plan);
    if (count == 0 || count > SIZE_MAX / sizeof(*job->items)) {
        errno = EINVAL;
        return -1;
    }
    if (manager->next_id == 0 || manager->next_id == UINT64_MAX) {
        errno = EOVERFLOW;
        return -1;
    }
    job = calloc(1, sizeof(*job));
    if (job == NULL) { errno = ENOMEM; return -1; }
    job->items = calloc(count, sizeof(*job->items));
    if (job->items == NULL) { free(job); errno = ENOMEM; return -1; }
    job->id = manager->next_id++;
    job->state = NINIT_JOB_QUEUED;
    job->count = count;
    job->wave_count = ninit_dependency_plan_wave_count(plan);
    job->max_parallel = max_parallel;
    job->is_stop = is_stop;
    for (i = 0; i < count; ++i) {
        job->items[i].unit = ninit_dependency_plan_unit(plan, i);
        job->items[i].wave = ninit_dependency_plan_wave(plan, i);
        if (job->items[i].unit == NULL || job->wave_count == 0 ||
            job->items[i].wave >= job->wave_count) {
            free(job->items); free(job); errno = EINVAL; return -1;
        }
        if (is_stop)
            job->items[i].wave = job->wave_count - 1 - job->items[i].wave;
    }
    job->next = manager->jobs;
    manager->jobs = job;
    *job_id = job->id;
    return 0;
}

int
ninit_job_manager_submit_start(struct ninit_job_manager *manager,
    const struct ninit_dependency_plan *plan, size_t max_parallel,
    uint64_t *job_id)
{
    return submit_plan(manager, plan, max_parallel, job_id, 0);
}

int
ninit_job_manager_submit_stop(struct ninit_job_manager *manager,
    const struct ninit_dependency_plan *plan, size_t max_parallel,
    uint64_t *job_id)
{
    return submit_plan(manager, plan, max_parallel, job_id, 1);
}

static void
skip_pending(struct job *job)
{
    size_t i;
    for (i = 0; i < job->count; ++i)
        if (job->items[i].state == ITEM_PENDING)
            job->items[i].state = ITEM_SKIPPED;
}

int
ninit_job_manager_is_stop(const struct ninit_job_manager *manager,
                         uint64_t id)
{
    struct job *job;

    if (manager == NULL) {
        errno = EINVAL;
        return -1;
    }
    job = find_job(manager, id);
    if (job == NULL) {
        errno = ENOENT;
        return -1;
    }
    return job->is_stop;
}

int
ninit_job_manager_dispatch(struct ninit_job_manager *manager, uint64_t id,
    const struct ninit_unit **unit, size_t *plan_index)
{
    struct job *job;
    size_t i;
    if (manager == NULL || unit == NULL || plan_index == NULL) {
        errno = EINVAL; return -1;
    }
    job = find_job(manager, id);
    if (job == NULL) { errno = ENOENT; return -1; }
    if (job->state == NINIT_JOB_QUEUED) job->state = NINIT_JOB_RUNNING;
    if (job->state != NINIT_JOB_RUNNING) return 0;

    for (;;) {
        int pending = 0;
        for (i = 0; i < job->count; ++i) {
            size_t item_index = job->is_stop ? job->count - 1 - i : i;
            if (job->items[item_index].wave == job->current_wave &&
                job->items[item_index].state == ITEM_PENDING) {
                pending = 1;
                if (job->running < job->max_parallel) {
                    job->items[item_index].state = ITEM_RUNNING;
                    ++job->running;
                    *unit = job->items[item_index].unit;
                    *plan_index = item_index;
                    return 1;
                }
            }
        }
        if (pending || job->running != 0) return 0;
        if (job->current_wave + 1 < job->wave_count) {
            ++job->current_wave;
            continue;
        }
        if (job->completed == job->count) job->state = NINIT_JOB_SUCCEEDED;
        return 0;
    }
}

int
ninit_job_manager_complete(struct ninit_job_manager *manager, uint64_t id,
    size_t index, int success, int error_code)
{
    struct job *job;
    struct job_item *item;
    if (manager == NULL) { errno = EINVAL; return -1; }
    job = find_job(manager, id);
    if (job == NULL) { errno = ENOENT; return -1; }
    if (index >= job->count) { errno = EINVAL; return -1; }
    item = &job->items[index];
    if (item->state != ITEM_RUNNING) { errno = EALREADY; return -1; }
    item->state = ITEM_DONE;
    --job->running;
    if (success) {
        ++job->completed;
    } else if (job->state != NINIT_JOB_CANCELLED &&
               job->state != NINIT_JOB_FAILED) {
        job->state = NINIT_JOB_FAILED;
        job->error_code = error_code != 0 ? error_code : EIO;
        skip_pending(job);
    }
    if (job->state == NINIT_JOB_RUNNING && job->completed == job->count &&
        job->running == 0) {
        job->state = NINIT_JOB_SUCCEEDED;
    }
    return 0;
}

int
ninit_job_manager_cancel(struct ninit_job_manager *manager, uint64_t id)
{
    struct job *job;
    if (manager == NULL) { errno = EINVAL; return -1; }
    job = find_job(manager, id);
    if (job == NULL) { errno = ENOENT; return -1; }
    if (job->state == NINIT_JOB_SUCCEEDED || job->state == NINIT_JOB_FAILED ||
        job->state == NINIT_JOB_CANCELLED) { errno = EALREADY; return -1; }
    skip_pending(job);
    job->state = NINIT_JOB_CANCELLED;
    job->error_code = ECANCELED;
    return 0;
}

int
ninit_job_manager_get_info(const struct ninit_job_manager *manager,
    uint64_t id, struct ninit_job_info *info)
{
    struct job *job;
    if (manager == NULL || info == NULL) { errno = EINVAL; return -1; }
    job = find_job(manager, id);
    if (job == NULL) { errno = ENOENT; return -1; }
    info->id = job->id;
    info->state = job->state;
    info->unit_count = job->count;
    info->completed_count = job->completed;
    info->running_count = job->running;
    info->error_code = job->error_code;
    return 0;
}
