#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <ninit/ipc_channel.h>
#include <ninit/ipc_handshake.h>
#include <ninit/job_manager.h>
#include <ninit/unit_manager.h>
#include <ninit/unit_file.h>
#include <ninit/dependency_plan.h>
#include <ninit/manager_child_reaper.h>
#include <ninit/service_runtime.h>
#include <ninit/version.h>

static void
print_usage(const char *program)
{
    printf("Usage: %s [--version|--help] --ipc-fd FD [--unit-dir DIR --target NAME] [optional shutdown.target]\n", program);
    puts("Ninit service manager (early development).");
}

static void
manager_child_exited(pid_t pid, int wait_status, void *userdata)
{
    struct ninit_service_runtime *runtime = userdata;

    if (ninit_service_runtime_child_exited(runtime, pid, wait_status) < 0 &&
        errno != ENOENT) {
        fprintf(stderr,
                "ninit-manager: failed to record child %ld exit: %s\n",
                (long) pid, strerror(errno));
    }
}

static int
stop_service_children(struct ninit_service_runtime *runtime,
                      struct ninit_manager_child_reaper *reaper,
                      struct ninit_job_manager *jobs,
                      const struct ninit_dependency_plan *plan)
{
    uint64_t stop_job_id = 0;
    int stop_submitted = 0;
    int force_kill = 0;

    if (runtime == NULL || reaper == NULL)
        return 0;
    if (ninit_service_runtime_tracked_count(runtime) == 0)
        return 0;

    /*
     * Prefer the dependency-aware Stop transaction. Services receive SIGTERM
     * in reverse dependency order and the Job completes only after reaping.
     * If no startup plan exists, retain the emergency cleanup path.
     */
    if (jobs != NULL && plan != NULL &&
        ninit_job_manager_submit_stop(jobs, plan, 4, &stop_job_id) == 0)
        stop_submitted = 1;
    else if (ninit_service_runtime_signal_all(runtime, SIGTERM) < 0)
        fprintf(stderr, "ninit-manager: SIGTERM broadcast failed: %s\n",
                strerror(errno));

    for (;;) {
        size_t count = ninit_service_runtime_exec_pending_count(runtime);
        struct pollfd *fds = calloc(count + 1, sizeof(*fds));
        pid_t *pids = count == 0 ? NULL : calloc(count, sizeof(*pids));
        size_t i;
        int result;
        int timeout_ms = force_kill ? -1 : 5000;
        struct ninit_job_info info;

        if (stop_submitted) {
            int dispatched;
            do {
                dispatched = ninit_service_runtime_dispatch(
                    runtime, stop_job_id, NULL);
            } while (dispatched > 0);
            if (dispatched < 0) {
                int dispatch_error = errno;
                fprintf(stderr,
                        "ninit-manager: Stop Job dispatch failed: %s\n",
                        strerror(dispatch_error));
                /*
                 * A failed dependency-aware transaction must not leave
                 * unrelated tracked services running until the hard timeout.
                 * Fall back to a best-effort TERM broadcast, then retain the
                 * existing bounded SIGKILL escalation.
                 */
                if (ninit_service_runtime_signal_all(runtime, SIGTERM) < 0)
                    fprintf(stderr,
                            "ninit-manager: fallback SIGTERM failed: %s\n",
                            strerror(errno));
                errno = dispatch_error;
            }
            if (ninit_job_manager_get_info(jobs, stop_job_id, &info) == 0 &&
                (info.state == NINIT_JOB_SUCCEEDED ||
                 info.state == NINIT_JOB_FAILED ||
                 info.state == NINIT_JOB_CANCELLED) &&
                ninit_service_runtime_tracked_count(runtime) == 0)
                break;
        }

        if (ninit_service_runtime_tracked_count(runtime) == 0)
            break;
        if (fds == NULL || (count != 0 && pids == NULL)) {
            free(pids);
            free(fds);
            return -1;
        }

        fds[0].fd = ninit_manager_child_reaper_fd(reaper);
        fds[0].events = POLLIN;
        for (i = 0; i < count; ++i) {
            int exec_fd;
            if (ninit_service_runtime_exec_pending_at(
                    runtime, i, &pids[i], &exec_fd) < 0) {
                free(pids);
                free(fds);
                return -1;
            }
            fds[i + 1].fd = exec_fd;
            fds[i + 1].events = POLLIN;
        }

        do {
            result = poll(fds, count + 1, timeout_ms);
        } while (result < 0 && errno == EINTR);
        if (result < 0) {
            free(pids);
            free(fds);
            return -1;
        }
        if (result == 0 && !force_kill) {
            force_kill = 1;
            fprintf(stderr,
                    "ninit-manager: service shutdown grace period expired; sending SIGKILL\n");
            if (ninit_service_runtime_signal_all(runtime, SIGKILL) < 0)
                fprintf(stderr, "ninit-manager: SIGKILL broadcast failed: %s\n",
                        strerror(errno));
        }

        for (i = 0; i < count; ++i) {
            if ((fds[i + 1].revents & (POLLIN | POLLHUP | POLLERR)) != 0) {
                int check = ninit_service_runtime_check_exec(runtime, pids[i]);
                if (check < 0 && errno != ECHILD && errno != EALREADY)
                    fprintf(stderr,
                            "ninit-manager: service %ld exec cleanup: %s\n",
                            (long) pids[i], strerror(errno));
            }
        }
        if (ninit_manager_child_reaper_drain(
                reaper, manager_child_exited, runtime) < 0) {
            free(pids);
            free(fds);
            return -1;
        }
        free(pids);
        free(fds);
    }
    return 0;
}

