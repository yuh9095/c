#define _GNU_SOURCE
#define _FILE_OFFSET_BITS 64

#include <ninit/mount_manager.h>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <unistd.h>

static int
has_dot_component(const char *path)
{
    const char *component;

    if (path == NULL || path[0] != '/')
        return 1;

    component = path + 1;
    while (*component != '\0') {
        const char *end = strchr(component, '/');
        size_t length = end == NULL ? strlen(component) :
                                      (size_t) (end - component);

        if ((length == 1 && component[0] == '.') ||
            (length == 2 && component[0] == '.' && component[1] == '.'))
            return 1;
        if (end == NULL)
            break;
        component = end + 1;
    }
    return 0;
}

static int
make_path(char *buffer, size_t size, const char *root, const char *path)
{
    size_t root_length;
    int n;

    if (path == NULL || path[0] != '/') {
        errno = EINVAL;
        return -1;
    }
    if (root == NULL)
        root = "/";
    if (root[0] != '/' || has_dot_component(root) ||
        has_dot_component(path)) {
        errno = EINVAL;
        return -1;
    }

    root_length = strlen(root);
    while (root_length > 1 && root[root_length - 1] == '/')
        root_length--;
    if (root_length >= size) {
        errno = ENAMETOOLONG;
        return -1;
    }

    if (root_length == 1)
        n = snprintf(buffer, size, "%s", path);
    else
        n = snprintf(buffer, size, "%.*s%s", (int) root_length, root, path);
    if (n < 0 || (size_t) n >= size) {
        errno = ENAMETOOLONG;
        return -1;
    }
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

/* Returns 1 for a mount at path, 0 if absent, and -1 on read failure. */
static int
find_mount(const char *root, const char *path, char *filesystem,
           size_t filesystem_size, unsigned int *security_options)
{
    char full[4096];
    char *line = NULL;
    size_t capacity = 0;
    FILE *file;
    int found = 0;

    if (make_path(full, sizeof(full), root, "/proc/self/mountinfo") < 0)
        return -1;
    file = fopen(full, "r");
    if (file == NULL)
        return -1;

    while (getline(&line, &capacity, file) >= 0) {
        char *save = NULL;
        char *token;
        char *mountpoint = NULL;
        char *mount_options = NULL;
        char *fstype = NULL;
        unsigned int field = 0;

        token = strtok_r(line, " ", &save);
        while (token != NULL) {
            field++;
            if (field == 5)
                mountpoint = token;
            else if (field == 6)
                mount_options = token;
            if (strcmp(token, "-") == 0) {
                fstype = strtok_r(NULL, " ", &save);
                break;
            }
            token = strtok_r(NULL, " ", &save);
        }
        if (mountpoint == NULL || mount_options == NULL || fstype == NULL)
            continue;
        mountpoint[strcspn(mountpoint, "\r\n")] = '\0';
        fstype[strcspn(fstype, "\r\n")] = '\0';
        decode_mount_field(mountpoint);
        if (strcmp(mountpoint, path) == 0) {
            size_t length = strlen(fstype);
            if (length >= filesystem_size) {
                free(line);
                fclose(file);
                errno = ENAMETOOLONG;
                return -1;
            }
            memcpy(filesystem, fstype, length + 1);
            if (security_options != NULL) {
                char *option_save = NULL;
                char *option;
                *security_options = 0;
                option = strtok_r(mount_options, ",", &option_save);
                while (option != NULL) {
                    if (strcmp(option, "nosuid") == 0)
                        *security_options |= NINIT_MOUNT_OPTION_NOSUID;
                    else if (strcmp(option, "nodev") == 0)
                        *security_options |= NINIT_MOUNT_OPTION_NODEV;
                    else if (strcmp(option, "noexec") == 0)
                        *security_options |= NINIT_MOUNT_OPTION_NOEXEC;
                    option = strtok_r(NULL, ",", &option_save);
                }
            }
            found = 1;
            break;
        }
    }

    if (ferror(file) && !found) {
        int saved = errno != 0 ? errno : EIO;
        free(line);
        fclose(file);
        errno = saved;
        return -1;
    }
    free(line);
    fclose(file);
    return found;
}

/*
 * Resolve each component relative to an already-open directory. O_NOFOLLOW
 * prevents symlink traversal during this walk; O_DIRECTORY rejects non-dirs.
 * The returned descriptor is closed before mount(2), so this is not yet a
 * race-free mountpoint binding.
 */
static int
check_mountpoint_directory(const char *root, const char *path)
{
    char full[4096];
    char *save = NULL;
    char *component;
    int directory_fd;
    int next_fd;

    if (make_path(full, sizeof(full), root, path) < 0)
        return -1;

    directory_fd = open("/", O_PATH | O_DIRECTORY | O_CLOEXEC);
    if (directory_fd < 0)
        return -1;

    component = strtok_r(full + 1, "/", &save);
    while (component != NULL) {
        struct stat st;

        next_fd = openat(directory_fd, component,
                         O_PATH | O_NOFOLLOW | O_CLOEXEC);
        if (next_fd < 0) {
            int saved_errno = errno;
            close(directory_fd);
            errno = saved_errno;
            return -1;
        }
        if (fstat(next_fd, &st) < 0) {
            int saved_errno = errno;
            close(next_fd);
            close(directory_fd);
            errno = saved_errno;
            return -1;
        }
        if (S_ISLNK(st.st_mode) || !S_ISDIR(st.st_mode)) {
            int saved_errno = S_ISLNK(st.st_mode) ? ELOOP : ENOTDIR;
            close(next_fd);
            close(directory_fd);
            errno = saved_errno;
            return 0;
        }
        close(directory_fd);
        directory_fd = next_fd;
        component = strtok_r(NULL, "/", &save);
    }

    close(directory_fd);
    return 1;
}

static int
default_mount(const char *source, const char *target, const char *filesystem,
              unsigned long flags, const char *data, void *userdata)
{
    (void) userdata;
    return mount(source, target, filesystem, flags, data);
}

static const struct ninit_mount_entry templates[NINIT_MOUNT_PLAN_COUNT] = {
    { NINIT_MOUNT_PROC, NINIT_MOUNT_SKIP, "/proc", "proc", "proc", NULL,
      MS_NOSUID | MS_NODEV | MS_NOEXEC, 0 },
    { NINIT_MOUNT_SYS, NINIT_MOUNT_SKIP, "/sys", "sysfs", "sysfs", NULL,
      MS_NOSUID | MS_NODEV | MS_NOEXEC, 0 },
    { NINIT_MOUNT_DEV, NINIT_MOUNT_SKIP, "/dev", "devtmpfs", "devtmpfs",
      "mode=0755", MS_NOSUID, 0 }
};

int
ninit_mount_manager_plan(const char *root,
                         const struct ninit_environment_policy *policy,
                         const struct ninit_environment_capabilities *caps,
                         struct ninit_mount_plan *plan)
{
    size_t i;

    if (policy == NULL || caps == NULL || plan == NULL) {
        errno = EINVAL;
        return -1;
    }
    memset(plan, 0, sizeof(*plan));
    if (!caps->mountinfo_available) {
        errno = EOPNOTSUPP;
        return -1;
    }

    for (i = 0; i < NINIT_MOUNT_PLAN_COUNT; i++) {
        struct ninit_mount_entry *entry = &plan->entries[i];
        char existing_fs[128];
        int mounted;
        int allowed;
        unsigned int existing_options = 0;

        *entry = templates[i];
        plan->count++;
        mounted = find_mount(root, entry->path, existing_fs,
                             sizeof(existing_fs), &existing_options);
        if (mounted < 0)
            return -1;
        if (mounted) {
            entry->existing_mount_options = existing_options;
            entry->action = strcmp(existing_fs, entry->filesystem) == 0 ?
                            NINIT_MOUNT_SKIP : NINIT_MOUNT_CONFLICT;
            continue;
        }

        allowed = i == NINIT_MOUNT_PROC ? policy->allow_mount_proc :
                  i == NINIT_MOUNT_SYS ? policy->allow_mount_sys :
                                         policy->allow_mount_devtmpfs;
        {
            int directory = check_mountpoint_directory(root, entry->path);
            if (directory < 0)
                return -1;
            if (directory == 0) {
                entry->action = NINIT_MOUNT_CONFLICT;
                continue;
            }
        }
        if (!allowed || !caps->status_available || !caps->cap_sys_admin ||
            !caps->mount_namespace_available ||
            !caps->mount_namespace_matches_pid1 ||
            !caps->root_mount_found || caps->root_mount_shared) {
            entry->action = NINIT_MOUNT_BLOCKED;
            continue;
        }
        entry->action = NINIT_MOUNT_APPLY;
    }
    return 0;
}

int
ninit_mount_manager_apply(const char *root,
                          const struct ninit_mount_plan *plan,
                          ninit_mount_callback callback,
                          void *userdata)
{
    struct ninit_environment_capabilities caps;
    size_t i;

    if (plan == NULL || plan->count > NINIT_MOUNT_PLAN_COUNT) {
        errno = EINVAL;
        return -1;
    }
    if (callback == NULL)
        callback = default_mount;

    for (i = 0; i < plan->count; i++) {
        const struct ninit_mount_entry *entry = &plan->entries[i];
        char existing_fs[128];
        char target[4096];
        int mounted;

        if (entry->action != NINIT_MOUNT_APPLY)
            continue;
        if (i >= NINIT_MOUNT_PLAN_COUNT ||
            entry->target != templates[i].target ||
            entry->path == NULL || entry->filesystem == NULL ||
            entry->source == NULL ||
            strcmp(entry->path, templates[i].path) != 0 ||
            strcmp(entry->filesystem, templates[i].filesystem) != 0 ||
            strcmp(entry->source, templates[i].source) != 0 ||
            entry->flags != templates[i].flags ||
            ((entry->data == NULL) != (templates[i].data == NULL)) ||
            (entry->data != NULL &&
             strcmp(entry->data, templates[i].data) != 0)) {
            errno = EINVAL;
            return -1;
        }
        if (ninit_environment_probe_capabilities(root, &caps) < 0 ||
            !caps.mountinfo_available || !caps.status_available ||
            !caps.cap_sys_admin || !caps.mount_namespace_available ||
            !caps.mount_namespace_matches_pid1 || !caps.root_mount_found ||
            caps.root_mount_shared) {
            errno = EPERM;
            return -1;
        }
        {
            int directory = check_mountpoint_directory(root, entry->path);
            if (directory < 0)
                return -1;
            if (directory == 0) {
                /* Preserve ELOOP versus ENOTDIR from the fd-relative walk. */
                return -1;
            }
        }
        mounted = find_mount(root, entry->path, existing_fs,
                             sizeof(existing_fs), NULL);
        if (mounted < 0)
            return -1;
        if (mounted) {
            errno = strcmp(existing_fs, entry->filesystem) == 0 ?
                    EALREADY : EBUSY;
            return -1;
        }
        if (make_path(target, sizeof(target), root, entry->path) < 0)
            return -1;
        if (callback(entry->source, target, entry->filesystem, entry->flags,
                     entry->data, userdata) < 0)
            return -1;
    }
    return 0;
}

const char *
ninit_mount_action_name(enum ninit_mount_action action)
{
    switch (action) {
    case NINIT_MOUNT_SKIP: return "skip";
    case NINIT_MOUNT_APPLY: return "mount";
    case NINIT_MOUNT_CONFLICT: return "conflict";
    case NINIT_MOUNT_BLOCKED: return "blocked";
    }
    return "unknown";
}

const char *
ninit_mount_target_name(enum ninit_mount_target target)
{
    switch (target) {
    case NINIT_MOUNT_PROC: return "proc";
    case NINIT_MOUNT_SYS: return "sys";
    case NINIT_MOUNT_DEV: return "dev";
    }
    return "unknown";
}
