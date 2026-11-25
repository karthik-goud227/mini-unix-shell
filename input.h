#ifndef MSH_INPUT_H
#define MSH_INPUT_H

#include <stddef.h>

#define MSH_MAX_LINE 4096

/*
 * Returns:
 *   1: a line is ready, without its trailing newline
 *   0: end of input
 *  -1: input/allocation error
 *  -2: invalid or oversized line was discarded
 *  -3: input was cancelled by Ctrl+C
 *
 * The caller owns the reusable buffer and must free it.
 */
int input_read_line(char **line, size_t *capacity);

/* Same contract, reading from a borrowed descriptor; does not close it. */
int input_read_line_fd(int fd, char **line, size_t *capacity);

/* Read a shell command, removing unquoted or double-quoted backslash-newlines. */
int input_read_command_line_fd(int fd, char **line, size_t *capacity);

#endif
