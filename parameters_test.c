#include "parameters.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int checks;
static int failures;

static void check(const char *name, int passed)
{
    checks++;
    printf("%s: %s\n", passed ? "PASS" : "FAIL", name);
    failures += !passed;
}

int main(void)
{
    check("initial argument count is zero", parameters_count() == 0);
    check("initial parameter is empty", strcmp(parameters_get(0), "") == 0);

    char zero[] = "demo.msh";
    char first[] = "hello world";
    char *arguments[] = {first, "", "third"};

    check("parameter setup succeeds",
          parameters_set(zero, 3, arguments) == 0);
    check("count excludes parameter zero", parameters_count() == 3);
    check("parameter zero is script name",
          strcmp(parameters_get(0), "demo.msh") == 0);
    check("spaces preserved in argument",
          strcmp(parameters_get(1), "hello world") == 0);
    check("empty argument retained", strcmp(parameters_get(2), "") == 0);
    check("later argument available",
          strcmp(parameters_get(3), "third") == 0);
    check("missing argument expands empty",
          strcmp(parameters_get(4), "") == 0);
    check("large index is safe",
          strcmp(parameters_get(SIZE_MAX), "") == 0);

    zero[0] = 'X';
    first[0] = 'X';
    check("stored strings are independent copies",
          strcmp(parameters_get(0), "demo.msh") == 0
          && strcmp(parameters_get(1), "hello world") == 0);

    check("NULL zero rejected without changing state",
          parameters_set(NULL, 0, NULL) == -1
          && parameters_count() == 3);
    check("NULL argument array rejected",
          parameters_set("bad", 1, NULL) == -1
          && strcmp(parameters_get(0), "demo.msh") == 0);

    char *invalid[] = {"first", NULL};
    check("NULL argument rejected without partial replacement",
          parameters_set("bad", 2, invalid) == -1
          && strcmp(parameters_get(3), "third") == 0);

    check("oversized count rejected",
          parameters_set("bad", SIZE_MAX, arguments) == -1
          && parameters_count() == 3);
    check("replacement with zero arguments succeeds",
          parameters_set("new.msh", 0, NULL) == 0
          && parameters_count() == 0
          && strcmp(parameters_get(0), "new.msh") == 0
          && strcmp(parameters_get(1), "") == 0);

    parameters_clear();
    check("clear resets storage",
          parameters_count() == 0 && strcmp(parameters_get(0), "") == 0);
    parameters_clear();
    check("repeated clear is safe", parameters_count() == 0);

    printf("\nChecks: %d, failures: %d\n", checks, failures);
    return failures ? 1 : 0;
}
