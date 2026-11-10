#define _POSIX_C_SOURCE 200809L

#include <ninit/manager_supervisor.h>
#include <ninit/pid1_runtime.h>
#include <ninit/pid1_setup.h>
#include <ninit/environment.h>

#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define DEFAULT_MANAGER_PATH "/usr/bin/ninit-manager"
#define DEFAULT_CONSOLE_PATH "/dev/console"
#define DEFAULT_UNIT_DIR "/etc/ninit/units"
#define DEFAULT_TARGET "default.target"
#define DEFAULT_ENVIRONMENT "auto"

struct init_context {
    struct ninit_manager_supervisor *supervisor;
    int stopping;
};

static int
get_monotonic_time(struct timespec *now)
{
    return clock_gettime(CLOCK_MONOTONIC, now);
}

static int
supervisor_timeout_ms(const struct ninit_manager_supervisor *supervisor,
                      const struct timespec *now)
{
    int64_t seconds;
    int64_t nanoseconds;
    int64_t milliseconds;

    if (supervisor->state != NINIT_MANAGER_BACKOFF) {
        return -1;
    }

    seconds = (int64_t) supervisor->restart_at.tv_sec -
              (int64_t) now->tv_sec;
    nanoseconds = (int64_t) supervisor->restart_at.tv_nsec -
                  (int64_t) now->tv_nsec;
    milliseconds = seconds * 1000 + nanoseconds / 1000000;
    if (nanoseconds > 0 && nanoseconds % 1000000 != 0) {
        milliseconds++;
    }
    if (milliseconds <= 0) {
        return 0;
    }
    return milliseconds > 2147483647 ? 2147483647 : (int) milliseconds;
}

static void
handle_signal(int signal_number, void *userdata)
{
    struct init_context *context = userdata;

    if (signal_number == SIGTERM || signal_number == SIGINT
#ifdef SIGPWR
        || signal_number == SIGPWR
#endif
    ) {
        context->stopping = 1;
    }
}

static void
handle_child_exit(pid_t pid, int wait_status, void *userdata)
{
    struct init_context *context = userdata;
    struct timespec now;

    if (context->supervisor->process.pid != pid) {
        /* PID 1 currently owns only the manager process. */
        return;
    }

    if (get_monotonic_time(&now) < 0) {
        now.tv_sec = 0;
        now.tv_nsec = 0;
    }
    (void) ninit_manager_supervisor_child_exited(
        context->supervisor, pid, wait_status, &now);
}

static void
usage(const char *program)
{
    fprintf(stderr,
            "Usage: %s [--console PATH] [--manager PATH] "
            "[--unit-dir DIR --target NAME] [--environment auto|host|container[:TYPE]]\n",
            program);
}

