
#include "assignment.h"

static int name_start(char c)
{
    return (c >= 'A' && c <= 'Z')
        || (c >= 'a' && c <= 'z') || c == '_';
}

static int name_char(char c)
{
    return name_start(c) || (c >= '0' && c <= '9');
}

int assignment_name_length(const char *raw, size_t *name_length)
{
    if (name_length == NULL) {
        return 0;
    }

    *name_length = 0;

    if (raw == NULL || !name_start(raw[0])) {
        return 0;
    }

    size_t i = 1;
    while (name_char(raw[i])) {
        i++;
    }

    if (raw[i] != '=') {
        return 0;
    }

    *name_length = i;
    return 1;
}
