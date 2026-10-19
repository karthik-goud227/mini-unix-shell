#ifndef MSH_JOBS_H
#define MSH_JOBS_H

#include <stddef.h>
#include <sys/types.h>
#include <termios.h>

#define MSH_MAX_JOBS 64
#define MSH_MAX_PROCESSES 16

typedef struct {
    pid_t pid;
    int wait_status;
    int completed;
    int stopped;
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

/* Creates a registered job with owned command text. */
Job *jobs_create(const char *command_text);

/* Removes the job from the table and frees its memory. */
void jobs_remove(Job *job);

Job *jobs_find(int id);

/* Records one waitpid() result in the matching process. */
void jobs_record_status(pid_t pid, int wait_status);

JobState jobs_get_state(const Job *job);

/* Marks unfinished processes running after a successful SIGCONT. */
void jobs_mark_running(Job *job);


/* Collect available child statuses without blocking. */
void jobs_reap_pending(void);

void jobs_notify(void);

int jobs_have_active(void);

int jobs_print(void);

/* Terminate remaining jobs, reap children and release the table. */
void jobs_shutdown(void);

#endif
