#include <ninit/ipc_channel.h>

#include <assert.h>
#include <fcntl.h>
#include <unistd.h>

int
main(void)
{
    int fds[2];
    int flags;
    unsigned char buffer[NINIT_IPC_HEADER_SIZE + 16];
    const unsigned char body[] = "ninit-ipc";
    const unsigned char *payload = NULL;
    struct ninit_ipc_header sent = {
        NINIT_IPC_VERSION, NINIT_IPC_MSG_PING, 0,
        (uint32_t) (sizeof(body) - 1), 77
    };
    struct ninit_ipc_header received;

    assert(ninit_ipc_socketpair(fds) == NINIT_IPC_OK);
    flags = fcntl(fds[0], F_GETFD);
    assert(flags >= 0 && (flags & FD_CLOEXEC) != 0);
    flags = fcntl(fds[1], F_GETFD);
    assert(flags >= 0 && (flags & FD_CLOEXEC) != 0);

    assert(ninit_ipc_send(fds[0], &sent, body) == NINIT_IPC_OK);
    assert(ninit_ipc_receive(fds[1], buffer, sizeof(buffer),
                             &received, &payload) == NINIT_IPC_OK);
    assert(received.type == sent.type);
    assert(received.request_id == sent.request_id);
    assert(payload != NULL);
    assert(payload[sizeof(body) - 2] == 'c');

    close(fds[0]);
    assert(ninit_ipc_receive(fds[1], buffer, sizeof(buffer),
                             &received, &payload) == NINIT_IPC_CLOSED);
    close(fds[1]);
    return 0;
}
