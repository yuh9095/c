#define _POSIX_C_SOURCE 200809L

#include <ninit/mount_manager.h>

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

struct callback_state {
    int calls;
    char last_target[512];
};

static int
record_mount(const char *source, const char *target, const char *filesystem,
             unsigned long flags, const char *data, void *userdata)
{
    struct callback_state *state = userdata;
    assert(source != NULL && filesystem != NULL);
    (void) flags;
    (void) data;
    state->calls++;
    snprintf(state->last_target, sizeof(state->last_target), "%s", target);
    return 0;
}

static void
mkdir_path(const char *path)
{
    assert(mkdir(path, 0700) == 0);
}

static void
write_file(const char *path, const char *contents)
{
    FILE *file = fopen(path, "w");
    assert(file != NULL);
    fputs(contents, file);
    fclose(file);
}

int
main(void)
{
    char root[] = "/tmp/ninit-mount-XXXXXX";
    char path[512];
    struct ninit_environment_policy policy;
    struct ninit_environment_capabilities caps;
    struct ninit_mount_plan plan;
    struct callback_state state = { 0, "" };
    size_t i;

    assert(mkdtemp(root) != NULL);
    snprintf(path, sizeof(path), "%s/proc", root); mkdir_path(path);
    snprintf(path, sizeof(path), "%s/proc/self", root); mkdir_path(path);
    snprintf(path, sizeof(path), "%s/proc/1", root); mkdir_path(path);
    snprintf(path, sizeof(path), "%s/proc/self/ns", root); mkdir_path(path);
    snprintf(path, sizeof(path), "%s/proc/1/ns", root); mkdir_path(path);
    snprintf(path, sizeof(path), "%s/proc/ns-mnt", root);
    write_file(path, "same mount namespace");
    {
        char target[512];
        snprintf(target, sizeof(target), "%s/proc/ns-mnt", root);
        snprintf(path, sizeof(path), "%s/proc/self/ns/mnt", root);
        assert(symlink(target, path) == 0);
        snprintf(path, sizeof(path), "%s/proc/1/ns/mnt", root);
        assert(symlink(target, path) == 0);
    }
    snprintf(path, sizeof(path), "%s/sys", root); mkdir_path(path);
    snprintf(path, sizeof(path), "%s/dev", root); mkdir_path(path);

    snprintf(path, sizeof(path), "%s/proc/self/mountinfo", root);
    write_file(path,
        "1 0 0:1 / / rw - ext4 /dev/root rw\n"
        "10 1 0:2 / /proc rw - proc proc rw\n");
    snprintf(path, sizeof(path), "%s/proc/self/status", root);
    write_file(path, "Name:\tninit\nCapEff:\t0000000000200000\n");

    memset(&policy, 0, sizeof(policy));
    policy.allow_mount_proc = 1;
    policy.allow_mount_sys = 1;
    policy.allow_mount_devtmpfs = 1;
    memset(&caps, 0, sizeof(caps));
    caps.mountinfo_available = 1;
    caps.status_available = 1;
    caps.cap_sys_admin = 1;
    caps.mount_namespace_available = 1;
    caps.mount_namespace_matches_pid1 = 1;
    caps.root_mount_found = 1;
    caps.root_mount_shared = 0;

    assert(ninit_mount_manager_plan(root, &policy, &caps, &plan) == 0);
    assert(plan.count == 3);
    assert(plan.entries[0].action == NINIT_MOUNT_SKIP);
    assert(plan.entries[1].action == NINIT_MOUNT_APPLY);
    assert(plan.entries[2].action == NINIT_MOUNT_APPLY);
    assert(ninit_mount_manager_apply(root, &plan, record_mount, &state) == 0);
    assert(state.calls == 2);
    assert(strcmp(state.last_target, "/tmp") != 0);

    /* A namespace mismatch blocks a previously valid plan. */
    {
        char other[512];
        char target[512];
        snprintf(other, sizeof(other), "%s/proc/other-ns", root);
        write_file(other, "different namespace");
        snprintf(path, sizeof(path), "%s/proc/1/ns/mnt", root);
        assert(unlink(path) == 0);
        assert(symlink(other, path) == 0);
        errno = 0;
        assert(ninit_mount_manager_apply(root, &plan, record_mount, &state) == -1);
        assert(errno == EPERM);
        assert(state.calls == 2);
        assert(unlink(path) == 0);
        snprintf(target, sizeof(target), "%s/proc/ns-mnt", root);
        assert(symlink(target, path) == 0);
        snprintf(path, sizeof(path), "%s/proc/other-ns", root);
        unlink(path);
    }
    assert(strstr(state.last_target, "/dev") != NULL);

    /* Shared root propagation must block missing mount targets. */
    snprintf(path, sizeof(path), "%s/proc/self/mountinfo", root);
    write_file(path,
        "1 0 0:1 / / rw,relatime shared:7 - ext4 /dev/root rw\n"
        "10 1 0:2 / /proc rw - proc proc rw\n");
    {
        struct ninit_environment_capabilities observed;
        assert(ninit_environment_probe_capabilities(root, &observed) == 0);
        assert(observed.root_mount_found == 1);
        assert(observed.root_mount_shared == 1);
        assert(ninit_mount_manager_plan(root, &policy, &observed, &plan) == 0);
        assert(plan.entries[0].action == NINIT_MOUNT_SKIP);
        assert(plan.entries[1].action == NINIT_MOUNT_BLOCKED);
        assert(plan.entries[2].action == NINIT_MOUNT_BLOCKED);
    }

    /* Restore a private root mount before stale-plan checks. */
    snprintf(path, sizeof(path), "%s/proc/self/mountinfo", root);
    write_file(path,
        "1 0 0:1 / / rw - ext4 /dev/root rw\n"
        "10 1 0:2 / /proc rw - proc proc rw\n");

    /* A stale plan must not mount over a mount added after planning. */
    snprintf(path, sizeof(path), "%s/proc/self/mountinfo", root);
    write_file(path,
        "1 0 0:1 / / rw - ext4 /dev/root rw\n"
        "10 1 0:2 / /proc rw - proc proc rw\n"
        "11 1 0:3 / /sys rw - tmpfs tmpfs rw\n");
    errno = 0;
    assert(ninit_mount_manager_apply(root, &plan, record_mount, &state) == -1);
    assert(errno == EBUSY);
    assert(state.calls == 2);

    /* A wrong filesystem at a requested target is a conflict, never replaced. */
    caps.cap_sys_admin = 1;
    assert(ninit_mount_manager_plan(root, &policy, &caps, &plan) == 0);
    assert(plan.entries[0].action == NINIT_MOUNT_SKIP);
    assert(plan.entries[1].action == NINIT_MOUNT_CONFLICT);

    /* Container policy preserves absent runtime mounts and does nothing. */
    memset(&policy, 0, sizeof(policy));
    write_file(path, "");
    assert(ninit_mount_manager_plan(root, &policy, &caps, &plan) == 0);
    for (i = 0; i < plan.count; i++)
        assert(plan.entries[i].action == NINIT_MOUNT_BLOCKED);

    /* Missing CAP_SYS_ADMIN blocks even host/VM policy. */
    policy.allow_mount_proc = policy.allow_mount_sys =
        policy.allow_mount_devtmpfs = 1;
    caps.cap_sys_admin = 0;
    assert(ninit_mount_manager_plan(root, &policy, &caps, &plan) == 0);
    for (i = 0; i < plan.count; i++)
        assert(plan.entries[i].action == NINIT_MOUNT_BLOCKED);

    snprintf(path, sizeof(path), "%s/proc/self/mountinfo", root); unlink(path);
    snprintf(path, sizeof(path), "%s/proc/self/status", root); unlink(path);
    snprintf(path, sizeof(path), "%s/proc/self/ns/mnt", root); unlink(path);
    snprintf(path, sizeof(path), "%s/proc/1/ns/mnt", root); unlink(path);
    snprintf(path, sizeof(path), "%s/proc/self/ns", root); rmdir(path);
    snprintf(path, sizeof(path), "%s/proc/1/ns", root); rmdir(path);
    snprintf(path, sizeof(path), "%s/proc/ns-mnt", root); unlink(path);
    snprintf(path, sizeof(path), "%s/proc/self", root); rmdir(path);
    snprintf(path, sizeof(path), "%s/proc/1", root); rmdir(path);
    snprintf(path, sizeof(path), "%s/proc", root); rmdir(path);
    snprintf(path, sizeof(path), "%s/sys", root); rmdir(path);
    snprintf(path, sizeof(path), "%s/dev", root); rmdir(path);
    assert(rmdir(root) == 0);
    return 0;
}
