#define _POSIX_C_SOURCE 200809L

#include "builtins.h"
#include "variables.h"
#include "assignment.h"
#include "history.h"
#include "jobs.h"
#include "job_control.h"
#include "terminal.h"
#include "redirection.h"
#include "parameters.h"
#include "input.h"

#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <inttypes.h>
#include <signal.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int test_number(const char *text, intmax_t *value)
{
    if (*text == '\0') return -1;
    errno = 0;
    char *end;
    intmax_t parsed = strtoimax(text, &end, 10);
    if (errno == ERANGE || *end != '\0') return -1;
    *value = parsed;
    return 0;
}

/* Return 0 for true, 1 for false, and 2 for invalid expressions. */
static int builtin_test(const Command *command)
{
    const char *name = command->argv[0];
    size_t end = command->argc;
    if (strcmp(name, "[") == 0) {
        if (end < 2 || strcmp(command->argv[end - 1], "]") != 0) {
            fprintf(stderr, "msh: [: missing ]\n");
            return 2;
        }
        end--;
    }
    const char **arg = (const char **)command->argv + 1;
    size_t count = end - 1;
    int negate = 0;
    while (count > 0 && strcmp(*arg, "!") == 0) {
        negate = !negate;
        arg++;
        count--;
    }
    int truth = 0;
    if (count == 0) {
        truth = 0;
    } else if (count == 1) {
        truth = **arg != '\0';
    } else if (count == 2) {
        const char *operand = arg[1];
        if (strcmp(arg[0], "-n") == 0) truth = *operand != '\0';
        else if (strcmp(arg[0], "-z") == 0) truth = *operand == '\0';
        else if (strcmp(arg[0], "-e") == 0) truth = access(operand, F_OK) == 0;
        else if (strcmp(arg[0], "-r") == 0) truth = access(operand, R_OK) == 0;
        else if (strcmp(arg[0], "-w") == 0) truth = access(operand, W_OK) == 0;
        else if (strcmp(arg[0], "-x") == 0) truth = access(operand, X_OK) == 0;
        else if (strcmp(arg[0], "-f") == 0 || strcmp(arg[0], "-d") == 0) {
            struct stat info;
            truth = stat(operand, &info) == 0
                && (strcmp(arg[0], "-f") == 0 ? S_ISREG(info.st_mode)
                                                : S_ISDIR(info.st_mode));
        } else goto invalid;
    } else if (count == 3) {
        const char *op = arg[1];
        if (strcmp(op, "=") == 0) truth = strcmp(arg[0], arg[2]) == 0;
        else if (strcmp(op, "!=") == 0) truth = strcmp(arg[0], arg[2]) != 0;
        else if (strcmp(op, "-eq") == 0 || strcmp(op, "-ne") == 0
                 || strcmp(op, "-lt") == 0 || strcmp(op, "-le") == 0
                 || strcmp(op, "-gt") == 0 || strcmp(op, "-ge") == 0) {
            intmax_t left, right;
            if (test_number(arg[0], &left) == -1
                || test_number(arg[2], &right) == -1) goto invalid;
            if (strcmp(op, "-eq") == 0) truth = left == right;
            else if (strcmp(op, "-ne") == 0) truth = left != right;
            else if (strcmp(op, "-lt") == 0) truth = left < right;
            else if (strcmp(op, "-le") == 0) truth = left <= right;
            else if (strcmp(op, "-gt") == 0) truth = left > right;
            else truth = left >= right;
        } else goto invalid;
    } else goto invalid;
    return (truth != negate) ? 0 : 1;
invalid:
    fprintf(stderr, "msh: %s: invalid expression\n", name);
    return 2;
}

static int builtin_shift(const Command *command)
{
    if (command->argc > 2) {
        fprintf(stderr, "msh: shift: expected zero or one count\n");
        return 2;
    }
    size_t count = 1;
    if (command->argc == 2) {
        const char *argument = command->argv[1];
        if (*argument == '\0') {
            fprintf(stderr, "msh: shift: count must be a nonnegative integer\n");
            return 2;
        }
        for (const char *digit = argument; *digit != '\0'; digit++) {
            if (*digit < '0' || *digit > '9') {
                fprintf(stderr, "msh: shift: count must be a nonnegative integer\n");
                return 2;
            }
        }
        errno = 0;
        uintmax_t parsed = strtoumax(argument, NULL, 10);
        if (errno == ERANGE || parsed > SIZE_MAX) {
            fprintf(stderr, "msh: shift: count is too large\n");
            return 2;
        }
        count = (size_t)parsed;
    }
    if (parameters_shift(count) == -1) {
        fprintf(stderr, "msh: shift: count exceeds $#\n");
        return 1;
    }
    return 0;
}

