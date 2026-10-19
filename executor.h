#ifndef MSH_EXECUTOR_H
#define MSH_EXECUTOR_H

#include "parser.h"

/* Executes a parsed pipeline and returns a shell-style status. */
int execute_pipeline(const Pipeline *pipeline);

#endif
