#define _POSIX_C_SOURCE 200809L

#include <ninit/environment.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int
make_path(char *buffer, size_t size, const char *root, const char *path)
{
    int n;

    if (root == NULL || strcmp(root, "/") == 0) {
        n = snprintf(buffer, size, "%s", path);
    } else {
        n = snprintf(buffer, size, "%s%s", root, path);
    }
    if (n < 0 || (size_t) n >= size) {
        errno = ENAMETOOLONG;
        return -1;
    }
    return 0;
}

static int
path_exists(const char *root, const char *path)
{
    char full[4096];

    if (make_path(full, sizeof(full), root, path) < 0)
        return -1;
    if (access(full, F_OK) == 0)
        return 1;
    return errno == ENOENT ? 0 : -1;
}

static int
read_first_line(const char *root, const char *path,
                char *buffer, size_t size)
{
    char full[4096];
    FILE *file;

    if (make_path(full, sizeof(full), root, path) < 0)
        return -1;
    file = fopen(full, "r");
    if (file == NULL)
        return -1;
    if (fgets(buffer, (int) size, file) == NULL) {
        int saved = ferror(file) ? EIO : ENODATA;
        fclose(file);
        errno = saved;
        return -1;
    }
    fclose(file);
    buffer[strcspn(buffer, "\r\n")] = '\0';
    return 0;
}

static enum ninit_container_type
container_from_name(const char *name)
{
    if (name == NULL || name[0] == '\0')
        return NINIT_CONTAINER_NONE;
    if (strcmp(name, "docker") == 0)
        return NINIT_CONTAINER_DOCKER;
    if (strcmp(name, "podman") == 0)
        return NINIT_CONTAINER_PODMAN;
    if (strcmp(name, "lxc") == 0 || strcmp(name, "lxc-libvirt") == 0)
        return NINIT_CONTAINER_LXC;
    if (strcmp(name, "openvz") == 0)
        return NINIT_CONTAINER_OPENVZ;
    if (strcmp(name, "systemd-nspawn") == 0)
        return NINIT_CONTAINER_SYSTEMD_NSPAWN;
    if (strcmp(name, "wsl") == 0)
        return NINIT_CONTAINER_WSL;
    if (strcmp(name, "proot") == 0)
        return NINIT_CONTAINER_PROOT;
    if (strcmp(name, "oci") == 0)
        return NINIT_CONTAINER_OCI;
    return NINIT_CONTAINER_OTHER;
}

static enum ninit_container_type
detect_cgroup(const char *root)
{
    char full[4096];
    char line[4096];
    FILE *file;
    enum ninit_container_type result = NINIT_CONTAINER_NONE;

    if (make_path(full, sizeof(full), root, "/proc/1/cgroup") < 0)
        return NINIT_CONTAINER_NONE;
    file = fopen(full, "r");
    if (file == NULL)
        return NINIT_CONTAINER_NONE;

    while (fgets(line, sizeof(line), file) != NULL) {
        if (strstr(line, "docker") != NULL)
            result = NINIT_CONTAINER_DOCKER;
        else if (strstr(line, "libpod") != NULL ||
                 strstr(line, "containers") != NULL)
            result = NINIT_CONTAINER_PODMAN;
        else if (strstr(line, "kubepods") != NULL)
            result = NINIT_CONTAINER_OTHER;
        else if (strstr(line, "lxc") != NULL)
            result = NINIT_CONTAINER_LXC;
        else if (strstr(line, "machine.slice") != NULL)
            result = NINIT_CONTAINER_SYSTEMD_NSPAWN;
        if (result != NINIT_CONTAINER_NONE)
            break;
    }
    fclose(file);
    return result;
}

const char *
ninit_container_type_name(enum ninit_container_type type)
{
    switch (type) {
    case NINIT_CONTAINER_NONE: return "none";
    case NINIT_CONTAINER_DOCKER: return "docker";
    case NINIT_CONTAINER_PODMAN: return "podman";
    case NINIT_CONTAINER_LXC: return "lxc";
    case NINIT_CONTAINER_OPENVZ: return "openvz";
    case NINIT_CONTAINER_SYSTEMD_NSPAWN: return "systemd-nspawn";
    case NINIT_CONTAINER_WSL: return "wsl";
    case NINIT_CONTAINER_PROOT: return "proot";
    case NINIT_CONTAINER_OCI: return "oci";
    case NINIT_CONTAINER_OTHER: return "other";
    }
    return "unknown";
}

static int
apply_environment_override(const char *override,
                           struct ninit_environment *environment)
{
    enum ninit_container_type type;

    if (override == NULL || override[0] == '\\0' ||
        strcmp(override, "auto") == 0)
        return 0;

    if (strcmp(override, "host") == 0 || strcmp(override, "none") == 0) {
        environment->kind = NINIT_ENVIRONMENT_HOST;
        environment->container = NINIT_CONTAINER_NONE;
    } else {
        if (strncmp(override, "container:", 10) == 0)
            override += 10;
        else if (strcmp(override, "container") == 0)
            override = "other";

        type = container_from_name(override);
        if (type == NINIT_CONTAINER_NONE)
            return -1;
        environment->kind = NINIT_ENVIRONMENT_CONTAINER;
        environment->container = type;
    }
    environment->evidence = NINIT_ENV_EVIDENCE_OVERRIDE;
    return 1;
}

int
ninit_environment_detect(const struct ninit_environment_options *options,
                         struct ninit_environment *environment)
{
    const char *root = options != NULL && options->root != NULL ?
                       options->root : "/";
    const char *container_env = options != NULL ?
                                options->container_environment : NULL;
    enum ninit_container_type type;
    char value[256];
    int proc_status;

