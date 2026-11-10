#ifndef NINIT_IPC_HANDSHAKE_H
#define NINIT_IPC_HANDSHAKE_H

#ifdef __cplusplus
extern "C" {
#endif

#define NINIT_IPC_HANDSHAKE_TIMEOUT_MS 3000

/* Manager initiates the handshake; the init-side endpoint accepts it. */
int ninit_ipc_handshake_manager(int fd, int timeout_ms);
int ninit_ipc_handshake_init(int fd, int timeout_ms);

#ifdef __cplusplus
}
#endif

#endif
