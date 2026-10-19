#include "parser.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;

static void check(int condition, const char *description)
{
    if (!condition) {
        printf("  FAIL: %s\n", description);
        failures++;
    }
}

int main(void)
{
    const char *cases[] = {
        "echo \"hello world\" ''",
        "cat<input.txt|wc -l>>out.txt &",
        "echo x > first > second",
        "",
        "ls |",
        "| wc",
        "cat <",
        "echo a & echo b",
        "ls || wc",
        "> out.txt",
        "ls | > out.txt"
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        TokenList tokens;
        Pipeline pipeline;
        char error[128];

        printf("TEST %zu\n", i + 1);

        if (lex_line(cases[i], &tokens, error, sizeof(error)) == -1) {
            printf("  FAIL: unexpected lexer error: %s\n", error);
            failures++;
            continue;
        }

        int result = parse_pipeline(&tokens, &pipeline,
                                    error, sizeof(error));
        free_tokens(&tokens);

        if (i >= 4) {
            check(result == -1, "invalid syntax must be rejected");

            if (result == -1) {
                printf("  Expected error: %s\n", error);
                check(pipeline.commands == NULL
                      && pipeline.command_count == 0,
                      "failed parse must clean its result");
            }

            free_pipeline(&pipeline);
            continue;
        }

        check(result == 0, "valid input must parse");

        if (result == -1) {
            printf("  Error: %s\n", error);
            continue;
        }

        if (i == 0) {
            check(pipeline.command_count == 1, "one command");
            Command *command = &pipeline.commands[0];

            check(command->argc == 3, "three arguments");

            if (command->argc == 3) {
                check(strcmp(command->argv[0], "echo") == 0,
                      "command name preserved");
                check(strcmp(command->argv[1], "hello world") == 0,
                      "quoted spaces preserved");
                check(strcmp(command->argv[2], "") == 0,
                      "empty argument preserved");
            }

            check(command->argv[command->argc] == NULL,
                  "argv ends with NULL");
        } else if (i == 1) {
            check(pipeline.command_count == 2, "two pipeline stages");
            check(pipeline.background == 1, "background flag set");

            if (pipeline.command_count == 2) {
                Command *first = &pipeline.commands[0];
                Command *second = &pipeline.commands[1];

                check(first->redirection_count == 1,
                      "first stage has input redirection");
                check(second->redirection_count == 1,
                      "second stage has append redirection");

                if (first->redirection_count == 1) {
                    check(first->redirections[0].type == REDIRECT_IN,
                          "input redirection type");
                    check(strcmp(first->redirections[0].path,
                                 "input.txt") == 0,
                          "input filename preserved");
                }

                if (second->redirection_count == 1) {
                    check(second->redirections[0].type == REDIRECT_APPEND,
                          "append redirection type");
                    check(strcmp(second->redirections[0].path,
                                 "out.txt") == 0,
                          "output filename preserved");
                }
            }
        } else if (i == 2) {
            Command *command = &pipeline.commands[0];

            check(command->redirection_count == 2,
                  "both redirections retained");

            if (command->redirection_count == 2) {
                check(strcmp(command->redirections[0].path,
                             "first") == 0,
                      "first redirection stays first");
                check(strcmp(command->redirections[1].path,
                             "second") == 0,
                      "second redirection stays second");
            }
        } else {
            check(pipeline.command_count == 0,
                  "empty input produces no commands");
        }

        free_pipeline(&pipeline);
    }

    printf("\nFailures: %d\n", failures);
    return failures == 0 ? 0 : 1;
}
