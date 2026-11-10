#include <ninit/service_runtime.h>

#include <errno.h>
#include <stdlib.h>
#include <unistd.h>

#include <ninit/service_executor.h>

struct service_process {
    pid_t pid;
    int exec_fd;
    uint64_t job_id;
    size_t plan_index;
    int exec_confirmed;
    int child_exited;
    int wait_status;
};

struct ninit_service_runtime {
    struct ninit_job_manager *jobs;
    struct service_process *processes;
    size_t count;
    size_t capacity;
};

static struct service_process *
find_process(struct ninit_service_runtime *runtime, pid_t pid)
{
    size_t i;
    for (i = 0; i < runtime->count; ++i)
        if (runtime->processes[i].pid == pid)
            return &runtime->processes[i];
    return NULL;
}

static void
remove_process(struct ninit_service_runtime *runtime,
               struct service_process *process)
{
    size_t index = (size_t) (process - runtime->processes);
    if (process->exec_fd >= 0)
        close(process->exec_fd);
    runtime->processes[index] = runtime->processes[runtime->count - 1];
    runtime->count--;
}

static int
reserve_process(struct ninit_service_runtime *runtime)
{
    struct service_process *items;
    size_t capacity;
    if (runtime->count < runtime->capacity)
        return 0;
    capacity = runtime->capacity == 0 ? 8 : runtime->capacity * 2;
    if (capacity < runtime->capacity ||
        capacity > (size_t) -1 / sizeof(*items)) {
        errno = EOVERFLOW;
        return -1;
    }
    items = realloc(runtime->processes, capacity * sizeof(*items));
    if (items == NULL)
        return -1;
    runtime->processes = items;
    runtime->capacity = capacity;
    return 0;
}

struct ninit_service_runtime *
ninit_service_runtime_create(struct ninit_job_manager *jobs)
{
    struct ninit_service_runtime *runtime;
    if (jobs == NULL) {
        errno = EINVAL;
        return NULL;
    }
    runtime = calloc(1, sizeof(*runtime));
    if (runtime != NULL)
        runtime->jobs = jobs;
    return runtime;
}

void
ninit_service_runtime_destroy(struct ninit_service_runtime *runtime)
{
    size_t i;
    if (runtime == NULL)
        return;
    /*
     * The runtime does not own service processes. Callers must stop/reap
     * children before destroying it; close only our handshake descriptors.
     */
    for (i = 0; i < runtime->count; ++i)
        if (runtime->processes[i].exec_fd >= 0)
            close(runtime->processes[i].exec_fd);
    free(runtime->processes);
    free(runtime);
}

int
ninit_service_runtime_dispatch(struct ninit_service_runtime *runtime,
                               uint64_t job_id, pid_t *started_pid)
{
    const struct ninit_unit *unit;
    size_t plan_index;
    int result;

    if (runtime == NULL) {
        errno = EINVAL;
        return -1;
    }
    result = ninit_job_manager_dispatch(runtime->jobs, job_id,
                                        &unit, &plan_index);
    if (result <= 0)
        return result;

    if (unit->type == NINIT_UNIT_TARGET) {
        if (ninit_job_manager_complete(runtime->jobs, job_id,
                                       plan_index, 1, 0) < 0)
            return -1;
        if (started_pid != NULL)
            *started_pid = -1;
        return 1;
    }
    if (unit->type != NINIT_UNIT_SERVICE) {
        (void) ninit_job_manager_complete(runtime->jobs, job_id,
                                          plan_index, 0, EINVAL);
        errno = EINVAL;
        return -1;
    }

    if (reserve_process(runtime) < 0) {
        int saved_errno = errno;
        (void) ninit_job_manager_complete(runtime->jobs, job_id,
                                          plan_index, 0, saved_errno);
        errno = saved_errno;
        return -1;
    }

    {
        pid_t pid;
        int exec_fd;
        if (ninit_service_executor_spawn(unit, &pid, &exec_fd) < 0) {
            int saved_errno = errno;
            (void) ninit_job_manager_complete(runtime->jobs, job_id,
                                              plan_index, 0, saved_errno);
            errno = saved_errno;
            return -1;
        }
        runtime->processes[runtime->count].pid = pid;
        runtime->processes[runtime->count].exec_fd = exec_fd;
        runtime->processes[runtime->count].job_id = job_id;
        runtime->processes[runtime->count].plan_index = plan_index;
        runtime->processes[runtime->count].exec_confirmed = 0;
        runtime->processes[runtime->count].child_exited = 0;
        runtime->processes[runtime->count].wait_status = 0;
        runtime->count++;
        if (started_pid != NULL)
            *started_pid = pid;
    }
    return 1;
}


