#define _POSIX_C_SOURCE 200809L

#include "jobs.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static int failures;
static int total;

static void check(int condition, const char *description)
{
    total++;
    printf("%s: %s\n", condition ? "PASS" : "FAIL", description);
    if (!condition) {
        failures++;
    }
}

int main(void)
{
    Job *job = jobs_create("real resource collection test");
    if (job == NULL) {
        return 1;
    }

    pid_t child = fork();
    if (child == -1) {
        perror("fork");
        jobs_remove(job);
        return 1;
    }

    if (child == 0) {
        volatile unsigned long value = 1;

        for (unsigned long i = 0; i < 10000000UL; i++) {
            value = value * 33UL + i;
        }

        _exit(7);
    }

    job->process_count = 1;
    job->processes[0].pid = child;

    /* Poll for at most roughly five seconds. */
    for (int i = 0; i < 500; i++) {
        jobs_reap_pending();

        if (job->processes[0].completed) {
            break;
        }

        struct timespec delay = {
            .tv_sec = 0,
            .tv_nsec = 10000000L
        };

        while (nanosleep(&delay, &delay) == -1 && errno == EINTR) {
        }
    }

    Process *process = &job->processes[0];

    check(process->completed, "child completion collected");

    if (!process->completed) {
        kill(child, SIGKILL);
        while (waitpid(child, NULL, 0) == -1 && errno == EINTR) {
        }
        jobs_remove(job);
        return 1;
    }

    check(WIFEXITED(process->wait_status)
          && WEXITSTATUS(process->wait_status) == 7,
          "child exit status preserved");

    check(process->has_usage, "real resource measurements available");

    check(process->usage.ru_utime.tv_sec >= 0
          && process->usage.ru_utime.tv_usec >= 0
          && process->usage.ru_utime.tv_usec < 1000000
          && process->usage.ru_stime.tv_sec >= 0
          && process->usage.ru_stime.tv_usec >= 0
          && process->usage.ru_stime.tv_usec < 1000000,
          "CPU time fields are valid");

    check(process->usage.ru_maxrss >= 0,
          "maximum resident memory is nonnegative");

    printf("\nCollected measurements:\n");
    printf("User CPU:   %ld.%06ld seconds\n",
           (long)process->usage.ru_utime.tv_sec,
           (long)process->usage.ru_utime.tv_usec);
    printf("System CPU: %ld.%06ld seconds\n",
           (long)process->usage.ru_stime.tv_sec,
           (long)process->usage.ru_stime.tv_usec);
    printf("Max RSS:    %ld (raw ru_maxrss)\n",
           process->usage.ru_maxrss);

    jobs_remove(job);

    printf("\nTests: %d, failures: %d\n", total, failures);
    return failures ? 1 : 0;
}
