#include <ninit/manager_supervisor.h>

#include <stdint.h>
#include <string.h>

#include <ninit/ipc_protocol.h>
#include <ninit/ipc_channel.h>
#include <errno.h>

#define MANAGER_STABLE_RESET_MS 60000U

static int
timespec_compare(const struct timespec *a, const struct timespec *b)
{
    if (a->tv_sec != b->tv_sec) {
        return a->tv_sec < b->tv_sec ? -1 : 1;
    }
    if (a->tv_nsec != b->tv_nsec) {
        return a->tv_nsec < b->tv_nsec ? -1 : 1;
    }
    return 0;
}

static uint64_t
elapsed_ms(const struct timespec *start, const struct timespec *end)
{
    uint64_t seconds;
    int64_t nanoseconds;

    if (timespec_compare(end, start) < 0) {
        return 0;
    }

    seconds = (uint64_t) (end->tv_sec - start->tv_sec);
    nanoseconds = (int64_t) end->tv_nsec - (int64_t) start->tv_nsec;
    if (nanoseconds < 0) {
        seconds--;
        nanoseconds += 1000000000;
    }
    return seconds * UINT64_C(1000) +
           (uint64_t) nanoseconds / UINT64_C(1000000);
}

static void
add_ms(struct timespec *value, uint64_t milliseconds)
{
    value->tv_sec += (time_t) (milliseconds / UINT64_C(1000));
    value->tv_nsec += (long) ((milliseconds % UINT64_C(1000)) *
                              UINT64_C(1000000));
    if (value->tv_nsec >= 1000000000L) {
        value->tv_sec++;
        value->tv_nsec -= 1000000000L;
    }
}

static int
schedule_restart(struct ninit_manager_supervisor *supervisor,
                 const struct timespec *now)
{
    uint64_t delay;
    unsigned int shift = supervisor->restart_count;

    if (supervisor->stopping) {
        supervisor->state = NINIT_MANAGER_STOPPED;
        return NINIT_IPC_OK;
    }
    if (supervisor->restart_count >= supervisor->max_restarts) {
        supervisor->state = NINIT_MANAGER_FAILED;
        return NINIT_IPC_UNSUPPORTED;
    }

    if (shift > 31U) {
        shift = 31U;
    }
    delay = (uint64_t) supervisor->base_delay_ms << shift;
    if (delay > supervisor->max_delay_ms) {
        delay = supervisor->max_delay_ms;
    }

    supervisor->restart_count++;
    supervisor->restart_at = *now;
    add_ms(&supervisor->restart_at, delay);
    supervisor->state = NINIT_MANAGER_BACKOFF;
    return NINIT_IPC_OK;
}

int
ninit_manager_supervisor_init(
    struct ninit_manager_supervisor *supervisor,
    const char *manager_path,
    unsigned int max_restarts,
    unsigned int base_delay_ms,
    unsigned int max_delay_ms,
    int handshake_timeout_ms)
{
    if (supervisor == NULL || manager_path == NULL ||
        base_delay_ms == 0 || max_delay_ms < base_delay_ms ||
        handshake_timeout_ms < 0) {
        return NINIT_IPC_INVALID_ARGUMENT;
    }

    memset(supervisor, 0, sizeof(*supervisor));
    supervisor->manager_path = manager_path;
    supervisor->process.pid = -1;
    supervisor->process.ipc_fd = -1;
    supervisor->state = NINIT_MANAGER_STOPPED;
    supervisor->max_restarts = max_restarts;
    supervisor->base_delay_ms = base_delay_ms;
    supervisor->max_delay_ms = max_delay_ms;
    supervisor->handshake_timeout_ms = handshake_timeout_ms;
    return NINIT_IPC_OK;
}

int
ninit_manager_supervisor_set_startup_target(
    struct ninit_manager_supervisor *supervisor,
    const char *unit_dir,
    const char *target)
{
    if (supervisor == NULL ||
        ((unit_dir == NULL) != (target == NULL)) ||
        (unit_dir != NULL && (*unit_dir == '\0' || *target == '\0')) ||
        supervisor->state != NINIT_MANAGER_STOPPED) {
        return NINIT_IPC_INVALID_ARGUMENT;
    }
    supervisor->unit_dir = unit_dir;
    supervisor->target = target;
    return NINIT_IPC_OK;
}

