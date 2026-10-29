#!/usr/bin/env python3
import os
import subprocess
import tempfile
from pathlib import Path

shell = Path(__file__).resolve().parent.parent / "msh"
total = 0
failures = 0

env = os.environ.copy()
env.pop("MSH_LIST_TEST", None)


def check(name, commands, expected_out="", expected_status=0, error=None):
    global total, failures
    total += 1

    with tempfile.TemporaryDirectory(prefix="msh-list-") as workdir:
        try:
            result = subprocess.run(
                [str(shell)],
                input=commands,
                text=True,
                capture_output=True,
                cwd=workdir,
                env=env,
                timeout=10,
            )
        except subprocess.TimeoutExpired:
            failures += 1
            print(f"FAIL: {name}: timed out")
            return

    stderr_ok = (
        result.stderr == "" if error is None
        else error in result.stderr
    )

    if (result.stdout == expected_out
            and result.returncode == expected_status
            and stderr_ok):
        print(f"PASS: {name}")
    else:
        failures += 1
        print(f"FAIL: {name}")
        print(f"  expected stdout: {expected_out!r}")
        print(f"  actual stdout: {result.stdout!r}")
        print(f"  status: expected {expected_status}, got {result.returncode}")
        print(f"  stderr: {result.stderr!r}")


check(
    "commands execute in order",
    "echo first;echo second\nexit\n",
    "first\nsecond\n",
)
check(
    "failure does not stop semicolon list",
    "false; echo continued\nexit\n",
    "continued\n",
)
check(
    "last command determines status",
    "true; false\nexit\n",
    expected_status=1,
)
check(
    "status expansion happens per item",
    "false; echo $?; true; echo $?\nexit\n",
    "1\n0\n",
)
check(
    "later item sees updated variable",
    'export MSH_LIST_TEST=hello; echo "$MSH_LIST_TEST"\nexit\n',
    "hello\n",
)
check(
    "later item sees unset variable",
    "export MSH_LIST_TEST=hello; unset MSH_LIST_TEST; "
    "printf '<%s>\\n' \"$MSH_LIST_TEST\"\nexit\n",
    "<>\n",
)
check(
    "quoted and escaped semicolons remain literal",
    "echo 'a;b'; echo a\\;b\nexit\n",
    "a;b\na;b\n",
)
check(
    "pipeline inside a list",
    "echo hello | cat; echo done\nexit\n",
    "hello\ndone\n",
)
check(
    "redirection and later input",
    "echo first > data; echo second >> data; cat < data\nexit\n",
    "first\nsecond\n",
)
check(
    "trailing semicolon",
    "echo done;\nexit\n",
    "done\n",
)
check(
    "exit skips remaining commands",
    "exit 7; echo forbidden\n",
    expected_status=7,
)
check(
    "exit uses preceding status",
    "false; exit; echo forbidden\n",
    expected_status=1,
)
check(
    "invalid exit allows next command",
    "exit abc; echo recovered\nexit 0\n",
    "recovered\n",
    error="exit: status must be 0 through 255",
)
check(
    "later syntax error prevents earlier execution",
    "echo forbidden; echo broken |\nexit\n",
    expected_status=2,
    error="empty command in pipeline",
)
check(
    "later syntax error prevents environment change",
    'export MSH_LIST_TEST=changed; echo broken |\n'
    'printf "<%s>\\n" "$MSH_LIST_TEST"\nexit 0\n',
    "<>\n",
    error="empty command in pipeline",
)
check(
    "repeated separator rejected",
    "echo forbidden;; echo forbidden\nexit\n",
    expected_status=2,
    error="empty command in list",
)
check(
    "leading separator rejected",
    "; echo forbidden\nexit\n",
    expected_status=2,
    error="empty command in list",
)
check(
    "command-not-found allows next command",
    "nosuchcommand_msh_list_test; echo recovered\nexit\n",
    "recovered\n",
    error="nosuchcommand_msh_list_test",
)

print(f"\nTests: {total}, failures: {failures}")
raise SystemExit(1 if failures else 0)
