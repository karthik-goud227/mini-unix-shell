#define _POSIX_C_SOURCE 200809L

#include "executor.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

int execute_pipeline(const Pipeline *pipeline)
{
    if (pipeline->command_count == 0) {
        return 0;
    }

    if (pipeline->command_count != 1
        || pipeline->background
        || pipeline->commands[0].redirection_count != 0) {
        fprintf(stderr,
                "msh: execution currently supports only one "
                "foreground command without redirection\n");
        return 2;
    }

    const Command *command = &pipeline->commands[0];

    if (command->argc == 0 || command->argv[0][0] == '\0') {
        fprintf(stderr, "msh: empty command name\n");
        return 127;
    }

    /*
     * Flush parent output before fork so the child does not
     * inherit pending buffered output.
     */
    fflush(NULL);

    pid_t child = fork();

    if (child == -1) {
        perror("msh: fork");
        return 1;
    }

    if (child == 0) {
        execvp(command->argv[0], command->argv);

        /* execvp returns only when execution fails. */
        int saved_errno = errno;

        fprintf(stderr, "msh: %s: %s\n",
                command->argv[0], strerror(saved_errno));

        _exit(saved_errno == ENOENT || saved_errno == ENOTDIR
              ? 127 : 126);
    }

    int wait_status;

    for (;;) {
        if (waitpid(child, &wait_status, 0) != -1) {
            break;
        }

        if (errno == EINTR) {
            continue;
        }

        perror("msh: waitpid");
        return 1;
    }

    if (WIFEXITED(wait_status)) {
        return WEXITSTATUS(wait_status);
    }

    if (WIFSIGNALED(wait_status)) {
        return 128 + WTERMSIG(wait_status);
    }

    return 1;
}
