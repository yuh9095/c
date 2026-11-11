#include <ninit/boot_coordinator.h>

#include <errno.h>
#include <stddef.h>

static enum ninit_boot_state
state_for_target(enum ninit_boot_target target)
{
    switch (target) {
    case NINIT_BOOT_TARGET_SYSINIT:
        return NINIT_BOOT_RUNNING_SYSINIT;
    case NINIT_BOOT_TARGET_BOOT:
        return NINIT_BOOT_RUNNING_BOOT;
    case NINIT_BOOT_TARGET_DEFAULT:
        return NINIT_BOOT_RUNNING_DEFAULT;
    case NINIT_BOOT_TARGET_NONE:
    default:
        return NINIT_BOOT_FAILED;
    }
}

static enum ninit_boot_target
next_target(enum ninit_boot_target target)
{
    switch (target) {
    case NINIT_BOOT_TARGET_SYSINIT:
        return NINIT_BOOT_TARGET_BOOT;
    case NINIT_BOOT_TARGET_BOOT:
        return NINIT_BOOT_TARGET_DEFAULT;
    case NINIT_BOOT_TARGET_DEFAULT:
    case NINIT_BOOT_TARGET_NONE:
    default:
        return NINIT_BOOT_TARGET_NONE;
    }
}

void
ninit_boot_coordinator_init(struct ninit_boot_coordinator *coordinator)
{
    if (coordinator == NULL) {
        return;
    }
    coordinator->state = NINIT_BOOT_IDLE;
    coordinator->current_target = NINIT_BOOT_TARGET_NONE;
    coordinator->last_error = 0;
}

int
ninit_boot_coordinator_start(struct ninit_boot_coordinator *coordinator,
                             ninit_boot_submit_callback submit,
                             void *userdata)
{
    if (coordinator == NULL || submit == NULL ||
        coordinator->state != NINIT_BOOT_IDLE) {
        errno = EINVAL;
        return -1;
    }

    coordinator->current_target = NINIT_BOOT_TARGET_SYSINIT;
    coordinator->state = NINIT_BOOT_RUNNING_SYSINIT;
    errno = 0;
    if (submit(coordinator->current_target, userdata) != 0) {
        coordinator->state = NINIT_BOOT_FAILED;
        coordinator->last_error = errno != 0 ? errno : EIO;
        return -1;
    }
    return 0;
}

int
ninit_boot_coordinator_target_complete(
    struct ninit_boot_coordinator *coordinator,
    enum ninit_boot_target target,
    int success,
    int error_code,
    ninit_boot_submit_callback submit,
    void *userdata)
{
    enum ninit_boot_target following;

    if (coordinator == NULL || target == NINIT_BOOT_TARGET_NONE ||
        coordinator->current_target != target ||
        coordinator->state != state_for_target(target)) {
        errno = EINVAL;
        return -1;
    }

    if (!success) {
        coordinator->state = NINIT_BOOT_FAILED;
        coordinator->last_error = error_code != 0 ? error_code : EIO;
        return 0;
    }

    following = next_target(target);
    if (following == NINIT_BOOT_TARGET_NONE) {
        coordinator->current_target = NINIT_BOOT_TARGET_NONE;
        coordinator->state = NINIT_BOOT_COMPLETE;
        coordinator->last_error = 0;
        return 0;
    }

    if (submit == NULL) {
        errno = EINVAL;
        return -1;
    }
    coordinator->current_target = following;
    coordinator->state = state_for_target(following);
    errno = 0;
    if (submit(following, userdata) != 0) {
        coordinator->state = NINIT_BOOT_FAILED;
        coordinator->last_error = errno != 0 ? errno : EIO;
        return -1;
    }
    return 0;
}

void
ninit_boot_coordinator_shutdown(
    struct ninit_boot_coordinator *coordinator)
{
    if (coordinator == NULL) {
        return;
    }
    coordinator->state = NINIT_BOOT_SHUTTING_DOWN;
}
