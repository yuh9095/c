#define _POSIX_C_SOURCE 200809L

#include <ninit/manager_supervisor.h>
#include <ninit/ipc_protocol.h>
#include <ninit/ipc_channel.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static struct timespec
at_ms(time_t seconds, long milliseconds)
{
    struct timespec value;
    value.tv_sec = seconds;
    value.tv_nsec = milliseconds * 1000000L;
    return value;
}

static void
check_shutdown_action(enum ninit_manager_shutdown_action action,
                      const char *expected)
{
    struct ninit_manager_supervisor supervisor;
    struct ninit_ipc_header header;
    unsigned char buffer[128];
    const unsigned char *payload = NULL;
    int sockets[2];

    memset(&supervisor, 0, sizeof(supervisor));
    assert(ninit_ipc_socketpair(sockets) == NINIT_IPC_OK);
    supervisor.process.pid = 12345;
    supervisor.process.ipc_fd = sockets[0];
    supervisor.state = NINIT_MANAGER_RUNNING;
    assert(ninit_manager_supervisor_shutdown_action(&supervisor, action) == 0);
    assert(supervisor.state == NINIT_MANAGER_STOPPING);
    assert(ninit_ipc_receive(sockets[1], buffer, sizeof(buffer),
                             &header, &payload) == NINIT_IPC_OK);
    assert(header.type == NINIT_IPC_MSG_SHUTDOWN);
    assert(header.payload_size == strlen(expected));
    assert(memcmp(payload, expected, strlen(expected)) == 0);
    close(sockets[1]);
}

int
main(int argc, char **argv)
{
    struct ninit_manager_supervisor supervisor;
    struct timespec now;
    pid_t first_pid;
    pid_t second_pid;
    int status = 0;
    int result;
    char directory[] = "/tmp/ninit-supervisor-units-XXXXXX";
    char unit_path[256];
    FILE *unit_file;

    assert(argc == 2);
    check_shutdown_action(NINIT_MANAGER_SHUTDOWN_HALT, "halt");
    check_shutdown_action(NINIT_MANAGER_SHUTDOWN_POWEROFF, "poweroff");
    check_shutdown_action(NINIT_MANAGER_SHUTDOWN_REBOOT, "reboot");
    assert(mkdtemp(directory) != NULL);
    assert(snprintf(unit_path, sizeof(unit_path), "%s/default.target",
                    directory) > 0);
    unit_file = fopen(unit_path, "w");
    assert(unit_file != NULL);
    assert(fputs("[Unit]\nDescription=Supervisor target\n[Target]\n",
                 unit_file) >= 0);
    assert(fclose(unit_file) == 0);
    assert(ninit_manager_supervisor_init(&supervisor, argv[1],
                                         2, 10, 40, 3000) == NINIT_IPC_OK);

    assert(ninit_manager_supervisor_set_startup_target(
               &supervisor, directory, "default.target") == NINIT_IPC_OK);

    now = at_ms(100, 0);
    assert(ninit_manager_supervisor_start(&supervisor, &now) == NINIT_IPC_OK);
    assert(supervisor.state == NINIT_MANAGER_RUNNING);
    first_pid = supervisor.process.pid;

    assert(kill(first_pid, SIGKILL) == 0);
    assert(waitpid(first_pid, &status, 0) == first_pid);
    now = at_ms(101, 0);
    assert(ninit_manager_supervisor_child_exited(&supervisor, first_pid,
                                                 status, &now) == NINIT_IPC_OK);
    assert(supervisor.state == NINIT_MANAGER_BACKOFF);
    assert(supervisor.restart_count == 1);

    now = at_ms(101, 9);
    assert(ninit_manager_supervisor_tick(&supervisor, &now) == NINIT_IPC_OK);
    assert(supervisor.state == NINIT_MANAGER_BACKOFF);

    now = at_ms(101, 10);
    assert(ninit_manager_supervisor_tick(&supervisor, &now) == NINIT_IPC_OK);
    assert(supervisor.state == NINIT_MANAGER_RUNNING);
    second_pid = supervisor.process.pid;
    assert(second_pid > 0);

    ninit_manager_supervisor_shutdown(&supervisor);
    assert(supervisor.state == NINIT_MANAGER_STOPPING);
    assert(waitpid(second_pid, &status, 0) == second_pid);
    now = at_ms(102, 0);
    result = ninit_manager_supervisor_child_exited(&supervisor, second_pid,
                                                   status, &now);
    assert(result == NINIT_IPC_OK);
    assert(supervisor.state == NINIT_MANAGER_STOPPED);
    assert(unlink(unit_path) == 0);
    assert(rmdir(directory) == 0);
    return 0;
}
