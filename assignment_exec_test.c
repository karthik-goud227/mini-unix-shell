
#define _POSIX_C_SOURCE 200809L
#include "assignment.h"
#include "variables.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

int main(void)
{
    const char *names[] = {
        "MSH_ASSIGN_A", "MSH_ASSIGN_B", "MSH_ASSIGN_SINGLE",
        "MSH_ASSIGN_DOUBLE", "MSH_ASSIGN_DOLLAR", "MSH_ASSIGN_EMPTY",
        "MSH_ASSIGN_STATUS", "MSH_ASSIGN_EQUALS", "MSH_ASSIGN_EXPORT"
    };

    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        if (unsetenv(names[i]) == -1) {
            return 1;
        }
    }

    char error[128];
    char *ordered[] = {
        "MSH_ASSIGN_A=hello",
        "MSH_ASSIGN_B=\"$MSH_ASSIGN_A world\""
    };

    int result = assignments_apply(ordered, 2, 0, 0, error, sizeof(error));
    check("ordered assignments succeed", result == 0);
    check("first value stored",
          equals(variables_get("MSH_ASSIGN_A"), "hello"));
    check("later value sees earlier assignment",
          equals(variables_get("MSH_ASSIGN_B"), "hello world"));
    check("new local values stay out of environment",
          getenv("MSH_ASSIGN_A") == NULL
          && getenv("MSH_ASSIGN_B") == NULL);

    char *quoted[] = {
        "MSH_ASSIGN_SINGLE='$MSH_ASSIGN_A'",
        "MSH_ASSIGN_DOUBLE=\"$MSH_ASSIGN_A world\"",
        "MSH_ASSIGN_DOLLAR=\\$MSH_ASSIGN_A",
        "MSH_ASSIGN_EMPTY=",
        "MSH_ASSIGN_STATUS=$?",
        "MSH_ASSIGN_EQUALS=a=b=c"
    };

    result = assignments_apply(quoted, 6, 0, 7, error, sizeof(error));
    if (result == -1) {
        fprintf(stderr, "Assignment setup failed: %s\n", error);
        variables_clear();
        return 1;
    }

    check("single quotes prevent expansion",
          equals(variables_get("MSH_ASSIGN_SINGLE"), "$MSH_ASSIGN_A"));
    check("double quotes allow expansion",
          equals(variables_get("MSH_ASSIGN_DOUBLE"), "hello world"));
    check("escaped dollar stays literal",
          equals(variables_get("MSH_ASSIGN_DOLLAR"), "$MSH_ASSIGN_A"));
    check("empty value remains present",
          equals(variables_get("MSH_ASSIGN_EMPTY"), ""));
    check("previous status expands in value",
          equals(variables_get("MSH_ASSIGN_STATUS"), "7"));
    check("additional equals signs preserved",
          equals(variables_get("MSH_ASSIGN_EQUALS"), "a=b=c"));

    char *exported[] = {"MSH_ASSIGN_EXPORT=first"};
    result = assignments_apply(exported, 1, 1, 0, error, sizeof(error));
    check("exported assignment updates environment",
          result == 0 && equals(getenv("MSH_ASSIGN_EXPORT"), "first"));

    char *replace[] = {"MSH_ASSIGN_EXPORT=second"};
    result = assignments_apply(replace, 1, 0, 0, error, sizeof(error));
    check("replacement preserves export status",
          result == 0 && equals(getenv("MSH_ASSIGN_EXPORT"), "second"));

    char *invalid[] = {"MSH_ASSIGN_A=kept", "1BAD=value"};
    result = assignments_apply(invalid, 2, 0, 0, error, sizeof(error));
    check("later invalid assignment reports error and preserves earlier change",
          result == -1 && error[0] != '\0'
          && equals(variables_get("MSH_ASSIGN_A"), "kept"));

    check("empty assignment list succeeds",
          assignments_apply(NULL, 0, 0, 0, error, sizeof(error)) == 0);

    variables_clear();
    printf("\nChecks: %d, failures: %d\n", checks, failures);
    return failures ? 1 : 0;
}
