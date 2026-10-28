#include "jobs.h"

#include <stdio.h>
#include <stdlib.h>

static int total;
static int failures;

static void check(const char *name, int passed)
{
    total++;
    printf("%s: %s\n", passed ? "PASS" : "FAIL", name);
    if (!passed) {
        failures++;
    }
}

int main(void)
{
    Job *job = jobs_create("sample pipeline");
    if (job == NULL) {
        return EXIT_FAILURE;
    }

    /* Fake PIDs: no actual children are created in this unit test. */
    job->process_count = 2;
    job->processes[0].pid = 10001;
    job->processes[1].pid = 10002;

    struct rusage sample = {0};
    sample.ru_utime.tv_sec = 1;
    sample.ru_utime.tv_usec = 250000;
    sample.ru_stime.tv_usec = 50000;
    sample.ru_maxrss = 4096;

    check("measurements initially unavailable",
          !job->processes[0].has_usage
          && !job->processes[1].has_usage);

    /* A wait status of zero represents normal exit with status zero. */
    jobs_record_usage(10002, 0, &sample);

    check("correct process marked completed",
          job->processes[1].completed
          && !job->processes[0].completed);

    check("measurements saved for correct process",
          job->processes[1].has_usage
          && !job->processes[0].has_usage);

    check("CPU and memory values copied",
          job->processes[1].usage.ru_utime.tv_sec == 1
          && job->processes[1].usage.ru_utime.tv_usec == 250000
          && job->processes[1].usage.ru_stime.tv_usec == 50000
          && job->processes[1].usage.ru_maxrss == 4096);

    sample.ru_maxrss = 8192;
    check("saved measurements are an independent copy",
          job->processes[1].usage.ru_maxrss == 4096);

    jobs_record_usage(10002, 0, &sample);
    check("duplicate report preserves original measurements",
          job->processes[1].usage.ru_maxrss == 4096);

    jobs_record_usage(10001, 0, NULL);
    check("missing measurements still record completion",
          job->processes[0].completed
          && !job->processes[0].has_usage);

    struct rusage zero = {0};
    jobs_record_usage(10001, 0, &zero);
    check("zero measurements are valid available measurements",
          job->processes[0].has_usage
          && job->processes[0].usage.ru_maxrss == 0);

    jobs_record_usage(99999, 0, &sample);
    check("unknown PID leaves tracked measurements unchanged",
          job->processes[0].usage.ru_maxrss == 0
          && job->processes[1].usage.ru_maxrss == 4096);

    jobs_remove(job);

    printf("\nTests: %d, failures: %d\n", total, failures);
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
