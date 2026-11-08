#ifndef MSH_EXECUTOR_H
#define MSH_EXECUTOR_H

#include "parser.h"

int execute_pipeline(const Pipeline *pipeline,
                     const char *command_text);

/*
 * Child-only: apply redirections and execute one command.
 * Executes a built-in or replaces the process using execvp.
 * Always terminates the child on completion or failure.
 */
_Noreturn void execute_child_command(const Command *command,
                                     int last_status);

#endif
