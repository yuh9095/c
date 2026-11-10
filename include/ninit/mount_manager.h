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

enum ninit_mount_error_class {
    NINIT_MOUNT_ERROR_PERMISSION = 0,
    NINIT_MOUNT_ERROR_UNSUPPORTED,
    NINIT_MOUNT_ERROR_READ_ONLY,
    NINIT_MOUNT_ERROR_TARGET,
    NINIT_MOUNT_ERROR_CONFLICT,
    NINIT_MOUNT_ERROR_INVALID,
    NINIT_MOUNT_ERROR_IO
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
 * Apply only entries marked APPLY and still permitted by the environment
 * policy used by the caller. Before each mount, mountinfo and CAP_SYS_ADMIN
 * are rechecked to reject stale plans. The caller must pass the policy
 * derived from its current environment detection; a NULL policy is rejected. With callback == NULL,
 * production uses fsopen/fsconfig/fsmount/mount_setattr/move_mount and binds
 * the detached mount to an already-open target directory fd. Unsupported
 * kernels fail closed; there is no mount(2) pathname fallback. A non-NULL
 * callback replaces the production syscall sequence, primarily for tests.
 * Existing mounts are never replaced.
 */
struct ninit_mount_result {
    enum ninit_mount_target target;
    enum ninit_mount_action action;
    int error_number;
};

typedef int (*ninit_mount_callback)(
    const char *source, const char *target, const char *filesystem,
    unsigned long flags, const char *data, void *userdata);

int ninit_mount_manager_apply(const char *root,
                              const struct ninit_environment_policy *policy,
                              const struct ninit_mount_plan *plan,
                              ninit_mount_callback callback,
                              void *userdata);
/*
 * Apply each requested target independently. Results are written in plan
 * order; a failure for one target does not suppress attempts for later ones.
 * Returns the number of failed targets, or -1 for an invalid report request.
 */
int ninit_mount_manager_apply_report(
    const char *root, const struct ninit_environment_policy *policy,
    const struct ninit_mount_plan *plan, ninit_mount_callback callback,
    void *userdata, struct ninit_mount_result *results, size_t result_count);

enum ninit_mount_error_class ninit_mount_manager_classify_error(int error_number);
const char *ninit_mount_error_class_name(enum ninit_mount_error_class error_class);
const char *ninit_mount_action_name(enum ninit_mount_action action);
const char *ninit_mount_target_name(enum ninit_mount_target target);

#ifdef __cplusplus
}
#endif

#endif
