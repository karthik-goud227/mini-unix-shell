
#include "assignment.h"

#include <stdio.h>

static int checks;
static int failures;

static void check(const char *name, const char *raw,
                  int expected, size_t expected_length)
{
    size_t length = 999;
    int result = assignment_name_length(raw, &length);
    int passed = result == expected && length == expected_length;

    checks++;
    printf("%s: %s\n", passed ? "PASS" : "FAIL", name);
    if (!passed) {
        failures++;
        printf("  result=%d, name length=%zu\n", result, length);
    }
}

int main(void)
{
    check("simple assignment", "NAME=value", 1, 4);
    check("empty value", "NAME=", 1, 4);
    check("underscore and digits", "_NAME2=value", 1, 6);
    check("double-quoted value", "NAME=\"hello world\"", 1, 4);
    check("single-quoted value", "NAME='hello world'", 1, 4);
    check("escaped value", "NAME=hello\\ world", 1, 4);
    check("value contains equals", "NAME=a=b", 1, 4);
    check("value contains expansion", "NAME=$HOME", 1, 4);

    check("ordinary word", "echo", 0, 0);
    check("empty input", "", 0, 0);
    check("NULL input", NULL, 0, 0);
    check("missing name", "=value", 0, 0);
    check("name starts with digit", "1NAME=value", 0, 0);
    check("hyphen in name", "MY-NAME=value", 0, 0);
    check("quoted whole word", "'NAME=value'", 0, 0);
    check("quoted name", "\"NAME\"=value", 0, 0);
    check("partly quoted name", "NA'ME'=value", 0, 0);
    check("escaped equals", "NAME\\=value", 0, 0);
    check("expanded name", "$NAME=value", 0, 0);

    checks++;
    int passed = assignment_name_length("NAME=value", NULL) == 0;
    printf("%s: NULL length destination rejected\n",
           passed ? "PASS" : "FAIL");
    if (!passed) {
        failures++;
    }

    printf("\nChecks: %d, failures: %d\n", checks, failures);
    return failures ? 1 : 0;
}