static int builtin_read(const Command *command)
{
    if (command->argc != 2
        || !variables_valid_name(command->argv[1])) {
        fprintf(stderr, "msh: read: expected one variable name\n");
        return 2;
    }

    char *line = NULL;
    size_t capacity = 0;
    int result = input_read_line_fd(STDIN_FILENO, &line, &capacity);
    if (result == -1 || result == -2) {
        free(line);
        return 1;
    }
    if (result == -3) {
        free(line);
        return 130;
    }
    if (variables_set(command->argv[1], result == 0 ? "" : line, 0) == -1) {
        perror("msh: read");
        free(line);
        return 1;
    }
    free(line);
    return result == 0 ? 1 : 0;
}

static int builtin_set(const Command *command)
{
    if (command->argc < 2 || strcmp(command->argv[1], "--") != 0) {
        fprintf(stderr, "msh: set: expected set -- [arguments...]\n");
        return 2;
    }
    if (parameters_set(parameters_get(0), command->argc - 2,
                       command->argc > 2 ? command->argv + 2 : NULL) == -1) {
        perror("msh: set arguments");
        return 1;
    }
    return 0;
}

static int builtin_umask(const Command *command)
{
    if (command->argc == 1) {
        mode_t current = umask(0);
        umask(current);
        return printf("%04o\n", (unsigned int)current) < 0 ? 1 : 0;
    }
    if (command->argc != 2 || command->argv[1][0] == '\0') {
        fprintf(stderr, "msh: umask: expected an octal mask\n");
        return 2;
    }
    unsigned int mask = 0;
    for (const char *digit = command->argv[1]; *digit != '\0'; digit++) {
        if (*digit < '0' || *digit > '7' || mask > 0777U / 8U) {
            fprintf(stderr, "msh: umask: expected 0000 through 0777\n");
            return 2;
        }
        mask = mask * 8U + (unsigned int)(*digit - '0');
    }
    if (mask > 0777U) {
        fprintf(stderr, "msh: umask: expected 0000 through 0777\n");
        return 2;
    }
    umask((mode_t)mask);
    return 0;
}

static int builtin_exec(const Command *command)
{
    if (command->argc < 2) {
        fprintf(stderr, "msh: exec: expected a command\n");
        return 2;
    }
    if (terminal_is_interactive()) {
        fprintf(stderr, "msh: exec: supported in script mode only\n");
        return 2;
    }
    fflush(NULL);
    execvp(command->argv[1], command->argv + 1);
    int saved_errno = errno;
    fprintf(stderr, "msh: exec: %s: %s\n", command->argv[1],
            strerror(saved_errno));
    return saved_errno == ENOENT || saved_errno == ENOTDIR ? 127 : 126;
}

static int builtin_stats(const Command *command)
{
    if (command->argc != 1) {
        fprintf(stderr, "msh: stats: arguments are not supported\n");
        return 2;
    }

    size_t count = jobs_statistics_count();

    if (count == 0) {
        return puts("No completed job statistics.") == EOF ? 1 : 0;
    }

    if (puts("Completed jobs (newest first):") == EOF) {
        return 1;
    }

    for (size_t i = 0; i < count; i++) {
        JobStatistics summary;

        if (jobs_statistics_get(i, &summary) == -1) {
            fprintf(stderr, "msh: stats: could not read history\n");
            return 1;
        }

        if (printf("[%d] %s\n"
                   "  Measured processes: %zu/%zu\n",
                   summary.job_id, summary.command_text,
                   summary.measured_process_count,
                   summary.process_count) < 0) {
            return 1;
        }

        if (summary.measured_process_count == 0) {
            if (puts("  Resource measurements unavailable.") == EOF) {
                return 1;
            }
            continue;
        }

        if (printf("  User CPU: %.6f s; System CPU: %.6f s\n"
                   "  Largest process max RSS: %ld KiB\n",
                   summary.user_cpu_seconds,
                   summary.system_cpu_seconds,
                   summary.largest_process_maxrss) < 0) {
            return 1;
        }

        if (summary.measured_process_count < summary.process_count) {
            if (puts("  Partial measurements: measured processes only.")
                == EOF) {
                return 1;
            }
        }
    }

    if (puts("Memory is the largest individual process peak, "
             "not the total pipeline peak.") == EOF) {
        return 1;
    }

    return 0;
}

static int valid_environment_name(const char *name, size_t length)
{
    if (length == 0) {
        return 0;
    }

    for (size_t i = 0; i < length; i++) {
        char c = name[i];
        int letter = (c >= 'A' && c <= 'Z')
                  || (c >= 'a' && c <= 'z');
        int digit = c >= '0' && c <= '9';

        if (!(letter || c == '_' || (i > 0 && digit))) {
            return 0;
        }
    }

    return 1;
}

