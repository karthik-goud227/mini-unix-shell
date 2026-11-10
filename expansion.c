#include "expansion.h"
#include "variables.h"
#include "parameters.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *message;
    char detail[128];
    int required;
} ExpansionError;

static int name_start(char c)
{
    return (c >= 'A' && c <= 'Z')
        || (c >= 'a' && c <= 'z') || c == '_';
}

static int name_char(char c)
{
    return name_start(c) || (c >= '0' && c <= '9');
}

enum {
    WORD_SPLIT = 1,
    WORD_KEEP_EMPTY = 2
};

typedef struct {
    char *text;
    unsigned char *flags; /* One entry per byte, plus the final boundary. */
    size_t length;
} WordBuffer;

/* Append bytes while keeping the result NUL-terminated. */
static int append(WordBuffer *word, const char *text, size_t length)
{
    if (length > SIZE_MAX - word->length - 1) {
        return -1;
    }

    char *grown = realloc(word->text, word->length + length + 1);
    if (grown == NULL) {
        return -1;
    }

    word->text = grown;

    int first_allocation = word->flags == NULL;
    unsigned char *flags =
        realloc(word->flags, word->length + length + 1);
    if (flags == NULL) {
        return -1;
    }
    word->flags = flags;

    if (first_allocation) {
        flags[word->length] = 0;
    }
    if (length > 0) {
        memset(flags + word->length + 1, 0, length);
    }

    memcpy(word->text + word->length, text, length);
    word->length += length;
    word->text[word->length] = '\0';
    return 0;
}

/* Record whether substituted bytes came from an unquoted expansion. */
static int append_expansion(WordBuffer *word, const char *text,
                            size_t length, char quote)
{
    size_t start = word->length;
    if (append(word, text, length) == -1) {
        return -1;
    }

    if (quote == '\0') {
        for (size_t i = start; i < word->length; i++) {
            word->flags[i] |= WORD_SPLIT;
        }
    }
    return 0;
}

/* Find the matching brace without evaluating the parameter contents. */
static int append_fragment(WordBuffer *word,
                           const WordBuffer *fragment)
{
    size_t start = word->length;
    if (append(word, fragment->text, fragment->length) == -1) {
        return -1;
    }

    /* Include the final boundary so quoted empty values survive. */
    for (size_t i = 0; i <= fragment->length; i++) {
        word->flags[start + i] |= fragment->flags[i];
    }
    return 0;
}

/* Append one field as data, never as new assignment syntax. */
static inline int append_field(TokenList *fields, const Token *source,
                               const char *text, size_t length)
{
    if (length == SIZE_MAX
        || fields->count >= SIZE_MAX / sizeof(Token)) {
        return -1;
    }

    char *value = malloc(length + 1);
    if (value == NULL) {
        return -1;
    }
    memcpy(value, text, length);
    value[length] = '\0';

    char *raw = malloc(1);
    if (raw == NULL) {
        free(value);
        return -1;
    }
    raw[0] = '\0';

    Token *grown = realloc(fields->items,
                           (fields->count + 1) * sizeof(*grown));
    if (grown == NULL) {
        free(value);
        free(raw);
        return -1;
    }

    fields->items = grown;
    fields->items[fields->count++] = (Token) {
        .type = TOKEN_WORD,
        .text = value,
        .raw_text = raw,
        .position = source->position,
        .assignment_ineligible = 1
    };
    return 0;
}

/* Split only eligible whitespace; quote boundaries retain empty fields. */
static inline int split_word_fields(const WordBuffer *word,
                                    const Token *source, TokenList *fields)
{
    size_t start = 0;
    int keep = 0;

    for (size_t i = 0; i <= word->length; i++) {
        keep |= (word->flags[i] & WORD_KEEP_EMPTY) != 0;

        int at_end = i == word->length;
        int separator = !at_end
            && (word->flags[i] & WORD_SPLIT)
            && (word->text[i] == ' ' || word->text[i] == '\t'
                || word->text[i] == '\n');

        if (!at_end && !separator) {
            continue;
        }

        if (i > start || keep) {
            if (append_field(fields, source, word->text + start,
                             i - start) == -1) {
                return -1;
            }
        }

        start = i + 1;
        keep = 0;
    }
    return 0;
}

