#ifndef NINIT_BOOT_COORDINATOR_H
#define NINIT_BOOT_COORDINATOR_H

#ifdef __cplusplus
extern "C" {
#endif

enum ninit_boot_target {
    NINIT_BOOT_TARGET_NONE = 0,
    NINIT_BOOT_TARGET_SYSINIT,
    NINIT_BOOT_TARGET_BOOT,
    NINIT_BOOT_TARGET_DEFAULT
};

enum ninit_boot_state {
    NINIT_BOOT_IDLE = 0,
    NINIT_BOOT_RUNNING_SYSINIT,
    NINIT_BOOT_RUNNING_BOOT,
    NINIT_BOOT_RUNNING_DEFAULT,
    NINIT_BOOT_COMPLETE,
    NINIT_BOOT_FAILED,
    NINIT_BOOT_SHUTTING_DOWN
};

/*
 * Submit one target to the future Job Manager.
 * Return 0 when accepted, or -1 when submission fails.
 * Completion is reported separately through target_complete().
 */
typedef int (*ninit_boot_submit_callback)(enum ninit_boot_target target,
                                          void *userdata);

struct ninit_boot_coordinator {
    enum ninit_boot_state state;
    enum ninit_boot_target current_target;
    int last_error;
};

void ninit_boot_coordinator_init(
    struct ninit_boot_coordinator *coordinator);

/* Submit sysinit and enter the boot sequence. */
int ninit_boot_coordinator_start(
    struct ninit_boot_coordinator *coordinator,
    ninit_boot_submit_callback submit,
    void *userdata);

/*
 * Complete the currently running target. On success, the next target is
 * submitted in order: sysinit -> boot -> default. A failed target stops boot.
 */
int ninit_boot_coordinator_target_complete(
    struct ninit_boot_coordinator *coordinator,
    enum ninit_boot_target target,
    int success,
    int error_code,
    ninit_boot_submit_callback submit,
    void *userdata);

/* Mark boot as shutting down; no new targets will be submitted. */
void ninit_boot_coordinator_shutdown(
    struct ninit_boot_coordinator *coordinator);

#ifdef __cplusplus
}
#endif

#endif
