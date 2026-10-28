#include "jobs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int total;
static int failures;

static void check(int condition, const char *description)
{
    total++;
    printf("%s: %s\n", condition ? "PASS" : "FAIL", description);
    if (!condition) {
        failures++;
    }
}

static Job *create_job(const char *command)
{
    Job *job = jobs_create(command);
    if (job == NULL) {
        fprintf(stderr, "Could not create test job\n");
        exit(EXIT_FAILURE);
    }
    return job;
}

int main(void)
{
    JobStatistics summary = {0};

    check(jobs_statistics_count() == 0, "history initially empty");
    check(jobs_statistics_get(0, &summary) == -1,
          "missing entry rejected");
    check(jobs_statistics_get(0, NULL) == -1,
          "NULL destination rejected");

    Job *job = create_job("empty job");
    jobs_remove(job);
    check(jobs_statistics_count() == 0, "empty job not saved");

    job = create_job("unfinished job");
    job->process_count = 1;
    jobs_remove(job);
    check(jobs_statistics_count() == 0, "unfinished job not saved");

    job = create_job("two-stage pipeline");
    int pipeline_id = job->id;
    job->process_count = 2;

    for (size_t i = 0; i < 2; i++) {
        job->processes[i].completed = 1;
        job->processes[i].has_usage = 1;
    }

    job->processes[0].usage.ru_utime.tv_sec = 1;
    job->processes[0].usage.ru_utime.tv_usec = 250000;
    job->processes[0].usage.ru_stime.tv_usec = 250000;
    job->processes[0].usage.ru_maxrss = 4096;

    job->processes[1].usage.ru_utime.tv_usec = 500000;
    job->processes[1].usage.ru_stime.tv_usec = 500000;
    job->processes[1].usage.ru_maxrss = 8192;

    jobs_remove(job);

    check(jobs_statistics_count() == 1
          && jobs_statistics_get(0, &summary) == 0,
          "completed job saved");

    check(summary.job_id == pipeline_id
          && strcmp(summary.command_text, "two-stage pipeline") == 0,
          "identity and command survive job removal");

    check(summary.process_count == 2
          && summary.measured_process_count == 2,
          "measurement coverage recorded");

    check(summary.user_cpu_seconds == 1.75
          && summary.system_cpu_seconds == 0.75,
          "CPU times summed across stages");

    check(summary.largest_process_maxrss == 8192,
          "memory uses largest individual peak, not sum");

    summary.command_text[0] = 'X';
    jobs_statistics_get(0, &summary);
    check(strcmp(summary.command_text, "two-stage pipeline") == 0,
          "retrieved summary is an independent copy");

    job = create_job("partial measurements");
    int partial_id = job->id;
    job->process_count = 2;
    job->processes[0].completed = 1;
    job->processes[1].completed = 1;
    job->processes[0].has_usage = 1;
    job->processes[0].usage.ru_utime.tv_sec = 2;
    jobs_remove(job);

    jobs_statistics_get(0, &summary);
    check(summary.job_id == partial_id
          && summary.process_count == 2
          && summary.measured_process_count == 1
          && summary.user_cpu_seconds == 2.0,
          "partial measurement coverage preserved");

    jobs_statistics_get(1, &summary);
    check(summary.job_id == pipeline_id, "newest entry comes first");

    int ids[MSH_STATS_HISTORY + 2];

    for (int i = 0; i < MSH_STATS_HISTORY + 2; i++) {
        char command[64];
        snprintf(command, sizeof(command), "history entry %d", i);
        job = create_job(command);
        ids[i] = job->id;
        job->process_count = 1;
        job->processes[0].completed = 1;
        jobs_remove(job);
    }

    check(jobs_statistics_count() == MSH_STATS_HISTORY,
          "history stays within its limit");

    int ordered = 1;
    for (size_t i = 0; i < MSH_STATS_HISTORY; i++) {
        if (jobs_statistics_get(i, &summary) != 0
            || summary.job_id != ids[MSH_STATS_HISTORY + 1 - i]) {
            ordered = 0;
        }
    }
    check(ordered, "newest 16 entries retained in order");

    check(jobs_statistics_get(MSH_STATS_HISTORY, &summary) == -1,
          "index beyond history rejected");

    printf("\nTests: %d, failures: %d\n", total, failures);
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
