#define _POSIX_C_SOURCE 200809L

#include <ninit/environment.h>

#include <assert.h>
#include <errno.h>
#include <string.h>
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
    struct ninit_environment_capabilities shutdown_capabilities;
    char config_value[64];

    assert(mkdtemp(root) != NULL);
    snprintf(path, sizeof(path), "%s/proc", root); make_dir(path);
    snprintf(path, sizeof(path), "%s/proc/1", root); make_dir(path);
    snprintf(path, sizeof(path), "%s/proc/self", root); make_dir(path);
    snprintf(path, sizeof(path), "%s/proc/self/status", root); make_file(path, "Name:\tninit\n");
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
    options.is_pid1 = 0;

    /* Probe mountinfo and CapEff without performing privileged operations. */
    snprintf(path, sizeof(path), "%s/proc/self/mountinfo", root);
    make_file(path,
        "36 25 0:32 / /proc rw,nosuid - proc proc rw\n"
        "37 25 0:33 / /sys rw,nosuid - sysfs sysfs rw\n"
        "38 25 0:34 / /dev rw,nosuid - devtmpfs devtmpfs rw\n"
        "39 25 0:35 / /sys/fs/cgroup rw,nosuid - cgroup2 cgroup rw\n");
    snprintf(path, sizeof(path), "%s/proc/self/status", root);
    make_file(path, "Name:\tninit\nCapEff:\t0000000000600000\n");
    {
        struct ninit_environment_capabilities capabilities;
        assert(ninit_environment_probe_capabilities(root, &capabilities) == 0);
        assert(capabilities.mountinfo_available == 1);
        assert(capabilities.status_available == 1);
        assert(capabilities.proc_mounted == 1);
        assert(capabilities.sys_mounted == 1);
        assert(capabilities.devtmpfs_mounted == 1);
        assert(capabilities.cgroup_mounted == 1);
        assert(capabilities.cap_sys_admin == 1);
        assert(capabilities.cap_sys_boot == 1);
    }

    /*
     * A visible procfs and an ordinary cgroup root do not prove that this
     * process is in the host's initial namespaces. Auto detection stays
     * UNKNOWN for non-PID-1 callers until positive evidence is available.
     */
    snprintf(path, sizeof(path), "%s/proc/1/cgroup", root);
    make_file(path, "0::/\n");
    assert(ninit_environment_detect(&options, &environment) == 0);
    assert(environment.kind == NINIT_ENVIRONMENT_UNKNOWN);
    ninit_environment_get_policy(&environment, &policy);
    assert(policy.allow_mount_proc == 0);
    assert(policy.allow_host_shutdown == 0);

    /* systemd does not infer a container from cgroup path names. */
    make_file(path, "0::/system.slice/docker-0123456789.scope\n");
    assert(ninit_environment_detect(&options, &environment) == 0);
    assert(environment.kind == NINIT_ENVIRONMENT_UNKNOWN);

    /* Proot is detected through the process named by TracerPid. */
    snprintf(path, sizeof(path), "%s/proc/123", root); make_dir(path);
    snprintf(path, sizeof(path), "%s/proc/123/comm", root);
    make_file(path, "proot\n");
    snprintf(path, sizeof(path), "%s/proc/self/status", root);
    make_file(path, "Name:\tninit\nTracerPid:\t123\n");
    assert(ninit_environment_detect(&options, &environment) == 0);
    assert(environment.kind == NINIT_ENVIRONMENT_CONTAINER);
    assert(environment.container == NINIT_CONTAINER_PROOT);
    /* A tracer with a different command must not be classified as Proot. */
    snprintf(path, sizeof(path), "%s/proc/123/comm", root);
    make_file(path, "strace\n");
    snprintf(path, sizeof(path), "%s/proc/self/status", root);
    make_file(path, "Name:\tninit\nTracerPid:\t123\n");
    assert(ninit_environment_detect(&options, &environment) == 0);
    assert(environment.kind == NINIT_ENVIRONMENT_UNKNOWN);
    /* Malformed and zero tracer IDs are not positive evidence. */
    snprintf(path, sizeof(path), "%s/proc/123/comm", root);
    make_file(path, "proot\n");
    snprintf(path, sizeof(path), "%s/proc/self/status", root);
    make_file(path, "Name:\tninit\nTracerPid:\t0\n");
    assert(ninit_environment_detect(&options, &environment) == 0);
    assert(environment.kind == NINIT_ENVIRONMENT_UNKNOWN);
    make_file(path, "Name:\tninit\nTracerPid:\tinvalid\n");
    assert(ninit_environment_detect(&options, &environment) == 0);
    assert(environment.kind == NINIT_ENVIRONMENT_UNKNOWN);
    snprintf(path, sizeof(path), "%s/proc/123/comm", root); unlink(path);
    snprintf(path, sizeof(path), "%s/proc/123", root); rmdir(path);
    snprintf(path, sizeof(path), "%s/proc/self/status", root);
    make_file(path, "Name:\tninit\n");


    /* The inherited container= variable is authoritative only for PID 1. */
    options.container_environment = "podman";
    assert(ninit_environment_detect(&options, &environment) == 0);
    assert(environment.kind == NINIT_ENVIRONMENT_UNKNOWN);
    options.is_pid1 = 1;
    assert(ninit_environment_detect(&options, &environment) == 0);
    assert(environment.kind == NINIT_ENVIRONMENT_CONTAINER);
    assert(environment.container == NINIT_CONTAINER_PODMAN);
    /* PID-1's generic OCI hint is refined by the Podman marker. */
    options.container_environment = "oci";
    snprintf(path, sizeof(path), "%s/run/.containerenv", root);
    make_file(path, "");
    assert(ninit_environment_detect(&options, &environment) == 0);
    assert(environment.container == NINIT_CONTAINER_PODMAN);
    unlink(path);
    options.is_pid1 = 0;
    options.container_environment = NULL;

    /* Container manager identification file follows systemd's ordering. */
    snprintf(path, sizeof(path), "%s/run/host", root); make_dir(path);
    snprintf(path, sizeof(path), "%s/run/host/container-manager", root);
    make_file(path, "oci\n");
    options.is_pid1 = 1;
    options.container_environment = "lxc";
    assert(ninit_environment_detect(&options, &environment) == 0);
    assert(environment.kind == NINIT_ENVIRONMENT_CONTAINER);
    assert(environment.container == NINIT_CONTAINER_OTHER);
    options.container_environment = NULL;
    snprintf(path, sizeof(path), "%s/run/.containerenv", root);
    make_file(path, "");
    assert(ninit_environment_detect(&options, &environment) == 0);
    assert(environment.container == NINIT_CONTAINER_PODMAN);
    unlink(path);
    snprintf(path, sizeof(path), "%s/run/host/container-manager", root);
    unlink(path);
    snprintf(path, sizeof(path), "%s/run/host", root); rmdir(path);
    options.is_pid1 = 0;

    /* Runtime marker files identify their respective container runtimes. */
    snprintf(path, sizeof(path), "%s/run/.containerenv", root);
    make_file(path, "");
    assert(ninit_environment_detect(&options, &environment) == 0);
    assert(environment.kind == NINIT_ENVIRONMENT_CONTAINER);
    assert(environment.container == NINIT_CONTAINER_PODMAN);
    unlink(path);
    snprintf(path, sizeof(path), "%s/.dockerenv", root);
    make_file(path, "");
    assert(ninit_environment_detect(&options, &environment) == 0);
    assert(environment.kind == NINIT_ENVIRONMENT_CONTAINER);
    assert(environment.container == NINIT_CONTAINER_DOCKER);
    unlink(path);

    /* An empty PID-1 container= value is authoritative: ignore image markers. */
    snprintf(path, sizeof(path), "%s/.dockerenv", root);
    make_file(path, "");
    options.is_pid1 = 1;
    options.container_environment = "";
    assert(ninit_environment_detect(&options, &environment) == 0);
    assert(environment.kind == NINIT_ENVIRONMENT_UNKNOWN);
    options.container_environment = NULL;
    assert(ninit_environment_detect(&options, &environment) == 0);
    assert(environment.container == NINIT_CONTAINER_DOCKER);
    unlink(path);
    options.is_pid1 = 0;

    /* Reset the cgroup fixture before testing PID namespace evidence. */
    snprintf(path, sizeof(path), "%s/proc/1/cgroup", root);
    make_file(path, "0::/\n");
    options.is_pid1 = 1;
    /* A PID-1 namespace handle that cannot answer NS_GET_PARENT is
     * inconclusive; never classify it as HOST using an inode constant.
     */
    snprintf(path, sizeof(path), "%s/proc/self/ns", root); make_dir(path);
    snprintf(path, sizeof(path), "%s/proc/self/ns/pid", root);
    make_file(path, "not an nsfs handle");
    options.is_pid1 = 1;
    assert(ninit_environment_detect(&options, &environment) == 0);
    assert(environment.kind == NINIT_ENVIRONMENT_UNKNOWN);
    assert(environment.pid_namespace_available == 0);
    assert(environment.pid_namespace_parent_visible == 0);
    assert((environment.evidence & NINIT_ENV_EVIDENCE_PID_NAMESPACE) == 0);
    options.is_pid1 = 0;
    /* Namespace evidence is valid for non-PID-1 callers too. */
    assert(ninit_environment_detect(&options, &environment) == 0);
    assert(environment.kind == NINIT_ENVIRONMENT_UNKNOWN);
    assert(environment.pid_namespace_available == 0);
    unlink(path);
    snprintf(path, sizeof(path), "%s/proc/self/ns", root); rmdir(path);

    /* systemd's marker takes precedence over generic cgroup heuristics. */
    snprintf(path, sizeof(path), "%s/run/systemd/container", root);
    make_file(path, "podman\n");
    assert(ninit_environment_detect(&options, &environment) == 0);
    assert(environment.kind == NINIT_ENVIRONMENT_CONTAINER);
    assert(environment.container == NINIT_CONTAINER_PODMAN);
    assert((environment.evidence & NINIT_ENV_EVIDENCE_SYSTEMD_FILE) != 0);
    /* PID 1 does not consume /run/systemd/container as its own hint. */
    options.is_pid1 = 1;
    assert(ninit_environment_detect(&options, &environment) == 0);
    assert(environment.kind == NINIT_ENVIRONMENT_UNKNOWN);
    options.is_pid1 = 0;
    /* Fall back to PID 1 environ when the published marker is absent. */
    unlink(path);
    snprintf(path, sizeof(path), "%s/proc/1/environ", root);
    {
        static const char environ_data[] = "container=lxc\0PATH=/bin\0";
        FILE *file = fopen(path, "wb");
        assert(file != NULL);
        assert(fwrite(environ_data, 1, sizeof(environ_data) - 1, file) ==
               sizeof(environ_data) - 1);
        assert(fclose(file) == 0);
    }
    assert(ninit_environment_detect(&options, &environment) == 0);
    assert(environment.kind == NINIT_ENVIRONMENT_CONTAINER);
    assert(environment.container == NINIT_CONTAINER_LXC);
    assert((environment.evidence & NINIT_ENV_EVIDENCE_ENVIRONMENT) != 0);
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


    /* systemd trusts container= only when running as PID 1. */
    unlink(path);
    options.is_pid1 = 1;
    options.container_environment = "lxc";
    assert(ninit_environment_detect(&options, &environment) == 0);
    assert(environment.container == NINIT_CONTAINER_LXC);
    assert((environment.evidence & NINIT_ENV_EVIDENCE_ENVIRONMENT) != 0);

    /* WSL takes precedence over generic image marker files. */
    options.container_environment = NULL;
    snprintf(path, sizeof(path), "%s/.dockerenv", root); make_file(path, "");
    snprintf(path, sizeof(path), "%s/proc/sys", root); make_dir(path);
    snprintf(path, sizeof(path), "%s/proc/sys/kernel", root); make_dir(path);
    snprintf(path, sizeof(path), "%s/proc/sys/kernel/osrelease", root);
    make_file(path, "6.6.0-microsoft-standard-WSL2\n");
    assert(ninit_environment_detect(&options, &environment) == 0);
    assert(environment.kind == NINIT_ENVIRONMENT_VIRTUAL_MACHINE);
    assert(environment.virtual_machine == NINIT_VM_WSL);
    assert(environment.container == NINIT_CONTAINER_NONE);
    snprintf(path, sizeof(path), "%s/.dockerenv", root); unlink(path);
    ninit_environment_get_policy(&environment, &policy);
    assert(policy.allow_host_shutdown == 0);
    assert(policy.allow_host_cgroup_management == 0);

    /*
     * Host reboot/poweroff requires both explicit host policy and an
     * observable effective CAP_SYS_BOOT bit. Containers and guests never
     * authorize host-level shutdown.
     */
    memset(&shutdown_capabilities, 0, sizeof(shutdown_capabilities));
    shutdown_capabilities.status_available = 1;
    shutdown_capabilities.cap_sys_boot = 1;
    ninit_environment_get_policy(&environment, &policy);
    assert(ninit_environment_authorize_host_shutdown(
               &policy, &shutdown_capabilities) == -1);
    assert(errno == EPERM);

    options.environment_override = "host";
    assert(ninit_environment_detect(&options, &environment) == 0);
    ninit_environment_get_policy(&environment, &policy);
    assert(ninit_environment_authorize_host_shutdown(
               &policy, &shutdown_capabilities) == 0);
    shutdown_capabilities.cap_sys_boot = 0;
    assert(ninit_environment_authorize_host_shutdown(
               &policy, &shutdown_capabilities) == -1);
    assert(errno == EPERM);
    shutdown_capabilities.cap_sys_boot = 1;
    shutdown_capabilities.status_available = 0;
    assert(ninit_environment_authorize_host_shutdown(
               &policy, &shutdown_capabilities) == -1);
    assert(errno == EOPNOTSUPP);
    shutdown_capabilities.status_available = 1;
    assert(ninit_environment_authorize_host_shutdown(NULL,
               &shutdown_capabilities) == -1);
    assert(errno == EINVAL);
    options.environment_override = NULL;

    /* Missing procfs and missing markers must not be mistaken for a host. */
    snprintf(path, sizeof(path), "%s/proc/sys/kernel/osrelease", root); unlink(path);
    snprintf(path, sizeof(path), "%s/proc/self/status", root); unlink(path);
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
    snprintf(path, sizeof(path), "%s/proc/self/mountinfo", root); unlink(path);
    snprintf(path, sizeof(path), "%s/proc/self/status", root); unlink(path);
    snprintf(path, sizeof(path), "%s/proc/self", root); rmdir(path);
    snprintf(path, sizeof(path), "%s/proc/sys/kernel/osrelease", root); unlink(path);
    snprintf(path, sizeof(path), "%s/proc/sys/kernel", root); rmdir(path);
    snprintf(path, sizeof(path), "%s/proc/sys", root); rmdir(path);
    snprintf(path, sizeof(path), "%s/proc", root); rmdir(path);
    assert(rmdir(root) == 0);
    return 0;
}
