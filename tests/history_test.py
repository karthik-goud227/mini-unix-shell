#!/usr/bin/env python3
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parent.parent

driver_source = r'''
#define _POSIX_C_SOURCE 200809L
#include "history.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv)
{
    int failed = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "@clear") == 0) {
            history_clear();
        } else if (strcmp(argv[i], "@copy") == 0) {
            char command[] = "echo original";
            if (history_add(command) == -1) {
                failed = 1;
                break;
            }
            command[0] = 'X';
        } else if (strcmp(argv[i], "@null") == 0) {
            errno = 0;
            if (history_add(NULL) != -1 || errno != EINVAL) {
                failed = 1;
                break;
            }
        } else if (history_add(argv[i]) == -1) {
            perror("history_add");
            failed = 1;
            break;
        }
    }

    if (!failed && history_print() == -1) {
        failed = 1;
    }

    if (fflush(stdout) == EOF) {
        failed = 1;
    }

    history_clear();
    history_clear();
    return failed;
}
'''

cases = [
    ("empty history", [], ""),
    ("blank lines skipped", ["", " ", "\t", "\n"], ""),
    (
        "commands numbered in order",
        ["echo first", "echo second"],
        "1  echo first\n2  echo second\n",
    ),
    (
        "blank lines do not consume numbers",
        ["echo first", "   ", "echo second"],
        "1  echo first\n2  echo second\n",
    ),
    (
        "original spelling preserved",
        ['  echo "$HOME"  '],
        '1    echo "$HOME"  \n',
    ),
    ("command stored as an independent copy", ["@copy"],
     "1  echo original\n"),
    ("clear removes entries", ["echo old", "@clear"], ""),
    (
        "clear restarts numbering",
        ["echo old", "@clear", "echo new"],
        "1  echo new\n",
    ),
    ("repeated clear is safe", ["@clear", "@clear"], ""),
    (
        "NULL input rejected without changing history",
        ["echo kept", "@null"],
        "1  echo kept\n",
    ),
    (
        "latest 100 commands retained",
        [f"echo {i}" for i in range(1, 106)],
        "".join(f"{i}  echo {i}\n" for i in range(6, 106)),
    ),
    (
        "clear after overflow",
        [f"echo {i}" for i in range(1, 106)]
        + ["@clear", "echo fresh"],
        "1  echo fresh\n",
    ),
]

failures = 0

with tempfile.TemporaryDirectory(prefix="msh-history-tests-") as directory:
    directory = Path(directory)
    driver = directory / "driver.c"
    executable = directory / "history-test"
    driver.write_text(driver_source)

    subprocess.run(
        [
            "gcc", "-std=c11", "-D_POSIX_C_SOURCE=200809L",
            "-Wall", "-Wextra", "-Wpedantic", "-g", "-O0",
            "-I", str(root),
            str(driver), str(root / "history.c"),
            "-o", str(executable),
        ],
        check=True,
    )

    for name, arguments, expected in cases:
        try:
            result = subprocess.run(
                [str(executable), *arguments],
                text=True,
                capture_output=True,
                timeout=10,
            )
        except subprocess.TimeoutExpired:
            failures += 1
            print(f"FAIL: {name}: timed out")
            continue

        if (result.returncode == 0
                and result.stdout == expected
                and result.stderr == ""):
            print(f"PASS: {name}")
        else:
            failures += 1
            print(f"FAIL: {name}")
            print(f"  status: {result.returncode}")
            print(f"  expected stdout: {expected!r}")
            print(f"  actual stdout: {result.stdout!r}")
            print(f"  stderr: {result.stderr!r}")

print(f"\nTests: {len(cases)}, failures: {failures}")
raise SystemExit(1 if failures else 0)
