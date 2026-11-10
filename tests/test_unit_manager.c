#include <ninit/unit_manager.h>

#include <assert.h>
#include <errno.h>
#include <string.h>

int
main(void)
{
    struct ninit_unit_manager *manager = ninit_unit_manager_create();
    const char *requires[] = { "local-fs.target" };
    struct ninit_unit_spec service = {
        "network.service", "Network service", NINIT_UNIT_SERVICE,
        "/usr/libexec/networkd", requires, 1, NULL, 0, NULL, 0, NULL, 0
    };
    struct ninit_unit_spec target = {
        "local-fs.target", "Local filesystems", NINIT_UNIT_TARGET,
        NULL, NULL, 0, NULL, 0, NULL, 0, NULL, 0
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
    assert(strcmp(found->exec_start, "/usr/libexec/networkd") == 0);

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