size_t
ninit_service_runtime_exec_pending_count(
    const struct ninit_service_runtime *runtime)
{
    size_t i;
    size_t count = 0;

    if (runtime == NULL)
        return 0;
    for (i = 0; i < runtime->count; ++i)
        if (runtime->processes[i].exec_fd >= 0)
            ++count;
    return count;
}

int
ninit_service_runtime_exec_pending_at(
    const struct ninit_service_runtime *runtime, size_t index,
    pid_t *pid, int *exec_fd)
{
    size_t i;

    if (runtime == NULL || pid == NULL || exec_fd == NULL) {
        errno = EINVAL;
        return -1;
    }
    for (i = 0; i < runtime->count; ++i) {
        if (runtime->processes[i].exec_fd < 0)
            continue;
        if (index == 0) {
            *pid = runtime->processes[i].pid;
            *exec_fd = runtime->processes[i].exec_fd;
            return 0;
        }
        --index;
    }
    errno = ERANGE;
    return -1;
}

int
ninit_service_runtime_exec_fd(
    const struct ninit_service_runtime *runtime, pid_t pid)
{
    size_t i;
    if (runtime == NULL || pid <= 0) {
        errno = EINVAL;
        return -1;
    }
    for (i = 0; i < runtime->count; ++i)
        if (runtime->processes[i].pid == pid)
            return runtime->processes[i].exec_fd;
    errno = ENOENT;
    return -1;
}

int
ninit_service_runtime_check_exec(
    struct ninit_service_runtime *runtime, pid_t pid)
{
    struct service_process *process;
    int result;
    int saved_errno;

    if (runtime == NULL || pid <= 0) {
        errno = EINVAL;
        return -1;
    }
    process = find_process(runtime, pid);
    if (process == NULL) {
        errno = ENOENT;
        return -1;
    }
    if (process->exec_confirmed) {
        errno = EALREADY;
        return -1;
    }

    result = ninit_service_executor_check_exec(process->exec_fd);
    if (result == 0)
        return 0;
    saved_errno = result < 0 ? errno : 0;
    close(process->exec_fd);
    process->exec_fd = -1;
    process->exec_confirmed = 1;

    if (ninit_job_manager_complete(runtime->jobs, process->job_id,
                                   process->plan_index,
                                   result > 0, saved_errno) < 0)
        return -1;

    if (process->child_exited)
        remove_process(runtime, process);
    if (result < 0) {
        errno = saved_errno;
        return -1;
    }
    return 1;
}

int
ninit_service_runtime_child_exited(
    struct ninit_service_runtime *runtime, pid_t pid, int wait_status)
{
    struct service_process *process;
    if (runtime == NULL || pid <= 0) {
        errno = EINVAL;
        return -1;
    }
    process = find_process(runtime, pid);
    if (process == NULL) {
        errno = ENOENT;
        return -1;
    }
    if (process->child_exited) {
        errno = EALREADY;
        return -1;
    }
    process->child_exited = 1;
    process->wait_status = wait_status;
    if (process->exec_confirmed)
        remove_process(runtime, process);
    return 0;
}

size_t
ninit_service_runtime_tracked_count(
    const struct ninit_service_runtime *runtime)
{
    return runtime == NULL ? 0 : runtime->count;
}
