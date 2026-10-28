#define _POSIX_C_SOURCE 200809L

#include "jobs.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <sys/wait.h>
#include <unistd.h>

static int failures;

static void check(int condition, const char *description)
{
    printf("%s: %s\n", condition ? "PASS" : "FAIL", description);

    if (!condition) {
        failures++;
    }
}

static int collect(pid_t pid, int options)
{
    int status;
    pid_t result;

    do {
        result = waitpid(pid, &status, options);
    } while (result == -1 && errno == EINTR);

    if (result == -1) {
        perror("waitpid");
        return -1;
    }

    /* Sample values test recording rules, not actual usage. */
    struct rusage sample = {0};
    sample.ru_utime.tv_usec = 250000;
    sample.ru_maxrss = 4096;
    jobs_record_usage(result, status, &sample);
    return 0;
}

int main(void)
{
    Job *job = jobs_create("job state test");

    if (job == NULL) {
        return 1;
    }

    int id = job->id;
    pid_t child = fork();

    if (child == -1) {
        perror("fork");
        jobs_remove(job);
        return 1;
    }

    if (child == 0) {
        raise(SIGSTOP);

        for (;;) {
            pause();
        }
    }

    job->process_count = 1;
    job->processes[0].pid = child;

    check(jobs_find(id) == job, "job lookup");
    check(jobs_get_state(job) == JOB_RUNNING, "initially running");

    if (collect(child, WUNTRACED) == -1) {
        goto cleanup;
    }

    check(jobs_get_state(job) == JOB_STOPPED,
          "stopped child makes job stopped");

    check(!job->processes[0].has_usage,
          "stopped child has no final resource measurements");

    if (kill(child, SIGCONT) == -1) {
        perror("SIGCONT");
        failures++;
        goto cleanup;
    }

    jobs_mark_running(job);

    check(jobs_get_state(job) == JOB_RUNNING,
          "resume marks job running");

    if (collect(child, WCONTINUED) == -1) {
        goto cleanup;
    }

    check(jobs_get_state(job) == JOB_RUNNING,
          "continued status keeps job running");

    check(!job->processes[0].has_usage,
          "continued child has no final resource measurements");

    if (kill(child, SIGSTOP) == -1) {
        perror("SIGSTOP");
        failures++;
        goto cleanup;
    }

    if (collect(child, WUNTRACED) == -1) {
        goto cleanup;
    }

    /*
     * Add an already-completed stage to check that it does not
     * prevent the unfinished, stopped stage from defining the job.
     */
    job->process_count = 2;
    job->processes[1].completed = 1;

    check(jobs_get_state(job) == JOB_STOPPED,
          "completed plus stopped stages means stopped");

    check(!job->processes[0].has_usage,
          "repeated stop still has no final resource measurements");

    if (kill(child, SIGKILL) == -1) {
        perror("SIGKILL");
        failures++;
        goto cleanup;
    }

    if (collect(child, 0) == -1) {
        goto cleanup;
    }

    check(jobs_get_state(job) == JOB_DONE,
          "all stages completed means done");

    check(job->processes[0].has_usage
          && job->processes[0].usage.ru_utime.tv_usec == 250000
          && job->processes[0].usage.ru_maxrss == 4096,
          "signal termination saves final resource measurements");

    jobs_remove(job);
    check(jobs_find(id) == NULL, "removed job is absent");

    printf("Failures: %d\n", failures);
    return failures == 0 ? 0 : 1;

cleanup:
    kill(child, SIGKILL);

    while (waitpid(child, NULL, 0) == -1 && errno == EINTR) {
    }

    jobs_remove(job);
    return 1;
}
