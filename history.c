#define _POSIX_C_SOURCE 200809L

#include "history.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    unsigned long number;
    char *line;
} HistoryEntry;

static HistoryEntry entries[MSH_HISTORY_LIMIT];
static size_t entry_count;
static unsigned long next_number = 1;

int history_add(const char *line)
{
    if (line == NULL) {
        errno = EINVAL;
        return -1;
    }

    const unsigned char *cursor = (const unsigned char *)line;
    while (*cursor != '\0' && isspace(*cursor)) {
        cursor++;
    }

    if (*cursor == '\0') {
        return 0;
    }

    if (next_number == ULONG_MAX) {
        errno = EOVERFLOW;
        return -1;
    }

    /* Allocate first so failure leaves existing history unchanged. */
    char *copy = strdup(line);
    if (copy == NULL) {
        return -1;
    }

    if (entry_count == MSH_HISTORY_LIMIT) {
        free(entries[0].line);
        memmove(&entries[0], &entries[1],
                (MSH_HISTORY_LIMIT - 1) * sizeof(entries[0]));
        entry_count--;
    }

    entries[entry_count++] = (HistoryEntry) {
        .number = next_number++,
        .line = copy
    };

    return 0;
}

int history_print(void)
{
    for (size_t i = 0; i < entry_count; i++) {
        if (printf("%lu  %s\n",
                   entries[i].number, entries[i].line) < 0) {
            return -1;
        }
    }

    return 0;
}

void history_clear(void)
{
    for (size_t i = 0; i < entry_count; i++) {
        free(entries[i].line);
        entries[i] = (HistoryEntry) {0};
    }

    entry_count = 0;
    next_number = 1;
}
