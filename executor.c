#define _POSIX_C_SOURCE 200809L

#include "executor.h"
#include "builtins.h"
#include "redirection.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

static void close_pipes(int (*pipes)[2], size_t count)
{
    for (size_t i = 0; i < count; i++) {
        close(pipes[i][0]);
        close(pipes[i][1]);
    }
}

static int wait_for_child(pid_t pid, int *status)
{
    for (;;) {
        if (waitpid(pid, status, 0) != -1) {
            return 0;
        }

        if (errno != EINTR) {
            return -1;
        }
    }
}

static void run_child(const Pipeline *pipeline, size_t index,
                      int (*pipes)[2], size_t pipe_count)
{
    const Command *command = &pipeline->commands[index];

    if (index > 0) {
        if (dup2(pipes[index - 1][0], STDIN_FILENO) == -1) {
            perror("msh: pipe input");
            _exit(1);
        }
    }

    if (index + 1 < pipeline->command_count) {
        if (dup2(pipes[index][1], STDOUT_FILENO) == -1) {
            perror("msh: pipe output");
            _exit(1);
        }
    }

    close_pipes(pipes, pipe_count);

    /* Explicit file redirections override pipe connections. */
    if (apply_redirections(command) == -1) {
        _exit(1);
    }

    if (is_builtin(command)) {
        BuiltinResult result = run_builtin(command, 0);

        if (fflush(stdout) == EOF) {
            _exit(1);
        }

        _exit(result.status);
    }

    execvp(command->argv[0], command->argv);

    int saved_errno = errno;

    fprintf(stderr, "msh: %s: %s\n",
            command->argv[0], strerror(saved_errno));

    _exit(saved_errno == ENOENT || saved_errno == ENOTDIR
          ? 127 : 126);
}

int execute_pipeline(const Pipeline *pipeline)
{
    size_t count = pipeline->command_count;

    if (count == 0) {
        return 0;
    }

    if (pipeline->background) {
        fprintf(stderr, "msh: background execution is not supported yet\n");
        return 2;
    }

    if (count > 16) {
        fprintf(stderr, "msh: maximum pipeline length is 16 commands\n");
        return 2;
    }

    /* Validate the whole pipeline before creating files or children. */
    for (size_t i = 0; i < count; i++) {
        const Command *command = &pipeline->commands[i];

        if (command->argc == 0 || command->argv[0][0] == '\0') {
            fprintf(stderr, "msh: empty command name\n");
            return 127;
        }

        if (is_builtin(command)
            && strcmp(command->argv[0], "pwd") != 0) {
            fprintf(stderr,
                    "msh: %s is not supported in a pipeline\n",
                    command->argv[0]);
            return 2;
        }
    }

    size_t pipe_count = count - 1;
    int (*pipes)[2] = NULL;

    if (pipe_count > 0) {
        pipes = malloc(pipe_count * sizeof(*pipes));

        if (pipes == NULL) {
            fprintf(stderr, "msh: out of memory\n");
            return 1;
        }
    }

    size_t opened = 0;

    for (; opened < pipe_count; opened++) {
        if (pipe(pipes[opened]) == -1) {
            perror("msh: pipe");
            close_pipes(pipes, opened);
            free(pipes);
            return 1;
        }
    }

    pid_t children[16];
    size_t launched = 0;

    fflush(NULL);

    for (size_t i = 0; i < count; i++) {
        pid_t child = fork();

        if (child == -1) {
            perror("msh: fork");
            close_pipes(pipes, pipe_count);
            free(pipes);

            for (size_t j = 0; j < launched; j++) {
                kill(children[j], SIGKILL);
            }

            for (size_t j = 0; j < launched; j++) {
                int ignored;
                wait_for_child(children[j], &ignored);
            }

            return 1;
        }

        if (child == 0) {
            run_child(pipeline, i, pipes, pipe_count);
        }

        children[launched++] = child;
    }

    /* Parent must not retain pipe ends while waiting. */
    close_pipes(pipes, pipe_count);
    free(pipes);

    int last_status = 0;
    int wait_failed = 0;

    for (size_t i = 0; i < launched; i++) {
        int status;

        if (wait_for_child(children[i], &status) == -1) {
            perror("msh: waitpid");
            wait_failed = 1;
            continue;
        }

        if (i + 1 == launched) {
            last_status = status;
        }
    }

    if (wait_failed) {
        return 1;
    }

    if (WIFEXITED(last_status)) {
        return WEXITSTATUS(last_status);
    }

    if (WIFSIGNALED(last_status)) {
        return 128 + WTERMSIG(last_status);
    }

    return 1;
}
