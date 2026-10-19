#define _POSIX_C_SOURCE 200809L

#include "builtins.h"

#include <errno.h>
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

BuiltinResult run_builtin(const Command *command, int last_status)
{
    BuiltinResult result = {0};

    if (command->argc == 0) {
        return result;
    }

    const char *name = command->argv[0];

    if (strcmp(name, "cd") == 0) {
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
