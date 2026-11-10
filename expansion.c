#include "expansion.h"
#include "variables.h"
#include "parameters.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int name_start(char c)
{
    return (c >= 'A' && c <= 'Z')
        || (c >= 'a' && c <= 'z') || c == '_';
}

static int name_char(char c)
{
    return name_start(c) || (c >= '0' && c <= '9');
}

/* Append bytes while keeping the result NUL-terminated. */
static int append(char **out, size_t *used,
                  const char *text, size_t length)
{
    if (length > SIZE_MAX - *used - 1) {
        return -1;
    }

    char *grown = realloc(*out, *used + length + 1);
    if (grown == NULL) {
        return -1;
    }

    memcpy(grown + *used, text, length);
    *used += length;
    grown[*used] = '\0';
    *out = grown;
    return 0;
}

static char *expand_word(const char *raw, int last_status,
                         const char **failure)
{
    *failure = "out of memory during expansion";
    char *out = NULL;
    size_t used = 0;
    size_t i = 0;
    char quote = '\0';

    if (append(&out, &used, "", 0) == -1) {
        return NULL;
    }

    while (raw[i] != '\0') {
        char c = raw[i];

        /* Opening and closing quotes are not copied. */
        if (quote == '\0' && (c == '\'' || c == '"')) {
            quote = c;
            i++;
            continue;
        }

        if (quote != '\0' && c == quote) {
            quote = '\0';
            i++;
            continue;
        }

        /* Single-quoted content is entirely literal. */
        if (quote == '\'') {
            if (append(&out, &used, raw + i, 1) == -1) {
                goto fail;
            }
            i++;
            continue;
        }

        /*
         * Outside quotes, backslash escapes the next character.
         * Inside double quotes, recognize \", \\ and \$.
         */
        if (c == '\\' && raw[i + 1] != '\0'
            && (quote == '\0'
                || raw[i + 1] == '"'
                || raw[i + 1] == '\\'
                || raw[i + 1] == '$')) {
            if (append(&out, &used, raw + i + 1, 1) == -1) {
                goto fail;
            }
            i += 2;
            continue;
        }

        if (c == '$' && raw[i + 1] == '{') {
            size_t start = i + 2;
            size_t end = start;
            while (raw[end] != '\0' && raw[end] != '}') {
                end++;
            }

            if (raw[end] != '}') {
                *failure = "missing closing brace in parameter expansion";
                goto fail;
            }

            size_t length = end - start;
            if (length == 0) {
                *failure = "empty braced parameter";
                goto fail;
            }

            const char *value = NULL;
            char number[32];

            if (length == 1 && raw[start] == '?') {
                snprintf(number, sizeof(number), "%d", last_status);
                value = number;
            } else if (length == 1 && raw[start] == '#') {
                snprintf(number, sizeof(number), "%zu", parameters_count());
                value = number;
            } else if (raw[start] >= '0' && raw[start] <= '9') {
                size_t index = 0;
                for (size_t j = start; j < end; j++) {
                    if (raw[j] < '0' || raw[j] > '9') {
                        *failure = "invalid braced positional parameter";
                        goto fail;
                    }
                    size_t digit = (size_t)(raw[j] - '0');
                    if (index > (SIZE_MAX - digit) / 10) {
                        *failure = "positional parameter index is too large";
                        goto fail;
                    }
                    index = index * 10 + digit;
                }
                value = parameters_get(index);
            } else {
                char *name = malloc(length + 1);
                if (name == NULL) {
                    goto fail;
                }
                memcpy(name, raw + start, length);
                name[length] = '\0';

                if (!variables_valid_name(name)) {
                    free(name);
                    *failure = "invalid or unsupported braced parameter";
                    goto fail;
                }

                value = variables_get(name);
                free(name);
            }

            if (value != NULL
                && append(&out, &used, value, strlen(value)) == -1) {
                goto fail;
            }

            i = end + 1;
            continue;
        }

        if (c == '$' && raw[i + 1] == '?') {
            char number[32];
            snprintf(number, sizeof(number), "%d", last_status);

            if (append(&out, &used, number, strlen(number)) == -1) {
                goto fail;
            }
            i += 2;
            continue;
        }

        if (c == '$' && raw[i + 1] >= '0' && raw[i + 1] <= '9') {
            size_t index = (size_t)(raw[i + 1] - '0');
            const char *value = parameters_get(index);
            if (append(&out, &used, value, strlen(value)) == -1) {
                goto fail;
            }
            i += 2;
            continue;
        }

        if (c == '$' && raw[i + 1] == '#') {
            char number[32];
            snprintf(number, sizeof(number), "%zu", parameters_count());
            if (append(&out, &used, number, strlen(number)) == -1) {
                goto fail;
            }
            i += 2;
            continue;
        }

        if (c == '$' && name_start(raw[i + 1])) {
            size_t start = ++i;

            while (name_char(raw[i])) {
                i++;
            }

            size_t length = i - start;
            char *name = malloc(length + 1);
            if (name == NULL) {
                goto fail;
            }

            memcpy(name, raw + start, length);
            name[length] = '\0';

            const char *value = variables_get(name);
            free(name);

            if (value != NULL
                && append(&out, &used, value, strlen(value)) == -1) {
                goto fail;
            }
            continue;
        }

        if (append(&out, &used, raw + i, 1) == -1) {
            goto fail;
        }
        i++;
    }

    return out;

fail:
    free(out);
    return NULL;
}

int expand_tokens(TokenList *tokens, int last_status,
                  char *error, size_t error_size)
{
    if (error_size > 0) {
        error[0] = '\0';
    }

    for (size_t i = 0; i < tokens->count; i++) {
        Token *token = &tokens->items[i];

        if (token->type != TOKEN_WORD) {
            continue;
        }

        if (token->raw_text == NULL) {
            if (error_size > 0) {
                snprintf(error, error_size,
                         "missing original token text");
            }
            return -1;
        }

        const char *failure = NULL;
        char *expanded = expand_word(token->raw_text, last_status, &failure);
        if (expanded == NULL) {
            if (error_size > 0) {
                snprintf(error, error_size, "%s", failure);
            }
            return -1;
        }

        free(token->text);
        token->text = expanded;
    }

    return 0;
}
