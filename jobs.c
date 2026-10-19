#define _POSIX_C_SOURCE 200809L

#include "jobs.h"

#include <limits.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>

static Job *job_table[MSH_MAX_JOBS];
static int next_job_id = 1;

Job *jobs_create(const char *command_text)
{
    size_t slot;

    for (slot = 0; slot < MSH_MAX_JOBS; slot++) {
        if (job_table[slot] == NULL) {
            break;
        }
    }

    if (slot == MSH_MAX_JOBS) {
        fprintf(stderr, "msh: job table is full\n");
        return NULL;
    }

    if (next_job_id == INT_MAX) {
        fprintf(stderr, "msh: job ID limit reached\n");
        return NULL;
    }

    Job *job = calloc(1, sizeof(*job));

    if (job == NULL) {
        fprintf(stderr, "msh: out of memory\n");
        return NULL;
    }

    job->command_text = strdup(command_text);

    if (job->command_text == NULL) {
        fprintf(stderr, "msh: out of memory\n");
        free(job);
        return NULL;
    }

    job->id = next_job_id++;
    job_table[slot] = job;

    return job;
}

void jobs_remove(Job *job)
{
    if (job == NULL) {
        return;
    }

    for (size_t i = 0; i < MSH_MAX_JOBS; i++) {
        if (job_table[i] == job) {
            job_table[i] = NULL;
            free(job->command_text);
            free(job);
            return;
        }
    }
}

Job *jobs_find(int id)
{
    for (size_t i = 0; i < MSH_MAX_JOBS; i++) {
        if (job_table[i] != NULL && job_table[i]->id == id) {
            return job_table[i];
        }
    }

    return NULL;
}

void jobs_record_status(pid_t pid, int wait_status)
{
    for (size_t i = 0; i < MSH_MAX_JOBS; i++) {
        Job *job = job_table[i];

        if (job == NULL) {
            continue;
        }

        for (size_t j = 0; j < job->process_count; j++) {
            Process *process = &job->processes[j];

            if (process->pid != pid) {
                continue;
            }

            process->wait_status = wait_status;

            if (WIFEXITED(wait_status) || WIFSIGNALED(wait_status)) {
                process->completed = 1;
                process->stopped = 0;
            } else if (WIFSTOPPED(wait_status)) {
                process->stopped = 1;
            } else if (WIFCONTINUED(wait_status)) {
                process->stopped = 0;
            }

            job->notified = 0;
            return;
        }
    }
}

JobState jobs_get_state(const Job *job)
{
    size_t unfinished = 0;
    size_t stopped = 0;

    for (size_t i = 0; i < job->process_count; i++) {
        const Process *process = &job->processes[i];

        if (!process->completed) {
            unfinished++;

            if (process->stopped) {
                stopped++;
            }
        }
    }

    if (unfinished == 0) {
        return JOB_DONE;
    }

    if (unfinished == stopped) {
        return JOB_STOPPED;
    }

    return JOB_RUNNING;
}

void jobs_mark_running(Job *job)
{
    for (size_t i = 0; i < job->process_count; i++) {
        if (!job->processes[i].completed) {
            job->processes[i].stopped = 0;
        }
    }

    job->notified = 0;
}


void jobs_reap_pending(void)
{
    for (;;) {
        int status;
        pid_t pid = waitpid(-1, &status,
                            WNOHANG | WUNTRACED | WCONTINUED);

        if (pid > 0) {
            jobs_record_status(pid, status);
        } else if (pid == -1 && errno == EINTR) {
            continue;
        } else {
            break;
        }
    }
}

int jobs_have_active(void)
{
    jobs_reap_pending();

    for (size_t i = 0; i < MSH_MAX_JOBS; i++) {
        if (job_table[i] != NULL
            && jobs_get_state(job_table[i]) != JOB_DONE) {
            return 1;
        }
    }

    return 0;
}

static void signal_active_jobs(int signal_number)
{
    for (size_t i = 0; i < MSH_MAX_JOBS; i++) {
        Job *job = job_table[i];

        if (job != NULL && job->pgid > 0
            && jobs_get_state(job) != JOB_DONE) {
            if (kill(-job->pgid, signal_number) == -1
                && errno != ESRCH) {
                perror("msh: signal job during shutdown");
            }
        }
    }
}

static void shutdown_grace_period(void)
{
    /* At most about 200 ms, with status collection between waits. */
    for (int i = 0; i < 20 && jobs_have_active(); i++) {
        struct timespec remaining = {
            .tv_sec = 0,
            .tv_nsec = 10000000L
        };

        while (nanosleep(&remaining, &remaining) == -1
               && errno == EINTR) {
        }
    }

    jobs_reap_pending();
}

void jobs_shutdown(void)
{
    jobs_reap_pending();

    signal_active_jobs(SIGHUP);
    signal_active_jobs(SIGCONT);
    shutdown_grace_period();

    signal_active_jobs(SIGTERM);
    shutdown_grace_period();

    signal_active_jobs(SIGKILL);

    for (size_t i = 0; i < MSH_MAX_JOBS; i++) {
        Job *job = job_table[i];

        if (job == NULL) {
            continue;
        }

        for (size_t j = 0; j < job->process_count; j++) {
            Process *process = &job->processes[j];

            if (process->completed) {
                continue;
            }

            int status;
            pid_t pid;

            do {
                pid = waitpid(process->pid, &status, 0);
            } while (pid == -1 && errno == EINTR);

            if (pid > 0) {
                jobs_record_status(pid, status);
            }
        }

        jobs_remove(job);
    }
}
