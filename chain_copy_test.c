
#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L
#include "chain.c"

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

int main(void)
{
    TokenList original = {0};
    TokenList copy = {0};
    char error[128];

    if (setenv("MSH_CHAIN_COPY_TEST", "hello world", 1) == -1
        || lex_line("echo \"$MSH_CHAIN_COPY_TEST\"",
                    &original, error, sizeof(error)) == -1) {
        return 1;
    }

    int result = chain_copy_tokens(&original, 0, original.count, &copy);
    check("token range copied successfully",
          result == 0 && copy.count == original.count);
    if (result == -1) {
        free_tokens(&original);
        return 1;
    }

    check("copied strings have independent storage",
          copy.items[1].text != original.items[1].text
          && copy.items[1].raw_text != original.items[1].raw_text);
    check("type and position preserved",
          copy.items[1].type == original.items[1].type
          && copy.items[1].position == original.items[1].position);

    result = expand_tokens(&copy, 0, error, sizeof(error));
    check("copied token expands correctly",
          result == 0
          && strcmp(copy.items[1].text, "hello world") == 0);
    check("original text remains unexpanded",
          strcmp(original.items[1].text, "$MSH_CHAIN_COPY_TEST") == 0);
    check("original quote spelling preserved",
          strcmp(original.items[1].raw_text,
                 "\"$MSH_CHAIN_COPY_TEST\"") == 0);

    free_tokens(&copy);
    check("original remains usable after copy is freed",
          strcmp(original.items[0].text, "echo") == 0);

    result = chain_copy_tokens(&original, original.count, 0, &copy);
    check("empty range succeeds",
          result == 0 && copy.items == NULL && copy.count == 0);
    free_tokens(&copy);

    result = chain_copy_tokens(&original, original.count + 1, 1, &copy);
    check("invalid range rejected with empty result",
          result == -1 && copy.items == NULL && copy.count == 0);

    free_tokens(&original);

    printf("\nChecks: %d, failures: %d\n", checks, failures);
    return failures ? 1 : 0;
}
