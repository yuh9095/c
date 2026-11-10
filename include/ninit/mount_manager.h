#ifndef NINIT_MOUNT_MANAGER_H
#define NINIT_MOUNT_MANAGER_H

#include <stddef.h>
#include <ninit/environment.h>

#ifdef __cplusplus
extern "C" {
#endif

enum ninit_mount_action {
    NINIT_MOUNT_SKIP = 0,
    NINIT_MOUNT_APPLY,
    NINIT_MOUNT_CONFLICT,
    NINIT_MOUNT_BLOCKED
};

enum ninit_mount_option {
    NINIT_MOUNT_OPTION_NOSUID = 1U << 0,
    NINIT_MOUNT_OPTION_NODEV  = 1U << 1,
    NINIT_MOUNT_OPTION_NOEXEC = 1U << 2
};

enum ninit_mount_target {
    NINIT_MOUNT_PROC = 0,
    NINIT_MOUNT_SYS,
    NINIT_MOUNT_DEV
};

struct ninit_mount_entry {
    enum ninit_mount_target target;
    enum ninit_mount_action action;
    const char *path;
    const char *filesystem;
    const char *source;
    const char *data;
    unsigned long flags;
    /* Effective VFS security options on a pre-existing mount; zero if new. */
    unsigned int existing_mount_options;
};

#define NINIT_MOUNT_PLAN_COUNT 3

struct ninit_mount_plan {
    struct ninit_mount_entry entries[NINIT_MOUNT_PLAN_COUNT];
    size_t count;
};

/* The plan is read-only and never performs mount(2). */
int ninit_mount_manager_plan(
    const char *root,
    const struct ninit_environment_policy *policy,
    const struct ninit_environment_capabilities *capabilities,
    struct ninit_mount_plan *plan);

/*
 * Apply only entries marked APPLY. Before each mount, mountinfo and
 * CAP_SYS_ADMIN are rechecked to reject stale plans. A non-NULL callback
 * replaces mount(2), primarily for tests. Existing mounts are never replaced.
 */
typedef int (*ninit_mount_callback)(
    const char *source, const char *target, const char *filesystem,
    unsigned long flags, const char *data, void *userdata);

int ninit_mount_manager_apply(const char *root,
                              const struct ninit_mount_plan *plan,
                              ninit_mount_callback callback,
                              void *userdata);

const char *ninit_mount_action_name(enum ninit_mount_action action);
const char *ninit_mount_target_name(enum ninit_mount_target target);

#ifdef __cplusplus
}
#endif

#endif