static int
run_shutdown_target(struct ninit_service_runtime *runtime,
                    struct ninit_manager_child_reaper *reaper,
                    struct ninit_job_manager *jobs,
                    const struct ninit_dependency_plan *plan)
{
    uint64_t job_id;
    size_t i;

    if (plan == NULL)
        return 0;
    for (i = 0; i < ninit_dependency_plan_count(plan); ++i) {
        const struct ninit_unit *unit = ninit_dependency_plan_unit(plan, i);
        if (unit == NULL ||
            (unit->type == NINIT_UNIT_SERVICE && !unit->oneshot)) {
            errno = EOPNOTSUPP;
            fprintf(stderr,
                    "ninit-manager: shutdown.target only supports oneshot services; refusing unsafe daemon start\n");
            return -1;
        }
    }
    if (ninit_job_manager_submit_start(jobs, plan, 4, &job_id) < 0)
        return -1;

    for (;;) {
        struct ninit_job_info info;
        size_t pending = ninit_service_runtime_exec_pending_count(runtime);
        struct pollfd *fds = calloc(pending + 1, sizeof(*fds));
        pid_t *pids = pending == 0 ? NULL : calloc(pending, sizeof(*pids));
        size_t j;
        int dispatched;
        int polled;

        if (fds == NULL || (pending != 0 && pids == NULL)) {
            free(pids);
            free(fds);
            return -1;
        }
        do {
            dispatched = ninit_service_runtime_dispatch(runtime, job_id, NULL);
        } while (dispatched > 0);
        if (dispatched < 0) {
            free(pids);
            free(fds);
            return -1;
        }
        if (ninit_job_manager_get_info(jobs, job_id, &info) < 0) {
            free(pids);
            free(fds);
            return -1;
        }
        if (info.state == NINIT_JOB_SUCCEEDED) {
            free(pids);
            free(fds);
            fprintf(stderr, "ninit-manager: shutdown.target completed\n");
            return 0;
        }
        if (info.state == NINIT_JOB_FAILED ||
            info.state == NINIT_JOB_CANCELLED) {
            int e = info.error_code != 0 ? info.error_code : EIO;
            free(pids);
            free(fds);
            errno = e;
            fprintf(stderr, "ninit-manager: shutdown.target failed: %s\n",
                    strerror(e));
            return -1;
        }

        fds[0].fd = ninit_manager_child_reaper_fd(reaper);
        fds[0].events = POLLIN;
        for (j = 0; j < pending; ++j) {
            int exec_fd;
            if (ninit_service_runtime_exec_pending_at(
                    runtime, j, &pids[j], &exec_fd) < 0) {
                free(pids);
                free(fds);
                return -1;
            }
            fds[j + 1].fd = exec_fd;
            fds[j + 1].events = POLLIN;
        }
        do {
            polled = poll(fds, pending + 1, -1);
        } while (polled < 0 && errno == EINTR);
        if (polled < 0) {
            free(pids);
            free(fds);
            return -1;
        }
        for (j = 0; j < pending; ++j) {
            if ((fds[j + 1].revents & (POLLIN | POLLHUP | POLLERR)) != 0) {
                int check = ninit_service_runtime_check_exec(runtime, pids[j]);
                if (check < 0 && errno != ECHILD && errno != EALREADY)
                    fprintf(stderr,
                            "ninit-manager: shutdown service %ld exec: %s\n",
                            (long) pids[j], strerror(errno));
            }
        }
        if (ninit_manager_child_reaper_drain(
                reaper, manager_child_exited, runtime) < 0) {
            free(pids);
            free(fds);
            return -1;
        }
        free(pids);
        free(fds);
    }
}

