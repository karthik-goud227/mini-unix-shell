
#define _POSIX_C_SOURCE 200809L

#include "assignment.h"
#include "expansion.h"
#include "variables.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int assignments_apply(char *const assignments[], size_t count,
                      int exported, int last_status,
                      char *error, size_t error_size)
{
    if (error_size > 0) {
        error[0] = '\0';
    }

    for (size_t i = 0; i < count; i++) {
        size_t length;
        const char *raw = assignments[i];

        if (!assignment_name_length(raw, &length)) {
            if (error_size > 0) {
                snprintf(error, error_size, "invalid assignment");
            }
            return -1;
        }

        char *name = strndup(raw, length);
        if (name == NULL) {
            goto memory_error;
        }

        Token token = {
            .type = TOKEN_WORD,
            .text = NULL,
            .raw_text = strdup(raw + length + 1),
            .position = 0
        };

        if (token.raw_text == NULL) {
            free(name);
            goto memory_error;
        }

        TokenList view = {.items = &token, .count = 1};
        int result = expand_tokens(&view, last_status, error, error_size);

        if (result == 0) {
            result = variables_set(name, token.text, exported);
            if (result == -1 && error_size > 0) {
                snprintf(error, error_size,
                         "could not assign variable: %s", name);
            }
        }

        free(name);
        free(token.text);
        free(token.raw_text);

        if (result == -1) {
            return -1;
        }
    }

    return 0;

memory_error:
    if (error_size > 0) {
        snprintf(error, error_size, "out of memory during assignment");
    }
    return -1;
}
