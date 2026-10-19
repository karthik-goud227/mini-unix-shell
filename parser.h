#ifndef MSH_PARSER_H
#define MSH_PARSER_H

#include "lexer.h"

#include <stddef.h>

#define MSH_MAX_ARGS 128

typedef enum {
    REDIRECT_IN,
    REDIRECT_OUT,
    REDIRECT_APPEND
} RedirectType;

typedef struct {
    RedirectType type;
    char *path;
} Redirection;

typedef struct {
    char **argv;
    size_t argc;

    Redirection *redirections;
    size_t redirection_count;
} Command;

typedef struct {
    Command *commands;
    size_t command_count;
    int background;
} Pipeline;

/*
 * Returns 0 on success, -1 on failure.
 * Empty input succeeds with command_count == 0.
 *
 * The result owns copies of argument and redirection strings.
 * Tokens may be freed independently after parsing.
 * Release a successful result with free_pipeline().
 */
int parse_pipeline(const TokenList *tokens, Pipeline *result,
                   char *error, size_t error_size);

void free_pipeline(Pipeline *pipeline);

#endif
