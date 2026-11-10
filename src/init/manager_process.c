#define _POSIX_C_SOURCE 200809L

#include <ninit/manager_process.h>

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <spawn.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

#include <ninit/ipc_channel.h>
#include <ninit/ipc_handshake.h>

extern char **environ;

static int
duplicate_for_child(int fd)
{
    return fcntl(fd, F_DUPFD_CLOEXEC, 10);
}

int
ninit_manager_start(const char *manager_path,
                    int handshake_timeout_ms,
                    struct ninit_manager_process *process)
{
    int sockets[2] = { -1, -1 };
    int child_fd = -1;
    posix_spawn_file_actions_t actions;
    pid_t pid;
    int error;
    char *const arguments[] = {
        (char *) manager_path, (char *) "--ipc-fd", (char *) "3", NULL
    };

    if (manager_path == NULL || process == NULL || handshake_timeout_ms < 0) {
        return NINIT_IPC_INVALID_ARGUMENT;
    }
    process->pid = -1;
    process->ipc_fd = -1;

    if (ninit_ipc_socketpair(sockets) != NINIT_IPC_OK) {
        return NINIT_IPC_IO_ERROR;
    }

    child_fd = duplicate_for_child(sockets[1]);
    if (child_fd < 0) {
        close(sockets[0]);
        close(sockets[1]);
        return NINIT_IPC_IO_ERROR;
    }

    error = posix_spawn_file_actions_init(&actions);
    if (error != 0) {
        close(child_fd);
        close(sockets[0]);
        close(sockets[1]);
        errno = error;
        return NINIT_IPC_IO_ERROR;
    }

    error = posix_spawn_file_actions_addclose(&actions, sockets[0]);
    if (error == 0) {
        error = posix_spawn_file_actions_adddup2(&actions, child_fd, 3);
    }
    if (error == 0) {
        error = posix_spawn_file_actions_addclose(&actions, child_fd);
    }
    if (error == 0) {
        error = posix_spawn(&pid, manager_path, &actions, NULL,
                            arguments, environ);
    }
    (void) posix_spawn_file_actions_destroy(&actions);
    close(child_fd);
    close(sockets[1]);

    if (error != 0) {
        close(sockets[0]);
        errno = error;
        return NINIT_IPC_IO_ERROR;
    }

    process->pid = pid;
    process->ipc_fd = sockets[0];

    error = ninit_ipc_handshake_init(process->ipc_fd, handshake_timeout_ms);
    if (error != NINIT_IPC_OK) {
        (void) kill(pid, SIGTERM);
        (void) waitpid(pid, NULL, 0);
        close(process->ipc_fd);
        process->pid = -1;
        process->ipc_fd = -1;
        return error;
    }

    return NINIT_IPC_OK;
}

void
ninit_manager_disconnect(struct ninit_manager_process *process)
{
    if (process == NULL) {
        return;
    }
    if (process->ipc_fd >= 0) {
        close(process->ipc_fd);
        process->ipc_fd = -1;
    }
}

int
ninit_manager_reap(struct ninit_manager_process *process, int *status)
{
    pid_t result;

    if (process == NULL || process->pid <= 0) {
        errno = EINVAL;
        return -1;
    }

    do {
        result = waitpid(process->pid, status, WNOHANG);
    } while (result < 0 && errno == EINTR);

    if (result == 0) {
        return 0;
    }
    if (result < 0) {
        return -1;
    }

    process->pid = -1;
    return 1;
}