int
ninit_manager_supervisor_start(
    struct ninit_manager_supervisor *supervisor,
    const struct timespec *now)
{
    int result;

    if (supervisor == NULL || now == NULL ||
        supervisor->state != NINIT_MANAGER_STOPPED) {
        return NINIT_IPC_INVALID_ARGUMENT;
    }

    /* A new explicit start also resumes supervision after a rejected shutdown. */
    supervisor->stopping = 0;
    result = ninit_manager_start_with_config(supervisor->manager_path,
                                 supervisor->unit_dir, supervisor->target,
                                 supervisor->handshake_timeout_ms,
                                 &supervisor->process);
    if (result != NINIT_IPC_OK) {
        (void) schedule_restart(supervisor, now);
        return result;
    }

    supervisor->started_at = *now;
    supervisor->state = NINIT_MANAGER_RUNNING;
    return NINIT_IPC_OK;
}

int
ninit_manager_supervisor_child_exited(
    struct ninit_manager_supervisor *supervisor,
    pid_t child_pid,
    int wait_status,
    const struct timespec *now)
{
    if (supervisor == NULL || now == NULL ||
        supervisor->process.pid <= 0 ||
        child_pid != supervisor->process.pid) {
        return NINIT_IPC_INVALID_ARGUMENT;
    }

    ninit_manager_disconnect(&supervisor->process);
    supervisor->process.pid = -1;
    supervisor->last_wait_status = wait_status;

    if (supervisor->stopping) {
        supervisor->state = NINIT_MANAGER_STOPPED;
        return NINIT_IPC_OK;
    }

    if (elapsed_ms(&supervisor->started_at, now) >=
        MANAGER_STABLE_RESET_MS) {
        supervisor->restart_count = 0;
    }

    return schedule_restart(supervisor, now);
}

int
ninit_manager_supervisor_tick(
    struct ninit_manager_supervisor *supervisor,
    const struct timespec *now)
{
    int result;

    if (supervisor == NULL || now == NULL) {
        return NINIT_IPC_INVALID_ARGUMENT;
    }
    if (supervisor->state != NINIT_MANAGER_BACKOFF ||
        timespec_compare(now, &supervisor->restart_at) < 0) {
        return NINIT_IPC_OK;
    }

    result = ninit_manager_start_with_config(supervisor->manager_path,
                                 supervisor->unit_dir, supervisor->target,
                                 supervisor->handshake_timeout_ms,
                                 &supervisor->process);
    if (result != NINIT_IPC_OK) {
        (void) schedule_restart(supervisor, now);
        return result;
    }

    supervisor->started_at = *now;
    supervisor->state = NINIT_MANAGER_RUNNING;
    return NINIT_IPC_OK;
}

int
ninit_manager_supervisor_shutdown_action(
    struct ninit_manager_supervisor *supervisor,
    enum ninit_manager_shutdown_action action)
{
    const char *name;
    struct ninit_ipc_header header;
    int result = NINIT_IPC_OK;

    if (supervisor == NULL) {
        errno = EINVAL;
        return -1;
    }
    switch (action) {
    case NINIT_MANAGER_SHUTDOWN_HALT: name = "halt"; break;
    case NINIT_MANAGER_SHUTDOWN_POWEROFF: name = "poweroff"; break;
    case NINIT_MANAGER_SHUTDOWN_REBOOT: name = "reboot"; break;
    default: errno = EINVAL; return -1;
    }

    if (supervisor->process.ipc_fd >= 0) {
        memset(&header, 0, sizeof(header));
        header.version = NINIT_IPC_VERSION;
        header.type = NINIT_IPC_MSG_SHUTDOWN;
        header.payload_size = (uint32_t) strlen(name);
        result = ninit_ipc_send(supervisor->process.ipc_fd, &header, name);
    }
    supervisor->stopping = 1;
    ninit_manager_disconnect(&supervisor->process);
    supervisor->state = supervisor->process.pid > 0
        ? NINIT_MANAGER_STOPPING : NINIT_MANAGER_STOPPED;
    if (result != NINIT_IPC_OK) {
        errno = EIO;
        return -1;
    }
    return 0;
}

void
ninit_manager_supervisor_shutdown(
    struct ninit_manager_supervisor *supervisor)
{
    if (supervisor == NULL) {
        return;
    }

    supervisor->stopping = 1;
    ninit_manager_disconnect(&supervisor->process);
    supervisor->state = supervisor->process.pid > 0
        ? NINIT_MANAGER_STOPPING : NINIT_MANAGER_STOPPED;
}
