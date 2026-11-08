
#!/usr/bin/env python3
import os
import signal
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parent.parent

harness = r"""
#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L
#include "chain.c"

int main(int argc, char **argv)
{
    if (argc != 2 || setenv("MSH_CHAIN_VALUE", "parent", 1) == -1) {
        return 99;
    }

    char *directory = getcwd(NULL, 0);
    if (directory == NULL) {
        return 99;
    }

    TokenList tokens = {0};
    CommandList list = {0};
    char error[128];

    if (lex_line(argv[1], &tokens, error, sizeof(error)) == -1
        || parse_command_list(&tokens, &list,
                              error, sizeof(error)) == -1) {
        fprintf(stderr, "%s\n", error);
        free_tokens(&tokens);
        free(directory);
        return 99;
    }

    fflush(NULL);
    pid_t child = fork();
    if (child == -1) {
        return 99;
    }

    if (child == 0) {
        jobs_reset_after_fork();
        int status = chain_evaluate(&tokens, &list, 0, list.count, 0);
        free_command_list(&list);
        free_tokens(&tokens);
        free(directory);
        _exit(fflush(NULL) == EOF ? 1 : status);
    }

    int status;
    pid_t result;
    do {
        result = waitpid(child, &status, 0);
    } while (result == -1 && errno == EINTR);

    char *after = getcwd(NULL, 0);
    const char *value = getenv("MSH_CHAIN_VALUE");
    int isolated = after != NULL && strcmp(directory, after) == 0
        && value != NULL && strcmp(value, "parent") == 0;

    free(after);
    free(directory);
    free_command_list(&list);
    free_tokens(&tokens);

    if (!isolated || result != child || !WIFEXITED(status)) {
        return 99;
    }

    return WEXITSTATUS(status);
}
"""

cases = [
    ("AND executes", "true && echo yes", "yes\n", 0),
    ("AND skips", "false && echo forbidden", "", 1),
    ("OR executes", "false || echo recovered", "recovered\n", 0),
    ("OR skips", "true || echo forbidden", "", 0),
    ("mixed fallback", "false && echo forbidden || echo fallback",
     "fallback\n", 0),
    ("left-to-right evaluation", "true || echo forbidden && echo final",
     "final\n", 0),
    ("status expansion", "false || echo $?", "1\n", 0),
    ("environment expansion timing",
     'export MSH_CHAIN_VALUE=child && echo "$MSH_CHAIN_VALUE"',
     "child\n", 0),
    ("skipped environment change",
     'false && export MSH_CHAIN_VALUE=changed || echo "$MSH_CHAIN_VALUE"',
     "parent\n", 0),
    ("unset inside child",
     'unset MSH_CHAIN_VALUE && printf "<%s>\\n" "$MSH_CHAIN_VALUE"',
     "<>\n", 0),
    ("directory change stays in child",
     "cd / && pwd", "/\n", 0),
    ("exit stops chain",
     "true && exit 7 || echo forbidden", "", 7),
    ("pipeline controls next item",
     "true | false || echo recovered", "recovered\n", 0),
    ("background marker removed from private copy",
     "echo background &", "background\n", 0),
]


cases.extend([
    ("assignment-only command updates supervisor",
     'MSH_CHAIN_VALUE=supervisor && echo "$MSH_CHAIN_VALUE"',
     "supervisor\n", 0),
    ("ordered assignments inside supervisor",
     'MSH_CHAIN_FIRST=hello MSH_CHAIN_SECOND="$MSH_CHAIN_FIRST world" '
     '&& echo "$MSH_CHAIN_SECOND"',
     "hello world\n", 0),
    ("skipped assignment preserves supervisor value",
     'false && MSH_CHAIN_VALUE=changed || echo "$MSH_CHAIN_VALUE"',
     "parent\n", 0),
    ("external prefix leaves supervisor value intact",
     'MSH_CHAIN_VALUE=supervisor && '
     'MSH_CHAIN_VALUE=temporary printenv MSH_CHAIN_VALUE && '
     'echo "$MSH_CHAIN_VALUE"',
     "temporary\nsupervisor\n", 0),
    ("assignment value receives previous status",
     'false || MSH_CHAIN_STATUS=$? && echo "$MSH_CHAIN_STATUS"',
     "1\n", 0),
    ("chain pipeline prefixes stay isolated",
     'MSH_CHAIN_VALUE=left printenv MSH_CHAIN_VALUE | '
     'MSH_CHAIN_VALUE=right sh -c '
     '\'read line; printf "%s:%s\\n" "$line" "$MSH_CHAIN_VALUE"\' '
     '&& echo "$MSH_CHAIN_VALUE"',
     "left:right\nparent\n", 0),
])


cases.extend([
    ("assignment-only first stage preserves supervisor",
     'MSH_CHAIN_VALUE=child | cat && echo "$MSH_CHAIN_VALUE"',
     "parent\n", 0),
    ("assignment-only final stage returns success",
     'false | MSH_CHAIN_VALUE=child && echo "$MSH_CHAIN_VALUE"',
     "parent\n", 0),
    ("assignment-only earlier stage preserves final failure",
     "MSH_CHAIN_VALUE=child | false",
     "", 1),
])

failures = 0

with tempfile.TemporaryDirectory(prefix="msh-chain-evaluation-") as directory:
    workdir = Path(directory)
    test_source = workdir / "runner.c"
    binary = workdir / "runner"
    test_source.write_text(harness)

    objects = sorted(
        str(p) for p in root.glob("*.o")
        if p.name not in ("main.o", "chain.o")
    )
    subprocess.run([
        "gcc", "-std=c11", "-Wall", "-Wextra", "-Wpedantic",
        "-Wno-unused-function", "-g", "-O0", "-I", str(root),
        str(test_source), *objects, "-o", str(binary),
    ], check=True)

    for name, command, expected_out, expected_status in cases:
        process = subprocess.Popen(
            [str(binary), command],
            cwd=workdir,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            start_new_session=True,
        )
        try:
            out, err = process.communicate(timeout=10)
            passed = (
                out == expected_out and err == ""
                and process.returncode == expected_status
            )
        except subprocess.TimeoutExpired:
            out, err = "", "Timed out"
            passed = False
        finally:
            try:
                os.killpg(process.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
            process.communicate()

        print(f"{'PASS' if passed else 'FAIL'}: {name}")
        if not passed:
            failures += 1
            print(f"  stdout={out!r}, stderr={err!r}, status={process.returncode}")

print(f"\nChecks: {len(cases)}, failures: {failures}")
raise SystemExit(1 if failures else 0)
