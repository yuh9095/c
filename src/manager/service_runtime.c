#define _POSIX_C_SOURCE 200809L

#include <ninit/service_runtime.h>
#include <errno.h>
#include <signal.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ninit/service_executor.h>

struct service_process {
    pid_t pid;
    int exec_fd;
    uint64_t job_id;
    size_t plan_index;
    const struct ninit_unit *unit;
    uint64_t stop_job_id;
    size_t stop_plan_index;
    int stop_requested;
    pid_t stop_pid;
    int stop_exec_fd;
    int stop_exec_confirmed;
    int stop_child_exited;
    int stop_wait_status;
    int stop_success;
    int stop_signal_sent;
    int exec_confirmed;
    int start_completion_done;
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

static struct service_process *
find_stop_helper(struct ninit_service_runtime *runtime, pid_t pid)
{
    size_t i;
    for (i = 0; i < runtime->count; ++i)
        if (runtime->processes[i].stop_pid == pid)
            return &runtime->processes[i];
    return NULL;
}

static int
complete_oneshot_start_if_ready(struct ninit_service_runtime *runtime,
                               struct service_process *process)
{
    int success;

    if (!process->unit->oneshot || !process->exec_confirmed ||
        !process->child_exited || process->start_completion_done)
        return 0;
    success = WIFEXITED(process->wait_status) &&
              WEXITSTATUS(process->wait_status) == 0;
    if (ninit_job_manager_complete(runtime->jobs, process->job_id,
                                   process->plan_index, success,
                                   success ? 0 : EIO) < 0)
        return -1;
    process->start_completion_done = 1;
    return 1;
}

static int
complete_stop_if_ready(struct ninit_service_runtime *runtime,
                       struct service_process *process)
{
    if (!process->stop_requested || !process->child_exited ||
        !process->exec_confirmed || process->stop_pid > 0)
        return 0;
    if (ninit_job_manager_complete(runtime->jobs, process->stop_job_id,
                                   process->stop_plan_index,
                                   process->stop_success,
                                   process->stop_success ? 0 : EIO) < 0)
        return -1;
    process->stop_requested = 0;
    return 1;
}

static int
advance_stop_helper(struct ninit_service_runtime *runtime,
                    struct service_process *process)
{
    if (!process->stop_requested || !process->stop_exec_confirmed ||
        !process->stop_child_exited)
        return 0;
    if (process->stop_pid > 0)
        process->stop_pid = -1;
    if (!WIFEXITED(process->stop_wait_status) ||
        WEXITSTATUS(process->stop_wait_status) != 0)
        process->stop_success = 0;
    if (!process->stop_signal_sent) {
        if (kill(-process->pid, SIGTERM) == 0 || errno == ESRCH) {
            process->stop_signal_sent = 1;
        } else {
            process->stop_success = 0;
            process->stop_signal_sent = 1;
        }
    }
    return 0;
}

static void
remove_process(struct ninit_service_runtime *runtime,
               struct service_process *process)
{
    size_t index = (size_t) (process - runtime->processes);
    if (process->exec_fd >= 0) close(process->exec_fd);
    if (process->stop_exec_fd >= 0) close(process->stop_exec_fd);
    runtime->processes[index] = runtime->processes[runtime->count - 1];
    runtime->count--;
}

static int
reserve_process(struct ninit_service_runtime *runtime)
{
    struct service_process *items;
    size_t capacity;
    if (runtime->count < runtime->capacity) return 0;
    capacity = runtime->capacity == 0 ? 8 : runtime->capacity * 2;
    if (capacity < runtime->capacity ||
        capacity > (size_t) -1 / sizeof(*items)) {
        errno = EOVERFLOW;
        return -1;
    }
    items = realloc(runtime->processes, capacity * sizeof(*items));
    if (items == NULL) return -1;
    runtime->processes = items;
    runtime->capacity = capacity;
    return 0;
}

struct ninit_service_runtime *
ninit_service_runtime_create(struct ninit_job_manager *jobs)
{
    struct ninit_service_runtime *runtime;
    if (jobs == NULL) { errno = EINVAL; return NULL; }
    runtime = calloc(1, sizeof(*runtime));
    if (runtime != NULL) runtime->jobs = jobs;
    return runtime;
}

int
ninit_service_runtime_signal_all(struct ninit_service_runtime *runtime,
                                 int signal_number)
{
    size_t i;
    int first_error = 0;
    if (runtime == NULL || signal_number < 0) {
        errno = EINVAL; return -1;
    }
    for (i = 0; i < runtime->count; ++i) {
        pid_t pids[2] = { runtime->processes[i].pid,
                          runtime->processes[i].stop_pid };
        size_t j;
        for (j = 0; j < 2; ++j)
            if (pids[j] > 0 && kill(-pids[j], signal_number) < 0 &&
                errno != ESRCH && first_error == 0)
                first_error = errno;
    }
    if (first_error != 0) { errno = first_error; return -1; }
    return 0;
}

int
ninit_service_runtime_signal_job(struct ninit_service_runtime *runtime,
                                uint64_t job_id, int signal_number)
{
    size_t i;
    int first_error = 0;

    if (runtime == NULL || job_id == 0 || signal_number < 0) {
        errno = EINVAL;
        return -1;
    }
    for (i = 0; i < runtime->count; ++i) {
        struct service_process *process = &runtime->processes[i];
        pid_t pid = process->job_id == job_id ? process->pid : -1;
        pid_t stop_pid = process->stop_job_id == job_id ? process->stop_pid : -1;
        if (pid > 0 && kill(-pid, signal_number) < 0 && errno != ESRCH &&
            first_error == 0)
            first_error = errno;
        if (stop_pid > 0 && kill(-stop_pid, signal_number) < 0 &&
            errno != ESRCH && first_error == 0)
            first_error = errno;
    }
    if (first_error != 0) {
        errno = first_error;
        return -1;
    }
    return 0;
}

void
ninit_service_runtime_destroy(struct ninit_service_runtime *runtime)
{
    size_t i;
    if (runtime == NULL) return;
    for (i = 0; i < runtime->count; ++i) {
        if (runtime->processes[i].exec_fd >= 0)
            close(runtime->processes[i].exec_fd);
        if (runtime->processes[i].stop_exec_fd >= 0)
            close(runtime->processes[i].stop_exec_fd);
    }
    free(runtime->processes);
    free(runtime);
}

int
ninit_service_runtime_dispatch(struct ninit_service_runtime *runtime,
                               uint64_t job_id, pid_t *started_pid)
{
    const struct ninit_unit *unit;
    size_t plan_index;
    int result, is_stop;

    if (runtime == NULL) { errno = EINVAL; return -1; }
    result = ninit_job_manager_dispatch(runtime->jobs, job_id,
                                        &unit, &plan_index);
    if (result <= 0) return result;
    is_stop = ninit_job_manager_is_stop(runtime->jobs, job_id);
    if (is_stop < 0) {
        int e = errno;
        (void)ninit_job_manager_complete(runtime->jobs, job_id, plan_index, 0, e);
        errno = e; return -1;
    }

    if (is_stop) {
        size_t i;
        struct service_process *process = NULL;
        if (unit->type == NINIT_UNIT_SERVICE)
            for (i = 0; i < runtime->count; ++i)
                if (runtime->processes[i].unit == unit) {
                    process = &runtime->processes[i]; break;
                }
        if (process == NULL || process->child_exited) {
            if (ninit_job_manager_complete(runtime->jobs, job_id,
                                            plan_index, 1, 0) < 0)
                return -1;
        } else {
            if (process->stop_requested) {
                (void)ninit_job_manager_complete(runtime->jobs, job_id,
                                                 plan_index, 0, EBUSY);
                errno = EBUSY; return -1;
            }
            process->stop_requested = 1;
            process->stop_success = 1;
            process->stop_job_id = job_id;
            process->stop_plan_index = plan_index;
            if (unit->exec_stop_argc != 0) {
                if (ninit_service_executor_spawn_argv(
                        (const char *const *)unit->exec_stop_argv,
                        unit->exec_stop_argc, &process->stop_pid,
                        &process->stop_exec_fd) < 0) {
                    int e = errno;
                    process->stop_requested = 0;
                    (void)ninit_job_manager_complete(runtime->jobs, job_id,
                                                     plan_index, 0, e);
                    errno = e; return -1;
                }
            } else {
                process->stop_exec_confirmed = 1;
                process->stop_child_exited = 1;
                process->stop_success = 1;
                if (kill(process->pid, SIGTERM) == 0 || errno == ESRCH)
                    process->stop_signal_sent = 1;
                else {
                    int e = errno;
                    process->stop_requested = 0;
                    (void)ninit_job_manager_complete(runtime->jobs, job_id,
                                                     plan_index, 0, e);
                    errno = e; return -1;
                }
            }
            if (complete_stop_if_ready(runtime, process) < 0) return -1;
        }
        if (started_pid != NULL) *started_pid = -1;
        return 1;
    }

    if (unit->type == NINIT_UNIT_TARGET) {
        if (ninit_job_manager_complete(runtime->jobs, job_id,
                                       plan_index, 1, 0) < 0) return -1;
        if (started_pid != NULL) *started_pid = -1;
        return 1;
    }
    if (unit->type != NINIT_UNIT_SERVICE) {
        (void)ninit_job_manager_complete(runtime->jobs, job_id,
                                         plan_index, 0, EINVAL);
        errno = EINVAL; return -1;
    }
    if (reserve_process(runtime) < 0) {
        int e = errno;
        (void)ninit_job_manager_complete(runtime->jobs, job_id, plan_index, 0, e);
        errno = e; return -1;
    }
    {
        pid_t pid;
        int exec_fd;
        if (ninit_service_executor_spawn(unit, &pid, &exec_fd) < 0) {
            int e = errno;
            (void)ninit_job_manager_complete(runtime->jobs, job_id, plan_index, 0, e);
            errno = e; return -1;
        }
        {
            struct service_process *p = &runtime->processes[runtime->count++];
            memset(p, 0, sizeof(*p));
            p->pid = pid; p->exec_fd = exec_fd; p->job_id = job_id;
            p->plan_index = plan_index; p->unit = unit;
            p->stop_exec_fd = -1;
        }
        if (started_pid != NULL) *started_pid = pid;
    }
    return 1;
}

size_t
ninit_service_runtime_exec_pending_count(const struct ninit_service_runtime *r)
{
    size_t i, n = 0;
    if (r == NULL) return 0;
    for (i = 0; i < r->count; ++i) {
        if (r->processes[i].exec_fd >= 0) ++n;
        if (r->processes[i].stop_exec_fd >= 0) ++n;
    }
    return n;
}

int
ninit_service_runtime_exec_pending_at(const struct ninit_service_runtime *r,
                                      size_t index, pid_t *pid, int *fd)
{
    size_t i;
    if (r == NULL || pid == NULL || fd == NULL) {
        errno = EINVAL; return -1;
    }
    for (i = 0; i < r->count; ++i) {
        if (r->processes[i].exec_fd >= 0) {
            if (index-- == 0) {
                *pid = r->processes[i].pid; *fd = r->processes[i].exec_fd;
                return 0;
            }
        }
        if (r->processes[i].stop_exec_fd >= 0) {
            if (index-- == 0) {
                *pid = r->processes[i].stop_pid;
                *fd = r->processes[i].stop_exec_fd; return 0;
            }
        }
    }
    errno = ERANGE; return -1;
}

int
ninit_service_runtime_exec_fd(const struct ninit_service_runtime *r, pid_t pid)
{
    size_t i;
    if (r == NULL || pid <= 0) { errno = EINVAL; return -1; }
    for (i = 0; i < r->count; ++i) {
        if (r->processes[i].pid == pid) return r->processes[i].exec_fd;
        if (r->processes[i].stop_pid == pid)
            return r->processes[i].stop_exec_fd;
    }
    errno = ENOENT; return -1;
}

int
ninit_service_runtime_check_exec(struct ninit_service_runtime *r, pid_t pid)
{
    struct service_process *p;
    int helper = 0, result, e;
    if (r == NULL || pid <= 0) { errno = EINVAL; return -1; }
    p = find_process(r, pid);
    if (p == NULL) { p = find_stop_helper(r, pid); helper = 1; }
    if (p == NULL) { errno = ENOENT; return -1; }
    if (helper) {
        if (p->stop_exec_confirmed) { errno = EALREADY; return -1; }
        result = ninit_service_executor_check_exec(p->stop_exec_fd);
        if (result == 0) return 0;
        e = result < 0 ? errno : 0;
        close(p->stop_exec_fd); p->stop_exec_fd = -1;
        p->stop_exec_confirmed = 1;
        if (result < 0)
            p->stop_success = 0;
        if (advance_stop_helper(r, p) < 0)
            return -1;
    } else {
        if (p->exec_confirmed) { errno = EALREADY; return -1; }
        result = ninit_service_executor_check_exec(p->exec_fd);
        if (result == 0) return 0;
        e = result < 0 ? errno : 0;
        close(p->exec_fd); p->exec_fd = -1; p->exec_confirmed = 1;
        if (result < 0) {
            if (ninit_job_manager_complete(r->jobs, p->job_id,
                    p->plan_index, 0, e) < 0) return -1;
            p->start_completion_done = 1;
        } else if (!p->unit->oneshot) {
            if (ninit_job_manager_complete(r->jobs, p->job_id,
                    p->plan_index, 1, 0) < 0) return -1;
            p->start_completion_done = 1;
        } else if (complete_oneshot_start_if_ready(r, p) < 0) {
            return -1;
        }
    }
    if (p->child_exited && p->exec_confirmed && p->stop_pid <= 0) {
        if (complete_stop_if_ready(r, p) < 0) return -1;
        remove_process(r, p);
    }
    if (result < 0) { errno = e; return -1; }
    return 1;
}

int
ninit_service_runtime_child_exited(struct ninit_service_runtime *r,
                                   pid_t pid, int wait_status)
{
    struct service_process *p;
    if (r == NULL || pid <= 0) { errno = EINVAL; return -1; }
    p = find_stop_helper(r, pid);
    if (p != NULL) {
        if (p->stop_child_exited) { errno = EALREADY; return -1; }
        p->stop_child_exited = 1;
        p->stop_wait_status = wait_status;
        if (advance_stop_helper(r, p) < 0)
            return -1;
        if (p->child_exited && p->exec_confirmed && p->stop_pid <= 0) {
            if (complete_stop_if_ready(r, p) < 0) return -1;
            remove_process(r, p);
        }
        return 0;
    }
    p = find_process(r, pid);
    if (p == NULL) { errno = ENOENT; return -1; }
    if (p->child_exited) { errno = EALREADY; return -1; }
    p->child_exited = 1; p->wait_status = wait_status;
    if (p->unit->oneshot && p->exec_confirmed &&
        complete_oneshot_start_if_ready(r, p) < 0)
        return -1;
    if (p->exec_confirmed && p->stop_pid <= 0) {
        if (complete_stop_if_ready(r, p) < 0) return -1;
        remove_process(r, p);
    }
    return 0;
}

size_t
ninit_service_runtime_tracked_count(const struct ninit_service_runtime *r)
{
    return r == NULL ? 0 : r->count;
}
