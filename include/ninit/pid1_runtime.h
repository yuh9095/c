#ifndef NINIT_PID1_RUNTIME_H
#define NINIT_PID1_RUNTIME_H

#include <stddef.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Return 1 when this process is PID 1 in its active PID namespace,
 * 0 otherwise. This does not identify the host PID namespace.
 */
int ninit_pid1_is_init(void);

/*
 * Reap every currently waitable child without blocking.
 * This is the sole waitpid(-1, ...) entry point. Route each callback by PID
 * to the component that owns the child.
 */
typedef void (*ninit_child_exit_callback)(pid_t pid,
                                          int wait_status,
                                          void *userdata);

int ninit_pid1_reap_children(ninit_child_exit_callback callback,
                             void *userdata);

#define NINIT_PID1_EVENT_MAX_SIGNALS 4

struct ninit_pid1_event_source {
    int read_fd;
    int write_fd;
    int signals[NINIT_PID1_EVENT_MAX_SIGNALS];
    size_t signal_count;
};

typedef void (*ninit_pid1_signal_callback)(int signal_number,
                                           void *userdata);

/*
 * Install handlers for SIGCHLD, SIGTERM, SIGINT and SIGPWR (where available)
 * and create a nonblocking self-pipe. One event source may be active per
 * process. The function is independently testable outside PID 1.
 */
int ninit_pid1_event_source_init(struct ninit_pid1_event_source *source);

int ninit_pid1_event_source_fd(
    const struct ninit_pid1_event_source *source);

/*
 * Drain queued signal numbers and invoke callback in normal process context.
 * Returns records consumed, or -1 on error. After every wakeup, call the
 * centralized child-reaper even if no SIGCHLD record was observed.
 */
int ninit_pid1_event_source_drain(
    struct ninit_pid1_event_source *source,
    ninit_pid1_signal_callback callback,
    void *userdata);

/* Restore previous signal actions and close the self-pipe. */
void ninit_pid1_event_source_destroy(
    struct ninit_pid1_event_source *source);

#ifdef __cplusplus
}
#endif

#endif
