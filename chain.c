
#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L

#include "chain.h"
#include "parameters.h"
#include "executor.h"
#include "builtins.h"
#include "expansion.h"
#include "jobs.h"
#include "signals.h"
#include "terminal.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>


static volatile sig_atomic_t chain_signal;

static void chain_record_signal(int signal_number)
{
    if (chain_signal == 0) {
        chain_signal = signal_number;
    }
}

static int chain_prepare_supervisor_signals(void)
{
    if (terminal_prepare_child() == -1) {
        return -1;
    }

    const int caught[] = {SIGINT, SIGQUIT, SIGHUP, SIGTERM};
    struct sigaction action = {0};
    action.sa_handler = chain_record_signal;
    sigemptyset(&action.sa_mask);

    chain_signal = 0;

    for (size_t i = 0; i < sizeof(caught) / sizeof(caught[0]); i++) {
        if (sigaction(caught[i], &action, NULL) == -1) {
            return -1;
        }
    }

    return 0;
}

static void chain_close_pipes(int pipes[][2], size_t count)
{
    for (size_t i = 0; i < count; i++) {
        close(pipes[i][0]);
        close(pipes[i][1]);
    }
}

static _Noreturn void chain_run_stage(
    const Pipeline *pipeline, size_t index,
    int pipes[][2], size_t pipe_count,
    int gate[2], const sigset_t *execution_mask,
    int last_status)
{
    /* Children inherit the supervisor's process group. */
    close(gate[1]);

    if (terminal_prepare_child() == -1) {
        perror("msh: chain child signals");
        _exit(1);
    }

    if (sigprocmask(SIG_SETMASK, execution_mask, NULL) == -1) {
        perror("msh: chain child signal mask");
        _exit(1);
    }

    /* Wait until the supervisor has launched every pipeline stage. */
    char byte;
    ssize_t received;
    do {
        received = read(gate[0], &byte, 1);
    } while (received == -1 && errno == EINTR);

    close(gate[0]);
    if (received != 0) {
        _exit(1);
    }

    if (index > 0
        && dup2(pipes[index - 1][0], STDIN_FILENO) == -1) {
        perror("msh: chain pipe input");
        _exit(1);
    }

    if (index + 1 < pipeline->command_count
        && dup2(pipes[index][1], STDOUT_FILENO) == -1) {
        perror("msh: chain pipe output");
        _exit(1);
    }

    chain_close_pipes(pipes, pipe_count);
    execute_child_command(&pipeline->commands[index], last_status);
}

static int chain_wait_pipeline(Process processes[], size_t count)
{
    for (;;) {
        if (chain_signal != 0) {
            return -1;
        }

        size_t unfinished = 0;
        size_t stopped = 0;

        for (size_t i = 0; i < count; i++) {
            if (!processes[i].completed) {
                unfinished++;
                if (processes[i].stopped) {
                    stopped++;
                }
            }
        }

        if (unfinished == 0) {
            break;
        }

        if (unfinished == stopped) {
            /*
             * Report the stopped chain through its supervisor.
             * After resumption, continue any individually stopped stages.
             */
            if (raise(SIGSTOP) != 0) {
                perror("msh: stop chain supervisor");
                return -1;
            }

            for (size_t i = 0; i < count; i++) {
                if (!processes[i].completed && processes[i].stopped) {
                    if (kill(processes[i].pid, SIGCONT) == -1
                        && errno != ESRCH) {
                        perror("msh: resume chain stage");
                        return -1;
                    }
                    processes[i].stopped = 0;
                }
            }
        }

        int status;
        struct rusage usage = {0};
        pid_t child = wait4(-1, &status,
                            WNOHANG | WUNTRACED | WCONTINUED, &usage);

        if (child == 0) {
            struct timespec pause_time = {
                .tv_sec = 0,
                .tv_nsec = 10000000L
            };
            nanosleep(&pause_time, NULL);
            continue;
        }

        if (child == -1) {
            if (errno == EINTR) {
                continue;
            }
            perror("msh: wait for chain pipeline");
            return -1;
        }

        for (size_t i = 0; i < count; i++) {
            Process *process = &processes[i];

            if (process->pid != child) {
                continue;
            }

            process->wait_status = status;

            if (WIFEXITED(status) || WIFSIGNALED(status)) {
                process->completed = 1;
                process->stopped = 0;
                process->usage = usage;
                process->has_usage = 1;
            } else if (WIFSTOPPED(status)) {
                process->stopped = 1;
            } else if (WIFCONTINUED(status)) {
                process->stopped = 0;
            }
            break;
        }
    }

    int status = processes[count - 1].wait_status;

    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    if (WIFSIGNALED(status)) {
        return 128 + WTERMSIG(status);
    }

    return 1;
}

