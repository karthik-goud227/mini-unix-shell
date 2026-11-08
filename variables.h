#ifndef MSH_VARIABLES_H
#define MSH_VARIABLES_H

/*
 * Variable names follow [A-Za-z_][A-Za-z0-9_]*.
 * Return 1 for a valid name, otherwise 0.
 */
int variables_valid_name(const char *name);

/*
 * Look up a shell variable, falling back to the inherited environment.
 * Return NULL if absent. The returned string is borrowed.
 * Copy it before modifying variables or the environment.
 */
const char *variables_get(const char *name);

/*
 * Assign a value. New variables are local unless exported is nonzero.
 * Assignment preserves an existing variable's export status.
 * Exported values also update the environment used by child programs.
 *
 * Strings are copied. Return 0 on success, -1 on failure.
 */
int variables_set(const char *name, const char *value, int exported);

/*
 * Mark an existing variable for export.
 * Return 0 on success, -1 if absent or on failure.
 */
int variables_export(const char *name);

/*
 * Remove a variable from shell storage and the environment.
 * Removing an absent variable succeeds.
 * Return 0 on success, -1 on failure.
 */
int variables_unset(const char *name);

/*
 * Free shell-owned variable storage.
 * Does not modify the process environment.
 */
void variables_clear(void);

/* Opaque saved state for one variable. */
typedef struct VariableSnapshot VariableSnapshot;

/*
 * Save shell storage and the environment entry independently.
 * On success, *snapshot owns an independent copy.
 * Return 0 on success, -1 on failure.
 */
int variables_save(const char *name, VariableSnapshot **snapshot);

/*
 * Restore the saved state, including absence and export status.
 * On success, consume the snapshot; do not use it again.
 * On failure, retain it for retry or disposal.
 */
int variables_restore(VariableSnapshot *snapshot);

/* Release a snapshot without changing any variable. */
void variables_snapshot_free(VariableSnapshot *snapshot);

#endif
