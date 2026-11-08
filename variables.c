
#define _POSIX_C_SOURCE 200809L

#include "variables.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

typedef struct Variable {
    char *name;
    char *value;
    int exported;
    struct Variable *next;
} Variable;

static Variable *variables;

int variables_valid_name(const char *name)
{
    if (name == NULL || name[0] == '\0') {
        return 0;
    }

    for (size_t i = 0; name[i] != '\0'; i++) {
        unsigned char c = (unsigned char)name[i];
        int letter = (c >= 'A' && c <= 'Z')
            || (c >= 'a' && c <= 'z') || c == '_';

        if (!letter && !(i > 0 && c >= '0' && c <= '9')) {
            return 0;
        }
    }

    return 1;
}

static Variable *find_variable(const char *name)
{
    for (Variable *item = variables; item != NULL; item = item->next) {
        if (strcmp(item->name, name) == 0) {
            return item;
        }
    }

    return NULL;
}

const char *variables_get(const char *name)
{
    if (!variables_valid_name(name)) {
        return NULL;
    }

    Variable *item = find_variable(name);
    return item != NULL ? item->value : getenv(name);
}

int variables_set(const char *name, const char *value, int exported)
{
    if (!variables_valid_name(name) || value == NULL) {
        errno = EINVAL;
        return -1;
    }

    Variable *item = find_variable(name);
    int should_export = exported != 0
        || (item != NULL && item->exported)
        || getenv(name) != NULL;

    /* Allocate everything before changing either storage location. */
    char *value_copy = strdup(value);
    if (value_copy == NULL) {
        return -1;
    }

    Variable *created = NULL;
    if (item == NULL) {
        created = calloc(1, sizeof(*created));
        if (created == NULL) {
            free(value_copy);
            return -1;
        }

        created->name = strdup(name);
        if (created->name == NULL) {
            free(created);
            free(value_copy);
            return -1;
        }
    }

    if (should_export && setenv(name, value_copy, 1) == -1) {
        int saved_errno = errno;
        if (created != NULL) {
            free(created->name);
            free(created);
        }
        free(value_copy);
        errno = saved_errno;
        return -1;
    }

    if (item == NULL) {
        item = created;
        item->next = variables;
        variables = item;
    }

    free(item->value);
    item->value = value_copy;
    item->exported = should_export;
    return 0;
}

int variables_export(const char *name)
{
    const char *value = variables_get(name);

    if (value == NULL) {
        errno = variables_valid_name(name) ? ENOENT : EINVAL;
        return -1;
    }

    return variables_set(name, value, 1);
}

int variables_unset(const char *name)
{
    if (!variables_valid_name(name)) {
        errno = EINVAL;
        return -1;
    }

    if (unsetenv(name) == -1) {
        return -1;
    }

    Variable **link = &variables;
    while (*link != NULL) {
        Variable *item = *link;

        if (strcmp(item->name, name) == 0) {
            *link = item->next;
            free(item->name);
            free(item->value);
            free(item);
            break;
        }

        link = &item->next;
    }

    return 0;
}

void variables_clear(void)
{
    while (variables != NULL) {
        Variable *item = variables;
        variables = item->next;
        free(item->name);
        free(item->value);
        free(item);
    }
}

struct VariableSnapshot {
    char *name;
    Variable *saved;
    char *environment_value;
};

void variables_snapshot_free(VariableSnapshot *snapshot)
{
    if (snapshot == NULL) {
        return;
    }

    if (snapshot->saved != NULL) {
        free(snapshot->saved->name);
        free(snapshot->saved->value);
        free(snapshot->saved);
    }
    free(snapshot->environment_value);
    free(snapshot->name);
    free(snapshot);
}

int variables_save(const char *name, VariableSnapshot **snapshot)
{
    if (snapshot == NULL) {
        errno = EINVAL;
        return -1;
    }
    *snapshot = NULL;

    if (!variables_valid_name(name)) {
        errno = EINVAL;
        return -1;
    }

    VariableSnapshot *copy = calloc(1, sizeof(*copy));
    if (copy == NULL) {
        return -1;
    }

    copy->name = strdup(name);
    if (copy->name == NULL) {
        goto fail;
    }

    Variable *item = find_variable(name);
    if (item != NULL) {
        copy->saved = calloc(1, sizeof(*copy->saved));
        if (copy->saved == NULL) {
            goto fail;
        }
        copy->saved->name = strdup(item->name);
        copy->saved->value = strdup(item->value);
        copy->saved->exported = item->exported;
        if (copy->saved->name == NULL || copy->saved->value == NULL) {
            goto fail;
        }
    }

    const char *environment_value = getenv(name);
    if (environment_value != NULL) {
        copy->environment_value = strdup(environment_value);
        if (copy->environment_value == NULL) {
            goto fail;
        }
    }

    *snapshot = copy;
    return 0;

fail:
    {
        int saved_errno = errno;
        variables_snapshot_free(copy);
        errno = saved_errno;
        return -1;
    }
}

int variables_restore(VariableSnapshot *snapshot)
{
    if (snapshot == NULL) {
        errno = EINVAL;
        return -1;
    }

    int result = snapshot->environment_value != NULL
        ? setenv(snapshot->name, snapshot->environment_value, 1)
        : unsetenv(snapshot->name);

    if (result == -1) {
        return -1;
    }

    Variable **link = &variables;
    while (*link != NULL) {
        Variable *item = *link;
        if (strcmp(item->name, snapshot->name) == 0) {
            *link = item->next;
            free(item->name);
            free(item->value);
            free(item);
            break;
        }
        link = &item->next;
    }

    if (snapshot->saved != NULL) {
        snapshot->saved->next = variables;
        variables = snapshot->saved;
        snapshot->saved = NULL;
    }

    variables_snapshot_free(snapshot);
    return 0;
}
