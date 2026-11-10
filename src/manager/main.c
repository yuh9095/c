#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <ninit/ipc_channel.h>
#include <ninit/ipc_handshake.h>
#include <ninit/job_manager.h>
#include <ninit/manager_child_reaper.h>
#include <ninit/service_runtime.h>
#include <ninit/version.h>

static void
print_usage(const char *program)
{
    printf("Usage: %s [--version|--help|--ipc-fd FD]\n", program);
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
run_manager(int fd)
{
    unsigned char buffer[NINIT_IPC_HEADER_SIZE + NINIT_IPC_MAX_PAYLOAD];
    const unsigned char *payload = NULL;
    struct ninit_ipc_header header;
    struct pollfd *descriptors;
    pid_t *exec_pids;
    struct ninit_job_manager *jobs = NULL;
    struct ninit_service_runtime *runtime = NULL;
    struct ninit_manager_child_reaper *reaper = NULL;
    size_t pending_count;
    size_t i;
    short ipc_events;
    int result;
    int exit_code = 1;

    jobs = ninit_job_manager_create();
    if (jobs == NULL)
        goto cleanup;
    runtime = ninit_service_runtime_create(jobs);
    if (runtime == NULL)
        goto cleanup;
    reaper = ninit_manager_child_reaper_create();
    if (reaper == NULL)
        goto cleanup;

    result = ninit_ipc_handshake_manager(fd,
                                         NINIT_IPC_HANDSHAKE_TIMEOUT_MS);
    if (result != NINIT_IPC_OK) {
        fprintf(stderr, "ninit-manager: IPC handshake failed (%d)\\n", result);
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
                            "ninit-manager: service %ld exec failed: %s\\n",
                            (long) exec_pids[i], strerror(errno));
                }
            }
        }

        /* SIGCHLD is a wakeup hint; waitpid drains all waitable children. */
        if (ninit_manager_child_reaper_drain(
                reaper, manager_child_exited, runtime) < 0) {
            fprintf(stderr, "ninit-manager: child reap failed: %s\\n",
                    strerror(errno));
            free(exec_pids);
            free(descriptors);
            goto cleanup;
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
            fprintf(stderr, "ninit-manager: invalid IPC frame (%d)\\n", result);
            goto cleanup;
        }

        if (header.type == NINIT_IPC_MSG_PING) {
            header.type = NINIT_IPC_MSG_PONG;
            if (ninit_ipc_send(fd, &header, payload) != NINIT_IPC_OK)
                goto cleanup;
        } else {
            fprintf(stderr, "ninit-manager: unsupported IPC message %u\\n",
                    (unsigned int) header.type);
        }
    }
cleanup:
    ninit_manager_child_reaper_destroy(reaper);
    ninit_service_runtime_destroy(runtime);
    ninit_job_manager_destroy(jobs);
    return exit_code;
}

int
main(int argc, char **argv)
{
    char *end = NULL;
    long fd;

    if (argc == 2 && strcmp(argv[1], "--version") == 0) {
        printf("ninit-manager %s\n", NINIT_VERSION_STRING);
        return 0;
    }
    if (argc == 1 || (argc == 2 && strcmp(argv[1], "--help") == 0)) {
        print_usage(argv[0]);
        return 0;
    }
    if (argc == 3 && strcmp(argv[1], "--ipc-fd") == 0) {
        errno = 0;
        fd = strtol(argv[2], &end, 10);
        if (errno != 0 || end == argv[2] || *end != '\0' ||
            fd < 0 || fd > 1048576) {
            fprintf(stderr, "ninit-manager: invalid IPC descriptor\n");
            return 2;
        }
        return run_manager((int) fd);
    }

    fprintf(stderr, "ninit-manager: unsupported argument\n");
    print_usage(argv[0]);
    return 2;
}
