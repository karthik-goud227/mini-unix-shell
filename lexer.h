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
    TOKEN_SEMI
} TokenType;

typedef struct {
    TokenType type;
    char *text;
    char *raw_text; /* Original spelling, including quotes/escapes. */
    size_t position;
} Token;

typedef struct {
    Token *items;
    size_t count;
} TokenList;

int lex_line(const char *line, TokenList *result,
             char *error, size_t error_size);

void free_tokens(TokenList *tokens);

#endif
