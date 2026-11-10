#define _POSIX_C_SOURCE 200809L

#include <ninit/service_runtime.h>
#include <ninit/unit_manager.h>
#include <ninit/dependency_plan.h>

#include <assert.h>
#include <poll.h>
#include <errno.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

int
main(void)
{
    struct ninit_unit_manager *units = ninit_unit_manager_create();
    struct ninit_job_manager *jobs = ninit_job_manager_create();
    struct ninit_service_runtime *runtime;
    struct ninit_dependency_plan *plan = NULL;
    struct ninit_job_info info;
    struct ninit_unit_spec spec;
    const char *argv[] = { "/bin/true", NULL };
    uint64_t job_id;
    pid_t pid;
    int result;
    int status;
    int attempts;
    struct pollfd exec_poll;
    pid_t pending_pid;
    int pending_fd;

    assert(units != NULL && jobs != NULL);
    memset(&spec, 0, sizeof(spec));
    spec.name = "smoke.service";
    spec.type = NINIT_UNIT_SERVICE;
    spec.exec_start_argv = argv;
    spec.exec_start_argc = 1;
    assert(ninit_unit_manager_add(units, &spec) == 0);
    assert(ninit_unit_manager_find(units, "smoke.service")->exec_start_argv[1] == NULL);
    assert(ninit_dependency_plan_build(units, "smoke.service", &plan) == 0);
    assert(ninit_job_manager_submit_start(jobs, plan, 1, &job_id) == 0);
    runtime = ninit_service_runtime_create(jobs);
    assert(runtime != NULL);

    assert(ninit_service_runtime_dispatch(runtime, job_id, &pid) == 1);
    assert(pid > 0);
    assert(ninit_service_runtime_exec_pending_count(runtime) == 1);
    assert(ninit_service_runtime_exec_pending_at(
               runtime, 0, &pending_pid, &pending_fd) == 0);
    assert(pending_pid == pid && pending_fd >= 0);
    assert(ninit_service_runtime_exec_fd(runtime, pid) == pending_fd);
    exec_poll.fd = pending_fd;
    exec_poll.events = POLLIN;
    exec_poll.revents = 0;
    do {
        result = poll(&exec_poll, 1, 3000);
    } while (result < 0 && errno == EINTR);
    assert(result == 1);
    assert((exec_poll.revents & (POLLIN | POLLHUP)) != 0);
    result = ninit_service_runtime_check_exec(runtime, pid);
    assert(result == 1);
    assert(ninit_service_runtime_exec_pending_count(runtime) == 0);
    assert(ninit_service_runtime_exec_pending_at(
               runtime, 0, &pending_pid, &pending_fd) == -1);
    assert(errno == ERANGE);
    assert(ninit_job_manager_get_info(jobs, job_id, &info) == 0);
    assert(info.state == NINIT_JOB_SUCCEEDED);
    assert(info.completed_count == 1);

    assert(waitpid(pid, &status, 0) == pid);
    assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    assert(ninit_service_runtime_child_exited(runtime, pid, status) == 0);
    assert(ninit_service_runtime_tracked_count(runtime) == 0);

    ninit_service_runtime_destroy(runtime);
    ninit_dependency_plan_destroy(plan);
    ninit_job_manager_destroy(jobs);
    ninit_unit_manager_destroy(units);
    return 0;
}
