#define _POSIX_C_SOURCE 200809L

#include <ninit/manager_child_reaper.h>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <limits.h>
#include <stdint.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/prctl.h>

struct ninit_manager_child_reaper {
    int read_fd;
    int write_fd;
    struct sigaction previous_action;
    int previous_subreaper;
    int subreaper_changed;
};

static volatile sig_atomic_t signal_write_fd = -1;
static struct ninit_manager_child_reaper *active_reaper;

static void
manager_sigchld_handler(int signal_number)
{
    int saved_errno = errno;
    unsigned char byte = (unsigned char) signal_number;
    int fd = (int) signal_write_fd;

    if (fd >= 0)
        (void) write(fd, &byte, sizeof(byte));
    errno = saved_errno;
}

static int
set_pipe_flags(int fd)
{
    int flags = fcntl(fd, F_GETFL);
    int descriptor_flags;

    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0)
        return -1;
    descriptor_flags = fcntl(fd, F_GETFD);
    if (descriptor_flags < 0 ||
        fcntl(fd, F_SETFD, descriptor_flags | FD_CLOEXEC) < 0)
        return -1;
    return 0;
}

struct ninit_manager_child_reaper *
ninit_manager_child_reaper_create(void)
{
    struct ninit_manager_child_reaper *reaper;
    struct sigaction action;
    int pipe_fds[2] = { -1, -1 };

    if (active_reaper != NULL) {
        errno = EBUSY;
        return NULL;
    }
    reaper = calloc(1, sizeof(*reaper));
    if (reaper == NULL)
        return NULL;
    reaper->read_fd = -1;
    reaper->write_fd = -1;

    if (pipe(pipe_fds) < 0)
        goto fail;
    if (set_pipe_flags(pipe_fds[0]) < 0 ||
        set_pipe_flags(pipe_fds[1]) < 0)
        goto fail;

    /*
     * Adopt orphaned service descendants so the manager can reap them rather
     * than silently delegating them to the container or host init process.
     * Preserve the caller's prior setting for library/test embedding.
     */
    if (prctl(PR_GET_CHILD_SUBREAPER, &reaper->previous_subreaper) < 0)
        goto fail;
    if (prctl(PR_SET_CHILD_SUBREAPER, 1) < 0)
        goto fail;
    reaper->subreaper_changed = 1;

    memset(&action, 0, sizeof(action));
    action.sa_handler = manager_sigchld_handler;
    sigemptyset(&action.sa_mask);
    action.sa_flags = SA_RESTART | SA_NOCLDSTOP;

    signal_write_fd = (sig_atomic_t) pipe_fds[1];
    if (sigaction(SIGCHLD, &action, &reaper->previous_action) < 0) {
        signal_write_fd = -1;
        goto fail;
    }

    reaper->read_fd = pipe_fds[0];
    reaper->write_fd = pipe_fds[1];
    active_reaper = reaper;
    return reaper;

fail:
    {
        int saved_errno = errno;
        if (reaper->subreaper_changed)
            (void) prctl(PR_SET_CHILD_SUBREAPER,
                         reaper->previous_subreaper);
        if (pipe_fds[0] >= 0) close(pipe_fds[0]);
        if (pipe_fds[1] >= 0) close(pipe_fds[1]);
        free(reaper);
        errno = saved_errno;
        return NULL;
    }
}

int
ninit_manager_child_reaper_fd(
    const struct ninit_manager_child_reaper *reaper)
{
    if (reaper == NULL || reaper != active_reaper ||
        reaper->read_fd < 0) {
        errno = EINVAL;
        return -1;
    }
    return reaper->read_fd;
}

