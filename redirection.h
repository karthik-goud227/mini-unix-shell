#ifndef MSH_REDIRECTION_H
#define MSH_REDIRECTION_H

#include "parser.h"

/*
 * Changes this process's standard input/output.
 * Returns 0 on success, -1 on failure.
 * Earlier redirections may already have been applied on failure.
 */
int apply_redirections(const Command *command);

#endif
