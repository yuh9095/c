#define _POSIX_C_SOURCE 200809L

#include <ninit/manager_process.h>
#include <ninit/ipc_channel.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <poll.h>
#include <signal.h>
#include <time.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include <sys/stat.h>

int
main(int argc, char **argv)
{
    struct ninit_manager_process manager;
    struct ninit_ipc_header ping;
    struct ninit_ipc_header pong;
    const unsigned char body[] = "ping";
    unsigned char buffer[NINIT_IPC_HEADER_SIZE + sizeof(body)];
    const unsigned char *payload = NULL;
    int status = 0;
    int result;
    char directory[] = "/tmp/ninit-manager-units-XXXXXX";
    char unit_path[256];
    FILE *unit_file;

    assert(argc == 2);
    assert(mkdtemp(directory) != NULL);
    assert(snprintf(unit_path, sizeof(unit_path), "%s/default.target",
                    directory) > 0);
    unit_file = fopen(unit_path, "w");
    assert(unit_file != NULL);
    assert(fputs("[Unit]\\nDescription=Test default target\\n[Target]\\n",
                 unit_file) >= 0);
    assert(fclose(unit_file) == 0);
    assert(ninit_manager_start_with_config(argv[1], directory,
                                           "default.target", 3000,
                                           &manager) == NINIT_IPC_OK);
    assert(manager.pid > 0 && manager.ipc_fd >= 0);

    ping.version = NINIT_IPC_VERSION;
    ping.type = NINIT_IPC_MSG_PING;
    ping.flags = 0;
    ping.payload_size = (uint32_t) (sizeof(body) - 1);
    ping.request_id = 1234;
    assert(ninit_ipc_send(manager.ipc_fd, &ping, body) == NINIT_IPC_OK);

    {
        struct pollfd pfd = { manager.ipc_fd, POLLIN, 0 };
        assert(poll(&pfd, 1, 3000) == 1);
    }
    result = ninit_ipc_receive(manager.ipc_fd, buffer, sizeof(buffer),
                               &pong, &payload);
    assert(result == NINIT_IPC_OK);
    assert(pong.type == NINIT_IPC_MSG_PONG);
    assert(pong.request_id == ping.request_id);
    assert(payload != NULL && memcmp(payload, body, sizeof(body) - 1) == 0);

    ninit_manager_disconnect(&manager);
    for (int i = 0; i < 100; ++i) {
        pid_t waited = waitpid(manager.pid, &status, WNOHANG);
        result = waited == manager.pid ? 1 : (waited == 0 ? 0 : -1);
        if (result == 1) {
            manager.pid = -1;
            break;
        }
        assert(result == 0);
        {
            struct timespec delay = { 0, 10000000L };
            (void) nanosleep(&delay, NULL);
        }
    }
    if (manager.pid > 0) {
        (void) kill(manager.pid, SIGTERM);
        (void) waitpid(manager.pid, &status, 0);
        manager.pid = -1;
    }
    assert(manager.pid == -1);
    assert(unlink(unit_path) == 0);
    assert(rmdir(directory) == 0);
    assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    return 0;
}
