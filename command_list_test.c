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

                ListCondition expected_condition = LIST_ALWAYS;

                if (i > 0 && item.token_start > 0) {
                    TokenType preceding =
                        tokens.items[item.token_start - 1].type;

                    if (preceding == TOKEN_AND_IF) {
                        expected_condition = LIST_ON_SUCCESS;
                    } else if (preceding == TOKEN_OR_IF) {
                        expected_condition = LIST_ON_FAILURE;
                    }
                }

                if (item.condition != expected_condition) {
                    passed = 0;
                }

                size_t end = item.token_start + item.token_count;

                for (size_t j = item.token_start; j < end; j++) {
                    TokenType type = tokens.items[j].type;
                    if (type == TOKEN_SEMI || type == TOKEN_AND_IF
                        || type == TOKEN_OR_IF) {
                        passed = 0;
                    }
                }

                next = end;
                if (next < tokens.count) {
                    TokenType type = tokens.items[next].type;
                    if (type != TOKEN_SEMI && type != TOKEN_AND_IF
                        && type != TOKEN_OR_IF) {
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

    check("AND condition", "true && echo yes", 2, NULL);
    check("OR condition", "false || echo recovered", 2, NULL);
    check("mixed logical conditions",
          "false && echo skipped || echo fallback", 3, NULL);
    check("semicolon resets condition",
          "true || echo skipped; echo always", 3, NULL);
    check("pipeline before logical operator",
          "echo hello | cat && echo done", 2, NULL);
    check("quoted logical operators",
          "echo '&&' \"||\"; true", 2, NULL);
    check("logical operators without spaces",
          "true&&false||true", 3, NULL);
    check("logical chain with trailing semicolon",
          "true && echo done;", 2, NULL);
    check("background job after separate logical chain",
          "true && echo done; sleep 1 &", 3, NULL);

    check("leading AND", "&& echo forbidden", 0,
          "empty command in list");
    check("trailing AND", "true &&", 0,
          "expected pipeline after logical operator");
    check("trailing OR", "false ||", 0,
          "expected pipeline after logical operator");
    check("adjacent logical operators", "true && || false", 0,
          "empty command in list");
    check("semicolon after AND", "true &&; echo forbidden", 0,
          "empty command in list");
    check("missing filename before AND", "echo one > && true", 0,
          "expected filename after redirection");
    check("background AND chain rejected for now",
          "true && sleep 1 &", 0,
          "background logical chains are not supported");
    check("background OR chain rejected for now",
          "false || sleep 1 &", 0,
          "background logical chains are not supported");

    printf("\nTests: %d, failures: %d\n", total, failures);
    return failures ? 1 : 0;
}
