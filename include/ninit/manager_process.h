#ifndef NINIT_MANAGER_PROCESS_H
#define NINIT_MANAGER_PROCESS_H

#include <sys/types.h>
#include <ninit/ipc_protocol.h>

#ifdef __cplusplus
extern "C" {
#endif

struct ninit_manager_process {
    pid_t pid;
    int ipc_fd;
};

/*
 * Spawn ninit-manager, pass a private IPC endpoint as fd 3, and complete
 * the protocol handshake before returning success.
 */
int ninit_manager_start_with_config(const char *manager_path,
                        const char *unit_dir,
                        const char *target,
                        int handshake_timeout_ms,
                        struct ninit_manager_process *process);

int ninit_manager_start(const char *manager_path,
                        int handshake_timeout_ms,
                        struct ninit_manager_process *process);

/* Close the private channel. Manager observes EOF and exits its event loop. */
void ninit_manager_disconnect(struct ninit_manager_process *process);

#ifdef __cplusplus
}
#endif

#endif
