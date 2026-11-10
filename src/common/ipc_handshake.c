#include <ninit/ipc_handshake.h>

#include <errno.h>
#include <poll.h>
#include <stdint.h>
#include <string.h>

#include <ninit/ipc_channel.h>

#define HANDSHAKE_PAYLOAD_SIZE 8U
#define HANDSHAKE_ROLE_MANAGER 1U

static int
wait_fd(int fd, short events, int timeout_ms)
{
    struct pollfd pfd;
    int result;

    pfd.fd = fd;
    pfd.events = events;
    pfd.revents = 0;

    do {
        result = poll(&pfd, 1, timeout_ms);
    } while (result < 0 && errno == EINTR);

    if (result == 0) {
        return NINIT_IPC_WOULD_BLOCK;
    }
    if (result < 0 || (pfd.revents & (POLLERR | POLLNVAL)) != 0) {
        return NINIT_IPC_IO_ERROR;
    }
    if ((pfd.revents & (events | POLLHUP)) == 0) {
        return NINIT_IPC_IO_ERROR;
    }
    return NINIT_IPC_OK;
}

static int
receive_frame(int fd, int timeout_ms,
              struct ninit_ipc_header *header,
              const unsigned char **payload,
              unsigned char *buffer, size_t capacity)
{
    int result = wait_fd(fd, POLLIN, timeout_ms);

    if (result != NINIT_IPC_OK) {
        return result;
    }
    return ninit_ipc_receive(fd, buffer, capacity, header, payload);
}

int
ninit_ipc_handshake_manager(int fd, int timeout_ms)
{
    const unsigned char hello[HANDSHAKE_PAYLOAD_SIZE] = {
        0, NINIT_IPC_VERSION, 0, NINIT_IPC_VERSION,
        0, HANDSHAKE_ROLE_MANAGER, 0, 0
    };
    unsigned char buffer[NINIT_IPC_HEADER_SIZE + HANDSHAKE_PAYLOAD_SIZE];
    const unsigned char *payload = NULL;
    struct ninit_ipc_header header;
    int result;

    if (fd < 0 || timeout_ms < 0) {
        return NINIT_IPC_INVALID_ARGUMENT;
    }

    header.version = NINIT_IPC_VERSION;
    header.type = NINIT_IPC_MSG_HELLO;
    header.flags = 0;
    header.payload_size = HANDSHAKE_PAYLOAD_SIZE;
    header.request_id = 0;

    result = ninit_ipc_send(fd, &header, hello);
    if (result == NINIT_IPC_WOULD_BLOCK) {
        result = wait_fd(fd, POLLOUT, timeout_ms);
        if (result == NINIT_IPC_OK) {
            result = ninit_ipc_send(fd, &header, hello);
        }
    }
    if (result != NINIT_IPC_OK) {
        return result;
    }

    result = receive_frame(fd, timeout_ms, &header, &payload,
                           buffer, sizeof(buffer));
    if (result != NINIT_IPC_OK) {
        return result;
    }
    if (header.type != NINIT_IPC_MSG_HELLO_REPLY ||
        header.request_id != 0 ||
        header.payload_size != 4 ||
        payload == NULL ||
        payload[0] != 0 ||
        payload[1] != NINIT_IPC_VERSION ||
        payload[2] != 0 || payload[3] != 0) {
        return NINIT_IPC_UNSUPPORTED;
    }
    return NINIT_IPC_OK;
}

int
ninit_ipc_handshake_init(int fd, int timeout_ms)
{
    unsigned char buffer[NINIT_IPC_HEADER_SIZE + HANDSHAKE_PAYLOAD_SIZE];
    const unsigned char *payload = NULL;
    const unsigned char reply[4] = { 0, NINIT_IPC_VERSION, 0, 0 };
    struct ninit_ipc_header header;
    int result;

    if (fd < 0 || timeout_ms < 0) {
        return NINIT_IPC_INVALID_ARGUMENT;
    }

    result = receive_frame(fd, timeout_ms, &header, &payload,
                           buffer, sizeof(buffer));
    if (result != NINIT_IPC_OK) {
        return result;
    }
    if (header.type != NINIT_IPC_MSG_HELLO ||
        header.request_id != 0 ||
        header.payload_size != HANDSHAKE_PAYLOAD_SIZE ||
        payload == NULL ||
        payload[0] != 0 || payload[1] != NINIT_IPC_VERSION ||
        payload[2] != 0 || payload[3] != NINIT_IPC_VERSION ||
        payload[4] != 0 || payload[5] != HANDSHAKE_ROLE_MANAGER ||
        payload[6] != 0 || payload[7] != 0) {
        return NINIT_IPC_UNSUPPORTED;
    }

    header.type = NINIT_IPC_MSG_HELLO_REPLY;
    header.payload_size = sizeof(reply);
    header.request_id = 0;

    result = ninit_ipc_send(fd, &header, reply);
    if (result == NINIT_IPC_WOULD_BLOCK) {
        result = wait_fd(fd, POLLOUT, timeout_ms);
        if (result == NINIT_IPC_OK) {
            result = ninit_ipc_send(fd, &header, reply);
        }
    }
    return result;
}
