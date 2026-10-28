#include "expansion.h"

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

static char *expand_word(const char *raw, int last_status)
{
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

        if (c == '$' && raw[i + 1] == '?') {
            char number[32];
            snprintf(number, sizeof(number), "%d", last_status);

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

            const char *value = getenv(name);
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

        char *expanded = expand_word(token->raw_text, last_status);
        if (expanded == NULL) {
            if (error_size > 0) {
                snprintf(error, error_size,
                         "out of memory during expansion");
            }
            return -1;
        }

        free(token->text);
        token->text = expanded;
    }

    return 0;
}
