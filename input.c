#define _POSIX_C_SOURCE 200809L

#include "input.h"
#include "jobs.h"
#include "signals.h"

#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static int command_continues(const char *line, size_t used)
{
    if (used == 0 || line[used - 1] != '\\') return 0;
    char quote = '\0';
    for (size_t i = 0; i < used; i++) {
        char c = line[i];
        if (quote == '\'') {
            if (c == '\'') quote = '\0';
        } else if (c == '\\' && i + 1 < used) {
            i++;
        } else if (quote == '\0' && (c == '\'' || c == '"')) {
            quote = c;
        } else if (quote == '"' && c == '"') {
            quote = '\0';
        }
    }
    if (quote == '\'') return 0;
    size_t trailing = 0;
    while (trailing < used && line[used - trailing - 1] == '\\') trailing++;
    return trailing % 2 != 0;
}

static int input_read_line_internal(int fd, char **line, size_t *capacity,
                                    int command_mode)
{
    if (*capacity < MSH_MAX_LINE + 1) {
        char *grown = realloc(*line, MSH_MAX_LINE + 1);

        if (grown == NULL) {
            fprintf(stderr, "msh: input: out of memory\n");
            return -1;
        }

        *line = grown;
        *capacity = MSH_MAX_LINE + 1;
    }

    size_t used = 0;
    int too_long = 0;

    for (;;) {
        int interrupted = signals_take_interrupt();

        if (interrupted == -1) {
            return -1;
        }

        if (interrupted) {
            (*line)[0] = '\0';
            putchar('\n');
            return -3;
        }

        struct pollfd descriptors[2] = {
            {
                .fd = fd,
                .events = POLLIN
            },
            {
                .fd = signals_event_fd(),
                .events = POLLIN
            }
        };

        int ready = poll(descriptors, 2, -1);

        if (ready == -1) {
            if (errno == EINTR) {
                continue;
            }

            perror("msh: poll input");
            return -1;
        }

        if (descriptors[1].revents & POLLIN) {
            signals_drain();
            jobs_reap_pending();
            continue;
        }

        if ((descriptors[0].revents
             | descriptors[1].revents) & (POLLERR | POLLNVAL)) {
            fprintf(stderr, "msh: input event descriptor failed\n");
            return -1;
        }

        if (!(descriptors[0].revents & (POLLIN | POLLHUP))) {
            continue;
        }

        char byte;
        ssize_t count = read(fd, &byte, 1);

        if (count == -1) {
            if (errno == EINTR) {
                continue;
            }

            perror("msh: read input");
            return -1;
        }

        if (count > 0 && byte == '\n' && command_mode && !too_long
            && command_continues(*line, used)) {
            used--; /* Remove the unescaped backslash and newline. */
            continue;
        }

        if (count == 0 || byte == '\n') {
            (*line)[used] = '\0';

            if (too_long) {
                fprintf(stderr,
                        "msh: input exceeds %d bytes; line discarded\n",
                        MSH_MAX_LINE);
                return -2;
            }

            if (count == 0 && used == 0) {
                return 0;
            }

            return 1;
        }

        if (byte == '\0') {
            fprintf(stderr, "msh: NUL byte in input; line discarded\n");

            /*
             * Keep consuming this line so its remaining bytes
             * cannot accidentally become another command.
             */
            too_long = 1;
            continue;
        }

        if (used == MSH_MAX_LINE) {
            too_long = 1;
        } else if (!too_long) {
            (*line)[used++] = byte;
        }
    }
}

int input_read_line_fd(int fd, char **line, size_t *capacity)
{
    return input_read_line_internal(fd, line, capacity, 0);
}

int input_read_command_line_fd(int fd, char **line, size_t *capacity)
{
    return input_read_line_internal(fd, line, capacity, 1);
}

int input_read_line(char **line, size_t *capacity)
{
    return input_read_command_line_fd(STDIN_FILENO, line, capacity);
}
