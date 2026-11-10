#ifndef NINIT_MANAGER_CHILD_REAPER_H
#define NINIT_MANAGER_CHILD_REAPER_H

#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

struct ninit_manager_child_reaper;

/* One SIGCHLD self-pipe source may be active per process. */
struct ninit_manager_child_reaper *
ninit_manager_child_reaper_create(void);

/* Read end for poll(); returns -1 for an invalid object. */
int ninit_manager_child_reaper_fd(
    const struct ninit_manager_child_reaper *reaper);

/*
 * Drain signal notifications and reap every waitable child with
 * waitpid(-1, WNOHANG). The callback runs in normal process context.
 * Returns the number of children reaped, or -1 on error.
 */
typedef void (*ninit_manager_child_exit_callback)(
    pid_t pid, int wait_status, void *userdata);

int ninit_manager_child_reaper_drain(
    struct ninit_manager_child_reaper *reaper,
    ninit_manager_child_exit_callback callback,
    void *userdata);

void ninit_manager_child_reaper_destroy(
    struct ninit_manager_child_reaper *reaper);

#ifdef __cplusplus
}
#endif

#endif