static int builtin_export(const Command *command)
{
    if (command->argc != 2) {
        fprintf(stderr, "msh: export: usage: export NAME or export NAME=value\n");
        return 2;
    }

    const char *assignment = command->argv[1];
    const char *equals = strchr(assignment, '=');

    if (equals == NULL) {
        if (!variables_valid_name(assignment)) {
            fprintf(stderr, "msh: export: invalid variable name\n");
            return 2;
        }

        if (variables_export(assignment) == -1) {
            if (errno == ENOENT) {
                fprintf(stderr, "msh: export: variable is not set: %s\n",
                        assignment);
            } else {
                perror("msh: export");
            }
            return 1;
        }

        return 0;
    }

    size_t length = (size_t)(equals - assignment);

    if (!valid_environment_name(assignment, length)) {
        fprintf(stderr, "msh: export: invalid variable name\n");
        return 2;
    }

    char *name = strndup(assignment, length);
    if (name == NULL) {
        perror("msh: export");
        return 1;
    }

    int result = variables_set(name, equals + 1, 1);
    int saved_errno = errno;
    free(name);

    if (result == -1) {
        errno = saved_errno;
        perror("msh: export");
        return 1;
    }

    return 0;
}

static int builtin_unset(const Command *command)
{
    if (command->argc != 2) {
        fprintf(stderr, "msh: unset: usage: unset NAME\n");
        return 2;
    }

    const char *name = command->argv[1];

    if (!valid_environment_name(name, strlen(name))) {
        fprintf(stderr, "msh: unset: invalid variable name\n");
        return 2;
    }

    if (variables_unset(name) == -1) {
        perror("msh: unset");
        return 1;
    }

    return 0;
}

static int builtin_readonly(const Command *command)
{
    if (command->argc < 2) {
        fprintf(stderr, "msh: readonly: expected NAME or NAME=value\n");
        return 2;
    }
    for (size_t i = 1; i < command->argc; i++) {
        const char *argument = command->argv[i];
        const char *equals = strchr(argument, '=');
        size_t length = equals == NULL ? strlen(argument)
            : (size_t)(equals - argument);
        if (!valid_environment_name(argument, length)) {
            fprintf(stderr, "msh: readonly: invalid variable name\n");
            return 2;
        }
    }
    for (size_t i = 1; i < command->argc; i++) {
        const char *argument = command->argv[i];
        const char *equals = strchr(argument, '=');
        size_t length = equals == NULL ? strlen(argument)
            : (size_t)(equals - argument);
        char *name = strndup(argument, length);
        if (name == NULL) {
            perror("msh: readonly");
            return 1;
        }
        int result = 0;
        if (equals != NULL) result = variables_set(name, equals + 1, 0);
        if (result == 0) result = variables_mark_readonly(name);
        if (result == -1) perror("msh: readonly");
        free(name);
        if (result == -1) return 1;
    }
    return 0;
}

static int builtin_history(const Command *command)
{
    if (command->argc == 1) {
        if (history_print() == -1) {
            perror("msh: history");
            return 1;
        }
        return 0;
    }

    if (command->argc == 2
        && strcmp(command->argv[1], "-c") == 0) {
        history_clear();
        return 0;
    }

    fprintf(stderr, "msh: history: usage: history [-c]\n");
    return 2;
}

