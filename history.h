#ifndef MSH_HISTORY_H
#define MSH_HISTORY_H

#define MSH_HISTORY_LIMIT 100

/*
 * Store an owned copy of a nonblank input line.
 * Discard the oldest entry when the history is full.
 * Return 0 on success or skipped blank input, -1 on failure.
 */
int history_add(const char *line);

/* Print retained entries from oldest to newest with their numbers.
 * Return 0 on success, -1 on output failure.
 */
int history_print(void);

/* Free all entries and restart numbering at 1.
 * Also used during shell shutdown.
 */
void history_clear(void);

#endif
