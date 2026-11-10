#define _POSIX_C_SOURCE 200809L

#include <ninit/environment.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void
make_file(const char *path, const char *contents)
{
    FILE *file = fopen(path, "w");
    assert(file != NULL);
    if (contents != NULL)
        fputs(contents, file);
    fclose(file);
}

static void
make_dir(const char *path)
{
    assert(mkdir(path, 0700) == 0);
}

int
main(void)
{
    char root[] = "/tmp/ninit-env-XXXXXX";
    char path[512];
    struct ninit_environment_options options;
    struct ninit_environment environment;
    struct ninit_environment_policy policy;
    char config_value[64];

    assert(mkdtemp(root) != NULL);
    snprintf(path, sizeof(path), "%s/proc", root); make_dir(path);
    snprintf(path, sizeof(path), "%s/proc/1", root); make_dir(path);
    snprintf(path, sizeof(path), "%s/proc/self", root); make_dir(path);
    snprintf(path, sizeof(path), "%s/run", root); make_dir(path);
    snprintf(path, sizeof(path), "%s/run/systemd", root); make_dir(path);
    snprintf(path, sizeof(path), "%s/etc", root); make_dir(path);
    snprintf(path, sizeof(path), "%s/etc/ninit", root); make_dir(path);

    assert(ninit_environment_load_override(root, config_value,
                                          sizeof(config_value)) == 0);
    assert(strcmp(config_value, "auto") == 0);
    snprintf(path, sizeof(path), "%s/etc/ninit/environment.conf", root);
    make_file(path, "# Ninit environment policy\nEnvironment=container:lxc\n");
    assert(ninit_environment_load_override(root, config_value,
                                          sizeof(config_value)) == 0);
    assert(strcmp(config_value, "container:lxc") == 0);
    make_file(path, "Environment=host\nEnvironment=docker\n");
    assert(ninit_environment_load_override(root, config_value,
                                          sizeof(config_value)) == -1);
    make_file(path, "Environment=auto\n");

    options.root = root;
    options.container_environment = NULL;
    options.environment_override = NULL;
    options.is_pid1 = 1;

    /* A visible procfs with no container evidence is host-like. */
    snprintf(path, sizeof(path), "%s/proc/1/cgroup", root);
    make_file(path, "0::/\n");
    assert(ninit_environment_detect(&options, &environment) == 0);
    assert(environment.kind == NINIT_ENVIRONMENT_HOST);
    ninit_environment_get_policy(&environment, &policy);
    assert(policy.allow_mount_proc == 1);
    assert(policy.allow_host_shutdown == 1);

    /* systemd's marker takes precedence over generic cgroup heuristics. */
    snprintf(path, sizeof(path), "%s/run/systemd/container", root);
    make_file(path, "podman\n");
    assert(ninit_environment_detect(&options, &environment) == 0);
    assert(environment.kind == NINIT_ENVIRONMENT_CONTAINER);
    assert(environment.container == NINIT_CONTAINER_PODMAN);
    assert((environment.evidence & NINIT_ENV_EVIDENCE_SYSTEMD_FILE) != 0);
    ninit_environment_get_policy(&environment, &policy);
    assert(policy.allow_mount_devtmpfs == 0);
    assert(policy.allow_host_shutdown == 0);

    /* Explicit administrator override wins over detected markers. */
    options.environment_override = "host";
    assert(ninit_environment_detect(&options, &environment) == 0);
    assert(environment.kind == NINIT_ENVIRONMENT_HOST);
    assert(environment.container == NINIT_CONTAINER_NONE);
    assert(environment.evidence == NINIT_ENV_EVIDENCE_OVERRIDE);
    options.environment_override = "container:wsl";
    assert(ninit_environment_detect(&options, &environment) == 0);
    assert(environment.kind == NINIT_ENVIRONMENT_VIRTUAL_MACHINE);
    assert(environment.virtual_machine == NINIT_VM_WSL);
    options.environment_override = "container:docker";
    assert(ninit_environment_detect(&options, &environment) == 0);
    assert(environment.kind == NINIT_ENVIRONMENT_CONTAINER);
    assert(environment.container == NINIT_CONTAINER_DOCKER);
    options.environment_override = "invalid-value";
    assert(ninit_environment_detect(&options, &environment) == -1);
    options.environment_override = NULL;


    /* OpenRC/systemd-style explicit environment hint is also recognized. */
    unlink(path);
    options.container_environment = "lxc";
    assert(ninit_environment_detect(&options, &environment) == 0);
    assert(environment.container == NINIT_CONTAINER_LXC);
    assert((environment.evidence & NINIT_ENV_EVIDENCE_ENVIRONMENT) != 0);

    /* WSL is a Linux guest environment, not a container. */
    options.container_environment = NULL;
    snprintf(path, sizeof(path), "%s/proc/sys", root); make_dir(path);
    snprintf(path, sizeof(path), "%s/proc/sys/kernel", root); make_dir(path);
    snprintf(path, sizeof(path), "%s/proc/sys/kernel/osrelease", root);
    make_file(path, "6.6.0-microsoft-standard-WSL2\\n");
    assert(ninit_environment_detect(&options, &environment) == 0);
    assert(environment.kind == NINIT_ENVIRONMENT_VIRTUAL_MACHINE);
    assert(environment.virtual_machine == NINIT_VM_WSL);
    assert(environment.container == NINIT_CONTAINER_NONE);
    ninit_environment_get_policy(&environment, &policy);
    assert(policy.allow_host_shutdown == 0);
    assert(policy.allow_host_cgroup_management == 0);

    /* Missing procfs and missing markers must not be mistaken for a host. */
    options.container_environment = NULL;
    snprintf(path, sizeof(path), "%s/proc/1/cgroup", root);
    unlink(path);
    assert(ninit_environment_detect(&options, &environment) == 0);
    assert(environment.kind == NINIT_ENVIRONMENT_UNKNOWN);
    ninit_environment_get_policy(&environment, &policy);
    assert(policy.allow_mount_proc == 0);
    assert(policy.allow_host_shutdown == 0);


    snprintf(path, sizeof(path), "%s/proc/1/cgroup", root); unlink(path);
    snprintf(path, sizeof(path), "%s/etc/ninit/environment.conf", root); unlink(path);
    snprintf(path, sizeof(path), "%s/etc/ninit", root); rmdir(path);
    snprintf(path, sizeof(path), "%s/etc", root); rmdir(path);
    snprintf(path, sizeof(path), "%s/run/systemd", root); rmdir(path);
    snprintf(path, sizeof(path), "%s/run", root); rmdir(path);
    snprintf(path, sizeof(path), "%s/proc/1", root); rmdir(path);
    snprintf(path, sizeof(path), "%s/proc/self", root); rmdir(path);
    snprintf(path, sizeof(path), "%s/proc/sys/kernel/osrelease", root); unlink(path);
    snprintf(path, sizeof(path), "%s/proc/sys/kernel", root); rmdir(path);
    snprintf(path, sizeof(path), "%s/proc/sys", root); rmdir(path);
    snprintf(path, sizeof(path), "%s/proc", root); rmdir(path);
    assert(rmdir(root) == 0);
    return 0;
}
