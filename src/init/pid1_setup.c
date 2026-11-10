#define _POSIX_C_SOURCE 200809L

#include <ninit/pid1_setup.h>

#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

int
ninit_pid1_setup_console(const char *console_path)
{
    int fd;
    int target;

    if (console_path == NULL || console_path[0] == '\0') {
        errno = EINVAL;
        return -1;
    }

    fd = open(console_path, O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        return -1;
    }

    for (target = STDIN_FILENO; target <= STDERR_FILENO; target++) {
        if (dup2(fd, target) < 0) {
            int saved_errno = errno;
            if (fd > STDERR_FILENO) {
                close(fd);
            }
            errno = saved_errno;
            return -1;
        }
    }

    if (fd > STDERR_FILENO) {
        close(fd);
    }
    return 0;
}
