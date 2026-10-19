#define _POSIX_C_SOURCE 200809L

#include "signals.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <unistd.h>

static int event_pipe[2] = {-1, -1};

static void child_status_changed(int signal_number)
{
    int saved_errno = errno;
    char byte = 'C';

    (void)signal_number;

    /*
     * The pipe is nonblocking. If full, it is already readable,
     * so the main loop already has a pending wake-up.
     */
    ssize_t result;

    do {
        result = write(event_pipe[1], &byte, 1);
    } while (result == -1 && errno == EINTR);

    errno = saved_errno;
}

static int configure_fd(int fd)
{
    int flags = fcntl(fd, F_GETFL);

    if (flags == -1
        || fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        return -1;
    }

    flags = fcntl(fd, F_GETFD);

    if (flags == -1
        || fcntl(fd, F_SETFD, flags | FD_CLOEXEC) == -1) {
        return -1;
    }

    return 0;
}

int signals_init(void)
{
    if (pipe(event_pipe) == -1) {
        perror("msh: signal pipe");
        return -1;
    }

    if (configure_fd(event_pipe[0]) == -1
        || configure_fd(event_pipe[1]) == -1) {
        perror("msh: configure signal pipe");
        signals_close();
        return -1;
    }

    struct sigaction action = {0};

    action.sa_handler = child_status_changed;
    sigemptyset(&action.sa_mask);

    if (sigaction(SIGCHLD, &action, NULL) == -1) {
        perror("msh: install SIGCHLD handler");
        signals_close();
        return -1;
    }

    return 0;
}

int signals_event_fd(void)
{
    return event_pipe[0];
}

void signals_drain(void)
{
    char buffer[128];

    for (;;) {
        ssize_t count = read(event_pipe[0], buffer, sizeof(buffer));

        if (count > 0) {
            continue;
        }

        if (count == -1 && errno == EINTR) {
            continue;
        }

        break;
    }
}

void signals_close(void)
{
    struct sigaction action = {0};

    action.sa_handler = SIG_DFL;
    sigemptyset(&action.sa_mask);
    sigaction(SIGCHLD, &action, NULL);

    if (event_pipe[0] != -1) {
        close(event_pipe[0]);
        event_pipe[0] = -1;
    }

    if (event_pipe[1] != -1) {
        close(event_pipe[1]);
        event_pipe[1] = -1;
    }
}
