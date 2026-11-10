#include <ninit/ipc_channel.h>

#include <errno.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <sys/types.h>
#include <unistd.h>

int
ninit_ipc_socketpair(int descriptors[2])
{
    if (descriptors == NULL) {
        return NINIT_IPC_INVALID_ARGUMENT;
    }

    if (socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0,
                   descriptors) < 0) {
        return NINIT_IPC_IO_ERROR;
    }

    return NINIT_IPC_OK;
}

int
ninit_ipc_send(int fd,
               const struct ninit_ipc_header *header,
               const void *payload)
{
    unsigned char *packet;
    size_t packet_size;
    ssize_t sent;
    int result;

    if (fd < 0 || header == NULL) {
        return NINIT_IPC_INVALID_ARGUMENT;
    }

    packet = malloc(NINIT_IPC_HEADER_SIZE + NINIT_IPC_MAX_PAYLOAD);
    if (packet == NULL) {
        return NINIT_IPC_IO_ERROR;
    }

    result = ninit_ipc_encode(header, payload, packet,
                              NINIT_IPC_HEADER_SIZE + NINIT_IPC_MAX_PAYLOAD,
                              &packet_size);
    if (result != NINIT_IPC_OK) {
        free(packet);
        return result;
    }

    do {
        sent = send(fd, packet, packet_size, MSG_NOSIGNAL);
    } while (sent < 0 && errno == EINTR);

    free(packet);

    if (sent < 0) {
        return NINIT_IPC_IO_ERROR;
    }
    if ((size_t) sent != packet_size) {
        errno = EIO;
        return NINIT_IPC_IO_ERROR;
    }

    return NINIT_IPC_OK;
}

int
ninit_ipc_receive(int fd,
                  unsigned char *buffer,
                  size_t buffer_capacity,
                  struct ninit_ipc_header *header,
                  const unsigned char **payload)
{
    struct msghdr message;
    struct iovec vector;
    ssize_t received;

    if (fd < 0 || buffer == NULL || header == NULL || payload == NULL) {
        return NINIT_IPC_INVALID_ARGUMENT;
    }
    if (buffer_capacity < NINIT_IPC_HEADER_SIZE) {
        return NINIT_IPC_BUFFER_TOO_SMALL;
    }

    vector.iov_base = buffer;
    vector.iov_len = buffer_capacity;

    message.msg_name = NULL;
    message.msg_namelen = 0;
    message.msg_iov = &vector;
    message.msg_iovlen = 1;
    message.msg_control = NULL;
    message.msg_controllen = 0;
    message.msg_flags = 0;

    do {
        received = recvmsg(fd, &message, MSG_TRUNC);
    } while (received < 0 && errno == EINTR);

    if (received < 0) {
        return NINIT_IPC_IO_ERROR;
    }
    if (received == 0) {
        return NINIT_IPC_CLOSED;
    }
    if ((size_t) received > buffer_capacity) {
        return NINIT_IPC_BUFFER_TOO_SMALL;
    }

    return ninit_ipc_decode(buffer, (size_t) received, header, payload);
}
