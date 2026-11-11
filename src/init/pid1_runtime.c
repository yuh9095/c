#define _POSIX_C_SOURCE 200809L

#include <ninit/pid1_runtime.h>

#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/wait.h>

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
read_child_pids(pid_t **pids_out, size_t *count_out)
{
    char path[128];
    FILE *stream;
    char buffer[65536];
    size_t length;
    size_t capacity = 0;
    size_t count = 0;
    pid_t *pids = NULL;
    char *cursor;

    if (pids_out == NULL || count_out == NULL) {
        errno = EINVAL;
        return -1;
    }
    *pids_out = NULL;
    *count_out = 0;

    if (snprintf(path, sizeof(path), "/proc/self/task/%ld/children",
                 (long) getpid()) >= (int) sizeof(path)) {
        errno = ENAMETOOLONG;
        return -1;
    }
    stream = fopen(path, "r");
    if (stream == NULL)
        return -1;
    length = fread(buffer, 1, sizeof(buffer) - 1, stream);
    if (ferror(stream)) {
        int saved_errno = errno != 0 ? errno : EIO;
        fclose(stream);
        errno = saved_errno;
        return -1;
    }
    if (!feof(stream)) {
        fclose(stream);
        errno = EOVERFLOW;
        return -1;
    }
    fclose(stream);
    buffer[length] = '\0';

    cursor = buffer;
    while (*cursor != '\0') {
        char *end;
        long value;

        while (*cursor == ' ' || *cursor == '\n' ||
               *cursor == '\t' || *cursor == '\r')
            cursor++;
        if (*cursor == '\0')
            break;
        errno = 0;
        value = strtol(cursor, &end, 10);
        if (errno != 0 || end == cursor || value <= 1 || value > INT_MAX) {
            free(pids);
            errno = EPROTO;
            return -1;
        }
        if (count == capacity) {
            size_t new_capacity = capacity == 0 ? 8 : capacity * 2;
            pid_t *new_pids = realloc(pids, new_capacity * sizeof(*pids));
            if (new_pids == NULL) {
                free(pids);
                return -1;
            }
            pids = new_pids;
            capacity = new_capacity;
        }
        pids[count++] = (pid_t) value;
        cursor = end;
    }

    *pids_out = pids;
    *count_out = count;
    return 0;
}

static int
signal_children(const pid_t *pids, size_t count, int signal_number)
{
    size_t i;
    int signalled = 0;

    /*
     * Signal only the direct children listed by procfs. A child may share a
     * process group with unrelated processes, so kill(-pgid, ...) is not safe
     * for generic adopted descendants. Killing one level at a time reparents
     * the next level to PID 1; the outer loop then discovers and cleans it.
     */
    for (i = 0; i < count; i++) {
        if (kill(pids[i], signal_number) == 0) {
            signalled++;
        } else if (errno != ESRCH) {
            return -1;
        }
    }
    return signalled;
}

int
ninit_pid1_cleanup_orphaned_children(unsigned int grace_msec)
{
    struct timespec deadline;
    int kill_phase = 0;

    if (clock_gettime(CLOCK_MONOTONIC, &deadline) < 0)
        return -1;
    deadline.tv_sec += (time_t) (grace_msec / 1000U);
    deadline.tv_nsec += (long) (grace_msec % 1000U) * 1000000L;
    if (deadline.tv_nsec >= 1000000000L) {
        deadline.tv_sec++;
        deadline.tv_nsec -= 1000000000L;
    }

    for (;;) {
        pid_t *pids = NULL;
        size_t count = 0;
        struct timespec now;
        struct timespec pause_time = { 0, 50000000L };
        int reaped;

        if (read_child_pids(&pids, &count) < 0)
            return -1;
        if (count == 0) {
            free(pids);
            /* Drain any exit status that raced with the procfs snapshot. */
            if (ninit_pid1_reap_children(NULL, NULL) < 0)
                return -1;
            return 0;
        }

        if (!kill_phase) {
            if (clock_gettime(CLOCK_MONOTONIC, &now) < 0) {
                free(pids);
                return -1;
            }
            if (now.tv_sec > deadline.tv_sec ||
                (now.tv_sec == deadline.tv_sec &&
                 now.tv_nsec >= deadline.tv_nsec))
                kill_phase = 1;
        }

        if (signal_children(pids, count,
                                kill_phase ? SIGKILL : SIGTERM) < 0) {
            int saved_errno = errno;
            free(pids);
            errno = saved_errno;
            return -1;
        }
        free(pids);

        reaped = ninit_pid1_reap_children(NULL, NULL);
        if (reaped < 0)
            return -1;
        while (nanosleep(&pause_time, &pause_time) < 0 && errno == EINTR)
            ;
    }
}
