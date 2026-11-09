#define _POSIX_C_SOURCE 200809L

#include "parameters.h"

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static char **values;
static size_t argument_count;

static void free_values(char **items, size_t count)
{
    if (items == NULL) {
        return;
    }

    for (size_t i = 0; i < count; i++) {
        free(items[i]);
    }
    free(items);
}

void parameters_clear(void)
{
    if (values != NULL) {
        free_values(values, argument_count + 1);
    }
    values = NULL;
    argument_count = 0;
}

size_t parameters_count(void)
{
    return argument_count;
}

const char *parameters_get(size_t index)
{
    if (values == NULL || index > argument_count) {
        return "";
    }
    return values[index];
}

int parameters_set(const char *zero, size_t count,
                   char *const arguments[])
{
    if (zero == NULL || (count > 0 && arguments == NULL)) {
        errno = EINVAL;
        return -1;
    }

    if (count > SIZE_MAX / sizeof(char *) - 1) {
        errno = EOVERFLOW;
        return -1;
    }

    char **copy = calloc(count + 1, sizeof(*copy));
    if (copy == NULL) {
        return -1;
    }

    copy[0] = strdup(zero);
    if (copy[0] == NULL) {
        goto fail;
    }

    for (size_t i = 0; i < count; i++) {
        if (arguments[i] == NULL) {
            errno = EINVAL;
            goto fail;
        }
        copy[i + 1] = strdup(arguments[i]);
        if (copy[i + 1] == NULL) {
            goto fail;
        }
    }

    parameters_clear();
    values = copy;
    argument_count = count;
    return 0;

fail:
    {
        int saved_errno = errno;
        free_values(copy, count + 1);
        errno = saved_errno;
        return -1;
    }
}
