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

typedef enum {
    LIST_ALWAYS = 0,
    LIST_ON_SUCCESS,
    LIST_ON_FAILURE
} ListCondition;

/* One pipeline's token range and condition for execution. */
typedef struct {
    size_t token_start;
    size_t token_count;
    ListCondition condition;
} CommandListItem;

typedef struct {
    CommandListItem *items;
    size_t count;
} CommandList;

/*
 * Validate and divide unexpanded tokens into command-list items.
 * Returns 0 on success, -1 on failure.
 * Empty input succeeds with count == 0.
 *
 * The list owns its items array, but does not own the tokens.
 * Keep the original tokens alive while using these ranges.
 */
int parse_command_list(const TokenList *tokens, CommandList *result,
                       char *error, size_t error_size);

/*
 * Return the exclusive end index of the logical chain starting at start.
 * The caller supplies a valid list and starts at a chain boundary.
 * If start is at or beyond count, return count.
 */
size_t command_list_chain_end(const CommandList *list, size_t start);

void free_command_list(CommandList *list);

#endif
