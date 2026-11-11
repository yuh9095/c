#ifndef NINIT_JOB_MANAGER_H
#define NINIT_JOB_MANAGER_H
#include <stddef.h>
#include <stdint.h>
#include <ninit/dependency_plan.h>
#ifdef __cplusplus
extern "C" {
#endif

struct ninit_job_manager;
enum ninit_job_state {
    NINIT_JOB_QUEUED = 0,
    NINIT_JOB_RUNNING,
    NINIT_JOB_SUCCEEDED,
    NINIT_JOB_FAILED,
    NINIT_JOB_CANCELLED
};
struct ninit_job_info {
    uint64_t id;
    enum ninit_job_state state;
    size_t unit_count;
    size_t completed_count;
    size_t running_count;
    int error_code;
};
struct ninit_job_manager *ninit_job_manager_create(void);
void ninit_job_manager_destroy(struct ninit_job_manager *manager);
/* Submit a Start transaction. The plan's Unit pointers must outlive the job. */
int ninit_job_manager_submit_start(struct ninit_job_manager *manager,
    const struct ninit_dependency_plan *plan, size_t max_parallel,
    uint64_t *job_id);
/* Submit a shutdown Stop transaction in reverse dependency order. Services
 * tagged -shutdown or -stop are excluded from the ordered stop phase and are
 * left for the final shutdown signal. */
int ninit_job_manager_submit_stop(struct ninit_job_manager *manager,
    const struct ninit_dependency_plan *plan, size_t max_parallel,
    uint64_t *job_id);
/* Returns 1 with a Unit to dispatch, 0 when currently no work is dispatchable, -1 on error. */
/* Return 1 for a Stop transaction, 0 for Start, or -1 on error. */
int ninit_job_manager_is_stop(const struct ninit_job_manager *manager,
    uint64_t job_id);
int ninit_job_manager_dispatch(struct ninit_job_manager *manager, uint64_t job_id,
    const struct ninit_unit **unit, size_t *plan_index);
/* Complete exactly one previously dispatched plan index. */
int ninit_job_manager_complete(struct ninit_job_manager *manager, uint64_t job_id,
    size_t plan_index, int success, int error_code);
int ninit_job_manager_cancel(struct ninit_job_manager *manager, uint64_t job_id);
int ninit_job_manager_get_info(const struct ninit_job_manager *manager,
    uint64_t job_id, struct ninit_job_info *info);
#ifdef __cplusplus
}
#endif
#endif
