#define _POSIX_C_SOURCE 200809L

#include <ninit/service_executor.h>

#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

static int
set_cloexec(int fd)
{
    int flags = fcntl(fd, F_GETFD);
    if (flags < 0) return -1;
    return fcntl(fd, F_SETFD, flags | FD_CLOEXEC);
}

static int
set_nonblock(int fd)
{
    int flags = fcntl(fd, F_GETFL);
    if (flags < 0) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

int
ninit_service_executor_spawn_argv(const char *const *argv, size_t argc,
                                  pid_t *pid, int *exec_error_fd)
{
    int pipe_fds[2];
    pid_t child;

    if (argv == NULL || argc == 0 || argv[0] == NULL ||
        argv[0][0] == '\0' || pid == NULL || exec_error_fd == NULL) {
        errno = EINVAL;
        return -1;
    }
    if (pipe(pipe_fds) < 0)
        return -1;
    if (set_cloexec(pipe_fds[0]) < 0 ||
        set_cloexec(pipe_fds[1]) < 0 ||
        set_nonblock(pipe_fds[0]) < 0) {
        int saved_errno = errno;
        close(pipe_fds[0]);
        close(pipe_fds[1]);
        errno = saved_errno;
        return -1;
    }

    child = fork();
    if (child < 0) {
        int saved_errno = errno;
        close(pipe_fds[0]);
        close(pipe_fds[1]);
        errno = saved_errno;
        return -1;
    }
    if (child == 0) {
        int exec_errno;
        ssize_t written;
        close(pipe_fds[0]);
        execvp(argv[0], (char *const *) argv);
        exec_errno = errno;
        do {
            written = write(pipe_fds[1], &exec_errno, sizeof(exec_errno));
        } while (written < 0 && errno == EINTR);
        _exit(127);
    }
    close(pipe_fds[1]);
    *pid = child;
    *exec_error_fd = pipe_fds[0];
    return 0;
}

int
ninit_service_executor_spawn(const struct ninit_unit *unit,
                             pid_t *pid, int *exec_error_fd)
{
    if (unit == NULL || unit->type != NINIT_UNIT_SERVICE ||
        unit->exec_start_argv == NULL || unit->exec_start_argc == 0) {
        errno = EINVAL;
        return -1;
    }
    return ninit_service_executor_spawn_argv(
        (const char *const *) unit->exec_start_argv,
        unit->exec_start_argc, pid, exec_error_fd);
}

int
ninit_service_executor_check_exec(int exec_error_fd)
{
    int child_errno;
    ssize_t bytes;

    if (exec_error_fd < 0) {
        errno = EINVAL;
        return -1;
    }
    do {
        bytes = read(exec_error_fd, &child_errno, sizeof(child_errno));
    } while (bytes < 0 && errno == EINTR);
    if (bytes == 0) return 1;
    if (bytes < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return 0;
    if (bytes < 0) return -1;
    if (bytes != (ssize_t) sizeof(child_errno)) {
        errno = EIO;
        return -1;
    }
    errno = child_errno;
    return -1;
}
