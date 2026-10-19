#ifndef MSH_EXECUTOR_H
#define MSH_EXECUTOR_H

#include "parser.h"

int execute_pipeline(const Pipeline *pipeline,
                     const char *command_text);

#endif
