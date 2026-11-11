#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/file.h>
#include <sys/types.h>
#include <unistd.h>

static int
write_marker(const char *path)
{
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0)
        return -1;
    if (write(fd, "ok\n", 3) != 3) {
        int saved = errno;
        close(fd);
        errno = saved;
        return -1;
    }
    return close(fd);
}

int
main(int argc, char **argv)
{
    const char *first_marker;
    const char *restarted_marker;
    const char *lock_path;
    int lock_fd;

    if (argc != 4)
        return 2;
    first_marker = argv[1];
    restarted_marker = argv[2];
    lock_path = argv[3];

    lock_fd = open(lock_path, O_RDWR | O_CREAT, 0600);
    if (lock_fd < 0)
        return 3;

    if (access(first_marker, F_OK) == 0) {
        if (flock(lock_fd, LOCK_EX | LOCK_NB) < 0)
            return 4;
        if (write_marker(restarted_marker) < 0)
            return 5;
        (void) close(lock_fd);
        /* Ask PID 1 to perform the normal container shutdown path. */
        (void) kill(1, SIGTERM);
        return 0;
    }

    if (flock(lock_fd, LOCK_EX | LOCK_NB) < 0)
        return 6;
    {
        pid_t child = fork();
        if (child < 0)
            return 7;
        if (child == 0) {
            (void) signal(SIGTERM, SIG_IGN);
            for (;;)
                pause();
        }
    }

    if (write_marker(first_marker) < 0)
        return 8;

    /* This service deliberately crashes its direct parent: ninit-manager. */
    if (kill(getppid(), SIGKILL) < 0)
        return 9;
    for (;;)
        pause();
}
