#ifndef NINIT_ENVIRONMENT_H
#define NINIT_ENVIRONMENT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum ninit_environment_kind {
    NINIT_ENVIRONMENT_UNKNOWN = 0,
    NINIT_ENVIRONMENT_HOST,
    NINIT_ENVIRONMENT_CONTAINER,
    NINIT_ENVIRONMENT_VIRTUAL_MACHINE
};

enum ninit_virtual_machine_type {
    NINIT_VM_NONE = 0,
    NINIT_VM_WSL
};

enum ninit_container_type {
    NINIT_CONTAINER_NONE = 0,
    NINIT_CONTAINER_DOCKER,
    NINIT_CONTAINER_PODMAN,
    NINIT_CONTAINER_LXC,
    NINIT_CONTAINER_OPENVZ,
    NINIT_CONTAINER_SYSTEMD_NSPAWN,
    NINIT_CONTAINER_WSL,
    NINIT_CONTAINER_PROOT,
    NINIT_CONTAINER_OCI,
    NINIT_CONTAINER_OTHER
};

enum ninit_environment_evidence {
    NINIT_ENV_EVIDENCE_SYSTEMD_FILE = 1U << 0,
    NINIT_ENV_EVIDENCE_ENVIRONMENT  = 1U << 1,
    NINIT_ENV_EVIDENCE_MARKER       = 1U << 2,
    NINIT_ENV_EVIDENCE_CGROUP       = 1U << 3,
    NINIT_ENV_EVIDENCE_OPENVZ       = 1U << 4,
    NINIT_ENV_EVIDENCE_NAMESPACE    = 1U << 5,
    NINIT_ENV_EVIDENCE_OVERRIDE     = 1U << 6
};

struct ninit_environment {
    enum ninit_environment_kind kind;
    enum ninit_container_type container;
    enum ninit_virtual_machine_type virtual_machine;
    unsigned int evidence;
    int proc_available;
    int is_pid1;
};

/* root is normally "/" and exists primarily to make detection testable. */
struct ninit_environment_options {
    const char *root;
    const char *container_environment;
    const char *environment_override; /* auto, host, container, or container type */
    int is_pid1;
};

/*
 * Detect the execution environment using multiple independent indicators.
 * Detection is advisory: inability to inspect procfs produces UNKNOWN,
 * never an assumed host. Returns 0 or -1 with errno.
 */
int ninit_environment_detect(
    const struct ninit_environment_options *options,
    struct ninit_environment *environment);


/*
 * Read /etc/ninit/environment.conf beneath root. The file accepts one
 * Environment=VALUE assignment; a missing file selects "auto".
 * Returns 0 on success or -1 with errno on malformed/unreadable input.
 */
int ninit_environment_load_override(const char *root,
                                    char *buffer,
                                    size_t buffer_size);

const char *ninit_container_type_name(enum ninit_container_type type);

/*
 * Conservative startup permissions derived from detection. These are policy
 * decisions, not proof that a syscall will succeed; callers must still handle
 * EPERM/EBUSY and preserve existing mounts.
 */
/*
 * Read-only snapshot of the current process' mount namespace and effective
 * Linux capabilities. A zero field means either absent or not observable;
 * the corresponding *_available field distinguishes those cases.
 */
struct ninit_environment_capabilities {
    int mountinfo_available;
    int status_available;
    int proc_mounted;
    int sys_mounted;
    int devtmpfs_mounted;
    int cgroup_mounted;
    int mount_namespace_available;
    int mount_namespace_matches_pid1;
    uint64_t mount_namespace_inode;
    int cap_sys_admin;
    int cap_sys_boot;
};

int ninit_environment_probe_capabilities(
    const char *root,
    struct ninit_environment_capabilities *capabilities);

struct ninit_environment_policy {
    int allow_host_shutdown;
    int allow_mount_proc;
    int allow_mount_sys;
    int allow_mount_devtmpfs;
    int allow_host_cgroup_management;
};

void ninit_environment_get_policy(
    const struct ninit_environment *environment,
    struct ninit_environment_policy *policy);

#ifdef __cplusplus
}
#endif

#endif
