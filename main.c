#define _POSIX_C_SOURCE 200809L

#include "builtins.h"
#include "executor.h"
#include "expansion.h"
#include "input.h"
#include "history.h"
#include "jobs.h"
#include "lexer.h"
#include "parser.h"
#include "signals.h"
#include "terminal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void)
{
    if (terminal_init() == -1) {
        return EXIT_FAILURE;
    }

    if (signals_init() == -1) {
        terminal_reclaim();
        return EXIT_FAILURE;
    }

    char *line = NULL;
    size_t capacity = 0;
    int interactive = terminal_is_interactive();
    int status = EXIT_SUCCESS;
    int exit_warned = 0;

    for (;;) {
        jobs_notify();

        if (interactive) {
            fputs("msh> ", stdout);
            fflush(stdout);
        }

        int input_result = input_read_line(&line, &capacity);

        if (input_result == -3) {
            status = 130;
            exit_warned = 0;
            continue;
        }

        if (input_result == -2) {
            status = 2;
            exit_warned = 0;
            continue;
        }

        if (input_result == -1) {
            status = EXIT_FAILURE;
            break;
        }

        if (input_result == 0) {
            if (interactive && jobs_have_active() && !exit_warned) {
                fprintf(stderr,
                        "\nmsh: active jobs remain; "
                        "repeat exit or Ctrl+D to terminate them\n");
                exit_warned = 1;
                continue;
            }

            if (interactive) {
                putchar('\n');
            }
            break;
        }

        if (interactive && history_add(line) == -1) {
            perror("msh: history");
        }

        TokenList tokens;
        CommandList list;
        char error[128];

        if (lex_line(line, &tokens, error, sizeof(error)) == -1) {
            fprintf(stderr, "msh: %s\n", error);
            status = 2;
            exit_warned = 0;
            continue;
        }

        /* Validate the whole list before executing any part of it. */
        if (parse_command_list(&tokens, &list,
                               error, sizeof(error)) == -1) {
            fprintf(stderr, "msh: %s\n", error);
            free_tokens(&tokens);
            status = 2;
            exit_warned = 0;
            continue;
        }

        int should_exit = 0;

        for (size_t i = 0; i < list.count; i++) {
            CommandListItem item = list.items[i];

            /* This view borrows tokens; only the full list owns them. */
            TokenList part = {
                .items = tokens.items + item.token_start,
                .count = item.token_count
            };

            const Token *first = &part.items[0];
            const Token *last = &part.items[part.count - 1];
            size_t text_end = last->position + strlen(last->raw_text);

            char *command_text = strndup(
                line + first->position, text_end - first->position
            );

            if (command_text == NULL) {
                perror("msh: command text");
                status = EXIT_FAILURE;
                exit_warned = 0;
                break;
            }

            /* Earlier commands may have changed status or variables. */
            if (expand_tokens(&part, status,
                              error, sizeof(error)) == -1) {
                fprintf(stderr, "msh: %s\n", error);
                free(command_text);
                status = EXIT_FAILURE;
                exit_warned = 0;
                break;
            }

            Pipeline pipeline;

            if (parse_pipeline(&part, &pipeline,
                               error, sizeof(error)) == -1) {
                fprintf(stderr, "msh: %s\n", error);
                free(command_text);
                status = 2;
                exit_warned = 0;
                break;
            }

            if (pipeline.command_count == 1 && !pipeline.background) {
                BuiltinResult result =
                    run_builtin_redirected(&pipeline.commands[0], status);

                if (result.handled) {
                    status = result.status;
                    should_exit = result.should_exit;
                } else {
                    status = execute_pipeline(&pipeline, command_text);
                }
            } else {
                status = execute_pipeline(&pipeline, command_text);
            }

            free_pipeline(&pipeline);
            free(command_text);

            if (should_exit) {
                break;
            }

            /* A command between exit attempts cancels confirmation. */
            exit_warned = 0;
        }

        free_command_list(&list);
        free_tokens(&tokens);

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
    }

    jobs_shutdown();
    history_clear();
    signals_close();

    if (terminal_reclaim() == -1) {
        status = EXIT_FAILURE;
    }

    free(line);
    return status;
}
