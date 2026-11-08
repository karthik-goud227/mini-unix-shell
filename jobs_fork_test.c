
#define _POSIX_C_SOURCE 200809L
#include "jobs.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static int checks;
static int failures;

static void check(const char *name, int passed)
{
    checks++;
    printf("%s: %s\n", passed ? "PASS" : "FAIL", name);
    if (!passed) {
        failures++;
    }
}

int main(void)
{
    /* Synthetic completed record: no real process is signalled. */
    Job *finished = jobs_create("completed before fork");
    if (finished == NULL) {
        return 1;
    }
    finished->process_count = 1;
    finished->processes[0].completed = 1;
    jobs_remove(finished);

    Job *retained = jobs_create("parent job");
    if (retained == NULL) {
        return 1;
    }
    int retained_id = retained->id;

    fflush(NULL);
    pid_t child = fork();
    if (child == -1) {
        perror("fork");
        jobs_remove(retained);
        return 1;
    }

    if (child == 0) {
        jobs_reset_after_fork();

        int passed = jobs_find(retained_id) == NULL
            && jobs_statistics_count() == 0;

        /* Repeated reset must also be safe. */
        jobs_reset_after_fork();

        Job *fresh = jobs_create("child job");
        passed = passed && fresh != NULL;
        if (fresh != NULL) {
            passed = passed && fresh->id == 1;
        }

        jobs_reset_after_fork();
        passed = passed && jobs_find(1) == NULL
            && jobs_statistics_count() == 0;
        _exit(passed ? 0 : 1);
    }

    int status;
    pid_t waited;
    do {
        waited = waitpid(child, &status, 0);
    } while (waited == -1 && errno == EINTR);

    check("child reset clears records, resets numbering and is repeatable",
          waited == child && WIFEXITED(status)
          && WEXITSTATUS(status) == 0);
    check("parent job remains registered",
          jobs_find(retained_id) == retained);
    check("parent command text remains intact",
          strcmp(retained->command_text, "parent job") == 0);
    check("parent statistics count remains unchanged",
          jobs_statistics_count() == 1);

    JobStatistics summary;
    check("parent statistics contents remain intact",
          jobs_statistics_get(0, &summary) == 0
          && strcmp(summary.command_text, "completed before fork") == 0);

    Job *next = jobs_create("next parent job");
    check("parent numbering continues independently",
          next != NULL && next->id == retained_id + 1);

    jobs_remove(next);
    jobs_remove(retained);

    printf("\nChecks: %d, failures: %d\n", checks, failures);
    return failures ? 1 : 0;
}
