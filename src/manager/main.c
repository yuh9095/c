#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <ninit/ipc_channel.h>
#include <ninit/ipc_handshake.h>
#include <ninit/version.h>

static void
print_usage(const char *program)
{
    printf("Usage: %s [--version|--help|--ipc-fd FD]\n", program);
    puts("Ninit service manager (early development).");
}

static int
run_manager(int fd)
{
    unsigned char buffer[NINIT_IPC_HEADER_SIZE + NINIT_IPC_MAX_PAYLOAD];
    const unsigned char *payload = NULL;
    struct ninit_ipc_header header;
    struct pollfd pfd;
    int result;

    result = ninit_ipc_handshake_manager(fd,
                                         NINIT_IPC_HANDSHAKE_TIMEOUT_MS);
    if (result != NINIT_IPC_OK) {
        fprintf(stderr, "ninit-manager: IPC handshake failed (%d)\n", result);
        return 1;
    }

    for (;;) {
        pfd.fd = fd;
        pfd.events = POLLIN;
        pfd.revents = 0;
        do {
            result = poll(&pfd, 1, -1);
        } while (result < 0 && errno == EINTR);

        if (result < 0 || (pfd.revents & (POLLERR | POLLNVAL)) != 0) {
            return 1;
        }
        if ((pfd.revents & POLLHUP) != 0) {
            return 0;
        }
        if ((pfd.revents & POLLIN) == 0) {
            continue;
        }

        result = ninit_ipc_receive(fd, buffer, sizeof(buffer),
                                   &header, &payload);
        if (result == NINIT_IPC_CLOSED) {
            return 0;
        }
        if (result != NINIT_IPC_OK) {
            fprintf(stderr, "ninit-manager: invalid IPC frame (%d)\n", result);
            return 1;
        }

        if (header.type == NINIT_IPC_MSG_PING) {
            header.type = NINIT_IPC_MSG_PONG;
            if (ninit_ipc_send(fd, &header, payload) != NINIT_IPC_OK) {
                return 1;
            }
        } else {
            fprintf(stderr, "ninit-manager: unsupported IPC message %u\n",
                    (unsigned int) header.type);
        }
    }
}

int
main(int argc, char **argv)
{
    char *end = NULL;
    long fd;

    if (argc == 2 && strcmp(argv[1], "--version") == 0) {
        printf("ninit-manager %s\n", NINIT_VERSION_STRING);
        return 0;
    }
    if (argc == 1 || (argc == 2 && strcmp(argv[1], "--help") == 0)) {
        print_usage(argv[0]);
        return 0;
    }
    if (argc == 3 && strcmp(argv[1], "--ipc-fd") == 0) {
        errno = 0;
        fd = strtol(argv[2], &end, 10);
        if (errno != 0 || end == argv[2] || *end != '\0' ||
            fd < 0 || fd > 1048576) {
            fprintf(stderr, "ninit-manager: invalid IPC descriptor\n");
            return 2;
        }
        return run_manager((int) fd);
    }

    fprintf(stderr, "ninit-manager: unsupported argument\n");
    print_usage(argv[0]);
    return 2;
}
