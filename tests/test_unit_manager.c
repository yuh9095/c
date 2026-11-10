#include <ninit/unit_manager.h>

#include <assert.h>
#include <errno.h>
#include <string.h>

int
main(void)
{
    struct ninit_unit_manager *manager = ninit_unit_manager_create();
    const char *requires[] = { "local-fs.target" };
    const char *exec_argv[] = { "/usr/libexec/networkd" };
    const char *stop_argv[] = { "/usr/libexec/networkd-stop", "graceful" };
    struct ninit_unit_spec service = {
        "network.service", "Network service", NINIT_UNIT_SERVICE,
        exec_argv, 1, requires, 1, NULL, 0, NULL, 0, NULL, 0,
        stop_argv, 2, NULL, 0
    };
    struct ninit_unit_spec target = {
        "local-fs.target", "Local filesystems", NINIT_UNIT_TARGET,
        NULL, 0, NULL, 0, NULL, 0, NULL, 0
    };
    const struct ninit_unit *found;

    assert(manager != NULL);
    assert(ninit_unit_manager_add(manager, &target) == 0);
    assert(ninit_unit_manager_add(manager, &service) == 0);
    assert(ninit_unit_manager_count(manager) == 2);

    /* The manager owns copies; modifying the caller's input must not mutate it. */
    requires[0] = "changed.target";
    found = ninit_unit_manager_find(manager, "network.service");
    assert(found != NULL);
    assert(strcmp(found->requires[0], "local-fs.target") == 0);
    assert(found->exec_start_argc == 1);
    assert(strcmp(found->exec_start_argv[0], "/usr/libexec/networkd") == 0);
    stop_argv[0] = "/changed";
    assert(found->exec_stop_argc == 2);
    assert(strcmp(found->exec_stop_argv[0], "/usr/libexec/networkd-stop") == 0);
    assert(strcmp(found->exec_stop_argv[1], "graceful") == 0);

    errno = 0;
    assert(ninit_unit_manager_add(manager, &service) == -1);
    assert(errno == EEXIST);

    service.name = "bad/name.service";
    errno = 0;
    assert(ninit_unit_manager_add(manager, &service) == -1);
    assert(errno == EINVAL);

    ninit_unit_manager_destroy(manager);
    assert(ninit_unit_manager_find(NULL, "x") == NULL);
    return 0;
}
