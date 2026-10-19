#define _POSIX_C_SOURCE 200809L

#include "lexer.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static const char *token_name(TokenType type)
{
    switch (type) {
    case TOKEN_WORD:   return "WORD";
    case TOKEN_PIPE:   return "PIPE";
    case TOKEN_IN:     return "IN";
    case TOKEN_OUT:    return "OUT";
    case TOKEN_APPEND: return "APPEND";
    case TOKEN_AMP:    return "AMP";
    }

    return "UNKNOWN";
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
        char error[128];

        if (lex_line(line, &tokens, error, sizeof(error)) == -1) {
            fprintf(stderr, "msh: %s\n", error);
            status = 2;
            continue;
        }

        for (size_t i = 0; i < tokens.count; i++) {
            printf("%s [%s] @%zu\n",
                   token_name(tokens.items[i].type),
                   tokens.items[i].text,
                   tokens.items[i].position);
        }

        if (tokens.count > 0) {
            status = EXIT_SUCCESS;
        }

        free_tokens(&tokens);
    }

    free(line);
    return status;
}