static void chain_cleanup_stages(Process processes[], size_t count)
{
    for (size_t i = 0; i < count; i++) {
        if (!processes[i].completed) {
            kill(processes[i].pid, SIGKILL);
        }
    }

    for (size_t i = 0; i < count; i++) {
        if (processes[i].completed) {
            continue;
        }

        while (waitpid(processes[i].pid, NULL, 0) == -1
               && errno == EINTR) {
        }
    }
}

static int chain_execute_pipeline(const Pipeline *pipeline,
                                  int last_status)
{
    size_t count = pipeline->command_count;

    if (count == 0) {
        return 0;
    }

    if (count > MSH_MAX_PROCESSES) {
        fprintf(stderr, "msh: invalid chain pipeline length\n");
        return 2;
    }

    for (size_t i = 0; i < count; i++) {
        const Command *command = &pipeline->commands[i];

        if (command->argc == 0
        && (command->assignment_count > 0 || command->expansion_empty)) {
            continue;
        }

        if (command->argc == 0 || command->argv[0][0] == '\0') {
            fprintf(stderr, "msh: empty command name\n");
            return 127;
        }

        if (is_builtin(command)
            && strcmp(command->argv[0], "pwd") != 0
            && strcmp(command->argv[0], ":") != 0) {
            fprintf(stderr,
                    "msh: %s is not supported in a chain pipeline\n",
                    command->argv[0]);
            return 2;
        }
    }

    int pipes[MSH_MAX_PROCESSES - 1][2];
    size_t opened = 0;
    size_t pipe_count = count - 1;

    for (; opened < pipe_count; opened++) {
        if (pipe(pipes[opened]) == -1) {
            perror("msh: chain pipe");
            chain_close_pipes(pipes, opened);
            return 1;
        }
    }

    int gate[2];
    if (pipe(gate) == -1) {
        perror("msh: chain launch gate");
        chain_close_pipes(pipes, pipe_count);
        return 1;
    }

    sigset_t blocked;
    sigset_t original_mask;
    sigemptyset(&blocked);
    sigaddset(&blocked, SIGCHLD);
    sigaddset(&blocked, SIGINT);
    sigaddset(&blocked, SIGQUIT);
    sigaddset(&blocked, SIGHUP);
    sigaddset(&blocked, SIGTERM);

    if (sigprocmask(SIG_BLOCK, &blocked, &original_mask) == -1) {
        perror("msh: chain launch signal mask");
        chain_close_pipes(pipes, pipe_count);
        close(gate[0]);
        close(gate[1]);
        return 1;
    }

    Process processes[MSH_MAX_PROCESSES] = {0};
    size_t launched = 0;
    int failed = 0;
    fflush(NULL);

    for (size_t i = 0; i < count; i++) {
        pid_t child = fork();

        if (child == -1) {
            perror("msh: chain fork");
            failed = 1;
            break;
        }

        if (child == 0) {
            chain_run_stage(pipeline, i, pipes, pipe_count,
                            gate, &original_mask, last_status);
        }

        processes[launched++].pid = child;
    }

    chain_close_pipes(pipes, pipe_count);
    close(gate[0]);

    if (failed) {
        /* Children remain behind the gate until cleanup finishes. */
        chain_cleanup_stages(processes, launched);
    }

    close(gate[1]);

    if (sigprocmask(SIG_SETMASK, &original_mask, NULL) == -1) {
        perror("msh: restore chain signal mask");
        if (!failed) {
            chain_cleanup_stages(processes, launched);
        }
        return 1;
    }

    if (failed) {
        return 1;
    }

    int status = chain_wait_pipeline(processes, launched);

    if (status == -1) {
        chain_cleanup_stages(processes, launched);
        return 1;
    }

    return status;
}

static int chain_copy_tokens(const TokenList *source,
                             size_t start, size_t count,
                             TokenList *result)
{
    return tokens_copy_range(source, start, count, result);
}

