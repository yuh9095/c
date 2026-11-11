#define _POSIX_C_SOURCE 200809L

#include <ninit/service_runtime.h>
#include <ninit/unit_manager.h>
#include <ninit/dependency_plan.h>

#include <assert.h>
#include <poll.h>
#include <signal.h>
#include <errno.h>
#include <string.h>
#include <sys/wait.h>
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
    const char *argv[] = { "/bin/sleep", "30", NULL };
    const char *stop_argv[] = { "/bin/true", NULL };
    const char *stop_fail_argv[] = { "/bin/false", NULL };
    pid_t stop_pid;
    uint64_t job_id;
    pid_t pid;
    int result;
    int status;
    struct pollfd exec_poll;
    pid_t pending_pid;
    int pending_fd;

    assert(units != NULL && jobs != NULL);
    memset(&spec, 0, sizeof(spec));
    spec.name = "smoke.service";
    spec.type = NINIT_UNIT_SERVICE;
    spec.exec_start_argv = argv;
    spec.exec_start_argc = 2;
    spec.exec_stop_argv = stop_argv;
    spec.exec_stop_argc = 1;
    assert(ninit_unit_manager_add(units, &spec) == 0);
    assert(ninit_unit_manager_find(units, "smoke.service")->exec_start_argv[2] == NULL);
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

    /* Stop jobs signal the tracked process and finish only after reaping. */
    assert(ninit_job_manager_submit_stop(jobs, plan, 1, &job_id) == 0);
    assert(ninit_job_manager_is_stop(jobs, job_id) == 1);
    assert(ninit_service_runtime_dispatch(runtime, job_id, NULL) == 1);
    assert(ninit_job_manager_get_info(jobs, job_id, &info) == 0);
    assert(info.state == NINIT_JOB_RUNNING && info.completed_count == 0);
    assert(ninit_service_runtime_exec_pending_count(runtime) == 1);
    assert(ninit_service_runtime_exec_pending_at(
               runtime, 0, &stop_pid, &pending_fd) == 0);
    assert(stop_pid > 0 && stop_pid != pid);
    exec_poll.fd = pending_fd;
    exec_poll.events = POLLIN;
    exec_poll.revents = 0;
    do {
        result = poll(&exec_poll, 1, 3000);
    } while (result < 0 && errno == EINTR);
    assert(result == 1);
    assert(ninit_service_runtime_check_exec(runtime, stop_pid) == 1);
    assert(waitpid(stop_pid, &status, 0) == stop_pid);
    assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    assert(ninit_service_runtime_child_exited(runtime, stop_pid, status) == 0);
    assert(waitpid(pid, &status, 0) == pid);
    assert(WIFSIGNALED(status) && WTERMSIG(status) == SIGTERM);
    assert(ninit_service_runtime_child_exited(runtime, pid, status) == 0);
    assert(ninit_service_runtime_tracked_count(runtime) == 0);
    assert(ninit_job_manager_get_info(jobs, job_id, &info) == 0);
    assert(info.state == NINIT_JOB_SUCCEEDED && info.completed_count == 1);

    /* A failing ExecStop still terminates the service, but fails the Stop Job. */
    ninit_dependency_plan_destroy(plan);
    plan = NULL;
    memset(&spec, 0, sizeof(spec));
    spec.name = "stop-fail.service";
    spec.type = NINIT_UNIT_SERVICE;
    spec.exec_start_argv = argv;
    spec.exec_start_argc = 2;
    spec.exec_stop_argv = stop_fail_argv;
    spec.exec_stop_argc = 1;
    assert(ninit_unit_manager_add(units, &spec) == 0);
    assert(ninit_dependency_plan_build(units, "stop-fail.service", &plan) == 0);
    assert(ninit_job_manager_submit_start(jobs, plan, 1, &job_id) == 0);
    assert(ninit_service_runtime_dispatch(runtime, job_id, &pid) == 1);
    assert(ninit_service_runtime_exec_pending_at(
               runtime, 0, &pending_pid, &pending_fd) == 0);
    exec_poll.fd = pending_fd;
    exec_poll.events = POLLIN;
    exec_poll.revents = 0;
    do {
        result = poll(&exec_poll, 1, 3000);
    } while (result < 0 && errno == EINTR);
    assert(result == 1);
    assert(ninit_service_runtime_check_exec(runtime, pid) == 1);
    assert(ninit_job_manager_get_info(jobs, job_id, &info) == 0);
    assert(info.state == NINIT_JOB_SUCCEEDED);

    assert(ninit_job_manager_submit_stop(jobs, plan, 1, &job_id) == 0);
    assert(ninit_service_runtime_dispatch(runtime, job_id, NULL) == 1);
    assert(ninit_service_runtime_exec_pending_at(
               runtime, 0, &stop_pid, &pending_fd) == 0);
    exec_poll.fd = pending_fd;
    exec_poll.events = POLLIN;
    exec_poll.revents = 0;
    do {
        result = poll(&exec_poll, 1, 3000);
    } while (result < 0 && errno == EINTR);
    assert(result == 1);
    assert(ninit_service_runtime_check_exec(runtime, stop_pid) == 1);
    assert(waitpid(stop_pid, &status, 0) == stop_pid);
    assert(WIFEXITED(status) && WEXITSTATUS(status) == 1);
    assert(ninit_service_runtime_child_exited(runtime, stop_pid, status) == 0);
    assert(waitpid(pid, &status, 0) == pid);
    assert(WIFSIGNALED(status) && WTERMSIG(status) == SIGTERM);
    assert(ninit_service_runtime_child_exited(runtime, pid, status) == 0);
    assert(ninit_service_runtime_tracked_count(runtime) == 0);
    assert(ninit_job_manager_get_info(jobs, job_id, &info) == 0);
    assert(info.state == NINIT_JOB_FAILED && info.error_code == EIO);

    /* Job-scoped signalling must not depend on unrelated tracked children. */
    ninit_dependency_plan_destroy(plan);
    plan = NULL;
    memset(&spec, 0, sizeof(spec));
    spec.name = "signal.service";
    spec.type = NINIT_UNIT_SERVICE;
    spec.exec_start_argv = argv;
    spec.exec_start_argc = 2;
    assert(ninit_unit_manager_add(units, &spec) == 0);
    assert(ninit_dependency_plan_build(units, "signal.service", &plan) == 0);
    assert(ninit_job_manager_submit_start(jobs, plan, 1, &job_id) == 0);
    assert(ninit_service_runtime_dispatch(runtime, job_id, &pid) == 1);
    assert(ninit_service_runtime_exec_pending_at(
               runtime, 0, &pending_pid, &pending_fd) == 0);
    exec_poll.fd = pending_fd;
    exec_poll.events = POLLIN;
    exec_poll.revents = 0;
    do {
        result = poll(&exec_poll, 1, 3000);
    } while (result < 0 && errno == EINTR);
    assert(result == 1);
    assert(ninit_service_runtime_check_exec(runtime, pid) == 1);
    assert(ninit_service_runtime_signal_job(runtime, job_id, SIGTERM) == 0);
    assert(waitpid(pid, &status, 0) == pid);
    assert(WIFSIGNALED(status) && WTERMSIG(status) == SIGTERM);
    assert(ninit_service_runtime_child_exited(runtime, pid, status) == 0);
    assert(ninit_service_runtime_tracked_count(runtime) == 0);

    /* Oneshot Start jobs complete only after the child exits successfully. */
    ninit_dependency_plan_destroy(plan);
    plan = NULL;
    memset(&spec, 0, sizeof(spec));
    spec.name = "oneshot.service";
    spec.type = NINIT_UNIT_SERVICE;
    spec.exec_start_argv = (const char *const[]){ "/bin/true", NULL };
    spec.exec_start_argc = 1;
    spec.oneshot = 1;
    assert(ninit_unit_manager_add(units, &spec) == 0);
    assert(ninit_dependency_plan_build(units, "oneshot.service", &plan) == 0);
    assert(ninit_job_manager_submit_start(jobs, plan, 1, &job_id) == 0);
    assert(ninit_service_runtime_dispatch(runtime, job_id, &pid) == 1);
    assert(pid > 0);
    assert(ninit_service_runtime_exec_pending_at(
               runtime, 0, &pending_pid, &pending_fd) == 0);
    assert(pending_pid == pid);
    exec_poll.fd = pending_fd;
    exec_poll.events = POLLIN;
    exec_poll.revents = 0;
    do {
        result = poll(&exec_poll, 1, 3000);
    } while (result < 0 && errno == EINTR);
    assert(result == 1);
    assert(ninit_service_runtime_check_exec(runtime, pid) == 1);
    assert(ninit_job_manager_get_info(jobs, job_id, &info) == 0);
    assert(info.state == NINIT_JOB_RUNNING);
    assert(waitpid(pid, &status, 0) == pid);
    assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    assert(ninit_service_runtime_child_exited(runtime, pid, status) == 0);
    assert(ninit_service_runtime_tracked_count(runtime) == 0);
    assert(ninit_job_manager_get_info(jobs, job_id, &info) == 0);
    assert(info.state == NINIT_JOB_SUCCEEDED && info.completed_count == 1);

    /* A non-zero oneshot exit must fail the Start Job. */
    ninit_dependency_plan_destroy(plan);
    plan = NULL;
    memset(&spec, 0, sizeof(spec));
    spec.name = "oneshot-fail.service";
    spec.type = NINIT_UNIT_SERVICE;
    spec.exec_start_argv = (const char *const[]){ "/bin/false", NULL };
    spec.exec_start_argc = 1;
    spec.oneshot = 1;
    assert(ninit_unit_manager_add(units, &spec) == 0);
    assert(ninit_dependency_plan_build(units, "oneshot-fail.service", &plan) == 0);
    assert(ninit_job_manager_submit_start(jobs, plan, 1, &job_id) == 0);
    assert(ninit_service_runtime_dispatch(runtime, job_id, &pid) == 1);
    assert(ninit_service_runtime_exec_pending_at(
               runtime, 0, &pending_pid, &pending_fd) == 0);
    exec_poll.fd = pending_fd;
    exec_poll.events = POLLIN;
    exec_poll.revents = 0;
    do {
        result = poll(&exec_poll, 1, 3000);
    } while (result < 0 && errno == EINTR);
    assert(result == 1);
    assert(ninit_service_runtime_check_exec(runtime, pid) == 1);
    assert(waitpid(pid, &status, 0) == pid);
    assert(WIFEXITED(status) && WEXITSTATUS(status) == 1);
    assert(ninit_service_runtime_child_exited(runtime, pid, status) == 0);
    assert(ninit_job_manager_get_info(jobs, job_id, &info) == 0);
    assert(info.state == NINIT_JOB_FAILED && info.error_code == EIO);

    ninit_service_runtime_destroy(runtime);
    ninit_dependency_plan_destroy(plan);
    ninit_job_manager_destroy(jobs);
    ninit_unit_manager_destroy(units);
    return 0;
}
