#ifndef MSH_ASSIGNMENT_H
#define MSH_ASSIGNMENT_H

#include <stddef.h>

/*
 * Inspect a word's original spelling for an unquoted NAME= prefix.
 * Return 1 and set name_length for an assignment, otherwise return 0.
 * On a non-assignment, name_length is set to zero.
 *
 * Quotes and escapes are allowed in the value, after the equals sign.
 */
int assignment_name_length(const char *raw, size_t *name_length);


/*
 * Expand and apply original assignment spellings in source order.
 * exported controls whether new variables enter the child environment.
 * Existing export status is preserved.
 *
 * Return 0 on success, -1 with a diagnostic on failure.
 * Earlier successful assignments remain applied if a later one fails.
 */
int assignments_apply(char *const assignments[], size_t count,
                      int exported, int last_status,
                      char *error, size_t error_size);

#endif
