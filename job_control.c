#define _POSIX_C_SOURCE 200809L

#include "job_control.h"
#include "terminal.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

static int restore_job_modes(const Job *job)
{
    if (!terminal_is_interactive() || !job->has_terminal_modes) {
        return 0;
    }

    while (tcsetattr(STDIN_FILENO, TCSADRAIN,
                     &job->terminal_modes) == -1) {
        if (errno != EINTR) {
            perror("msh: restore job terminal modes");
            return -1;
        }
    }

    return 0;
}

static int get_job_status(const Job *job)
{
    if (jobs_get_state(job) == JOB_STOPPED) {
        for (size_t i = 0; i < job->process_count; i++) {
            if (job->processes[i].stopped) {
                return 128 + WSTOPSIG(job->processes[i].wait_status);
            }
        }
    }

    int status =
        job->processes[job->process_count - 1].wait_status;

    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }

    if (WIFSIGNALED(status)) {
        return 128 + WTERMSIG(status);
    }

    return 1;
}

int job_foreground(Job *job, int resume, int *command_status)
{
    if (job->pgid <= 0 || job->process_count == 0) {
        fprintf(stderr, "msh: invalid foreground job\n");
        return -1;
    }

    int failed = 0;

    *command_status = 1;

    if (terminal_give_to(job->pgid) == -1) {
        /* Attempt restoration even after a failed handoff. */
        terminal_reclaim();
        return -1;
    }

    job->background = 0;

    if (resume) {
        if (restore_job_modes(job) == -1) {
            failed = 1;
            goto reclaim;
        }

        if (kill(-job->pgid, SIGCONT) == -1) {
            perror("msh: continue foreground job");
            failed = 1;
            goto reclaim;
        }

        jobs_mark_running(job);
    }

    while (jobs_get_state(job) == JOB_RUNNING) {
        int status;
        pid_t pid = waitpid(-1, &status, WUNTRACED | WCONTINUED);

        if (pid == -1) {
            if (errno == EINTR) {
                continue;
            }

            perror("msh: wait for foreground job");
            failed = 1;
            break;
        }

        /*
         * Record every returned child's status, including another
         * job finishing while this one owns the foreground.
         */
        jobs_record_status(pid, status);
    }

    if (!failed && jobs_get_state(job) == JOB_STOPPED
        && terminal_is_interactive()) {
        int result;

        do {
            result = tcgetattr(STDIN_FILENO, &job->terminal_modes);
        } while (result == -1 && errno == EINTR);

        if (result == -1) {
            perror("msh: save stopped job terminal modes");
            failed = 1;
        } else {
            job->has_terminal_modes = 1;
        }
    }

reclaim:
    if (terminal_reclaim() == -1) {
        failed = 1;
    }

    if (failed) {
        return -1;
    }

    *command_status = get_job_status(job);
    return 0;
}
