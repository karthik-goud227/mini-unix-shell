#ifndef MSH_CHAIN_H
#define MSH_CHAIN_H

#include "parser.h"

#include <stddef.h>

/*
 * Launch list items [start, end) as one background logical chain.
 *
 * The range must contain one complete, syntax-validated chain.
 * Expansion happens in the child immediately before each executed
 * pipeline, using the child's environment and previous status.
 *
 * Tokens and list remain owned by the caller.
 * command_text contains the original spelling of the whole chain.
 *
 * Returns 0 after successful launch, 1 on operational failure,
 * or 2 for unsupported usage.
 */
int execute_background_chain(const TokenList *tokens,
                             const CommandList *list,
                             size_t start, size_t end,
                             int last_status,
                             const char *command_text);

#endif
