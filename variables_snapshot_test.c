#define _POSIX_C_SOURCE 200809L
#include "variables.h"

#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

static int checks;
static int failures;

static void check(const char *name, int passed)
{
    checks++;
    printf("%s: %s\n", passed ? "PASS" : "FAIL", name);
    failures += !passed;
}

static int equals(const char *value, const char *expected)
{
    return value != NULL && strcmp(value, expected) == 0;
}

int main(void)
{
    const char *name = "MSH_SNAPSHOT_TEST";
    VariableSnapshot *saved = NULL;

    variables_unset(name);
    check("save absent variable", variables_save(name, &saved) == 0);
    variables_set(name, "temporary", 1);
    check("restore absence",
          variables_restore(saved) == 0
          && variables_get(name) == NULL && getenv(name) == NULL);

    variables_set(name, "local", 0);
    check("save local variable", variables_save(name, &saved) == 0);
    variables_set(name, "temporary", 1);
    check("restore local value and environment absence",
          variables_restore(saved) == 0
          && equals(variables_get(name), "local") && getenv(name) == NULL);
    variables_set(name, "still-local", 0);
    check("restored export flag remains local", getenv(name) == NULL);

    variables_set(name, "", 1);
    check("save exported empty value", variables_save(name, &saved) == 0);
    variables_unset(name);
    check("restore exported empty value",
          variables_restore(saved) == 0
          && equals(variables_get(name), "") && equals(getenv(name), ""));

    variables_unset(name);
    setenv(name, "inherited", 1);
    check("save inherited environment entry", variables_save(name, &saved) == 0);
    variables_set(name, "temporary", 1);
    check("restore inherited value",
          variables_restore(saved) == 0
          && equals(variables_get(name), "inherited")
          && equals(getenv(name), "inherited"));
    setenv(name, "changed-directly", 1);
    check("restore does not create shadowing shell storage",
          equals(variables_get(name), "changed-directly"));

    variables_unset(name);
    variables_set(name, "original", 0);
    variables_save(name, &saved);
    variables_set(name, "keep-change", 0);
    variables_snapshot_free(saved);
    check("discard snapshot leaves current value intact",
          equals(variables_get(name), "keep-change"));

    saved = NULL;
    check("invalid save rejected",
          variables_save("1INVALID", &saved) == -1 && saved == NULL);
    check("NULL destination rejected", variables_save(name, NULL) == -1);
    check("NULL restore rejected", variables_restore(NULL) == -1);
    variables_snapshot_free(NULL);

    const char *readonly_name = "MSH_SNAPSHOT_READONLY";
    variables_set(readonly_name, "locked", 0);
    variables_mark_readonly(readonly_name);
    check("save readonly variable", variables_save(readonly_name, &saved) == 0);
    check("restore preserves readonly state", variables_restore(saved) == 0
          && variables_set(readonly_name, "changed", 0) == -1
          && errno == EPERM
          && equals(variables_get(readonly_name), "locked"));

    variables_unset(name);
    variables_clear();
    printf("\nChecks: %d, failures: %d\n", checks, failures);
    return failures ? 1 : 0;
}
