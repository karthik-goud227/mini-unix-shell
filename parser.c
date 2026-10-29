#define _POSIX_C_SOURCE 200809L

#include "parser.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void free_pipeline(Pipeline *pipeline)
{
    if (pipeline == NULL) {
        return;
    }

    for (size_t i = 0; i < pipeline->command_count; i++) {
        Command *command = &pipeline->commands[i];

        for (size_t j = 0; j < command->argc; j++) {
            free(command->argv[j]);
        }
        free(command->argv);

        for (size_t j = 0; j < command->redirection_count; j++) {
            free(command->redirections[j].path);
        }
        free(command->redirections);
    }

    free(pipeline->commands);
    *pipeline = (Pipeline) {0};
}

static int add_command(Pipeline *pipeline)
{
    size_t count = pipeline->command_count;
    Command *grown = realloc(pipeline->commands,
                             (count + 1) * sizeof(*grown));

    if (grown == NULL) {
        return -1;
    }

    pipeline->commands = grown;
    pipeline->commands[count] = (Command) {0};
    pipeline->command_count++;
    return 0;
}

static int add_argument(Command *command, const char *text)
{
    char *copy = strdup(text);

    if (copy == NULL) {
        return -1;
    }

    char **grown = realloc(command->argv,
                           (command->argc + 2) * sizeof(*grown));

    if (grown == NULL) {
        free(copy);
        return -1;
    }

    command->argv = grown;
    command->argv[command->argc++] = copy;
    command->argv[command->argc] = NULL;
    return 0;
}

static int add_redirection(Command *command,
                           TokenType token_type, const char *path)
{
    char *copy = strdup(path);

    if (copy == NULL) {
        return -1;
    }

    size_t count = command->redirection_count;
    Redirection *grown = realloc(command->redirections,
                                (count + 1) * sizeof(*grown));

    if (grown == NULL) {
        free(copy);
        return -1;
    }

    RedirectType type = REDIRECT_IN;

    if (token_type == TOKEN_OUT) {
        type = REDIRECT_OUT;
    } else if (token_type == TOKEN_APPEND) {
        type = REDIRECT_APPEND;
    }

    command->redirections = grown;
    command->redirections[count] = (Redirection) {
        .type = type,
        .path = copy
    };
    command->redirection_count++;
    return 0;
}

int parse_pipeline(const TokenList *tokens, Pipeline *result,
                   char *error, size_t error_size)
{
    const char *message = NULL;
    size_t position = 0;

    *result = (Pipeline) {0};

    if (error_size > 0) {
        error[0] = '\0';
    }

    if (tokens->count == 0) {
        return 0;
    }

    if (add_command(result) == -1) {
        message = "out of memory";
        goto fail;
    }

    for (size_t i = 0; i < tokens->count; i++) {
        const Token *token = &tokens->items[i];
        Command *command =
            &result->commands[result->command_count - 1];

        position = token->position;

        switch (token->type) {
        case TOKEN_WORD:
            if (command->argc >= MSH_MAX_ARGS) {
                message = "too many arguments (maximum 128)";
                goto fail;
            }

            if (add_argument(command, token->text) == -1) {
                message = "out of memory";
                goto fail;
            }
            break;

        case TOKEN_IN:
        case TOKEN_OUT:
        case TOKEN_APPEND:
            if (i + 1 >= tokens->count
                || tokens->items[i + 1].type != TOKEN_WORD) {
                message = "expected filename after redirection";
                goto fail;
            }

            if (add_redirection(command, token->type,
                                tokens->items[i + 1].text) == -1) {
                message = "out of memory";
                goto fail;
            }

            i++;
            break;

        case TOKEN_PIPE:
            if (command->argc == 0 || i + 1 == tokens->count) {
                message = "empty command in pipeline";
                goto fail;
            }

            if (add_command(result) == -1) {
                message = "out of memory";
                goto fail;
            }
            break;

        case TOKEN_SEMI:
            message = "unsupported unquoted shell syntax "
                      "(command lists not enabled yet)";
            goto fail;

        case TOKEN_AMP:
            if (command->argc == 0 || i + 1 != tokens->count) {
                message = "'&' must follow a command and end the line";
                goto fail;
            }

            result->background = 1;
            break;

        default:
            message = "unexpected token";
            goto fail;
        }
    }

    if (result->commands[result->command_count - 1].argc == 0) {
        message = "expected command name";
        goto fail;
    }

    return 0;

fail:
    if (error_size > 0) {
        snprintf(error, error_size, "%s at position %zu",
                 message, position);
    }

    free_pipeline(result);
    return -1;
}

void free_command_list(CommandList *list)
{
    if (list == NULL) {
        return;
    }

    free(list->items);
    *list = (CommandList) {0};
}

int parse_command_list(const TokenList *tokens, CommandList *result,
                       char *error, size_t error_size)
{
    *result = (CommandList) {0};

    if (error_size > 0) {
        error[0] = '\0';
    }

    size_t start = 0;
    ListCondition condition = LIST_ALWAYS;

    for (size_t i = 0; i <= tokens->count; i++) {
        int at_end = i == tokens->count;

        int separator = !at_end
            && (tokens->items[i].type == TOKEN_SEMI
                || tokens->items[i].type == TOKEN_AND_IF
                || tokens->items[i].type == TOKEN_OR_IF
                || tokens->items[i].type == TOKEN_AMP);

        if (!at_end && !separator) {
            continue;
        }

        if (i == start) {
            if (at_end && condition == LIST_ALWAYS) {
                /* Empty input or a trailing semicolon/ampersand. */
                break;
            }

            if (at_end) {
                if (error_size > 0) {
                    snprintf(error, error_size,
                             "expected pipeline after logical operator");
                }
                goto fail;
            }

            if (error_size > 0) {
                snprintf(error, error_size,
                         "empty command in list at position %zu",
                         tokens->items[i].position);
            }
            goto fail;
        }

        int background = !at_end
            && tokens->items[i].type == TOKEN_AMP;

        if (background && condition != LIST_ALWAYS) {
            if (error_size > 0) {
                snprintf(error, error_size,
                         "background logical chains are not supported");
            }
            goto fail;
        }

        /* Include '&' so parse_pipeline records background execution. */
        size_t token_count = i - start + (background ? 1 : 0);

        /* Borrow this range; do not free its tokens. */
        TokenList part = {
            .items = tokens->items + start,
            .count = token_count
        };
        Pipeline pipeline;

        if (parse_pipeline(&part, &pipeline, error, error_size) == -1) {
            goto fail;
        }
        free_pipeline(&pipeline);

        CommandListItem *grown = realloc(
            result->items,
            (result->count + 1) * sizeof(*grown)
        );

        if (grown == NULL) {
            if (error_size > 0) {
                snprintf(error, error_size, "out of memory");
            }
            goto fail;
        }

        result->items = grown;
        result->items[result->count++] = (CommandListItem) {
            .token_start = start,
            .token_count = token_count,
            .condition = condition
        };

        if (at_end) {
            break;
        }
        if (tokens->items[i].type == TOKEN_AND_IF) {
            condition = LIST_ON_SUCCESS;
        } else if (tokens->items[i].type == TOKEN_OR_IF) {
            condition = LIST_ON_FAILURE;
        } else {
            condition = LIST_ALWAYS;
        }

        start = i + 1;
    }

    return 0;

fail:
    free_command_list(result);
    return -1;
}
