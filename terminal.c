#define _POSIX_C_SOURCE 200809L

#include "terminal.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <termios.h>
#include <unistd.h>

static int interactive;
static pid_t shell_pgid;
static struct termios shell_modes;

static int set_signal_action(int signal_number,
                             void (*handler)(int))
{
    struct sigaction action = {0};

    action.sa_handler = handler;
    sigemptyset(&action.sa_mask);

    return sigaction(signal_number, &action, NULL);
}

static int set_terminal_group(pid_t pgid)
{
    while (tcsetpgrp(STDIN_FILENO, pgid) == -1) {
        if (errno != EINTR) {
            return -1;
        }
    }

    return 0;
}

int terminal_is_interactive(void)
{
    return interactive;
}

int terminal_init(void)
{
    interactive = isatty(STDIN_FILENO);

    if (!interactive) {
        return 0;
    }

    /*
     * A shell launched in the background must wait until its
     * parent shell places it in the foreground.
     */
    if (set_signal_action(SIGTTIN, SIG_DFL) == -1) {
        perror("msh: initialize SIGTTIN");
        return -1;
    }

    for (;;) {
        pid_t foreground = tcgetpgrp(STDIN_FILENO);

        if (foreground == -1) {
            perror("msh: read terminal group");
            return -1;
        }

        if (foreground == getpgrp()) {
            break;
        }

        if (kill(-getpgrp(), SIGTTIN) == -1) {
            perror("msh: wait for foreground");
            return -1;
        }
    }

    const int signals[] = {
        SIGINT, SIGQUIT, SIGTSTP, SIGTTIN, SIGTTOU
    };

    for (size_t i = 0; i < sizeof(signals) / sizeof(signals[0]); i++) {
        if (set_signal_action(signals[i], SIG_IGN) == -1) {
            perror("msh: initialize signal");
            return -1;
        }
    }

    shell_pgid = getpid();

    if (getpgrp() != shell_pgid) {
        if (setpgid(0, shell_pgid) == -1) {
            perror("msh: create shell process group");
            return -1;
        }
    }

    if (set_terminal_group(shell_pgid) == -1) {
        perror("msh: claim terminal");
        return -1;
    }

    if (tcgetattr(STDIN_FILENO, &shell_modes) == -1) {
        perror("msh: save terminal modes");
        return -1;
    }

    return 0;
}

int terminal_give_to(pid_t pgid)
{
    if (!interactive) {
        return 0;
    }

    if (set_terminal_group(pgid) == -1) {
        perror("msh: transfer terminal");
        return -1;
    }

    return 0;
}

int terminal_reclaim(void)
{
    if (!interactive) {
        return 0;
    }

    if (set_terminal_group(shell_pgid) == -1) {
        perror("msh: reclaim terminal");
        return -1;
    }

    while (tcsetattr(STDIN_FILENO, TCSADRAIN, &shell_modes) == -1) {
        if (errno != EINTR) {
            perror("msh: restore terminal modes");
            return -1;
        }
    }

    return 0;
}

int terminal_prepare_child(void)
{
    const int signals[] = {
        SIGINT, SIGQUIT, SIGTSTP, SIGTTIN, SIGTTOU, SIGCHLD
    };

    for (size_t i = 0; i < sizeof(signals) / sizeof(signals[0]); i++) {
        if (set_signal_action(signals[i], SIG_DFL) == -1) {
            return -1;
        }
    }

    return 0;
}
