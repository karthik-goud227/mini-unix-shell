#include "lexer.h"

#include <stdio.h>

static const char *token_name(TokenType type)
{
    switch (type) {
    case TOKEN_WORD:   return "WORD";
    case TOKEN_PIPE:   return "PIPE";
    case TOKEN_IN:     return "IN";
    case TOKEN_OUT:    return "OUT";
    case TOKEN_APPEND: return "APPEND";
    case TOKEN_AMP:    return "AMP";
    }

    return "UNKNOWN";
}

int main(void)
{
    const char *cases[] = {
        "cat<input.txt|wc -l &",
        "echo \"hello world\" '' a\"b c\"",
        "echo hello\\ world '|'",
        "echo first >> out.txt",
        "echo \"unfinished",
        "echo trailing\\",
        "   "
    };

    int failures = 0;

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        TokenList tokens;
        char error[128];
        int should_fail = (i == 4 || i == 5);
        int result = lex_line(cases[i], &tokens,
                              error, sizeof(error));

        printf("TEST %zu\n", i + 1);

        if (result == -1) {
            printf("  ERROR: %s\n", error);
        } else {
            for (size_t j = 0; j < tokens.count; j++) {
                printf("  %s [%s] @%zu\n",
                       token_name(tokens.items[j].type),
                       tokens.items[j].text,
                       tokens.items[j].position);
            }

            if (tokens.count == 0) {
                puts("  (no tokens)");
            }
        }

        if ((result == -1) != should_fail) {
            failures++;
        }

        free_tokens(&tokens);
    }

    return failures == 0 ? 0 : 1;
}