static int builtin_cd(const Command *command)
{
    if (command->argc > 2) {
        fprintf(stderr, "msh: cd: expected zero or one path\n");
        return 2;
    }

    const char *path;

    if (command->argc == 1) {
        path = variables_get("HOME");

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
        if (command->expansion_empty) {
            result.handled = 1;
            result.status = 0;
        }
        if (command->assignment_count > 0) {
            char error[128];
            result.handled = 1;
            result.status = 0;

            if (assignments_apply(command->assignments,
                                  command->assignment_count,
                                  0, last_status,
                                  error, sizeof(error)) == -1) {
                fprintf(stderr, "msh: %s\n", error);
                result.status = 1;
            }
        }
        return result;
    }

    const char *name = command->argv[0];

    if (strcmp(name, ":") == 0) {
        /* Arguments have already been expanded; the null command succeeds. */
        result.handled = 1;
        result.status = 0;
    } else if (strcmp(name, "test") == 0 || strcmp(name, "[") == 0) {
        result.handled = 1;
        result.status = builtin_test(command);
    } else if (strcmp(name, "shift") == 0) {
        result.handled = 1;
        result.status = builtin_shift(command);
    } else if (strcmp(name, "read") == 0) {
        result.handled = 1;
        result.status = builtin_read(command);
    } else if (strcmp(name, "set") == 0) {
        result.handled = 1;
        result.status = builtin_set(command);
    } else if (strcmp(name, "umask") == 0) {
        result.handled = 1;
        result.status = builtin_umask(command);
    } else if (strcmp(name, "exec") == 0) {
        result.handled = 1;
        result.status = builtin_exec(command);
    } else if (strcmp(name, "wait") == 0) {
        result.handled = 1;
        if (command->argc != 1) {
            fprintf(stderr, "msh: wait: arguments are not supported\n");
            result.status = 2;
        } else {
            result.status = jobs_wait_all();
        }
    } else if (strcmp(name, "history") == 0) {
        result.handled = 1;
        result.status = builtin_history(command);
    } else if (strcmp(name, "unset") == 0) {
        result.handled = 1;
        result.status = builtin_unset(command);
    } else if (strcmp(name, "readonly") == 0) {
        result.handled = 1;
        result.status = builtin_readonly(command);
    } else if (strcmp(name, "export") == 0) {
        result.handled = 1;
        result.status = builtin_export(command);
    } else if (strcmp(name, "stats") == 0) {
        result.handled = 1;
        result.status = builtin_stats(command);
    } else if (strcmp(name, "bg") == 0) {
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
        return command->assignment_count > 0 || command->expansion_empty;
    }

    const char *name = command->argv[0];

    return strcmp(name, ":") == 0
        || strcmp(name, "test") == 0
        || strcmp(name, "[") == 0
        || strcmp(name, "shift") == 0
        || strcmp(name, "read") == 0
        || strcmp(name, "set") == 0
        || strcmp(name, "umask") == 0
        || strcmp(name, "exec") == 0
        || strcmp(name, "wait") == 0
        || strcmp(name, "cd") == 0
        || strcmp(name, "pwd") == 0
        || strcmp(name, "exit") == 0
        || strcmp(name, "jobs") == 0
        || strcmp(name, "stats") == 0
        || strcmp(name, "export") == 0
        || strcmp(name, "unset") == 0
        || strcmp(name, "readonly") == 0
        || strcmp(name, "history") == 0
        || strcmp(name, "fg") == 0
        || strcmp(name, "bg") == 0;
}

static BuiltinResult run_builtin_with_prefixes(const Command *command,
                                               int last_status)
{
    if (command->argc > 0 && command->assignment_count > 0
        && strcmp(command->argv[0], "readonly") == 0) {
        fprintf(stderr, "msh: readonly: assignment prefixes are not supported\n");
        return (BuiltinResult) {.handled = 1, .status = 2};
    }
    if (command->argc == 0 || command->assignment_count == 0) {
        return run_builtin(command, last_status);
    }

    BuiltinResult result = {.handled = 1, .status = 1};
    VariableSnapshot **saved =
        calloc(command->assignment_count, sizeof(*saved));
    if (saved == NULL) {
        perror("msh: save assignment variables");
        return result;
    }

    size_t count = 0;
    for (; count < command->assignment_count; count++) {
        size_t length;
        if (!assignment_name_length(command->assignments[count], &length)) {
            fprintf(stderr, "msh: invalid built-in assignment\n");
            goto discard;
        }

        char *name = strndup(command->assignments[count], length);
        if (name == NULL) {
            perror("msh: save assignment name");
            goto discard;
        }

        int status = variables_save(name, &saved[count]);
        free(name);
        if (status == -1) {
            perror("msh: save assignment variable");
            goto discard;
        }
    }

    char error[128];
    if (assignments_apply(command->assignments, command->assignment_count,
                          1, last_status, error, sizeof(error)) == -1) {
        fprintf(stderr, "msh: %s\n", error);
    } else {
        result = run_builtin(command, last_status);
    }

    while (count > 0) {
        VariableSnapshot *snapshot = saved[--count];
        if (variables_restore(snapshot) == -1) {
            perror("msh: restore assignment variable");
            variables_snapshot_free(snapshot);
            result.status = 1;
            result.should_exit = 1;
        }
    }
    free(saved);
    return result;

discard:
    while (count > 0) {
        variables_snapshot_free(saved[--count]);
    }
    free(saved);
    return result;
}

BuiltinResult run_builtin_redirected(const Command *command,
                                     int last_status)
{
    if (!is_builtin(command)) {
        return (BuiltinResult) {0};
    }

    if (command->argc > 0 && command->redirection_count > 0
        && (strcmp(command->argv[0], "fg") == 0
            || strcmp(command->argv[0], "bg") == 0)) {
        fprintf(stderr, "msh: %s: redirection is not supported\n",
                command->argv[0]);
        return (BuiltinResult) {.handled = 1, .status = 2};
    }

    if (command->redirection_count == 0) {
        return run_builtin_with_prefixes(command, last_status);
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
        result = run_builtin_with_prefixes(command, last_status);
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