static int parameter_end(const char *raw, size_t start,
                         char outer_quote, size_t depth,
                         size_t *end, ExpansionError *failure)
{
    if (depth >= 64) {
        failure->message = "parameter expansion nesting is too deep";
        return -1;
    }

    char quote = '\0';
    for (size_t i = start; raw[i] != '\0'; i++) {
        char c = raw[i];

        if (quote == '\'') {
            if (c == '\'') {
                quote = '\0';
            }
            continue;
        }

        if (c == '\\' && raw[i + 1] != '\0') {
            i++;
            continue;
        }

        if (c == '"') {
            quote = quote == '"' ? '\0' : '"';
            continue;
        }

        if (c == '\'' && quote == '\0' && outer_quote != '"') {
            quote = '\'';
            continue;
        }

        if (c == '$' && raw[i + 1] == '{') {
            size_t nested_end;
            char context = quote == '"' ? '"' : outer_quote;
            if (parameter_end(raw, i + 2, context, depth + 1,
                              &nested_end, failure) == -1) {
                return -1;
            }
            i = nested_end;
            continue;
        }

        if (c == '}' && quote == '\0') {
            *end = i;
            return 0;
        }
    }

    failure->message = "missing closing brace in parameter expansion";
    return -1;
}

static char *expand_word(const char *raw, int last_status,
                         ExpansionError *failure, size_t depth, char initial_quote, WordBuffer *metadata)
{
    if (depth >= 64) {
        failure->message = "parameter expansion nesting is too deep";
        return NULL;
    }

    failure->message = "out of memory during expansion";
    WordBuffer word = {0};
    size_t i = 0;
    char quote = initial_quote;

    if (append(&word, "", 0) == -1) {
        free(word.text);
        free(word.flags);
        return NULL;
    }

    if (initial_quote != '\0') {
        word.flags[0] |= WORD_KEEP_EMPTY;
    }

    while (raw[i] != '\0') {
        char c = raw[i];

        /* Opening and closing quotes are not copied. */
        if (quote == '\0' && (c == '\'' || c == '"')) {
            quote = c;
            word.flags[word.length] |= WORD_KEEP_EMPTY;
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
            if (append(&word, raw + i, 1) == -1) {
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
            if (append(&word, raw + i + 1, 1) == -1) {
                goto fail;
            }
            i += 2;
            continue;
        }

        if (c == '$' && raw[i + 1] == '{') {
            size_t start = i + 2;
            size_t end;
            if (parameter_end(raw, start, quote, depth,
                              &end, failure) == -1) {
                goto fail;
            }

            size_t length = end - start;
            if (length == 0) {
                failure->message = "empty braced parameter";
                goto fail;
            }

            size_t name_end = start;
            int positional = raw[start] >= '0' && raw[start] <= '9';
            size_t positional_index = 0;

            if (name_start(raw[start])) {
                name_end++;
                while (name_end < end && name_char(raw[name_end])) {
                    name_end++;
                }
            } else if (positional) {
                while (name_end < end
                       && raw[name_end] >= '0' && raw[name_end] <= '9') {
                    size_t digit = (size_t)(raw[name_end] - '0');
                    if (positional_index > (SIZE_MAX - digit) / 10) {
                        failure->message = "positional parameter index is too large";
                        goto fail;
                    }
                    positional_index = positional_index * 10 + digit;
                    name_end++;
                }
            }

            if (name_end > start && name_end < end
                && (raw[name_end] == '-' || raw[name_end] == '='
                    || raw[name_end] == '?')) {
                int assign_default = raw[name_end] == '=';
                int require_value = raw[name_end] == '?';
                if (positional && assign_default) {
                    failure->message = "invalid braced positional parameter";
                    goto fail;
                }
                size_t name_length = name_end - start;
                char *name = malloc(name_length + 1);
                if (name == NULL) {
                    goto fail;
                }
                memcpy(name, raw + start, name_length);
                name[name_length] = '\0';
                const char *existing = positional
                    ? (parameters_is_set(positional_index)
                       ? parameters_get(positional_index) : NULL)
                    : variables_get(name);

                if (existing != NULL) {
                    int result = append_expansion(&word, existing, strlen(existing), quote);
                    free(name);
                    if (result == -1) {
                        goto fail;
                    }
                } else {
                    size_t fallback_length = end - name_end - 1;
                    char *fallback = malloc(fallback_length + 1);
                    if (fallback == NULL) {
                        free(name);
                        goto fail;
                    }
                    memcpy(fallback, raw + name_end + 1, fallback_length);
                    fallback[fallback_length] = '\0';

                    WordBuffer nested = {0};
                    char *expanded = expand_word(fallback, last_status,
                                                 failure, depth + 1, quote,
                                                 &nested);
                    free(fallback);
                    if (expanded == NULL) {
                        free(name);
                        goto fail;
                    }

                    if (require_value) {
                        snprintf(failure->detail, sizeof(failure->detail),
                                 "%s: %s", name,
                                 expanded[0] != '\0'
                                     ? expanded : "parameter is not set");
                        failure->message = failure->detail;
                        failure->required = 1;
                        free(name);
                        free(expanded);
                    free(nested.flags);
                        goto fail;
                    }

                    if (assign_default
                        && variables_set(name, expanded, 0) == -1) {
                        free(name);
                        free(expanded);
                    free(nested.flags);
                        failure->message = "could not assign parameter default";
                        goto fail;
                    }
                    free(name);

                    int result = append_fragment(&word, &nested);
                    free(expanded);
                    free(nested.flags);
                    if (result == -1) {
                        goto fail;
                    }
                }

                i = end + 1;
                continue;
            }

            const char *value = NULL;
            char number[32];

            if (length == 1 && raw[start] == '!') {
                pid_t pid = parameters_last_background_pid();
                number[0] = '\0';
                if (pid > 0) {
                    snprintf(number, sizeof(number), "%ld", (long)pid);
                }
                value = number;
            } else if (length == 1 && raw[start] == '$') {
                snprintf(number, sizeof(number), "%ld",
                         (long)parameters_shell_pid());
                value = number;
            } else if (length == 1 && raw[start] == '?') {
                snprintf(number, sizeof(number), "%d", last_status);
                value = number;
            } else if (length == 1 && raw[start] == '#') {
                snprintf(number, sizeof(number), "%zu", parameters_count());
                value = number;
            } else if (raw[start] >= '0' && raw[start] <= '9') {
                size_t index = 0;
                for (size_t j = start; j < end; j++) {
                    if (raw[j] < '0' || raw[j] > '9') {
                        failure->message = "invalid braced positional parameter";
                        goto fail;
                    }
                    size_t digit = (size_t)(raw[j] - '0');
                    if (index > (SIZE_MAX - digit) / 10) {
                        failure->message = "positional parameter index is too large";
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
                    failure->message = "invalid or unsupported braced parameter";
                    goto fail;
                }

                value = variables_get(name);
                free(name);
            }

            if (value != NULL
                && append_expansion(&word, value, strlen(value), quote) == -1) {
                goto fail;
            }

            i = end + 1;
            continue;
        }

        if (c == '$' && raw[i + 1] == '!') {
            pid_t pid = parameters_last_background_pid();
            if (pid > 0) {
                char number[32];
                snprintf(number, sizeof(number), "%ld", (long)pid);
                if (append_expansion(&word, number, strlen(number), quote) == -1) {
                    goto fail;
                }
            }
            i += 2;
            continue;
        }

        if (c == '$' && raw[i + 1] == '$') {
            char number[32];
            snprintf(number, sizeof(number), "%ld",
                     (long)parameters_shell_pid());

            if (append_expansion(&word, number, strlen(number), quote) == -1) {
                goto fail;
            }
            i += 2;
            continue;
        }

        if (c == '$' && raw[i + 1] == '?') {
            char number[32];
            snprintf(number, sizeof(number), "%d", last_status);

            if (append_expansion(&word, number, strlen(number), quote) == -1) {
                goto fail;
            }
            i += 2;
            continue;
        }

        if (c == '$' && raw[i + 1] >= '0' && raw[i + 1] <= '9') {
            size_t index = (size_t)(raw[i + 1] - '0');
            const char *value = parameters_get(index);
            if (append_expansion(&word, value, strlen(value), quote) == -1) {
                goto fail;
            }
            i += 2;
            continue;
        }

        if (c == '$' && raw[i + 1] == '#') {
            char number[32];
            snprintf(number, sizeof(number), "%zu", parameters_count());
            if (append_expansion(&word, number, strlen(number), quote) == -1) {
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
                && append_expansion(&word, value, strlen(value), quote) == -1) {
                goto fail;
            }
            continue;
        }

        size_t literal_start = word.length;
        if (append(&word, raw + i, 1) == -1) {
            goto fail;
        }
        if (depth > 0 && quote == '\0') {
            word.flags[literal_start] |= WORD_SPLIT;
        }
        i++;
    }

    if (metadata != NULL) {
        *metadata = word;
    } else {
        free(word.flags);
    }
    return word.text;

fail:
    free(word.text);
    free(word.flags);
    return NULL;
}

int expand_tokens_checked(TokenList *tokens, int last_status,
                          char *error, size_t error_size,
                          int *required_failure)
{
    if (required_failure != NULL) {
        *required_failure = 0;
    }

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

        ExpansionError failure = {0};
        char *expanded = expand_word(token->raw_text, last_status, &failure, 0, '\0', NULL);
        if (expanded == NULL) {
            if (required_failure != NULL) {
                *required_failure = failure.required;
            }
            if (error_size > 0) {
                snprintf(error, error_size, "%s", failure.message);
            }
            return -1;
        }

        free(token->text);
        token->text = expanded;
    }

    return 0;
}

int expand_tokens(TokenList *tokens, int last_status,
                  char *error, size_t error_size)
{
    return expand_tokens_checked(tokens, last_status, error, error_size, NULL);
}


/* Preserve original syntax for operators and leading assignments. */
static int copy_expansion_token(TokenList *output, const Token *source)
{
    if (append_field(output, source, source->text,
                     strlen(source->text)) == -1) {
        return -1;
    }

    Token *copy = &output->items[output->count - 1];
    char *raw = malloc(strlen(source->raw_text) + 1);
    if (raw == NULL) {
        return -1;
    }
    strcpy(raw, source->raw_text);
    free(copy->raw_text);
    copy->raw_text = raw;
    copy->type = source->type;
    copy->assignment_ineligible = source->assignment_ineligible;
    copy->expansion_empty = source->expansion_empty;
    return 0;
}

static int original_assignment(const Token *token)
{
    const char *raw = token->raw_text;
    if (token->assignment_ineligible || !name_start(raw[0])) {
        return 0;
    }

    size_t i = 1;
    while (name_char(raw[i])) {
        i++;
    }
    return raw[i] == '=';
}

/* Requires an owned token list. Failure leaves that list unchanged. */
int expand_pipeline_fields(TokenList *tokens, int last_status,
                           char *error, size_t error_size,
                           int *required_failure)
{
    TokenList output = {0};
    ExpansionError failure = {.message = "out of memory during expansion"};
    int command_started = 0;
    int redirect_target = 0;

    if (required_failure != NULL) {
        *required_failure = 0;
    }
    if (error_size > 0) {
        error[0] = '\0';
    }

    for (size_t i = 0; i < tokens->count; i++) {
        const Token *token = &tokens->items[i];
        if (token->raw_text == NULL || token->text == NULL) {
            failure.message = "missing token text";
            goto fail;
        }

        if (token->type != TOKEN_WORD) {
            if (copy_expansion_token(&output, token) == -1) {
                goto fail;
            }
            redirect_target = token->type == TOKEN_IN
                || token->type == TOKEN_OUT
                || token->type == TOKEN_APPEND;
            if (token->type == TOKEN_PIPE) {
                command_started = 0;
            }
            continue;
        }

        if (!redirect_target && !command_started
            && original_assignment(token)) {
            if (copy_expansion_token(&output, token) == -1) {
                goto fail;
            }
            continue;
        }

        if (!redirect_target
            && (strcmp(token->raw_text, "\"$@\"") == 0
                || strcmp(token->raw_text, "\"${@}\"") == 0)) {
            command_started = 1;
            size_t count = parameters_count();

            if (count == 0) {
                if (append_field(&output, token, "", 0) == -1) {
                    goto fail;
                }
                output.items[output.count - 1].expansion_empty = 1;
            } else {
                for (size_t argument = 1; argument <= count; argument++) {
                    const char *value = parameters_get(argument);
                    if (append_field(&output, token, value,
                                     strlen(value)) == -1) {
                        goto fail;
                    }
                }
            }
            continue;
        }

        WordBuffer word = {0};
        char *value = expand_word(token->raw_text, last_status,
                                  &failure, 0, '\0', &word);
        if (value == NULL) {
            goto fail;
        }

        int result;
        if (redirect_target) {
            result = append_field(&output, token, value, word.length);
            redirect_target = 0;
        } else {
            command_started = 1;
            size_t before = output.count;
            result = split_word_fields(&word, token, &output);

            if (result == 0 && output.count == before) {
                result = append_field(&output, token, "", 0);
                if (result == 0) {
                    output.items[output.count - 1].expansion_empty = 1;
                }
            }
        }

        free(word.text);
        free(word.flags);
        if (result == -1) {
            failure.message = "out of memory during expansion";
            goto fail;
        }
    }

    free_tokens(tokens);
    *tokens = output;
    return 0;

fail:
    if (required_failure != NULL) {
        *required_failure = failure.required;
    }
    if (error_size > 0) {
        snprintf(error, error_size, "%s", failure.message);
    }
    free_tokens(&output);
    return -1;
}
