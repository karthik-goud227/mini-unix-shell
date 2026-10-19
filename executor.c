#define _POSIX_C_SOURCE 200809L

#include "executor.h"
#include "builtins.h"
#include "job_control.h"
#include "jobs.h"
#include "redirection.h"
#include "terminal.h"
#include "signals.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static void close_pipes(int pipes[][2], size_t count)
{
    for (size_t i = 0; i < count; i++) {
        close(pipes[i][0]);
        close(pipes[i][1]);
    }
}

static void terminate_job(Job *job)
{
    /* Use individual PIDs even if group creation was incomplete. */
    for (size_t i = 0; i < job->process_count; i++) {
        if (!job->processes[i].completed) {
            kill(job->processes[i].pid, SIGKILL);
        }
    }

    for (size_t i = 0; i < job->process_count; i++) {
        if (job->processes[i].completed) {
            continue;
        }

        while (waitpid(job->processes[i].pid, NULL, 0) == -1
               && errno == EINTR) {
        }
    }
}

static void run_child(const Pipeline *pipeline, size_t index,
                      int pipes[][2], size_t pipe_count,
                      int gate[2], pid_t pgid,
                      const sigset_t *original_mask)
{
    close(gate[1]);
    signals_close();

    if (terminal_is_interactive()) {
        if (setpgid(0, pgid == 0 ? getpid() : pgid) == -1) {
            perror("msh: child process group");
            _exit(1);
        }
    }

    if (terminal_prepare_child() == -1) {
        perror("msh: child signals");
        _exit(1);
    }

    if (sigprocmask(SIG_SETMASK, original_mask, NULL) == -1) {
        perror("msh: child signal mask");
        _exit(1);
    }

    /* EOF means the parent has finished grouping and handoff. */
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
        perror("msh: pipe input");
        _exit(1);
    }

    if (index + 1 < pipeline->command_count
        && dup2(pipes[index][1], STDOUT_FILENO) == -1) {
        perror("msh: pipe output");
        _exit(1);
    }

    close_pipes(pipes, pipe_count);

    const Command *command = &pipeline->commands[index];

    if (apply_redirections(command) == -1) {
        _exit(1);
    }

    if (is_builtin(command)) {
        BuiltinResult result = run_builtin(command, 0);
        _exit(fflush(stdout) == EOF ? 1 : result.status);
    }

    execvp(command->argv[0], command->argv);

    int saved_errno = errno;

    fprintf(stderr, "msh: %s: %s\n",
            command->argv[0], strerror(saved_errno));

    _exit(saved_errno == ENOENT || saved_errno == ENOTDIR
          ? 127 : 126);
}

int execute_pipeline(const Pipeline *pipeline,
                     const char *command_text)
{
    size_t count = pipeline->command_count;

    if (count == 0) {
        return 0;
    }

    if (pipeline->background && !terminal_is_interactive()) {
        fprintf(stderr, "msh: background execution requires "
                        "an interactive terminal\n");
        return 2;
    }

    if (count > MSH_MAX_PROCESSES) {
        fprintf(stderr, "msh: maximum pipeline length is %d commands\n",
                MSH_MAX_PROCESSES);
        return 2;
    }

    for (size_t i = 0; i < count; i++) {
        const Command *command = &pipeline->commands[i];

        if (command->argc == 0 || command->argv[0][0] == '\0') {
            fprintf(stderr, "msh: empty command name\n");
            return 127;
        }

        if (is_builtin(command)
            && strcmp(command->argv[0], "pwd") != 0) {
            fprintf(stderr, "msh: %s is not supported in a pipeline "
                            "or background job\n",
                    command->argv[0]);
            return 2;
        }
    }

    Job *job = jobs_create(command_text);

    if (job == NULL) {
        return 1;
    }

    int pipes[MSH_MAX_PROCESSES - 1][2];
    size_t opened = 0;
    size_t pipe_count = count - 1;

    for (; opened < pipe_count; opened++) {
        if (pipe(pipes[opened]) == -1) {
            perror("msh: pipe");
            close_pipes(pipes, opened);
            jobs_remove(job);
            return 1;
        }
    }

    int gate[2];

    if (pipe(gate) == -1) {
        perror("msh: launch gate");
        close_pipes(pipes, pipe_count);
        jobs_remove(job);
        return 1;
    }

    sigset_t blocked;
    sigset_t original_mask;

    sigemptyset(&blocked);
    sigaddset(&blocked, SIGCHLD);

    if (sigprocmask(SIG_BLOCK, &blocked, &original_mask) == -1) {
        perror("msh: block SIGCHLD");
        close_pipes(pipes, pipe_count);
        close(gate[0]);
        close(gate[1]);
        jobs_remove(job);
        return 1;
    }

    fflush(NULL);
    int failed = 0;

    for (size_t i = 0; i < count; i++) {
        pid_t child = fork();

        if (child == -1) {
            perror("msh: fork");
            failed = 1;
            break;
        }

        if (child == 0) {
            run_child(pipeline, i, pipes, pipe_count,
                      gate, job->pgid, &original_mask);
            _exit(1);
        }

        job->processes[job->process_count++].pid = child;

        if (job->pgid == 0) {
            job->pgid = child;
        }

        if (terminal_is_interactive()
            && setpgid(child, job->pgid) == -1) {
            perror("msh: parent process group");
            failed = 1;
            break;
        }
    }

    close_pipes(pipes, pipe_count);
    close(gate[0]);

    if (!failed && !pipeline->background
        && terminal_give_to(job->pgid) == -1) {
        failed = 1;
    }

    if (failed) {
        /* Keep the gate closed to children until they are killed. */
        terminate_job(job);
    }

    close(gate[1]);

    if (sigprocmask(SIG_SETMASK, &original_mask, NULL) == -1) {
        perror("msh: restore signal mask");

        if (!failed) {
            terminate_job(job);
        }

        failed = 1;
    }

    if (failed) {
        terminal_reclaim();
        jobs_remove(job);
        return 1;
    }

    if (pipeline->background) {
        job->background = 1;
        printf("[%d] %ld\n", job->id, (long)job->pgid);
        return 0;
    }

    int status;

    if (job_foreground(job, 0, &status) == -1) {
        terminate_job(job);
        terminal_reclaim();
        jobs_remove(job);
        return 1;
    }

    if (jobs_get_state(job) == JOB_DONE) {
        jobs_remove(job);
    } else if (!terminal_is_interactive()) {
        fprintf(stderr, "msh: stopped jobs require an interactive terminal\n");
        terminate_job(job);
        jobs_remove(job);
    } else {
        job->background = 1;
        fprintf(stderr, "[%d] Stopped  %s\n",
                job->id, job->command_text);
        job->notified = 1;
    }

    return status;
}
