#include "expansion.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int total = 0;
static int failures = 0;

static void check(const char *name, const char *input,
                  int status, const char *expected)
{
    TokenList tokens;
    char error[128];
    total++;

    if (lex_line(input, &tokens, error, sizeof(error)) == -1) {
        printf("FAIL: %s: lexer: %s\n", name, error);
        failures++;
        return;
    }

    if (expand_tokens(&tokens, status, error, sizeof(error)) == -1) {
        printf("FAIL: %s: expansion: %s\n", name, error);
        failures++;
        free_tokens(&tokens);
        return;
    }

    if (tokens.count != 1
        || tokens.items[0].type != TOKEN_WORD
        || strcmp(tokens.items[0].text, expected) != 0
        || strcmp(tokens.items[0].raw_text, input) != 0) {
        printf("FAIL: %s\n", name);
        printf("  Expected one word: <%s>\n", expected);
        for (size_t i = 0; i < tokens.count; i++) {
            printf("  Actual token: <%s>\n", tokens.items[i].text);
        }
        failures++;
    } else {
        printf("PASS: %s\n", name);
    }

    free_tokens(&tokens);
}

int main(void)
{
    if (setenv("MSH_TEST_VALUE", "hello world", 1) == -1
        || setenv("MSH_TEST_LITERAL", "$HOME | cat", 1) == -1
        || unsetenv("MSH_TEST_UNSET") == -1) {
        perror("test environment");
        return 1;
    }

    check("plain text", "hello", 0, "hello");
    check("unquoted variable", "$MSH_TEST_VALUE", 0, "hello world");
    check("double-quoted variable", "\"$MSH_TEST_VALUE\"",
          0, "hello world");
    check("single quotes prevent expansion", "'$MSH_TEST_VALUE'",
          0, "$MSH_TEST_VALUE");
    check("escaped dollar", "\\$MSH_TEST_VALUE",
          0, "$MSH_TEST_VALUE");
    check("escaped dollar inside double quotes", "\"\\$MSH_TEST_VALUE\"",
          0, "$MSH_TEST_VALUE");

    check("previous failure status", "$?", 1, "1");
    check("previous interrupt status", "\"$?\"", 130, "130");
    check("single-quoted status", "'$?'", 130, "$?");

    check("unset variable remains one empty word",
          "$MSH_TEST_UNSET", 0, "");
    check("empty quotes", "\"\"", 0, "");
    check("adjacent fragments", "pre\"$MSH_TEST_VALUE\"post",
          0, "prehello worldpost");
    check("quote boundary ends variable name", "$MSH_TEST_VALUE'x'",
          0, "hello worldx");
    check("longest variable name", "$MSH_TEST_UNSET_suffix",
          0, "");

    check("bare dollar", "$", 0, "$");
    check("backslash-n preserved in double quotes", "\"a\\nb\"",
          0, "a\\nb");
    check("value is not expanded or parsed again", "$MSH_TEST_LITERAL",
          0, "$HOME | cat");

    printf("\nTests: %d, failures: %d\n", total, failures);
    return failures ? 1 : 0;
}
