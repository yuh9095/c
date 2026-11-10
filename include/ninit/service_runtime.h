#ifndef NINIT_SERVICE_RUNTIME_H
#define NINIT_SERVICE_RUNTIME_H

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>
#include <ninit/job_manager.h>

#ifdef __cplusplus
extern "C" {
#endif

struct ninit_service_runtime;

struct ninit_service_runtime *ninit_service_runtime_create(
    struct ninit_job_manager *jobs);
void ninit_service_runtime_destroy(struct ninit_service_runtime *runtime);

/*
 * Dispatch one Unit from a Start Job.
 * Returns 1 when a Unit was handled, 0 when no work is currently available,
 * or -1 on error. Targets are completed immediately. Services are spawned
 * and tracked until both their exec handshake and child-exit notification
 * have been processed.
 */
int ninit_service_runtime_dispatch(struct ninit_service_runtime *runtime,
                                   uint64_t job_id,
                                   pid_t *started_pid);

/* Obtain the exec-error pipe fd for polling; -1 means no pending handshake. */
int ninit_service_runtime_exec_fd(
    const struct ninit_service_runtime *runtime, pid_t pid);

/* Process the exec handshake for a PID: 0 pending, 1 success, -1 failure. */
int ninit_service_runtime_check_exec(
    struct ninit_service_runtime *runtime, pid_t pid);

/*
 * Called from the spawning process's child-exit callback.
 * Does not call waitpid(). Unknown PIDs return -1/ENOENT.
 */
int ninit_service_runtime_child_exited(
    struct ninit_service_runtime *runtime, pid_t pid, int wait_status);

size_t ninit_service_runtime_tracked_count(
    const struct ninit_service_runtime *runtime);

#ifdef __cplusplus
}
#endif

#endif
