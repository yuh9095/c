#define _POSIX_C_SOURCE 200809L

#include <ninit/manager_supervisor.h>
#include <ninit/ipc_protocol.h>

#include <assert.h>
#include <signal.h>
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

int
main(int argc, char **argv)
{
    struct ninit_manager_supervisor supervisor;
    struct timespec now;
    pid_t first_pid;
    pid_t second_pid;
    int status = 0;
    int result;

    assert(argc == 2);
    assert(ninit_manager_supervisor_init(&supervisor, argv[1],
                                         2, 10, 40, 3000) == NINIT_IPC_OK);

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
    return 0;
}
