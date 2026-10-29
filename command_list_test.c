#include "parser.h"

#include <stdio.h>
#include <string.h>

static int total;
static int failures;

static void check(const char *name, const char *input,
                  size_t expected_count, const char *expected_error)
{
    TokenList tokens;
    CommandList list;
    char error[128];
    total++;

    if (lex_line(input, &tokens, error, sizeof(error)) == -1) {
        printf("FAIL: %s: lexer: %s\n", name, error);
        failures++;
        return;
    }

    int result = parse_command_list(&tokens, &list,
                                    error, sizeof(error));
    int passed;

    if (expected_error != NULL) {
        passed = result == -1
            && strstr(error, expected_error) != NULL
            && list.items == NULL
            && list.count == 0;
    } else {
        passed = result == 0 && list.count == expected_count;

        /* Check that ranges cover all non-separator tokens in order. */
        size_t next = 0;

        if (result == 0) {
            for (size_t i = 0; i < list.count; i++) {
                CommandListItem item = list.items[i];

                if (item.token_start != next
                    || item.token_count == 0
                    || item.token_start > tokens.count
                    || item.token_count > tokens.count - item.token_start) {
                    passed = 0;
                    break;
                }

                size_t end = item.token_start + item.token_count;

                for (size_t j = item.token_start; j < end; j++) {
                    if (tokens.items[j].type == TOKEN_SEMI) {
                        passed = 0;
                    }
                }

                next = end;
                if (next < tokens.count) {
                    if (tokens.items[next].type != TOKEN_SEMI) {
                        passed = 0;
                    }
                    next++;
                }
            }

            if (next != tokens.count) {
                passed = 0;
            }
        }
    }

    printf("%s: %s\n", passed ? "PASS" : "FAIL", name);
    if (!passed) {
        failures++;
        printf("  result=%d, items=%zu, diagnostic=%s\n",
               result, list.count, error);
    }

    free_command_list(&list);
    free_tokens(&tokens);
}

int main(void)
{
    check("empty input", "", 0, NULL);
    check("single command", "echo one", 1, NULL);
    check("two commands", "echo one; echo two", 2, NULL);
    check("separator without spaces", "true;false", 2, NULL);
    check("trailing separator", "echo one;", 1, NULL);
    check("pipeline inside list", "echo one | cat; pwd", 2, NULL);
    check("quoted and escaped semicolons",
          "echo ';' \";\" \\;; echo done", 2, NULL);
    check("redirections inside list",
          "echo one > file; cat < file", 2, NULL);
    check("final background job", "echo one; sleep 1 &", 2, NULL);

    check("leading separator", "; echo one", 0,
          "empty command in list");
    check("repeated separator", "echo one;; echo two", 0,
          "empty command in list");
    check("separator alone", ";", 0,
          "empty command in list");
    check("malformed later pipeline", "echo one; echo two |", 0,
          "empty command in pipeline");
    check("missing redirection filename", "echo one >; echo two", 0,
          "expected filename after redirection");
    check("background before separator rejected for now",
          "sleep 1 &; echo two", 0, "'&' must end");
    check("background before another command rejected for now",
          "sleep 1 & echo two", 0, "'&' must end");

    printf("\nTests: %d, failures: %d\n", total, failures);
    return failures ? 1 : 0;
}
