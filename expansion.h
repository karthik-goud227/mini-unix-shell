#ifndef MSH_EXPANSION_H
#define MSH_EXPANSION_H

#include "lexer.h"

#include <stddef.h>

/*
 * Expand $NAME, $?, $0-$9, $#, $$, $! and braced forms.
 * $! is empty before the first successful background launch.
 * Braces support names, numeric indices, status and argument count.
 * Defaults ${NAME-word} and ${1-word} expand word only when unset.
 * ${NAME=word} also assigns the expanded default when NAME is unset.
 * Assignment to positional parameters is rejected.
 * Existing empty values are preserved. The '-' form does not assign.
 * ${NAME?message} and ${1?message} fail when the parameter is unset.
 * Messages expand only when needed. Other operators remain unsupported.
 * Single quotes and escaped dollar signs prevent expansion.
 *
 * Each word remains one token: no field splitting or wildcard expansion.
 * Operator tokens and original spelling remain unchanged.
 *
 * Returns 0 on success, -1 on failure.
 * On failure, discard the tokens using free_tokens().
 */
int expand_tokens(TokenList *tokens, int last_status,
                  char *error, size_t error_size);


/*
 * Same contract as expand_tokens. Also reports a required-parameter failure.
 * required_failure may be NULL; otherwise it is reset on each call.
 */
int expand_tokens_checked(TokenList *tokens, int last_status,
                          char *error, size_t error_size,
                          int *required_failure);

/*
 * Expand an owned pipeline token list into argument fields.
 * Leading assignments retain their original spelling.
 * Redirection targets expand without field splitting.
 * Failure preserves the token list; expansion side effects may remain.
 */
int expand_pipeline_fields(TokenList *tokens, int last_status,
                           char *error, size_t error_size,
                           int *required_failure);

#endif