int
main(int argc, char **argv)
{
    const char *console_path = DEFAULT_CONSOLE_PATH;
    const char *manager_path = DEFAULT_MANAGER_PATH;
    const char *unit_dir = DEFAULT_UNIT_DIR;
    const char *target = DEFAULT_TARGET;
    const char *environment_override = DEFAULT_ENVIRONMENT;
    struct ninit_pid1_event_source events;
    struct ninit_environment environment;
    struct ninit_environment_options environment_options;
    struct ninit_environment_policy environment_policy;
    struct ninit_manager_supervisor supervisor;
    struct init_context context;
    struct timespec now;
    int i;
    int result = 1;
    int events_ready = 0;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--console") == 0 && i + 1 < argc) {
            console_path = argv[++i];
        } else if (strcmp(argv[i], "--manager") == 0 && i + 1 < argc) {
            manager_path = argv[++i];
        } else if (strcmp(argv[i], "--unit-dir") == 0 && i + 1 < argc) {
            unit_dir = argv[++i];
        } else if (strcmp(argv[i], "--target") == 0 && i + 1 < argc) {
            target = argv[++i];
        } else if (strcmp(argv[i], "--environment") == 0 && i + 1 < argc) {
            environment_override = argv[++i];
        } else if (strcmp(argv[i], "--help") == 0) {
            usage(argv[0]);
            return 0;
        } else {
            usage(argv[0]);
            return 2;
        }
    }

    if (!ninit_pid1_is_init()) {
        fprintf(stderr, "ninit-init: refusing to run outside PID 1\n");
        return 1;
    }

    environment_options.root = "/";
    environment_options.container_environment = getenv("container");
    environment_options.environment_override = environment_override;
    environment_options.is_pid1 = 1;
    if (ninit_environment_detect(&environment_options, &environment) < 0) {
        perror("ninit-init: detect execution environment");
        return 1;
    }
    ninit_environment_get_policy(&environment, &environment_policy);
    if (environment.kind == NINIT_ENVIRONMENT_CONTAINER) {
        fprintf(stderr, "ninit-init: container environment: %s\n",
                ninit_container_type_name(environment.container));
    } else if (environment.kind == NINIT_ENVIRONMENT_HOST) {
        fprintf(stderr, "ninit-init: host-like environment\n");
    } else {
        fprintf(stderr, "ninit-init: unknown environment; restricted policy\n");
    }

    if (ninit_pid1_setup_console(console_path) < 0) {
        perror("ninit-init: setup console");
        return 1;
    }

    if (get_monotonic_time(&now) < 0) {
        perror("ninit-init: clock_gettime");
        return 1;
    }

    if (ninit_pid1_event_source_init(&events) < 0) {
        perror("ninit-init: initialize signal events");
        return 1;
    }
    events_ready = 1;

    if (ninit_manager_supervisor_init(&supervisor, manager_path,
                                      8, 250, 30000, 5000) != 0 ||
        ninit_manager_supervisor_set_startup_target(
            &supervisor, unit_dir, target) != 0) {
        fprintf(stderr, "ninit-init: invalid manager configuration\n");
        goto out;
    }

    context.supervisor = &supervisor;
    context.stopping = 0;

    if (ninit_manager_supervisor_start(&supervisor, &now) != 0) {
        fprintf(stderr, "ninit-init: manager start failed; retry scheduled\n");
    }

    for (;;) {
        int timeout_ms;
        int wait_result;

        if (get_monotonic_time(&now) < 0) {
            perror("ninit-init: clock_gettime");
            goto shutdown;
        }

        if (context.stopping) {
            ninit_manager_supervisor_shutdown(&supervisor);
            if (supervisor.state == NINIT_MANAGER_STOPPED) {
                result = 0;
                break;
            }
        } else {
            int tick_result = ninit_manager_supervisor_tick(&supervisor, &now);
            if (tick_result != 0 &&
                supervisor.state == NINIT_MANAGER_FAILED) {
                fprintf(stderr, "ninit-init: manager restart limit reached\n");
                goto shutdown;
            }
        }

        if (supervisor.state == NINIT_MANAGER_FAILED) {
            goto shutdown;
        }

        timeout_ms = supervisor_timeout_ms(&supervisor, &now);
        wait_result = ninit_pid1_event_source_wait(
            &events, timeout_ms, handle_signal, &context,
            handle_child_exit, &context);
        if (wait_result < 0 && errno != EINTR) {
            perror("ninit-init: event loop");
            goto shutdown;
        }

        if (context.stopping &&
            supervisor.state != NINIT_MANAGER_STOPPING &&
            supervisor.state != NINIT_MANAGER_STOPPED) {
            ninit_manager_supervisor_shutdown(&supervisor);
        }

        if (context.stopping &&
            supervisor.state == NINIT_MANAGER_STOPPED) {
            result = 0;
            break;
        }
    }
    goto out;

shutdown:
    ninit_manager_supervisor_shutdown(&supervisor);
    if (supervisor.state == NINIT_MANAGER_STOPPING) {
        /* The manager exits after its IPC channel is closed; reap it here. */
        while (supervisor.state == NINIT_MANAGER_STOPPING) {
            int wait_result = ninit_pid1_event_source_wait(
                &events, -1, handle_signal, &context,
                handle_child_exit, &context);
            if (wait_result < 0 && errno != EINTR) {
                perror("ninit-init: waiting for manager shutdown");
                break;
            }
        }
    }

out:
    if (events_ready) {
        ninit_pid1_event_source_destroy(&events);
    }
    return result;
}
