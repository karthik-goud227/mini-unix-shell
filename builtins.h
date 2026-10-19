#ifndef MSH_BUILTINS_H
#define MSH_BUILTINS_H

#include "parser.h"

typedef struct {
    int handled;
    int status;
    int should_exit;
} BuiltinResult;

BuiltinResult run_builtin(const Command *command, int last_status);

#endif
