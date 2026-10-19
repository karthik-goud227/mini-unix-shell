#define _POSIX_C_SOURCE 200809L

#include "builtins.h"
#include "jobs.h"
#include "job_control.h"
#include "terminal.h"
#include "redirection.h"

#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int builtin_cd(const Command *command)
{
    if (command->argc > 2) {
        fprintf(stderr, "msh: cd: expected zero or one path\n");
        return 2;
    }

    const char *path;

    if (command->argc == 1) {
        path = getenv("HOME");

        if (path == NULL || path[0] == '\0') {
            fprintf(stderr, "msh: cd: HOME is not set or is empty\n");
            return 1;
        }
    } else {
        path = command->argv[1];
    }

    if (chdir(path) == -1) {
        fprintf(stderr, "msh: cd: %s: %s\n",
                path, strerror(errno));
        return 1;
    }

    return 0;
}

static int builtin_pwd(const Command *command)
{
    if (command->argc != 1) {
        fprintf(stderr, "msh: pwd: arguments are not supported\n");
        return 2;
    }

    size_t capacity = 128;
    char *path = NULL;

    for (;;) {
        char *grown = realloc(path, capacity);

        if (grown == NULL) {
            free(path);
            fprintf(stderr, "msh: pwd: out of memory\n");
            return 1;
        }

        path = grown;

        if (getcwd(path, capacity) != NULL) {
            break;
        }

        if (errno != ERANGE) {
            perror("msh: pwd");
            free(path);
            return 1;
        }

        if (capacity > (size_t)-1 / 2) {
            fprintf(stderr, "msh: pwd: path is too long\n");
            free(path);
            return 1;
        }

        capacity *= 2;
    }

    int written = puts(path);
    free(path);

    if (written == EOF) {
        perror("msh: pwd");
        return 1;
    }

    return 0;
}

static int parse_exit_status(const char *text, int *status)
{
    if (text[0] == '\0') {
        return -1;
    }

    unsigned int value = 0;

    for (size_t i = 0; text[i] != '\0'; i++) {
        if (text[i] < '0' || text[i] > '9') {
            return -1;
        }

        value = value * 10 + (unsigned int)(text[i] - '0');

        if (value > 255) {
            return -1;
        }
    }

    *status = (int)value;
    return 0;
}


static Job *requested_job(const Command *command)
{
    const char *name = command->argv[0];

    if (!terminal_is_interactive()) {
        fprintf(stderr, "msh: %s: requires an interactive terminal\n", name);
        return NULL;
    }

    if (command->argc != 2
        || command->argv[1][0] != '%'
        || command->argv[1][1] < '0'
        || command->argv[1][1] > '9') {
        fprintf(stderr, "msh: %s: usage: %s %%N\n", name, name);
        return NULL;
    }

    char *end;
    errno = 0;
    long id = strtol(command->argv[1] + 1, &end, 10);

    if (errno == ERANGE || *end != '\0' || id <= 0 || id > INT_MAX) {
        fprintf(stderr, "msh: %s: invalid job ID\n", name);
        return NULL;
    }

    jobs_reap_pending();
    Job *job = jobs_find((int)id);

    if (job == NULL) {
        fprintf(stderr, "msh: %s: no such job: %s\n",
                name, command->argv[1]);
        return NULL;
    }

    if (jobs_get_state(job) == JOB_DONE) {
        fprintf(stderr, "msh: %s: job has already finished\n", name);
        jobs_remove(job);
        return NULL;
    }

    return job;
}


static int builtin_bg(const Command *command)
{
    Job *job = requested_job(command);

    if (job == NULL) {
        return 2;
    }

    if (kill(-job->pgid, SIGCONT) == -1) {
        perror("msh: bg");
        return 1;
    }

    jobs_mark_running(job);
    job->background = 1;

    if (printf("[%d] Running  %s\n",
               job->id, job->command_text) < 0) {
        return 1;
    }

    return 0;
}

