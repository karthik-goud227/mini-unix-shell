#ifndef MSH_PARAMETERS_H
#define MSH_PARAMETERS_H

#include <stddef.h>
#include <sys/types.h>

/*
 * Save independent copies of parameter zero and the arguments.
 * count excludes parameter zero. arguments may be NULL when count is zero.
 * Return 0 on success, -1 on failure.
 * Failure leaves the previous parameter state unchanged.
 */
int parameters_set(const char *zero, size_t count,
                   char *const arguments[]);

/*
 * Look up parameter zero or a numbered argument.
 * Return a borrowed string, or "" when the parameter is absent.
 */
const char *parameters_get(size_t index);

/* Return 1 when the parameter exists, including an empty value. */
int parameters_is_set(size_t index);

/* Number of arguments, excluding parameter zero. */
size_t parameters_count(void);

/* Free all storage. Safe to call repeatedly. */
void parameters_clear(void);

/* PID captured on first successful setup; zero before initialization.
 * Preserved across argument replacement, cleanup and fork.
 */
pid_t parameters_shell_pid(void);

#endif
