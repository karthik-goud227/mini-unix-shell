#include "lexer.h"

#include <stdio.h>
#include <string.h>

static const char *token_name(TokenType type)
{
    switch (type) {
    case TOKEN_WORD:   return "WORD";
    case TOKEN_PIPE:   return "PIPE";
    case TOKEN_IN:     return "IN";
    case TOKEN_OUT:    return "OUT";
    case TOKEN_APPEND: return "APPEND";
    case TOKEN_AMP:    return "AMP";
    case TOKEN_SEMI:   return "SEMI";
    case TOKEN_AND_IF: return "AND_IF";
    case TOKEN_OR_IF:  return "OR_IF";
    }

    return "UNKNOWN";
}

static int check_semicolon(const char *input, TokenType expected_type,
                           const char *expected_raw)
{
    TokenList tokens;
    char error[128];

    if (lex_line(input, &tokens, error, sizeof(error)) == -1) {
        printf("FAIL: %s: %s\n", input, error);
        return 1;
    }

    int passed = tokens.count == 3
        && tokens.items[0].type == TOKEN_WORD
        && strcmp(tokens.items[0].text, "a") == 0
        && tokens.items[1].type == expected_type
        && strcmp(tokens.items[1].text, ";") == 0
        && strcmp(tokens.items[1].raw_text, expected_raw) == 0
        && tokens.items[2].type == TOKEN_WORD
        && strcmp(tokens.items[2].text, "b") == 0;

    printf("%s: %s\n", passed ? "PASS" : "FAIL", input);
    free_tokens(&tokens);
    return passed ? 0 : 1;
}

static int check_operator(const char *input, TokenType expected_type,
                          const char *expected_text,
                          const char *expected_raw)
{
    TokenList tokens;
    char error[128];

    if (lex_line(input, &tokens, error, sizeof(error)) == -1) {
        printf("FAIL: %s: %s\n", input, error);
        return 1;
    }

    int passed = tokens.count == 3
        && tokens.items[0].type == TOKEN_WORD
        && strcmp(tokens.items[0].text, "a") == 0
        && tokens.items[1].type == expected_type
        && strcmp(tokens.items[1].text, expected_text) == 0
        && strcmp(tokens.items[1].raw_text, expected_raw) == 0
        && tokens.items[2].type == TOKEN_WORD
        && strcmp(tokens.items[2].text, "b") == 0;

    printf("%s: %s\n", passed ? "PASS" : "FAIL", input);
    free_tokens(&tokens);
    return passed ? 0 : 1;
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

    failures += check_semicolon("a;b", TOKEN_SEMI, ";");
    failures += check_semicolon("a ';' b", TOKEN_WORD, "';'");
    failures += check_semicolon("a \";\" b", TOKEN_WORD, "\";\"");
    failures += check_semicolon("a \\; b", TOKEN_WORD, "\\;");

    failures += check_operator("a&&b", TOKEN_AND_IF, "&&", "&&");
    failures += check_operator("a||b", TOKEN_OR_IF, "||", "||");
    failures += check_operator("a&b", TOKEN_AMP, "&", "&");
    failures += check_operator("a|b", TOKEN_PIPE, "|", "|");

    failures += check_operator("a '&&' b", TOKEN_WORD, "&&", "'&&'");
    failures += check_operator("a \"&&\" b", TOKEN_WORD, "&&", "\"&&\"");
    failures += check_operator("a \\&\\& b", TOKEN_WORD, "&&", "\\&\\&");

    failures += check_operator("a '||' b", TOKEN_WORD, "||", "'||'");
    failures += check_operator("a \"||\" b", TOKEN_WORD, "||", "\"||\"");
    failures += check_operator("a \\|\\| b", TOKEN_WORD, "||", "\\|\\|");

    printf("\nFailures: %d\n", failures);
    return failures == 0 ? 0 : 1;
}
