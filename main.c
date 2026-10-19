#define _POSIX_C_SOURCE 200809L

#include "builtins.h"
#include "executor.h"
#include "jobs.h"
#include "lexer.h"
#include "parser.h"
#include "terminal.h"

#include <stdio.h>
#include <stdlib.h>

int main(void)
{
    if (terminal_init() == -1) {
        return EXIT_FAILURE;
    }

    char *line = NULL;
    size_t capacity = 0;
    int interactive = terminal_is_interactive();
    int status = EXIT_SUCCESS;
    int exit_warned = 0;

    for (;;) {
        jobs_reap_pending();

        if (interactive) {
            fputs("msh> ", stdout);
            fflush(stdout);
        }

        ssize_t length = getline(&line, &capacity, stdin);

        if (length == -1) {
            if (ferror(stdin)) {
                perror("msh: input");
                status = EXIT_FAILURE;
                break;
            }

            if (interactive && jobs_have_active() && !exit_warned) {
                fprintf(stderr,
                        "\nmsh: active jobs remain; "
                        "repeat exit or Ctrl+D to terminate them\n");
                exit_warned = 1;
                clearerr(stdin);
                continue;
            }

            if (interactive) {
                putchar('\n');
            }
            break;
        }

        if (length > 0 && line[length - 1] == '\n') {
            line[--length] = '\0';
        }

        TokenList tokens;
        Pipeline pipeline;
        char error[128];

        if (lex_line(line, &tokens, error, sizeof(error)) == -1) {
            fprintf(stderr, "msh: %s\n", error);
            status = 2;
            exit_warned = 0;
            continue;
        }

        int parsed = parse_pipeline(&tokens, &pipeline,
                                    error, sizeof(error));
        free_tokens(&tokens);

        if (parsed == -1) {
            fprintf(stderr, "msh: %s\n", error);
            status = 2;
            exit_warned = 0;
            continue;
        }

        if (pipeline.command_count == 0) {
            free_pipeline(&pipeline);
            continue;
        }

        int should_exit = 0;

        if (pipeline.command_count == 1 && !pipeline.background) {
            BuiltinResult result =
                run_builtin_redirected(&pipeline.commands[0], status);

            if (result.handled) {
                status = result.status;
                should_exit = result.should_exit;
            } else {
                status = execute_pipeline(&pipeline, line);
            }
        } else {
            status = execute_pipeline(&pipeline, line);
        }

        free_pipeline(&pipeline);

        if (should_exit) {
            if (interactive && jobs_have_active() && !exit_warned) {
                fprintf(stderr,
                        "msh: active jobs remain; "
                        "repeat exit or Ctrl+D to terminate them\n");
                exit_warned = 1;
                continue;
            }

            break;
        }

        exit_warned = 0;
    }

    jobs_shutdown();

    if (terminal_reclaim() == -1) {
        status = EXIT_FAILURE;
    }

    free(line);
    return status;
}
