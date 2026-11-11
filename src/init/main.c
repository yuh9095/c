#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L

#include <ninit/manager_supervisor.h>
#include <ninit/pid1_runtime.h>
#include <ninit/pid1_setup.h>
#include <ninit/environment.h>
#include <ninit/mount_manager.h>

#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/reboot.h>
#include <unistd.h>

#define DEFAULT_MANAGER_PATH "/usr/bin/ninit-manager"
#define DEFAULT_CONSOLE_PATH "/dev/console"
#define DEFAULT_UNIT_DIR "/etc/ninit/units"
#define DEFAULT_TARGET "default.target"
#define DEFAULT_ENVIRONMENT "auto"

enum shutdown_action {
    SHUTDOWN_NONE = 0,
    SHUTDOWN_POWER_OFF,
    SHUTDOWN_RESTART
};

struct init_context {
    struct ninit_manager_supervisor *supervisor;
    int stopping;
    enum shutdown_action action;
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

    if (context->action != SHUTDOWN_NONE)
        return;

    if (signal_number == SIGINT) {
        /* Ctrl-Alt-Del style request: orderly stop followed by restart. */
        context->action = SHUTDOWN_RESTART;
        context->stopping = 1;
    } else if (signal_number == SIGTERM
#ifdef SIGPWR
               || signal_number == SIGPWR
#endif
    ) {
        context->action = SHUTDOWN_POWER_OFF;
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

static int
perform_shutdown(const struct ninit_environment_policy *policy,
                 enum shutdown_action action)
{
    struct ninit_environment_capabilities capabilities;
    int command;

    if (policy == NULL || action == SHUTDOWN_NONE) {
        errno = EINVAL;
        return -1;
    }
    memset(&capabilities, 0, sizeof(capabilities));
    if (ninit_environment_probe_capabilities("/", &capabilities) < 0)
        return -1;

    if (policy->allow_host_shutdown) {
        if (ninit_environment_authorize_host_shutdown(
                policy, &capabilities) < 0)
            return -1;
    } else if (ninit_environment_authorize_guest_shutdown(
                   policy, &capabilities) < 0) {
        return -1;
    }

    command = action == SHUTDOWN_RESTART ? RB_AUTOBOOT : RB_POWER_OFF;
    /* Match normal init shutdown ordering: services first, then sync storage. */
    sync();
    if (reboot(command) < 0)
        return -1;

    /* Linux reboot(2) is not expected to return after a successful request. */
    errno = EIO;
    return -1;
}

static void
finish_shutdown_request(struct init_context *context,
                        const struct ninit_environment_policy *policy)
{
    struct timespec now;
    int saved_errno;

    if (context == NULL || !context->stopping ||
        context->supervisor->state != NINIT_MANAGER_STOPPED)
        return;

    if (perform_shutdown(policy, context->action) == 0)
        return;

    saved_errno = errno;
    fprintf(stderr, "ninit-init: %s request denied or failed: errno=%d (%s)\n",
            context->action == SHUTDOWN_RESTART ? "restart" : "poweroff",
            saved_errno, strerror(saved_errno));

    /*
     * PID 1 must remain alive if reboot(2) is rejected. Resume service
     * supervision instead of returning from main and panicking the kernel.
     */
    context->stopping = 0;
    context->action = SHUTDOWN_NONE;
    if (get_monotonic_time(&now) < 0) {
        perror("ninit-init: clock_gettime after failed shutdown");
        return;
    }
    if (ninit_manager_supervisor_start(context->supervisor, &now) != 0)
        fprintf(stderr, "ninit-init: manager restart after failed shutdown "
                "scheduled or failed\n");
}

static void
usage(const char *program)
{
    fprintf(stderr,
            "Usage: %s [--console PATH] [--manager PATH] "
            "[--unit-dir DIR --target NAME] [--environment auto|host|container[:TYPE]]\n",
            program);
}

static void
report_mount_snapshot(const struct ninit_environment_policy *policy)
{
    struct ninit_environment_capabilities capabilities;
    struct ninit_mount_plan plan;
    size_t i;

    memset(&capabilities, 0, sizeof(capabilities));
    if (ninit_environment_probe_capabilities("/", &capabilities) < 0) {
        int saved_errno = errno;
        fprintf(stderr,
                "ninit-init: mount state refresh failed during capability "
                "probe: errno=%d (%s)\n",
                saved_errno, strerror(saved_errno));
        return;
    }
    if (!capabilities.mountinfo_available) {
        fprintf(stderr,
                "ninit-init: mount state refresh unavailable: "
                "errno=%d (%s)\n",
                EOPNOTSUPP, strerror(EOPNOTSUPP));
        return;
    }
    if (ninit_mount_manager_plan("/", policy, &capabilities, &plan) < 0) {
        int saved_errno = errno;
        fprintf(stderr,
                "ninit-init: mount state refresh failed: "
                "errno=%d (%s)\n",
                saved_errno, strerror(saved_errno));
        return;
    }

    for (i = 0; i < plan.count; i++) {
        const struct ninit_mount_entry *entry = &plan.entries[i];
        fprintf(stderr,
                "ninit-init: mount state: %s: %s",
                ninit_mount_target_name(entry->target),
                ninit_mount_action_name(entry->action));
        if (entry->action == NINIT_MOUNT_SKIP)
            fprintf(stderr, " (kernel mountinfo confirms expected filesystem)");
        fprintf(stderr, "\n");
    }
}

int
main(int argc, char **argv)
{
    const char *console_path = DEFAULT_CONSOLE_PATH;
    const char *manager_path = DEFAULT_MANAGER_PATH;
    const char *unit_dir = DEFAULT_UNIT_DIR;
    const char *target = DEFAULT_TARGET;
    const char *environment_override = DEFAULT_ENVIRONMENT;
    char configured_environment[64];
    int environment_override_set = 0;
    struct ninit_pid1_event_source events;
    struct ninit_environment environment;
    struct ninit_environment_options environment_options;
    struct ninit_environment_policy environment_policy;
    struct ninit_environment_capabilities environment_capabilities;
    struct ninit_mount_plan mount_plan;
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
            environment_override_set = 1;
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

    if (!environment_override_set) {
        if (ninit_environment_load_override("/", configured_environment,
                                           sizeof(configured_environment)) < 0) {
            perror("ninit-init: read environment configuration");
            return 1;
        }
        environment_override = configured_environment;
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
    memset(&environment_capabilities, 0, sizeof(environment_capabilities));
    if (ninit_environment_probe_capabilities(
            "/", &environment_capabilities) == 0) {
        if (ninit_environment_authorize_host_shutdown(
                &environment_policy, &environment_capabilities) == 0) {
            fprintf(stderr,
                    "ninit-init: host shutdown policy gate: authorized "
                    "(a reboot/poweroff request is still not issued here)\n");
        } else {
            int shutdown_error = errno;
            fprintf(stderr,
                    "ninit-init: host shutdown policy gate: denied "
                    "errno=%d (%s)\n",
                    shutdown_error, strerror(shutdown_error));
        }
        fprintf(stderr, "ninit-init: capability probe: mountinfo=%s status=%s "
                "CAP_SYS_ADMIN=%s CAP_SYS_BOOT=%s mount-ns=%s "
                "matches-pid1=%s inode=%llu root-mount=%s "
                "root-shared=%s root-read-only=%s\n",
                environment_capabilities.mountinfo_available ? "yes" : "no",
                environment_capabilities.status_available ? "yes" : "no",
                environment_capabilities.status_available ?
                    (environment_capabilities.cap_sys_admin ? "yes" : "no") :
                    "unknown",
                environment_capabilities.status_available ?
                    (environment_capabilities.cap_sys_boot ? "yes" : "no") :
                    "unknown",
                environment_capabilities.mount_namespace_available ? "yes" : "no",
                environment_capabilities.mount_namespace_available ?
                    (environment_capabilities.mount_namespace_matches_pid1 ?
                        "yes" : "no") : "unknown",
                (unsigned long long)
                    environment_capabilities.mount_namespace_inode,
                environment_capabilities.root_mount_found ? "yes" : "no",
                environment_capabilities.root_mount_found ?
                    (environment_capabilities.root_mount_shared ? "yes" : "no") :
                    "unknown",
                environment_capabilities.root_mount_found ?
                    (environment_capabilities.root_mount_read_only ? "yes" : "no") :
                    "unknown");
    }
    if (environment_capabilities.mountinfo_available &&
        ninit_mount_manager_plan("/", &environment_policy,
                                 &environment_capabilities, &mount_plan) == 0) {
        size_t mount_index;
        int have_mount_work = 0;

        for (mount_index = 0; mount_index < mount_plan.count; mount_index++) {
            const struct ninit_mount_entry *entry =
                &mount_plan.entries[mount_index];
            fprintf(stderr,
                    "ninit-init: mount plan: %s -> %s (%s) "
                    "existing-options=nosuid:%s,nodev:%s,noexec:%s\n",
                    entry->path, entry->filesystem,
                    ninit_mount_action_name(entry->action),
                    (entry->existing_mount_options &
                     NINIT_MOUNT_OPTION_NOSUID) ? "yes" : "no",
                    (entry->existing_mount_options &
                     NINIT_MOUNT_OPTION_NODEV) ? "yes" : "no",
                    (entry->existing_mount_options &
                     NINIT_MOUNT_OPTION_NOEXEC) ? "yes" : "no");
            if (entry->action == NINIT_MOUNT_APPLY)
                have_mount_work = 1;
        }

        /*
         * The environment policy is passed again at apply time. This is
         * intentional: a stale host plan must not authorize mounts if the
         * detected environment policy has become restrictive.
         */
        if (have_mount_work) {
            struct ninit_mount_result mount_results[NINIT_MOUNT_PLAN_COUNT];
            int failures = ninit_mount_manager_apply_report(
                "/", &environment_policy, &mount_plan, NULL, NULL,
                mount_results, NINIT_MOUNT_PLAN_COUNT);
            if (failures < 0) {
                int mount_error = errno;
                fprintf(stderr,
                        "ninit-init: mount report failed: errno=%d (%s)\n",
                        mount_error, strerror(mount_error));
            } else {
                for (mount_index = 0; mount_index < mount_plan.count;
                     mount_index++) {
                    const struct ninit_mount_result *mount_result =
                        &mount_results[mount_index];
                    if (mount_result->action == NINIT_MOUNT_APPLY &&
                        mount_result->error_number == 0) {
                        fprintf(stderr,
                                "ninit-init: mount result: %s: applied\n",
                                ninit_mount_target_name(mount_result->target));
                    } else if (mount_result->error_number != 0) {
                        int mount_error = mount_result->error_number;
                        fprintf(stderr,
                                "ninit-init: mount result: %s: failed "
                                "class=%s errno=%d (%s)\n",
                                ninit_mount_target_name(mount_result->target),
                                ninit_mount_error_class_name(
                                    ninit_mount_manager_classify_error(
                                        mount_error)),
                                mount_error, strerror(mount_error));
                    }
                }
            }
            /*
             * mountinfo is the authoritative state store. Re-read it after
             * all attempts so partial success is visible and the next PID 1
             * startup can independently reconstruct the same state.
             */
            report_mount_snapshot(&environment_policy);
        }
    } else {
        int plan_error = errno;
        fprintf(stderr,
                "ninit-init: mount plan unavailable: errno=%d (%s); "
                "no mount changes attempted\n",
                plan_error, strerror(plan_error));
    }

    if (environment.kind == NINIT_ENVIRONMENT_CONTAINER) {
        fprintf(stderr, "ninit-init: container environment: %s\n",
                ninit_container_type_name(environment.container));
    } else if (environment.kind == NINIT_ENVIRONMENT_VIRTUAL_MACHINE) {
        fprintf(stderr, "ninit-init: virtual machine guest: %s\n",
                environment.virtual_machine == NINIT_VM_WSL ? "wsl" : "unknown");
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
    context.action = SHUTDOWN_NONE;

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
            if (supervisor.state == NINIT_MANAGER_STOPPED)
                finish_shutdown_request(&context, &environment_policy);
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
            supervisor.state == NINIT_MANAGER_STOPPED)
            finish_shutdown_request(&context, &environment_policy);
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
