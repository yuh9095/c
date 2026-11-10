#ifndef NINIT_SERVICE_EXECUTOR_H
#define NINIT_SERVICE_EXECUTOR_H

#include <sys/types.h>
#include <ninit/unit.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Start a Service Unit.
 *
 * On success, returns 0, stores the child PID and a nonblocking, close-on-exec
 * error-pipe read descriptor. Read the descriptor with
 * ninit_service_executor_check_exec() until it reports completion.
 *
 * EOF means exec succeeded (the child closed the pipe during exec).
 * A child-reported errno means exec failed. This API never waits for or
 * reaps children; the process that spawned them must reap them through its child reaper.
 *
 * Target Units have no process to start and are rejected with EINVAL; their
 * successful handling belongs to the job dispatcher.
 */
int ninit_service_executor_spawn(const struct ninit_unit *unit,
                                 pid_t *pid,
                                 int *exec_error_fd);

/* Spawn an argv command with the same asynchronous exec-error handshake. */
int ninit_service_executor_spawn_argv(const char *const *argv,
                                      size_t argc,
                                      pid_t *pid,
                                      int *exec_error_fd);

/*
 * Check the exec handshake.
 * Returns 0 while pending, 1 when exec succeeded, or -1 when exec failed
 * (errno is the exec errno) or the pipe protocol failed.
 * On completion the caller owns and must close exec_error_fd.
 */
int ninit_service_executor_check_exec(int exec_error_fd);

#ifdef __cplusplus
}
#endif

#endif
