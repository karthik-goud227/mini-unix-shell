#ifndef MSH_EXPANSION_H
#define MSH_EXPANSION_H

#include "lexer.h"

#include <stddef.h>

/*
 * Expand $NAME, $?, $0-$9 and $# using original word spelling.
 * Braces support names, numeric indices, status and argument count.
 * Named-variable defaults ${NAME-word} expand word only when unset.
 * ${NAME=word} also assigns the expanded default when NAME is unset.
 * Existing empty values are preserved. The '-' form does not assign.
 * Other braced operators are not supported yet.
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

#endif