static int chain_evaluate(const TokenList *tokens,
                          const CommandList *list,
                          size_t start, size_t end,
                          int status)
{
    if (start >= end || end > list->count) {
        fprintf(stderr, "msh: invalid logical chain range\n");
        return 2;
    }

    for (size_t i = start; i < end; i++) {
        if (chain_signal != 0) {
            return 128 + chain_signal;
        }

        const CommandListItem *item = &list->items[i];

        if ((item->condition == LIST_ON_SUCCESS && status != 0)
            || (item->condition == LIST_ON_FAILURE && status == 0)) {
            continue;
        }

        TokenList part = {0};
        char error[128];

        if (chain_copy_tokens(tokens, item->token_start,
                              item->token_count, &part) == -1) {
            perror("msh: copy chain tokens");
            return 1;
        }

        /*
         * The supervisor already represents background execution.
         * Remove the final '&' from this private pipeline copy.
         */
        if (part.count > 0
            && part.items[part.count - 1].type == TOKEN_AMP) {
            Token *ampersand = &part.items[part.count - 1];
            free(ampersand->text);
            free(ampersand->raw_text);
            *ampersand = (Token) {0};
            part.count--;
        }

        if (expand_pipeline_fields(&part, status, error, sizeof(error), NULL) == -1) {
            fprintf(stderr, "msh: %s\n", error);
            free_tokens(&part);
            return 1;
        }

        Pipeline pipeline = {0};
        int parsed = parse_pipeline_assignments(&part, &pipeline,
                                                error, sizeof(error));
        free_tokens(&part);

        if (parsed == -1) {
            fprintf(stderr, "msh: %s\n", error);
            return 2;
        }

        int unsupported_assignment = 0;
        for (size_t stage = 0; stage < pipeline.command_count; stage++) {
            const Command *command = &pipeline.commands[stage];
            if (command->assignment_count > 0
                && command->argc > 0 && is_builtin(command)
                && (pipeline.command_count != 1 || pipeline.background)
                    && strcmp(command->argv[0], "pwd") != 0
                    && strcmp(command->argv[0], ":") != 0) {
                unsupported_assignment = 1;
            }
        }

        if (unsupported_assignment) {
            fprintf(stderr,
                    "msh: chain assignments require an assignment-only "
                    "command or an external command/pipeline stage\n");
            free_pipeline(&pipeline);
            status = 2;
            continue;
        }

        int should_exit = 0;

        if (pipeline.command_count == 1 && !pipeline.background) {
            const Command *command = &pipeline.commands[0];
            const char *name =
                command->argc > 0 ? command->argv[0] : "";

            int unsupported =
                strcmp(name, "jobs") == 0
                || strcmp(name, "fg") == 0
                || strcmp(name, "bg") == 0
                || strcmp(name, "stats") == 0
                || strcmp(name, "history") == 0;

            if (unsupported) {
                fprintf(stderr,
                        "msh: %s is not supported in a background chain\n",
                        name);
                status = 2;
            } else {
                BuiltinResult result =
                    run_builtin_redirected(command, status);

                if (result.handled) {
                    status = result.status;
                    should_exit = result.should_exit;
                } else {
                    status = chain_execute_pipeline(&pipeline, status);
                }
            }
        } else {
            status = chain_execute_pipeline(&pipeline, status);
        }

        free_pipeline(&pipeline);

        if (chain_signal != 0) {
            return 128 + chain_signal;
        }

        if (should_exit) {
            break;
        }
    }

    return status;
}

static _Noreturn void chain_run_supervisor(
    const TokenList *tokens, const CommandList *list,
    size_t start, size_t end, int last_status,
    int gate[2], int ready[2], const sigset_t *original_mask)
{
    close(gate[1]);
    close(ready[0]);
    signals_close();

    if (setpgid(0, 0) == -1) {
        perror("msh: chain supervisor process group");
        _exit(1);
    }

    jobs_reset_after_fork();

    if (chain_prepare_supervisor_signals() == -1) {
        perror("msh: chain supervisor signals");
        _exit(1);
    }

    /* Confirm initialization before the parent releases the launch gate. */
    char byte = 'R';
    ssize_t written;
    do {
        written = write(ready[1], &byte, 1);
    } while (written == -1 && errno == EINTR);

    close(ready[1]);
    if (written != 1) {
        _exit(1);
    }

    ssize_t received;
    do {
        received = read(gate[0], &byte, 1);
    } while (received == -1 && errno == EINTR);

    close(gate[0]);
    if (received != 0) {
        _exit(1);
    }

    if (sigprocmask(SIG_SETMASK, original_mask, NULL) == -1) {
        perror("msh: chain supervisor signal mask");
        _exit(1);
    }

    int status = chain_evaluate(tokens, list, start, end, last_status);
    if (fflush(NULL) == EOF && chain_signal == 0) {
        status = 1;
    }

    /*
     * Preserve actual signal termination for the parent's job status
     * and foreground interrupt formatting.
     */
    if (chain_signal != 0) {
        int number = chain_signal;
        struct sigaction action = {0};
        action.sa_handler = SIG_DFL;
        sigemptyset(&action.sa_mask);

        if (sigaction(number, &action, NULL) == 0) {
            sigset_t unblocked;
            sigemptyset(&unblocked);
            sigaddset(&unblocked, number);
            sigprocmask(SIG_UNBLOCK, &unblocked, NULL);
            raise(number);
        }

        _exit(128 + number);
    }

    _exit(status);
}

