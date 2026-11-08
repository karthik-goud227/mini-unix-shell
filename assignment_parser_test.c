
#include "parser.h"

#include <stdio.h>
#include <string.h>

static int checks;
static int failures;

static void check(const char *name, const char *input,
                  size_t stages, size_t assignments, size_t argc,
                  const char *first_assignment, const char *first_argument,
                  const char *expected_error)
{
    TokenList tokens = {0};
    Pipeline pipeline = {0};
    char error[128];
    checks++;

    if (lex_line(input, &tokens, error, sizeof(error)) == -1) {
        printf("FAIL: %s: lexer: %s\n", name, error);
        failures++;
        return;
    }

    int result = parse_pipeline_assignments(
        &tokens, &pipeline, error, sizeof(error)
    );
    free_tokens(&tokens);

    int passed;
    if (expected_error != NULL) {
        passed = result == -1
            && strstr(error, expected_error) != NULL
            && pipeline.commands == NULL
            && pipeline.command_count == 0;
    } else {
        passed = result == 0 && pipeline.command_count == stages;

        if (passed && stages > 0) {
            const Command *command = &pipeline.commands[0];
            passed = command->assignment_count == assignments
                && command->argc == argc;

            if (passed && first_assignment != NULL) {
                passed = assignments > 0
                    && strcmp(command->assignments[0],
                              first_assignment) == 0;
            }

            if (passed && first_argument != NULL) {
                passed = argc > 0
                    && strcmp(command->argv[0], first_argument) == 0;
            }

            if (passed && argc > 0) {
                passed = command->argv[argc] == NULL;
            }
        }
    }

    printf("%s: %s\n", passed ? "PASS" : "FAIL", name);
    if (!passed) {
        failures++;
        printf("  result=%d, stages=%zu, diagnostic=%s\n",
               result, pipeline.command_count, error);
    }

    free_pipeline(&pipeline);
}

int main(void)
{
    check("assignment-only command", "NAME=value",
          1, 1, 0, "NAME=value", NULL, NULL);
    check("multiple assignments", "A=one B=two",
          1, 2, 0, "A=one", NULL, NULL);
    check("assignment before command", "NAME=value echo hello",
          1, 1, 2, "NAME=value", "echo", NULL);
    check("assignment spelling after command is an argument",
          "echo NAME=value", 1, 0, 2, NULL, "echo", NULL);
    check("quoted whole word is a command",
          "'NAME=value'", 1, 0, 1, NULL, "NAME=value", NULL);
    check("quoted name is a command",
          "\"NAME\"=value", 1, 0, 1, NULL, "NAME=value", NULL);
    check("escaped equals is a command",
          "NAME\\=value", 1, 0, 1, NULL, "NAME=value", NULL);
    check("assignment preserves value quotes",
          "NAME=\"hello world\"", 1, 1, 0,
          "NAME=\"hello world\"", NULL, NULL);
    check("redirection before assignment",
          "> output NAME=value", 1, 1, 0, "NAME=value", NULL, NULL);
    check("redirection filename is not an assignment",
          "echo > NAME=value", 1, 0, 1, NULL, "echo", NULL);
    check("assignment-only pipeline stage",
          "NAME=value | cat", 2, 1, 0, "NAME=value", NULL, NULL);
    check("background assignment-only command",
          "NAME=value &", 1, 1, 0, "NAME=value", NULL, NULL);
    check("invalid name remains ordinary command text",
          "1NAME=value", 1, 0, 1, NULL, "1NAME=value", NULL);

    check("trailing pipe rejected", "NAME=value |",
          0, 0, 0, NULL, NULL, "empty command in pipeline");
    check("missing redirection filename rejected", "NAME=value >",
          0, 0, 0, NULL, NULL, "expected filename after redirection");

    printf("\nChecks: %d, failures: %d\n", checks, failures);
    return failures ? 1 : 0;
}