int
ninit_manager_child_reaper_drain(
    struct ninit_manager_child_reaper *reaper,
    ninit_manager_child_exit_callback callback,
    void *userdata)
{
    unsigned char buffer[128];
    int reaped = 0;

    if (reaper == NULL || reaper != active_reaper) {
        errno = EINVAL;
        return -1;
    }

    for (;;) {
        ssize_t bytes = read(reaper->read_fd, buffer, sizeof(buffer));
        if (bytes > 0)
            continue;
        if (bytes < 0 && errno == EINTR)
            continue;
        if (bytes < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
            break;
        if (bytes == 0)
            break;
        return -1;
    }

    /*
     * Pipe notifications may coalesce or overflow. waitpid is authoritative,
     * so always drain all waitable children after consuming the wakeup.
     */
    for (;;) {
        int status;
        pid_t pid = waitpid(-1, &status, WNOHANG);
        if (pid > 0) {
            reaped++;
            if (callback != NULL)
                callback(pid, status, userdata);
            continue;
        }
        if (pid == 0 || (pid < 0 && errno == ECHILD))
            return reaped;
        if (pid < 0 && errno == EINTR)
            continue;
        return -1;
    }
}

static int
read_child_list(struct ninit_manager_child_reaper *reaper,
                char *buffer, size_t capacity, size_t *length)
{
    char path[128];
    int fd;
    ssize_t bytes;
    int n;

    if (reaper == NULL || reaper != active_reaper ||
        buffer == NULL || capacity < 2 || length == NULL) {
        errno = EINVAL;
        return -1;
    }
    n = snprintf(path, sizeof(path), "/proc/self/task/%ld/children",
                 (long)getpid());
    if (n < 0 || (size_t)n >= sizeof(path)) {
        errno = EOVERFLOW;
        return -1;
    }
    fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return -1;
    bytes = read(fd, buffer, capacity - 1);
    {
        int saved = errno;
        close(fd);
        errno = saved;
    }
    if (bytes < 0)
        return -1;
    if ((size_t)bytes == capacity - 1) {
        errno = EOVERFLOW;
        return -1;
    }
    buffer[bytes] = '\0';
    *length = (size_t)bytes;
    return 0;
}

int
ninit_manager_child_reaper_has_children(
    struct ninit_manager_child_reaper *reaper)
{
    char buffer[65536];
    size_t length;
    size_t i;

    if (read_child_list(reaper, buffer, sizeof(buffer), &length) < 0)
        return -1;
    for (i = 0; i < length; ++i)
        if (buffer[i] != ' ' && buffer[i] != '\n' &&
            buffer[i] != '\t' && buffer[i] != '\r')
            return 1;
    return 0;
}

int
ninit_manager_child_reaper_signal_children(
    struct ninit_manager_child_reaper *reaper, int signal_number)
{
    char buffer[65536];
    size_t length, offset = 0;
    int signalled = 0;
    int first_error = 0;

    if (signal_number < 0) {
        errno = EINVAL;
        return -1;
    }
    if (read_child_list(reaper, buffer, sizeof(buffer), &length) < 0)
        return -1;
    while (offset < length) {
        char *end;
        long value;
        pid_t pid, pgid;

        while (offset < length &&
               (buffer[offset] == ' ' || buffer[offset] == '\n' ||
                buffer[offset] == '\t' || buffer[offset] == '\r'))
            ++offset;
        if (offset == length)
            break;
        errno = 0;
        value = strtol(buffer + offset, &end, 10);
        if (errno != 0 || end == buffer + offset || value <= 0 ||
            value > INT_MAX) {
            errno = EIO;
            return -1;
        }
        offset = (size_t)(end - buffer);
        pid = (pid_t)value;
        pgid = getpgid(pid);
        if (pgid > 0) {
            if (kill(-pgid, signal_number) == 0)
                ++signalled;
            else if (errno != ESRCH && first_error == 0)
                first_error = errno;
        } else if (kill(pid, signal_number) == 0) {
            ++signalled;
        } else if (errno != ESRCH && first_error == 0) {
            first_error = errno;
        }
    }
    if (first_error != 0) {
        errno = first_error;
        return -1;
    }
    return signalled;
}

void
ninit_manager_child_reaper_destroy(
    struct ninit_manager_child_reaper *reaper)
{
    sigset_t blocked;
    sigset_t previous_mask;

    if (reaper == NULL || reaper != active_reaper)
        return;

    sigemptyset(&blocked);
    sigaddset(&blocked, SIGCHLD);
    (void) sigprocmask(SIG_BLOCK, &blocked, &previous_mask);
    signal_write_fd = -1;
    (void) sigaction(SIGCHLD, &reaper->previous_action, NULL);
    (void) prctl(PR_SET_CHILD_SUBREAPER, reaper->previous_subreaper);
    active_reaper = NULL;
    if (reaper->read_fd >= 0) close(reaper->read_fd);
    if (reaper->write_fd >= 0) close(reaper->write_fd);
    free(reaper);
    (void) sigprocmask(SIG_SETMASK, &previous_mask, NULL);
}