    if (environment == NULL) {
        errno = EINVAL;
        return -1;
    }
    memset(environment, 0, sizeof(*environment));
    environment->kind = NINIT_ENVIRONMENT_UNKNOWN;
    environment->container = NINIT_CONTAINER_NONE;
    environment->is_pid1 = options != NULL && options->is_pid1;

    /* systemd's explicit runtime marker has higher precedence than heuristics. */
    if (read_first_line(root, "/run/systemd/container",
                        value, sizeof(value)) == 0 && value[0] != '\0') {
        type = container_from_name(value);
        environment->kind = NINIT_ENVIRONMENT_CONTAINER;
        environment->container = type == NINIT_CONTAINER_NONE ?
                                 NINIT_CONTAINER_OTHER : type;
        environment->evidence |= NINIT_ENV_EVIDENCE_SYSTEMD_FILE;
        return 0;
    }

    if (container_env != NULL && container_env[0] != '\0') {
        type = container_from_name(container_env);
        environment->kind = NINIT_ENVIRONMENT_CONTAINER;
        environment->container = type == NINIT_CONTAINER_NONE ?
                                 NINIT_CONTAINER_OTHER : type;
        environment->evidence |= NINIT_ENV_EVIDENCE_ENVIRONMENT;
        return 0;
    }

    if (path_exists(root, "/run/.containerenv") > 0) {
        environment->kind = NINIT_ENVIRONMENT_CONTAINER;
        environment->container = NINIT_CONTAINER_PODMAN;
        environment->evidence |= NINIT_ENV_EVIDENCE_MARKER;
        return 0;
    }
    if (path_exists(root, "/.dockerenv") > 0) {
        environment->kind = NINIT_ENVIRONMENT_CONTAINER;
        environment->container = NINIT_CONTAINER_DOCKER;
        environment->evidence |= NINIT_ENV_EVIDENCE_MARKER;
        return 0;
    }

    /* OpenRC-compatible WSL indicators. */
    if (path_exists(root, "/proc/sys/fs/binfmt_misc/WSLInterop") > 0 ||
        getenv("WSL_DISTRO_NAME") != NULL) {
        environment->kind = NINIT_ENVIRONMENT_CONTAINER;
        environment->container = NINIT_CONTAINER_WSL;
        environment->evidence |= NINIT_ENV_EVIDENCE_MARKER;
        return 0;
    }
    if (read_first_line(root, "/proc/sys/kernel/osrelease",
                        value, sizeof(value)) == 0 &&
        (strstr(value, "Microsoft") != NULL ||
         strstr(value, "microsoft") != NULL)) {
        environment->kind = NINIT_ENVIRONMENT_CONTAINER;
        environment->container = NINIT_CONTAINER_WSL;
        environment->evidence |= NINIT_ENV_EVIDENCE_MARKER;
        return 0;
    }

    /* OpenVZ's /proc/vz exists on host and guest; /proc/bc is host-only. */
    if (path_exists(root, "/proc/vz") > 0 &&
        path_exists(root, "/proc/bc") == 0) {
        environment->kind = NINIT_ENVIRONMENT_CONTAINER;
        environment->container = NINIT_CONTAINER_OPENVZ;
        environment->evidence |= NINIT_ENV_EVIDENCE_OPENVZ;
        return 0;
    }

    type = detect_cgroup(root);
    if (type != NINIT_CONTAINER_NONE) {
        environment->kind = NINIT_ENVIRONMENT_CONTAINER;
        environment->container = type;
        environment->evidence |= NINIT_ENV_EVIDENCE_CGROUP;
        return 0;
    }

    proc_status = path_exists(root, "/proc/1/cgroup");
    if (proc_status > 0) {
        struct stat pid_namespace;
        char namespace_path[4096];

        environment->proc_available = 1;
        /*
         * Linux reserves inode 0xEFFFFFFC for the initial PID namespace.
         * This mirrors systemd's final fallback for an otherwise unknown
         * container, and is only consulted when this process is PID 1.
         */
        if (environment->is_pid1 &&
            make_path(namespace_path, sizeof(namespace_path), root,
                      "/proc/1/ns/pid") == 0 &&
            stat(namespace_path, &pid_namespace) == 0 &&
            (unsigned long long) pid_namespace.st_ino != 0xEFFFFFFCULL) {
            environment->kind = NINIT_ENVIRONMENT_CONTAINER;
            environment->container = NINIT_CONTAINER_OTHER;
            environment->evidence |= NINIT_ENV_EVIDENCE_NAMESPACE;
            return 0;
        }
        environment->kind = NINIT_ENVIRONMENT_HOST;
    } else {
        proc_status = path_exists(root, "/proc/self/status");
        if (proc_status > 0) {
            environment->proc_available = 1;
            environment->kind = NINIT_ENVIRONMENT_HOST;
        }
    }
    return 0;
}

void
ninit_environment_get_policy(const struct ninit_environment *environment,
                             struct ninit_environment_policy *policy)
{
    if (policy == NULL)
        return;
    memset(policy, 0, sizeof(*policy));
    if (environment == NULL)
        return;

    if (environment->kind == NINIT_ENVIRONMENT_HOST) {
        policy->allow_host_shutdown = 1;
        policy->allow_mount_proc = 1;
        policy->allow_mount_sys = 1;
        policy->allow_mount_devtmpfs = 1;
        policy->allow_host_cgroup_management = 1;
    } else if (environment->kind == NINIT_ENVIRONMENT_CONTAINER) {
        /* Never replace container-manager-provided mounts or reboot its host. */
        policy->allow_mount_proc = 0;
        policy->allow_mount_sys = 0;
        policy->allow_mount_devtmpfs = 0;
        policy->allow_host_shutdown = 0;
        policy->allow_host_cgroup_management = 0;
    }
    /* UNKNOWN deliberately receives the most restrictive policy. */
}
