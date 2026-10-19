#ifndef MSH_INPUT_H
#define MSH_INPUT_H

#include <stddef.h>

#define MSH_MAX_LINE 4096

/*
 * Returns:
 *   1: a line is ready, without its trailing newline
 *   0: end of input
 *  -1: input/allocation error
 *  -2: line exceeded MSH_MAX_LINE and was discarded
 *
 * The caller owns the reusable buffer and must free it.
 */
int input_read_line(char **line, size_t *capacity);

#endif
