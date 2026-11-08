#define _POSIX_C_SOURCE 200809L

#include "builtins.h"
#include "variables.h"
#include "chain.h"
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

            /*
             * Launch a background logical chain before evaluating
             * any of its conditions or expanding any of its words.
             */
            size_t chain_end = command_list_chain_end(&list, i);
            const CommandListItem *last_item = &list.items[chain_end - 1];
            const Token *chain_last = &tokens.items[
                last_item->token_start + last_item->token_count - 1
            ];

            if (chain_end > i + 1 && chain_last->type == TOKEN_AMP) {
                const Token *chain_first = &tokens.items[item.token_start];
                size_t text_end = chain_last->position
                    + strlen(chain_last->raw_text);

                char *chain_text = strndup(
                    line + chain_first->position,
                    text_end - chain_first->position
                );

                if (chain_text == NULL) {
                    perror("msh: chain command text");
                    status = EXIT_FAILURE;
                    exit_warned = 0;
                    break;
                }

                status = execute_background_chain(
                    &tokens, &list, i, chain_end, status, chain_text
                );
                free(chain_text);
                i = chain_end - 1;
                exit_warned = 0;
                continue;
            }

            if ((item.condition == LIST_ON_SUCCESS && status != 0)
                || (item.condition == LIST_ON_FAILURE && status == 0)) {
                continue;
            }

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

            if (parse_pipeline_assignments(&part, &pipeline,
                               error, sizeof(error)) == -1) {
                fprintf(stderr, "msh: %s\n", error);
                free(command_text);
                status = 2;
                exit_warned = 0;
                break;
            }

            int unsupported_assignment = 0;
            for (size_t stage = 0; stage < pipeline.command_count; stage++) {
                const Command *command = &pipeline.commands[stage];
                if (command->assignment_count > 0
                    && command->argc > 0 && is_builtin(command)
                    && (pipeline.command_count != 1 || pipeline.background)) {
                    unsupported_assignment = 1;
                }
            }

            if (unsupported_assignment) {
                fprintf(stderr,
                        "msh: assignments require a standalone foreground "
                        "assignment-only command or an external "
                        "command/pipeline stage\n");
                free_pipeline(&pipeline);
                free(command_text);
                status = 2;
                exit_warned = 0;
                continue;
            }

            if (pipeline.command_count == 1 && !pipeline.background) {
                BuiltinResult result =
                    run_builtin_redirected(&pipeline.commands[0], status);

                if (result.handled) {
                    status = result.status;
                    should_exit = result.should_exit;
                } else {
                    status = execute_pipeline(&pipeline, command_text, status);
                }
            } else {
                status = execute_pipeline(&pipeline, command_text, status);
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
    variables_clear();
    history_clear();
    signals_close();

    if (terminal_reclaim() == -1) {
        status = EXIT_FAILURE;
    }

    free(line);
    return status;
}
