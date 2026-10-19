#define _POSIX_C_SOURCE 200809L

#include "executor.h"
#include "lexer.h"
#include "parser.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

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

        if (pipeline.command_count > 0) {
            status = execute_pipeline(&pipeline);
        }

        free_pipeline(&pipeline);
    }

    free(line);
    return status;
}
