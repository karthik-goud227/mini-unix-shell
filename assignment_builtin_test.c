
#define _POSIX_C_SOURCE 200809L
#include "builtins.h"
#include "parser.h"
#include "variables.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int checks;
static int failures;

static void check(const char *name, int passed)
{
    checks++;
    printf("%s: %s\n", passed ? "PASS" : "FAIL", name);
    if (!passed) {
        failures++;
    }
}

static int equals(const char *value, const char *expected)
{
    return value != NULL && strcmp(value, expected) == 0;
}

static BuiltinResult run(const char *line, int last_status)
{
    TokenList tokens = {0};
    Pipeline pipeline = {0};
    char error[128];
    BuiltinResult result = {.handled = 1, .status = 99};

    if (lex_line(line, &tokens, error, sizeof(error)) == -1) {
        return result;
    }

    if (parse_pipeline_assignments(&tokens, &pipeline,
                                   error, sizeof(error)) == 0) {
        result = run_builtin_redirected(&pipeline.commands[0], last_status);
    } else {
        fprintf(stderr, "test parser: %s\n", error);
    }

    free_pipeline(&pipeline);
    free_tokens(&tokens);
    return result;
}

int main(void)
{
    const char *name = "MSH_ASSIGN_DISPATCH_TEST";
    if (variables_unset(name) == -1) {
        return 1;
    }

    BuiltinResult result = run("MSH_ASSIGN_DISPATCH_TEST=local", 0);
    check("assignment-only command handled successfully",
          result.handled && result.status == 0 && !result.should_exit);
    check("assignment stored in shell",
          equals(variables_get(name), "local"));

    /* Environment access here deliberately checks child inheritance. */
    extern char *getenv(const char *);
    check("assignment remains local", getenv(name) == NULL);

    struct stat before;
    struct stat after;
    if (fstat(STDOUT_FILENO, &before) == -1) {
        return 1;
    }

    result = run(
        "MSH_ASSIGN_DISPATCH_TEST=redirected > assignment-output", 0
    );
    check("successful redirection permits assignment",
          result.status == 0
          && equals(variables_get(name), "redirected"));

    struct stat file;
    check("assignment redirection creates empty output file",
          stat("assignment-output", &file) == 0 && file.st_size == 0);
    check("stdout descriptor restored",
          fstat(STDOUT_FILENO, &after) == 0
          && before.st_dev == after.st_dev
          && before.st_ino == after.st_ino);

    result = run(
        "MSH_ASSIGN_DISPATCH_TEST=forbidden < missing-assignment-input", 0
    );
    check("failed redirection returns failure",
          result.handled && result.status == 1);
    check("failed redirection prevents assignment",
          equals(variables_get(name), "redirected"));

    result = run("MSH_ASSIGN_DISPATCH_TEST=$?", 7);
    check("assignment receives previous status",
          result.status == 0 && equals(variables_get(name), "7"));

    variables_clear();
    printf("\nChecks: %d, failures: %d\n", checks, failures);
    return failures ? 1 : 0;
}
