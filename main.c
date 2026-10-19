#define _POSIX_C_SOURCE 200809L

#include "lexer.h"
#include "parser.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static const char *redirect_name(RedirectType type)
{
    switch (type) {
    case REDIRECT_IN:     return "<";
    case REDIRECT_OUT:    return ">";
    case REDIRECT_APPEND: return ">>";
    }

    return "?";
}

static void print_pipeline(const Pipeline *pipeline)
{
    if (pipeline->command_count == 0) {
        return;
    }

    printf("Pipeline: %zu command(s), background=%s\n",
           pipeline->command_count,
           pipeline->background ? "yes" : "no");

    for (size_t i = 0; i < pipeline->command_count; i++) {
        const Command *command = &pipeline->commands[i];

        printf("  Command %zu:\n", i + 1);

        for (size_t j = 0; j < command->argc; j++) {
            printf("    argv[%zu] = [%s]\n", j, command->argv[j]);
        }

        for (size_t j = 0; j < command->redirection_count; j++) {
            const Redirection *redirect = &command->redirections[j];

            printf("    redirect %s [%s]\n",
                   redirect_name(redirect->type), redirect->path);
        }
    }
}

int main(void)
{
    char *line = NULL;
    size_t capacity = 0;
    int interactive = isatty(STDIN_FILENO);
    int status = EXIT_SUCCESS;

    for (;;) {
        if (interactive) {
            fputs("msh> ", stdout);
            fflush(stdout);
        }

        ssize_t length = getline(&line, &capacity, stdin);

        if (length == -1) {
            if (ferror(stdin)) {
                perror("msh: input");
                status = EXIT_FAILURE;
            } else if (interactive) {
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
            continue;
        }

        int parsed = parse_pipeline(&tokens, &pipeline,
                                    error, sizeof(error));
        free_tokens(&tokens);

        if (parsed == -1) {
            fprintf(stderr, "msh: %s\n", error);
            status = 2;
            continue;
        }

        print_pipeline(&pipeline);

        if (pipeline.command_count > 0) {
            status = EXIT_SUCCESS;
        }

        free_pipeline(&pipeline);
    }

    free(line);
    return status;
}
