#include <ninit/manager_process.h>
#include <ninit/ipc_channel.h>

#include <assert.h>
#include <signal.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

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

    assert(argc == 2);
    assert(ninit_manager_start(argv[1], 3000, &manager) == NINIT_IPC_OK);
    assert(manager.pid > 0 && manager.ipc_fd >= 0);

    ping.version = NINIT_IPC_VERSION;
    ping.type = NINIT_IPC_MSG_PING;
    ping.flags = 0;
    ping.payload_size = (uint32_t) (sizeof(body) - 1);
    ping.request_id = 1234;
    assert(ninit_ipc_send(manager.ipc_fd, &ping, body) == NINIT_IPC_OK);

    result = ninit_ipc_receive(manager.ipc_fd, buffer, sizeof(buffer),
                               &pong, &payload);
    assert(result == NINIT_IPC_OK);
    assert(pong.type == NINIT_IPC_MSG_PONG);
    assert(pong.request_id == ping.request_id);
    assert(payload != NULL && memcmp(payload, body, sizeof(body) - 1) == 0);

    ninit_manager_disconnect(&manager);
    for (int i = 0; i < 100; ++i) {
        result = ninit_manager_reap(&manager, &status);
        if (result == 1) {
            break;
        }
        assert(result == 0);
        usleep(10000);
    }
    if (manager.pid > 0) {
        (void) kill(manager.pid, SIGTERM);
        (void) waitpid(manager.pid, &status, 0);
        manager.pid = -1;
    }
    assert(manager.pid == -1);
    assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    return 0;
}
