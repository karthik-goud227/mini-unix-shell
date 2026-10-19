#include "lexer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int is_operator(char c)
{
    return c == '|' || c == '<' || c == '>' || c == '&';
}

static int is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\n';
}

void free_tokens(TokenList *tokens)
{
    if (tokens == NULL) {
        return;
    }

    for (size_t i = 0; i < tokens->count; i++) {
        free(tokens->items[i].text);
    }

    free(tokens->items);
    tokens->items = NULL;
    tokens->count = 0;
}

int lex_line(const char *line, TokenList *result,
             char *error, size_t error_size)
{
    size_t i = 0;
    size_t capacity = 0;
    size_t line_length = strlen(line);
    char *word = NULL;
    const char *message = NULL;

    result->items = NULL;
    result->count = 0;

    if (error_size > 0) {
        error[0] = '\0';
    }

    while (line[i] != '\0') {
        if (is_space(line[i])) {
            i++;
            continue;
        }

        size_t start = i;
        size_t used = 0;
        TokenType type = TOKEN_WORD;

        word = malloc(line_length - i + 1);
        if (word == NULL) {
            message = "out of memory";
            goto fail;
        }

        if (is_operator(line[i])) {
            char op = line[i];
            word[used++] = line[i++];

            switch (op) {
            case '|': type = TOKEN_PIPE; break;
            case '<': type = TOKEN_IN; break;
            case '>': type = TOKEN_OUT; break;
            case '&': type = TOKEN_AMP; break;
            }

            if (op == '>' && line[i] == '>') {
                word[used++] = line[i++];
                type = TOKEN_APPEND;
            }
        } else {
            while (line[i] != '\0'
                   && !is_space(line[i])
                   && !is_operator(line[i])) {
                if (line[i] == ';' || line[i] == '('
                    || line[i] == ')' || line[i] == '`') {
                    message = "unsupported unquoted shell syntax";
                    goto fail;
                }

                if (line[i] == '\\') {
                    i++;
                    if (line[i] == '\0') {
                        message = "unfinished escape";
                        goto fail;
                    }
                    word[used++] = line[i++];
                } else if (line[i] == '\'' || line[i] == '"') {
                    char quote = line[i++];

                    while (line[i] != '\0' && line[i] != quote) {
                        if (quote == '"' && line[i] == '\\'
                            && (line[i + 1] == '"'
                                || line[i + 1] == '\\')) {
                            i++;
                        }
                        word[used++] = line[i++];
                    }

                    if (line[i] == '\0') {
                        message = "unclosed quote";
                        goto fail;
                    }

                    i++;
                } else {
                    word[used++] = line[i++];
                }
            }
        }

        word[used] = '\0';

        if (result->count == capacity) {
            size_t next = capacity == 0 ? 8 : capacity * 2;
            Token *grown = realloc(result->items,
                                   next * sizeof(*grown));

            if (grown == NULL) {
                message = "out of memory";
                goto fail;
            }

            result->items = grown;
            capacity = next;
        }

        result->items[result->count++] = (Token) {
            .type = type,
            .text = word,
            .position = start
        };

        word = NULL;
    }

    return 0;

fail:
    if (error_size > 0) {
        snprintf(error, error_size, "%s at position %zu",
                 message, i);
    }

    free(word);
    free_tokens(result);
    return -1;
}
