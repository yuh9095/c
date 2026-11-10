#include <ninit/pid1_runtime.h>

#include <errno.h>
#include <unistd.h>
#include <sys/wait.h>

int
ninit_pid1_is_init(void)
{
    return getpid() == 1;
}

int
ninit_pid1_reap_children(ninit_child_exit_callback callback,
                         void *userdata)
{
    int reaped = 0;

    for (;;) {
        int status;
        pid_t pid = waitpid(-1, &status, WNOHANG);

        if (pid > 0) {
            reaped++;
            if (callback != NULL) {
                callback(pid, status, userdata);
            }
            continue;
        }

        if (pid == 0) {
            return reaped;
        }

        if (errno == EINTR) {
            continue;
        }

        if (errno == ECHILD) {
            return reaped;
        }

        return -1;
    }
}
