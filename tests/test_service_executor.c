#define _POSIX_C_SOURCE 200809L

#include <ninit/service_executor.h>

#include <assert.h>
#include <errno.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static void
wait_for_exec(pid_t pid, int fd, int expected_result, int expected_errno)
{
    int result = 0;
    int attempts;
    for (attempts = 0; attempts < 1000; ++attempts) {
        result = ninit_service_executor_check_exec(fd);
        if (result != 0) break;
        { struct timespec delay = { 0, 1000000L };\n          (void) nanosleep(&delay, NULL); }
    }
    assert(result == expected_result);
    if (expected_result < 0) assert(errno == expected_errno);
    close(fd);
    {
        int status;
        assert(waitpid(pid, &status, 0) == pid);
        assert(WIFEXITED(status));
        if (expected_result == 1) assert(WEXITSTATUS(status) == 0);
        else assert(WEXITSTATUS(status) == 127);
    }
}

int
main(void)
{
    struct ninit_unit unit;
    pid_t pid;
    int error_fd;
    char *true_argv[] = { "/bin/true", NULL };
    char *shell_argv[] = {
        "/bin/sh", "-c", "test \"$1\" = preserved", "ninit-test",
        "preserved", NULL
    };
    char *missing_argv[] = { "/ninit/does-not-exist", NULL };

    memset(&unit, 0, sizeof(unit));
    unit.type = NINIT_UNIT_SERVICE;
    unit.exec_start_argv = true_argv;
    unit.exec_start_argc = 1;
    assert(ninit_service_executor_spawn(&unit, &pid, &error_fd) == 0);
    wait_for_exec(pid, error_fd, 1, 0);

    unit.exec_start_argv = shell_argv;
    unit.exec_start_argc = 5;
    assert(ninit_service_executor_spawn(&unit, &pid, &error_fd) == 0);
    wait_for_exec(pid, error_fd, 1, 0);

    unit.exec_start_argv = missing_argv;
    unit.exec_start_argc = 1;
    assert(ninit_service_executor_spawn(&unit, &pid, &error_fd) == 0);
    wait_for_exec(pid, error_fd, -1, ENOENT);

    unit.type = NINIT_UNIT_TARGET;
    errno = 0;
    assert(ninit_service_executor_spawn(&unit, &pid, &error_fd) == -1);
    assert(errno == EINVAL);
    return 0;
}
