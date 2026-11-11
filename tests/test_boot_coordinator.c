#include <ninit/boot_coordinator.h>

#include <assert.h>
#include <errno.h>

struct submitted_targets {
    enum ninit_boot_target targets[3];
    unsigned int count;
    enum ninit_boot_target fail_target;
};

static int
submit_target(enum ninit_boot_target target, void *userdata)
{
    struct submitted_targets *submitted = userdata;

    if (target == submitted->fail_target) {
        errno = EACCES;
        return -1;
    }
    assert(submitted->count < 3);
    submitted->targets[submitted->count++] = target;
    return 0;
}

int
main(void)
{
    struct ninit_boot_coordinator coordinator;
    struct submitted_targets submitted = {
        { NINIT_BOOT_TARGET_NONE, NINIT_BOOT_TARGET_NONE,
          NINIT_BOOT_TARGET_NONE }, 0, NINIT_BOOT_TARGET_NONE
    };

    ninit_boot_coordinator_init(&coordinator);
    assert(coordinator.state == NINIT_BOOT_IDLE);
    assert(ninit_boot_coordinator_start(&coordinator, submit_target,
                                        &submitted) == 0);
    assert(coordinator.state == NINIT_BOOT_RUNNING_SYSINIT);
    assert(submitted.count == 1);
    assert(submitted.targets[0] == NINIT_BOOT_TARGET_SYSINIT);

    assert(ninit_boot_coordinator_target_complete(
        &coordinator, NINIT_BOOT_TARGET_SYSINIT, 1, 0,
        submit_target, &submitted) == 0);
    assert(coordinator.state == NINIT_BOOT_RUNNING_BOOT);
    assert(submitted.targets[1] == NINIT_BOOT_TARGET_BOOT);

    assert(ninit_boot_coordinator_target_complete(
        &coordinator, NINIT_BOOT_TARGET_BOOT, 1, 0,
        submit_target, &submitted) == 0);
    assert(coordinator.state == NINIT_BOOT_RUNNING_DEFAULT);
    assert(submitted.targets[2] == NINIT_BOOT_TARGET_DEFAULT);

    assert(ninit_boot_coordinator_target_complete(
        &coordinator, NINIT_BOOT_TARGET_DEFAULT, 1, 0,
        submit_target, &submitted) == 0);
    assert(coordinator.state == NINIT_BOOT_COMPLETE);
    assert(coordinator.current_target == NINIT_BOOT_TARGET_NONE);

    ninit_boot_coordinator_init(&coordinator);
    submitted.count = 0;
    submitted.fail_target = NINIT_BOOT_TARGET_NONE;
    assert(ninit_boot_coordinator_start(&coordinator, submit_target,
                                        &submitted) == 0);
    assert(ninit_boot_coordinator_target_complete(
        &coordinator, NINIT_BOOT_TARGET_SYSINIT, 0, EIO,
        submit_target, &submitted) == 0);
    assert(coordinator.state == NINIT_BOOT_FAILED);
    assert(coordinator.last_error == EIO);
    assert(submitted.count == 1);

    ninit_boot_coordinator_shutdown(&coordinator);
    assert(coordinator.state == NINIT_BOOT_SHUTTING_DOWN);
    return 0;
}
