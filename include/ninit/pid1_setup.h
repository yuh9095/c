#ifndef NINIT_PID1_SETUP_H
#define NINIT_PID1_SETUP_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Attach stdin, stdout, and stderr to the supplied console device.
 * Intended for the early PID 1 path; accepts an alternate path for tests.
 */
int ninit_pid1_setup_console(const char *console_path);

#ifdef __cplusplus
}
#endif

#endif