int execute_background_chain(const TokenList *tokens,
                             const CommandList *list,
                             size_t start, size_t end,
                             int last_status,
                             const char *command_text)
{
    if (!terminal_is_interactive()) {
        fprintf(stderr,
                "msh: background execution requires an interactive terminal\n");
        return 2;
    }

    if (start >= end || end > list->count
        || list->items[start].condition != LIST_ALWAYS
        || command_list_chain_end(list, start) != end) {
        fprintf(stderr, "msh: invalid background logical chain\n");
        return 2;
    }

    const CommandListItem *last = &list->items[end - 1];
    if (last->token_count == 0 || last->token_start > tokens->count
        || last->token_count > tokens->count - last->token_start
        || tokens->items[last->token_start + last->token_count - 1].type
           != TOKEN_AMP) {
        fprintf(stderr, "msh: background chain must end with '&'\n");
        return 2;
    }

    Job *job = jobs_create(command_text);
    if (job == NULL) {
        return 1;
    }

    int gate[2];
    int ready[2];

    if (pipe(gate) == -1) {
        perror("msh: chain supervisor gate");
        jobs_remove(job);
        return 1;
    }

    if (pipe(ready) == -1) {
        perror("msh: chain supervisor readiness pipe");
        close(gate[0]);
        close(gate[1]);
        jobs_remove(job);
        return 1;
    }

    sigset_t blocked;
    sigset_t original_mask;
    sigemptyset(&blocked);
    sigaddset(&blocked, SIGCHLD);
    sigaddset(&blocked, SIGINT);
    sigaddset(&blocked, SIGQUIT);
    sigaddset(&blocked, SIGHUP);
    sigaddset(&blocked, SIGTERM);

    if (sigprocmask(SIG_BLOCK, &blocked, &original_mask) == -1) {
        perror("msh: block chain launch signals");
        close(gate[0]);
        close(gate[1]);
        close(ready[0]);
        close(ready[1]);
        jobs_remove(job);
        return 1;
    }

    fflush(NULL);
    pid_t child = fork();

    if (child == 0) {
        chain_run_supervisor(tokens, list, start, end, last_status,
                             gate, ready, &original_mask);
    }

    close(gate[0]);
    close(ready[1]);
    int failed = 0;

    if (child == -1) {
        perror("msh: fork chain supervisor");
        failed = 1;
    } else {
        job->pgid = child;
        job->process_count = 1;
        job->processes[0].pid = child;
        job->background = 1;

        if (setpgid(child, child) == -1) {
            perror("msh: parent chain process group");
            failed = 1;
        }

        if (!failed) {
            char byte;
            ssize_t received;
            do {
                received = read(ready[0], &byte, 1);
            } while (received == -1 && errno == EINTR);

            if (received != 1 || byte != 'R') {
                fprintf(stderr, "msh: chain supervisor initialization failed\n");
                failed = 1;
            }
        }
    }

    close(ready[0]);

    if (!failed
        && sigprocmask(SIG_SETMASK, &original_mask, NULL) == -1) {
        perror("msh: restore parent signal mask");
        failed = 1;
    }

    if (failed) {
        /* No pipeline can have passed the supervisor's gate yet. */
        if (child > 0) {
            kill(child, SIGKILL);
            int status;
            struct rusage usage = {0};
            pid_t result;
            do {
                result = wait4(child, &status, 0, &usage);
            } while (result == -1 && errno == EINTR);

            if (result == child) {
                jobs_record_usage(child, status, &usage);
            }
        }

        close(gate[1]);
        jobs_remove(job);

        if (sigprocmask(SIG_SETMASK, &original_mask, NULL) == -1) {
            perror("msh: restore parent signal mask after failed launch");
        }
        return 1;
    }

    close(gate[1]);
    parameters_set_background_pid(child);
    printf("[%d] %ld\n", job->id, (long)job->pgid);
    return 0;
}
