#ifndef NINIT_IPC_CHANNEL_H
#define NINIT_IPC_CHANNEL_H

#include <stddef.h>
#include <ninit/ipc_protocol.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Create a private AF_UNIX/SOCK_SEQPACKET pair. Both descriptors are
 * close-on-exec. The caller owns both descriptors and must close them.
 */
int ninit_ipc_socketpair(int descriptors[2]);

/* Send one complete protocol frame over a SOCK_SEQPACKET descriptor. */
int ninit_ipc_send(int fd,
                   const struct ninit_ipc_header *header,
                   const void *payload);

/*
 * Receive one frame into caller-owned storage. The payload pointer aliases
 * buffer and remains valid until that buffer is reused or released.
 * A too-small buffer consumes and rejects the oversized packet.
 */
int ninit_ipc_receive(int fd,
                      unsigned char *buffer,
                      size_t buffer_capacity,
                      struct ninit_ipc_header *header,
                      const unsigned char **payload);

#ifdef __cplusplus
}
#endif

#endif