static int
run_manager(int fd, const char *unit_dir, const char *target)
{
    unsigned char buffer[NINIT_IPC_HEADER_SIZE + NINIT_IPC_MAX_PAYLOAD];
    const unsigned char *payload = NULL;
    struct ninit_ipc_header header;
    struct pollfd *descriptors;
    pid_t *exec_pids;
    struct ninit_job_manager *jobs = NULL;
    struct ninit_unit_manager *units = NULL;
    struct ninit_dependency_plan *plan = NULL;
    struct ninit_dependency_plan *shutdown_plan = NULL;
    struct ninit_service_runtime *runtime = NULL;
    struct ninit_manager_child_reaper *reaper = NULL;
    size_t pending_count;
    size_t i;
    short ipc_events;
    int result;
    int exit_code = 1;
    uint64_t startup_job_id = 0;

    jobs = ninit_job_manager_create();
    if (jobs == NULL)
        goto cleanup;
    runtime = ninit_service_runtime_create(jobs);
    if (runtime == NULL)
        goto cleanup;
    reaper = ninit_manager_child_reaper_create();
    if (reaper == NULL)
        goto cleanup;

    if ((unit_dir == NULL) != (target == NULL)) {
        errno = EINVAL;
        fprintf(stderr, "ninit-manager: --unit-dir and --target must be used together\n");
        goto cleanup;
    }
    if (unit_dir != NULL) {
        units = ninit_unit_manager_create();
        if (units == NULL)
            goto cleanup;
        if (ninit_unit_file_load_directory(units, unit_dir) < 0) {
            fprintf(stderr, "ninit-manager: cannot load Unit directory %s: %s\n",
                    unit_dir, strerror(errno));
            goto cleanup;
        }
        if (ninit_unit_manager_find(units, target) == NULL) {
            errno = ENOENT;
            fprintf(stderr, "ninit-manager: target %s not found\n", target);
            goto cleanup;
        }
        if (ninit_dependency_plan_build(units, target, &plan) < 0) {
            fprintf(stderr, "ninit-manager: cannot build target %s: %s\n",
                    target, strerror(errno));
            goto cleanup;
        }
        if (ninit_unit_manager_find(units, "shutdown.target") != NULL) {
            if (ninit_dependency_plan_build(units, "shutdown.target",
                                            &shutdown_plan) < 0) {
                fprintf(stderr, "ninit-manager: cannot build shutdown.target: %s\n",
                        strerror(errno));
                goto cleanup;
            }
        }
        if (ninit_job_manager_submit_start(jobs, plan, 4, &startup_job_id) < 0) {
            fprintf(stderr, "ninit-manager: cannot submit startup job: %s\n",
                    strerror(errno));
            goto cleanup;
        }
    }

    result = ninit_ipc_handshake_manager(fd,
                                         NINIT_IPC_HANDSHAKE_TIMEOUT_MS);
    if (result != NINIT_IPC_OK) {
        fprintf(stderr, "ninit-manager: IPC handshake failed (%d)\n", result);
        goto cleanup;
    }

    if (plan != NULL) {
        int dispatched;
        do {
            dispatched = ninit_service_runtime_dispatch(runtime, startup_job_id, NULL);
        } while (dispatched > 0);
        if (dispatched < 0)
            goto cleanup;
    }

    for (;;) {
        pending_count = ninit_service_runtime_exec_pending_count(runtime);
        descriptors = calloc(pending_count + 2, sizeof(*descriptors));
        exec_pids = pending_count == 0 ? NULL :
                    calloc(pending_count, sizeof(*exec_pids));
        if (descriptors == NULL ||
            (pending_count != 0 && exec_pids == NULL)) {
            free(exec_pids);
            free(descriptors);
            goto cleanup;
        }

        descriptors[0].fd = fd;
        descriptors[0].events = POLLIN;
        descriptors[1].fd = ninit_manager_child_reaper_fd(reaper);
        descriptors[1].events = POLLIN;

        for (i = 0; i < pending_count; ++i) {
            int exec_fd;
            if (ninit_service_runtime_exec_pending_at(
                    runtime, i, &exec_pids[i], &exec_fd) < 0) {
                free(exec_pids);
                free(descriptors);
                goto cleanup;
            }
            descriptors[i + 2].fd = exec_fd;
            descriptors[i + 2].events = POLLIN;
        }

        result = poll(descriptors, pending_count + 2, -1);
        if (result < 0 && errno == EINTR) {
            free(exec_pids);
            free(descriptors);
            continue;
        }
        if (result < 0 ||
            (descriptors[0].revents & (POLLERR | POLLNVAL)) != 0 ||
            (descriptors[1].revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
            free(exec_pids);
            free(descriptors);
            goto cleanup;
        }

        /*
         * Process exec handshakes before child-exit notifications. If a
         * short-lived child has already exited, the runtime keeps its record
         * until both events have been observed, regardless of event order.
         */
        for (i = 0; i < pending_count; ++i) {
            if ((descriptors[i + 2].revents &
                 (POLLIN | POLLHUP | POLLERR)) != 0) {
                result = ninit_service_runtime_check_exec(
                    runtime, exec_pids[i]);
                if (result < 0) {
                    fprintf(stderr,
                            "ninit-manager: service %ld exec failed: %s\n",
                            (long) exec_pids[i], strerror(errno));
                }
            }
        }

        /* SIGCHLD is a wakeup hint; waitpid drains all waitable children. */
        if (ninit_manager_child_reaper_drain(
                reaper, manager_child_exited, runtime) < 0) {
            fprintf(stderr, "ninit-manager: child reap failed: %s\n",
                    strerror(errno));
            free(exec_pids);
            free(descriptors);
            goto cleanup;
        }

        if (plan != NULL) {
            int dispatched;
            do {
                dispatched = ninit_service_runtime_dispatch(runtime, startup_job_id, NULL);
            } while (dispatched > 0);
            if (dispatched < 0) {
                free(exec_pids);
                free(descriptors);
                goto cleanup;
            }
        }

        ipc_events = descriptors[0].revents;
        free(exec_pids);
        free(descriptors);

        if ((ipc_events & POLLHUP) != 0) {
            exit_code = 0;
            break;
        }
        if ((ipc_events & POLLIN) == 0)
            continue;

        result = ninit_ipc_receive(fd, buffer, sizeof(buffer),
                                   &header, &payload);
        if (result == NINIT_IPC_CLOSED) {
            exit_code = 0;
            break;
        }
        if (result != NINIT_IPC_OK) {
            fprintf(stderr, "ninit-manager: invalid IPC frame (%d)\n", result);
            goto cleanup;
        }

        if (header.type == NINIT_IPC_MSG_PING) {
            header.type = NINIT_IPC_MSG_PONG;
            if (ninit_ipc_send(fd, &header, payload) != NINIT_IPC_OK)
                goto cleanup;
        } else {
            fprintf(stderr, "ninit-manager: unsupported IPC message %u\n",
                    (unsigned int) header.type);
        }
    }
cleanup:
    {
        int stopped = stop_service_children(runtime, reaper, jobs, plan);
        if (stopped < 0)
            fprintf(stderr, "ninit-manager: failed to stop service children: %s\n",
                    strerror(errno));
        else if (exit_code == 0 && shutdown_plan != NULL &&
                 run_shutdown_target(runtime, reaper, jobs, shutdown_plan) < 0)
            fprintf(stderr, "ninit-manager: shutdown runlevel execution failed: %s\n",
                    strerror(errno));
    }
    ninit_manager_child_reaper_destroy(reaper);
    ninit_service_runtime_destroy(runtime);
    ninit_dependency_plan_destroy(shutdown_plan);
    ninit_dependency_plan_destroy(plan);
    ninit_unit_manager_destroy(units);
    ninit_job_manager_destroy(jobs);
    return exit_code;
}

int
main(int argc, char **argv)
{
    char *end = NULL;
    const char *unit_dir = NULL;
    const char *target = NULL;
    long fd = -1;
    int i;

    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--version") == 0 && argc == 2) {
            printf("ninit-manager %s\n", NINIT_VERSION_STRING);
            return 0;
        }
        if (strcmp(argv[i], "--help") == 0 && argc == 2) {
            print_usage(argv[0]);
            return 0;
        }
        if (strcmp(argv[i], "--ipc-fd") == 0 && i + 1 < argc) {
            errno = 0;
            fd = strtol(argv[++i], &end, 10);
            if (errno != 0 || end == argv[i] || *end != '\0' ||
                fd < 0 || fd > 1048576) {
                fprintf(stderr, "ninit-manager: invalid IPC descriptor\n");
                return 2;
            }
        } else if (strcmp(argv[i], "--unit-dir") == 0 && i + 1 < argc) {
            unit_dir = argv[++i];
            if (*unit_dir == '\0') {
                fprintf(stderr, "ninit-manager: empty Unit directory\n");
                return 2;
            }
        } else if (strcmp(argv[i], "--target") == 0 && i + 1 < argc) {
            target = argv[++i];
            if (*target == '\0') {
                fprintf(stderr, "ninit-manager: empty target name\n");
                return 2;
            }
        } else {
            fprintf(stderr, "ninit-manager: unsupported or incomplete argument\n");
            print_usage(argv[0]);
            return 2;
        }
    }

    if (fd < 0 || ((unit_dir == NULL) != (target == NULL))) {
        print_usage(argv[0]);
        return 2;
    }
    return run_manager((int) fd, unit_dir, target);
}
