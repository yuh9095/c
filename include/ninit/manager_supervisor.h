#ifndef NINIT_MANAGER_SUPERVISOR_H
#define NINIT_MANAGER_SUPERVISOR_H

#include <time.h>
#include <ninit/manager_process.h>

#ifdef __cplusplus
extern "C" {
#endif

enum ninit_manager_supervisor_state {
    NINIT_MANAGER_STOPPED = 0,
    NINIT_MANAGER_RUNNING,
    NINIT_MANAGER_BACKOFF,
    NINIT_MANAGER_STOPPING,
    NINIT_MANAGER_FAILED
};

/*
 * The caller owns manager_path and must keep it alive for the supervisor.
 * Child reaping remains centralized: the PID 1 SIGCHLD path calls
 * child_exited() after waitpid() has reaped the reported PID.
 */
struct ninit_manager_supervisor {
    const char *manager_path;
    const char *unit_dir;
    const char *target;
    struct ninit_manager_process process;
    enum ninit_manager_supervisor_state state;
    unsigned int restart_count;
    unsigned int max_restarts;
    unsigned int base_delay_ms;
    unsigned int max_delay_ms;
    int handshake_timeout_ms;
    int last_wait_status;
    int stopping;
    struct timespec started_at;
    struct timespec restart_at;
};

int ninit_manager_supervisor_init(
    struct ninit_manager_supervisor *supervisor,
    const char *manager_path,
    unsigned int max_restarts,
    unsigned int base_delay_ms,
    unsigned int max_delay_ms,
    int handshake_timeout_ms);

int ninit_manager_supervisor_set_startup_target(
    struct ninit_manager_supervisor *supervisor,
    const char *unit_dir,
    const char *target);

int ninit_manager_supervisor_start(
    struct ninit_manager_supervisor *supervisor,
    const struct timespec *now);

int ninit_manager_supervisor_child_exited(
    struct ninit_manager_supervisor *supervisor,
    pid_t child_pid,
    int wait_status,
    const struct timespec *now);

/* Starts a due restart; otherwise leaves the current state unchanged. */
int ninit_manager_supervisor_tick(
    struct ninit_manager_supervisor *supervisor,
    const struct timespec *now);

/* Closes the private channel. The caller still reaps the child via SIGCHLD. */
void ninit_manager_supervisor_shutdown(
    struct ninit_manager_supervisor *supervisor);

#ifdef __cplusplus
}
#endif

#endif
