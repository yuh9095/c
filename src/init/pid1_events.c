#define _POSIX_C_SOURCE 200809L

#include <ninit/pid1_runtime.h>

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

static volatile sig_atomic_t event_write_fd = -1;
static struct ninit_pid1_event_source *active_source;
static struct sigaction previous_actions[NINIT_PID1_EVENT_MAX_SIGNALS];

static void
pid1_signal_handler(int signal_number)
{
    int saved_errno = errno;
    int fd = (int) event_write_fd;

    if (fd >= 0) {
        (void) write(fd, &signal_number, sizeof(signal_number));
    }

    errno = saved_errno;
}

int
ninit_pid1_is_init(void)
{
    return getpid() == 1;
}

int
ninit_pid1_reap_children(ninit_child_exit_callback callback,
                         void *userdata)
{
    int reaped = 0;

    for (;;) {
        int status;
        pid_t pid = waitpid(-1, &status, WNOHANG);

        if (pid > 0) {
            reaped++;
            if (callback != NULL) {
                callback(pid, status, userdata);
            }
            continue;
        }

        if (pid == 0) {
            return reaped;
        }

        if (errno == EINTR) {
            continue;
        }

        if (errno == ECHILD) {
            return reaped;
        }

        return -1;
    }
}

static int
set_fd_flags(int fd)
{
    int flags = fcntl(fd, F_GETFL);
    int descriptor_flags;

    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        return -1;
    }

    descriptor_flags = fcntl(fd, F_GETFD);
    if (descriptor_flags < 0 ||
        fcntl(fd, F_SETFD, descriptor_flags | FD_CLOEXEC) < 0) {
        return -1;
    }

    return 0;
}

static void
build_signal_list(struct ninit_pid1_event_source *source)
{
    source->signals[source->signal_count++] = SIGCHLD;
    source->signals[source->signal_count++] = SIGTERM;
    source->signals[source->signal_count++] = SIGINT;
#ifdef SIGPWR
    source->signals[source->signal_count++] = SIGPWR;
#endif
}

int
ninit_pid1_event_source_init(struct ninit_pid1_event_source *source)
{
    int pipe_fds[2] = { -1, -1 };
    struct sigaction action;
    size_t installed = 0;
    size_t i;

    if (source == NULL || active_source != NULL) {
        errno = EINVAL;
        return -1;
    }

    memset(source, 0, sizeof(*source));
    source->read_fd = -1;
    source->write_fd = -1;
    build_signal_list(source);

    if (pipe(pipe_fds) < 0) {
        source->signal_count = 0;
        return -1;
    }
    if (set_fd_flags(pipe_fds[0]) < 0 ||
        set_fd_flags(pipe_fds[1]) < 0) {
        int saved_errno = errno;
        close(pipe_fds[0]);
        close(pipe_fds[1]);
        source->signal_count = 0;
        errno = saved_errno;
        return -1;
    }

    memset(&action, 0, sizeof(action));
    action.sa_handler = pid1_signal_handler;
    sigemptyset(&action.sa_mask);
    action.sa_flags = SA_RESTART;

    event_write_fd = (sig_atomic_t) pipe_fds[1];
    for (i = 0; i < source->signal_count; i++) {
        if (source->signals[i] == SIGCHLD) {
            action.sa_flags |= SA_NOCLDSTOP;
        } else {
            action.sa_flags &= ~SA_NOCLDSTOP;
        }

        if (sigaction(source->signals[i], &action,
                      &previous_actions[i]) < 0) {
            int saved_errno = errno;
            size_t j;

            event_write_fd = -1;
            for (j = 0; j < installed; j++) {
                (void) sigaction(source->signals[j],
                                 &previous_actions[j], NULL);
            }
            close(pipe_fds[0]);
            close(pipe_fds[1]);
            source->signal_count = 0;
            errno = saved_errno;
            return -1;
        }
        installed++;
    }

    source->read_fd = pipe_fds[0];
    source->write_fd = pipe_fds[1];
    active_source = source;
    return 0;
}

int
ninit_pid1_event_source_fd(
    const struct ninit_pid1_event_source *source)
{
    if (source == NULL) {
        errno = EINVAL;
        return -1;
    }
    return source->read_fd;
}

int
ninit_pid1_event_source_drain(
    struct ninit_pid1_event_source *source,
    ninit_pid1_signal_callback callback,
    void *userdata)
{
    int count = 0;

    if (source == NULL || source != active_source || source->read_fd < 0) {
        errno = EINVAL;
        return -1;
    }

    for (;;) {
        int signal_number;
        ssize_t bytes = read(source->read_fd, &signal_number,
                             sizeof(signal_number));

        if (bytes == (ssize_t) sizeof(signal_number)) {
            count++;
            if (callback != NULL) {
                callback(signal_number, userdata);
            }
            continue;
        }
        if (bytes == 0) {
            return count;
        }
        if (bytes < 0 && errno == EINTR) {
            continue;
        }
        if (bytes < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            return count;
        }
        if (bytes > 0) {
            errno = EIO;
        }
        return -1;
    }
}

void
ninit_pid1_event_source_destroy(
    struct ninit_pid1_event_source *source)
{
    sigset_t blocked;
    sigset_t previous_mask;
    size_t i;

    if (source == NULL || source != active_source) {
        return;
    }

    sigemptyset(&blocked);
    for (i = 0; i < source->signal_count; i++) {
        sigaddset(&blocked, source->signals[i]);
    }
    (void) sigprocmask(SIG_BLOCK, &blocked, &previous_mask);

    event_write_fd = -1;
    for (i = 0; i < source->signal_count; i++) {
        (void) sigaction(source->signals[i], &previous_actions[i], NULL);
    }

    active_source = NULL;
    if (source->read_fd >= 0) {
        close(source->read_fd);
    }
    if (source->write_fd >= 0) {
        close(source->write_fd);
    }

    source->read_fd = -1;
    source->write_fd = -1;
    source->signal_count = 0;
    (void) sigprocmask(SIG_SETMASK, &previous_mask, NULL);
}
