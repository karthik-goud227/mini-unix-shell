#ifndef MSH_JOBS_H
#define MSH_JOBS_H

#include <stddef.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <termios.h>

#define MSH_MAX_JOBS 64
#define MSH_MAX_PROCESSES 16

typedef struct {
    pid_t pid;
    int wait_status;
    int completed;
    int stopped;

    /* Final resource measurements collected when the child terminates. */
    struct rusage usage;
    int has_usage;
} Process;

typedef enum {
    JOB_RUNNING,
    JOB_STOPPED,
    JOB_DONE
} JobState;

typedef struct {
    int id;
    pid_t pgid;

    Process processes[MSH_MAX_PROCESSES];
    size_t process_count;

    char *command_text;
    int background;
    int notified;

    struct termios terminal_modes;
    int has_terminal_modes;
} Job;

/*
 * Completed-job history is limited to 16 entries.
 * Command text fits the shell's 4096-byte input limit plus NUL.
 */
#define MSH_STATS_HISTORY 16
#define MSH_STATS_COMMAND_SIZE 4097

typedef struct {
    int job_id;
    char command_text[MSH_STATS_COMMAND_SIZE];

    size_t process_count;
    size_t measured_process_count;

    double user_cpu_seconds;
    double system_cpu_seconds;

    /* Largest individual process ru_maxrss, not total pipeline peak. */
    long largest_process_maxrss;
} JobStatistics;

/* Completed-job history: index 0 is the newest entry. */
size_t jobs_statistics_count(void);

/* Copy one summary; return 0 on success, -1 for invalid arguments. */
int jobs_statistics_get(size_t index, JobStatistics *result);

/* Creates a registered job with owned command text. */
Job *jobs_create(const char *command_text);

/* Removes the job from the table and frees its memory. */
void jobs_remove(Job *job);

Job *jobs_find(int id);

/* Records one waitpid() result in the matching process. */
void jobs_record_status(pid_t pid, int wait_status);

/* Record status and final resource usage, when available. */
void jobs_record_usage(pid_t pid, int wait_status,
                       const struct rusage *usage);

JobState jobs_get_state(const Job *job);

/* Marks unfinished processes running after a successful SIGCONT. */
void jobs_mark_running(Job *job);


/* Collect available child statuses without blocking. */
void jobs_reap_pending(void);

void jobs_notify(void);

int jobs_have_active(void);

/* Wait until all running jobs finish or stop. Returns 1 if jobs stop or waiting fails. */
int jobs_wait_all(void);

int jobs_print(void);

/* Terminate remaining jobs, reap children and release the table. */
void jobs_shutdown(void);

/*
 * Child-only: discard inherited job records and statistics after fork.
 * Does not signal or wait for any process.
 */
void jobs_reset_after_fork(void);

#endif