static int builtin_fg(const Command *command)
{
    Job *job = requested_job(command);

    if (job == NULL) {
        return 2;
    }

    int resume = jobs_get_state(job) == JOB_STOPPED;
    int status;

    if (job_foreground(job, resume, &status) == -1) {
        job->background = 1;
        return 1;
    }

    if (jobs_get_state(job) == JOB_DONE) {
        jobs_remove(job);
    } else {
        job->background = 1;
        fprintf(stderr, "[%d] Stopped  %s\n",
                job->id, job->command_text);
        job->notified = 1;
    }

    return status;
}

BuiltinResult run_builtin(const Command *command, int last_status)
{
    BuiltinResult result = {0};

    if (command->argc == 0) {
        return result;
    }

    const char *name = command->argv[0];

    if (strcmp(name, "bg") == 0) {
        result.handled = 1;
        result.status = builtin_bg(command);
    } else if (strcmp(name, "fg") == 0) {
        result.handled = 1;
        result.status = builtin_fg(command);
    } else if (strcmp(name, "jobs") == 0) {
        result.handled = 1;

        if (!terminal_is_interactive()) {
            fprintf(stderr, "msh: jobs: requires an interactive terminal\n");
            result.status = 2;
        } else if (command->argc != 1) {
            fprintf(stderr, "msh: jobs: arguments are not supported\n");
            result.status = 2;
        } else {
            result.status = jobs_print();
        }
    } else if (strcmp(name, "cd") == 0) {
        result.handled = 1;
        result.status = builtin_cd(command);
    } else if (strcmp(name, "pwd") == 0) {
        result.handled = 1;
        result.status = builtin_pwd(command);
    } else if (strcmp(name, "exit") == 0) {
        result.handled = 1;
        result.status = last_status;

        if (command->argc > 2) {
            fprintf(stderr, "msh: exit: expected zero or one status\n");
            result.status = 2;
        } else if (command->argc == 2
                   && parse_exit_status(command->argv[1],
                                        &result.status) == -1) {
            fprintf(stderr, "msh: exit: status must be 0 through 255\n");
            result.status = 2;
        } else {
            result.should_exit = 1;
        }
    }

    return result;
}


int is_builtin(const Command *command)
{
    if (command->argc == 0) {
        return 0;
    }

    const char *name = command->argv[0];

    return strcmp(name, "cd") == 0
        || strcmp(name, "pwd") == 0
        || strcmp(name, "exit") == 0
        || strcmp(name, "jobs") == 0
        || strcmp(name, "fg") == 0
        || strcmp(name, "bg") == 0;
}

BuiltinResult run_builtin_redirected(const Command *command,
                                     int last_status)
{
    if (!is_builtin(command)) {
        return (BuiltinResult) {0};
    }

    if (command->redirection_count > 0
        && (strcmp(command->argv[0], "fg") == 0
            || strcmp(command->argv[0], "bg") == 0)) {
        fprintf(stderr, "msh: %s: redirection is not supported\n",
                command->argv[0]);
        return (BuiltinResult) {.handled = 1, .status = 2};
    }

    if (command->redirection_count == 0) {
        return run_builtin(command, last_status);
    }

    BuiltinResult result = {
        .handled = 1,
        .status = 1,
        .should_exit = 0
    };

    fflush(NULL);

    int saved_in = dup(STDIN_FILENO);

    if (saved_in == -1) {
        perror("msh: save stdin");
        return result;
    }

    int saved_out = dup(STDOUT_FILENO);

    if (saved_out == -1) {
        perror("msh: save stdout");
        close(saved_in);
        return result;
    }

    if (apply_redirections(command) == 0) {
        result = run_builtin(command, last_status);
    }

    /*
     * Built-in output must reach its redirected destination
     * before we restore stdout.
     */
    if (fflush(stdout) == EOF) {
        perror("msh: flush stdout");
        result.status = 1;
    }

    int restore_failed = 0;

    if (dup2(saved_in, STDIN_FILENO) == -1) {
        perror("msh: restore stdin");
        restore_failed = 1;
    }

    if (dup2(saved_out, STDOUT_FILENO) == -1) {
        perror("msh: restore stdout");
        restore_failed = 1;
    }

    close(saved_in);
    close(saved_out);

    clearerr(stdin);
    clearerr(stdout);

    if (restore_failed) {
        /* Continuing with broken standard descriptors is unsafe. */
        result.status = 1;
        result.should_exit = 1;
    }

    return result;
}
