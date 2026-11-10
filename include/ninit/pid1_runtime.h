#ifndef NINIT_PID1_RUNTIME_H
#define NINIT_PID1_RUNTIME_H

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
 *
 * The callback is invoked once for each reaped child. PID 1 must use one
 * centralized waitpid(-1, ...) path and route each result to the component
 * that owns that child (for example, the Manager supervisor).
 *
 * Returns the number of children reaped, or -1 on an unrecoverable error.
 */
typedef void (*ninit_child_exit_callback)(pid_t pid,
                                          int wait_status,
                                          void *userdata);

int ninit_pid1_reap_children(ninit_child_exit_callback callback,
                             void *userdata);

#ifdef __cplusplus
}
#endif

#endif
