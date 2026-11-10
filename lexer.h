#ifndef MSH_LEXER_H
#define MSH_LEXER_H

#include <stddef.h>

typedef enum {
    TOKEN_WORD,
    TOKEN_PIPE,
    TOKEN_IN,
    TOKEN_OUT,
    TOKEN_APPEND,
    TOKEN_AMP,
    TOKEN_SEMI,
    TOKEN_AND_IF,
    TOKEN_OR_IF
} TokenType;

typedef struct {
    TokenType type;
    char *text;
    char *raw_text; /* Original spelling, including quotes/escapes. */
    size_t position;
    int assignment_ineligible; /* Word produced by expansion. */
    int expansion_empty; /* Original word expanded to zero fields. */
} Token;

typedef struct {
    Token *items;
    size_t count;
} TokenList;

int lex_line(const char *line, TokenList *result,
             char *error, size_t error_size);

/*
 * Copy a valid token range into an independently owned list.
 * Failure leaves an empty result. Release it with free_tokens().
 */
int tokens_copy_range(const TokenList *source,
                      size_t start, size_t count, TokenList *result);

void free_tokens(TokenList *tokens);

#endif
