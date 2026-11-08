
#define _POSIX_C_SOURCE 200809L
#include "variables.h"

#include <errno.h>
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

static int equals(const char *actual, const char *expected)
{
    return actual != NULL && strcmp(actual, expected) == 0;
}

int main(void)
{
    const char *local = "MSH_VAR_TEST_LOCAL";
    const char *inherited = "MSH_VAR_TEST_INHERITED";

    if (unsetenv(local) == -1
        || setenv(inherited, "inherited", 1) == -1) {
        perror("prepare test environment");
        return 1;
    }

    check("valid names accepted",
          variables_valid_name("NAME")
          && variables_valid_name("_name2"));
    check("invalid names rejected",
          !variables_valid_name(NULL)
          && !variables_valid_name("")
          && !variables_valid_name("1NAME")
          && !variables_valid_name("A-B")
          && !variables_valid_name("A=B"));
    check("missing variable returns NULL",
          variables_get(local) == NULL);

    char value[] = "hello";
    check("local assignment succeeds",
          variables_set(local, value, 0) == 0);
    check("local value available to shell",
          equals(variables_get(local), "hello"));
    check("local variable absent from environment",
          getenv(local) == NULL);

    value[0] = 'X';
    check("stored value is an independent copy",
          equals(variables_get(local), "hello"));

    check("replacement succeeds",
          variables_set(local, "replacement", 0) == 0
          && equals(variables_get(local), "replacement"));
    check("empty value remains present",
          variables_set(local, "", 0) == 0
          && equals(variables_get(local), ""));
    check("existing local variable can be exported",
          variables_export(local) == 0);
    check("exported empty value present in environment",
          equals(getenv(local), ""));

    check("assignment preserves export status",
          variables_set(local, "updated", 0) == 0
          && equals(getenv(local), "updated"));
    check("inherited environment value available",
          equals(variables_get(inherited), "inherited"));
    check("assignment to inherited variable remains exported",
          variables_set(inherited, "changed", 0) == 0
          && equals(getenv(inherited), "changed"));

    errno = 0;
    check("invalid assignment rejected without changing existing value",
          variables_set("1BAD", "bad", 0) == -1
          && errno == EINVAL
          && equals(variables_get(local), "updated"));
    check("NULL value rejected",
          variables_set(local, NULL, 0) == -1
          && equals(variables_get(local), "updated"));

    check("unset removes shell and environment values",
          variables_unset(local) == 0
          && variables_get(local) == NULL
          && getenv(local) == NULL);
    check("unsetting missing variable succeeds",
          variables_unset(local) == 0);

    errno = 0;
    check("exporting absent variable reports ENOENT",
          variables_export(local) == -1 && errno == ENOENT);

    if (variables_set(local, "temporary", 0) == -1) {
        return 1;
    }
    variables_clear();

    check("clear removes local storage",
          variables_get(local) == NULL);
    check("clear leaves process environment intact",
          equals(getenv(inherited), "changed")
          && equals(variables_get(inherited), "changed"));

    variables_clear();
    check("repeated clear is safe",
          variables_get(local) == NULL);

    printf("\nChecks: %d, failures: %d\n", checks, failures);
    return failures ? 1 : 0;
}
