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


static char *
trim_space(char *text)
{
    char *end;

    while (*text != '\0' && (*text == ' ' || *text == '\t' ||
                             *text == '\r' || *text == '\n'))
        text++;
    end = text + strlen(text);
    while (end > text && (end[-1] == ' ' || end[-1] == '\t' ||
                          end[-1] == '\r' || end[-1] == '\n'))
        *--end = '\0';
    return text;
}

int
ninit_environment_load_override(const char *root, char *buffer,
                                size_t buffer_size)
{
    char full[4096];
    char line[512];
    FILE *file;
    int found = 0;

    if (buffer == NULL || buffer_size < 5) {
        errno = EINVAL;
        return -1;
    }
    if (make_path(full, sizeof(full), root, "/etc/ninit/environment.conf") < 0)
        return -1;

    (void) snprintf(buffer, buffer_size, "auto");
    file = fopen(full, "r");
    if (file == NULL)
        return errno == ENOENT ? 0 : -1;

    while (fgets(line, sizeof(line), file) != NULL) {
        char *entry = trim_space(line);
        char *value;

        if (*entry == '\0' || *entry == '#')
            continue;
        if (strncmp(entry, "Environment", 11) != 0 ||
            (entry[11] != '=' && entry[11] != ' ' && entry[11] != '\t')) {
            fclose(file);
            errno = EINVAL;
            return -1;
        }
        value = entry + 11;
        while (*value == ' ' || *value == '\t')
            value++;
        if (*value != '=') {
            fclose(file);
            errno = EINVAL;
            return -1;
        }
        value = trim_space(value + 1);
        if (*value == '\0' || strchr(value, ' ') != NULL ||
            strchr(value, '\t') != NULL || found) {
            fclose(file);
            errno = EINVAL;
            return -1;
        }
        if (strlen(value) >= buffer_size) {
            fclose(file);
            errno = ENAMETOOLONG;
            return -1;
        }
        strcpy(buffer, value);
        found = 1;
    }

    if (ferror(file)) {
        int saved = errno != 0 ? errno : EIO;
        fclose(file);
        errno = saved;
        return -1;
    }
    fclose(file);
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

    if (override == NULL || override[0] == '\0' ||
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
        if (type == NINIT_CONTAINER_NONE ||
            (type == NINIT_CONTAINER_OTHER &&
             strcmp(override, "other") != 0))
            return -1;
        if (type == NINIT_CONTAINER_WSL) {
            environment->kind = NINIT_ENVIRONMENT_VIRTUAL_MACHINE;
            environment->container = NINIT_CONTAINER_NONE;
            environment->virtual_machine = NINIT_VM_WSL;
        } else {
            environment->kind = NINIT_ENVIRONMENT_CONTAINER;
            environment->container = type;
        }
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

    /* Administrator policy, like OpenRC rc_sys, overrides auto detection. */
    proc_status = apply_environment_override(
        options != NULL ? options->environment_override : NULL,
        environment);
    if (proc_status < 0) {
        errno = EINVAL;
        return -1;
    }
    if (proc_status > 0)
        return 0;

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
        environment->kind = NINIT_ENVIRONMENT_VIRTUAL_MACHINE;
        environment->container = NINIT_CONTAINER_NONE;
        environment->virtual_machine = NINIT_VM_WSL;
        environment->evidence |= NINIT_ENV_EVIDENCE_MARKER;
        return 0;
    }
    if (read_first_line(root, "/proc/sys/kernel/osrelease",
                        value, sizeof(value)) == 0 &&
        (strstr(value, "Microsoft") != NULL ||
         strstr(value, "microsoft") != NULL)) {
        environment->kind = NINIT_ENVIRONMENT_VIRTUAL_MACHINE;
        environment->container = NINIT_CONTAINER_NONE;
        environment->virtual_machine = NINIT_VM_WSL;
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

    proc_status = path_exists(root, "/proc/self/status");
    if (proc_status > 0)
        environment->proc_available = 1;

    /*
     * PID namespace identity is independent of cgroup layout. Containers
     * using cgroup v2, private cgroup mounts, or hidden cgroup files must
     * still be recognized from /proc/1/ns/pid when Ninit is PID 1.
     */
    if (environment->is_pid1 && environment->proc_available) {
        struct stat pid_namespace;
        char namespace_path[4096];

        if (make_path(namespace_path, sizeof(namespace_path), root,
                      "/proc/1/ns/pid") < 0 ||
            stat(namespace_path, &pid_namespace) < 0) {
            /* PID 1 without namespace visibility is not safe to call a host. */
            return 0;
        }
        environment->evidence |= NINIT_ENV_EVIDENCE_NAMESPACE;
        if ((unsigned long long) pid_namespace.st_ino != 0xEFFFFFFCULL) {
            environment->kind = NINIT_ENVIRONMENT_CONTAINER;
            environment->container = NINIT_CONTAINER_OTHER;
            return 0;
        }
    }

    if (environment->proc_available)
        environment->kind = NINIT_ENVIRONMENT_HOST;
    return 0;
}


static void
decode_mount_field(char *field)
{
    char *src = field;
    char *dst = field;

    while (*src != '\0') {
        if (src[0] == '\\' && src[1] >= '0' && src[1] <= '7' &&
            src[2] >= '0' && src[2] <= '7' &&
            src[3] >= '0' && src[3] <= '7') {
            *dst++ = (char) ((src[1] - '0') * 64 +
                             (src[2] - '0') * 8 + (src[3] - '0'));
            src += 4;
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
}

static int
read_effective_capabilities(const char *root, int *cap_admin, int *cap_boot)
{
    char full[4096];
    char *line = NULL;
    size_t capacity = 0;
    FILE *file;
    int found = 0;

    if (make_path(full, sizeof(full), root, "/proc/self/status") < 0)
        return -1;
    file = fopen(full, "r");
    if (file == NULL)
        return -1;

    while (getline(&line, &capacity, file) >= 0) {
        unsigned long long effective;
        if (strncmp(line, "CapEff:", 7) != 0)
            continue;
        if (sscanf(line + 7, "%llx", &effective) != 1) {
            errno = EINVAL;
            free(line);
            fclose(file);
            return -1;
        }
        *cap_admin = (effective & (1ULL << 21)) != 0;
        *cap_boot = (effective & (1ULL << 22)) != 0;
        found = 1;
        break;
    }
    free(line);
    fclose(file);
    if (!found) {
        errno = ENODATA;
        return -1;
    }
    return 0;
}

static void
probe_mount_namespace(const char *root,
                      struct ninit_environment_capabilities *capabilities)
{
    char self_path[4096];
    char pid1_path[4096];
    struct stat self_ns;
    struct stat pid1_ns;

    if (make_path(self_path, sizeof(self_path), root,
                  "/proc/self/ns/mnt") < 0 ||
        make_path(pid1_path, sizeof(pid1_path), root,
                  "/proc/1/ns/mnt") < 0)
        return;
    if (stat(self_path, &self_ns) < 0 || stat(pid1_path, &pid1_ns) < 0)
        return;

    capabilities->mount_namespace_available = 1;
    capabilities->mount_namespace_inode = (uint64_t) self_ns.st_ino;
    capabilities->mount_namespace_matches_pid1 =
        self_ns.st_dev == pid1_ns.st_dev && self_ns.st_ino == pid1_ns.st_ino;
}

int
ninit_environment_probe_capabilities(
    const char *root,
    struct ninit_environment_capabilities *capabilities)
{
    char full[4096];
    char *line = NULL;
    size_t capacity = 0;
    FILE *file;

    if (capabilities == NULL) {
        errno = EINVAL;
        return -1;
    }
    memset(capabilities, 0, sizeof(*capabilities));
    probe_mount_namespace(root, capabilities);

    if (make_path(full, sizeof(full), root, "/proc/self/mountinfo") == 0) {
        file = fopen(full, "r");
        if (file != NULL) {
            capabilities->mountinfo_available = 1;
            while (getline(&line, &capacity, file) >= 0) {
                char *save = NULL;
                char *token;
                char *mountpoint = NULL;
                char *separator = NULL;
                char *filesystem = NULL;
                int root_shared = 0;
                unsigned int field = 0;

                token = strtok_r(line, " ", &save);
                while (token != NULL) {
                    field++;
                    if (field == 5)
                        mountpoint = token;
                    else if (field > 6 && strncmp(token, "shared:", 7) == 0)
                        root_shared = 1;
                    if (strcmp(token, "-") == 0) {
                        separator = token;
                        filesystem = strtok_r(NULL, " ", &save);
                        break;
                    }
                    token = strtok_r(NULL, " ", &save);
                }
                if (mountpoint == NULL || separator == NULL ||
                    filesystem == NULL)
                    continue;
                mountpoint[strcspn(mountpoint, "\r\n")] = '\0';
                filesystem[strcspn(filesystem, "\r\n")] = '\0';
                decode_mount_field(mountpoint);

                if (strcmp(mountpoint, "/") == 0) {
                    capabilities->root_mount_found = 1;
                    if (root_shared)
                        capabilities->root_mount_shared = 1;
                }
                if (strcmp(mountpoint, "/proc") == 0 &&
                    strcmp(filesystem, "proc") == 0)
                    capabilities->proc_mounted = 1;
                else if (strcmp(mountpoint, "/sys") == 0 &&
                         strcmp(filesystem, "sysfs") == 0)
                    capabilities->sys_mounted = 1;
                else if (strcmp(mountpoint, "/dev") == 0 &&
                         strcmp(filesystem, "devtmpfs") == 0)
                    capabilities->devtmpfs_mounted = 1;

                if (strcmp(filesystem, "cgroup") == 0 ||
                    strcmp(filesystem, "cgroup2") == 0)
                    capabilities->cgroup_mounted = 1;
            }
            free(line);
            line = NULL;
            capacity = 0;
            fclose(file);
        }
    }

    if (read_effective_capabilities(root, &capabilities->cap_sys_admin,
                                    &capabilities->cap_sys_boot) == 0)
        capabilities->status_available = 1;
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
    } else if (environment->kind == NINIT_ENVIRONMENT_VIRTUAL_MACHINE) {
        /*
         * A VM is a complete guest OS: guest-local pseudo-filesystems may be
         * initialized, but host-level shutdown and cgroup control are not.
         */
        policy->allow_mount_proc = 1;
        policy->allow_mount_sys = 1;
        policy->allow_mount_devtmpfs = 1;
        policy->allow_host_shutdown = 0;
        policy->allow_host_cgroup_management = 0;
    }
    /* UNKNOWN deliberately receives the most restrictive policy. */
}
