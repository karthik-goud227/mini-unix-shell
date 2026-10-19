#!/usr/bin/env python3
import subprocess
import tempfile
from pathlib import Path

shell = Path(__file__).resolve().parent.parent / "msh"
failures = 0
total = 0


def check(name, commands, expected_out, expected_status=0,
          error_contains=None):
    global failures, total
    total += 1

    try:
        result = subprocess.run(
            [str(shell)],
            input=commands,
            text=True,
            capture_output=True,
            cwd=workdir,
            timeout=10,
        )
    except subprocess.TimeoutExpired:
        print(f"FAIL: {name}: timed out")
        failures += 1
        return

    problems = []

    if result.stdout != expected_out:
        problems.append(
            f"stdout: expected {expected_out!r}, got {result.stdout!r}"
        )

    if result.returncode != expected_status:
        problems.append(
            f"status: expected {expected_status}, got {result.returncode}"
        )

    if error_contains is None:
        if result.stderr:
            problems.append(f"unexpected stderr: {result.stderr!r}")
    elif error_contains not in result.stderr:
        problems.append(
            f"missing diagnostic {error_contains!r}; "
            f"stderr was {result.stderr!r}"
        )

    if problems:
        failures += 1
        print(f"FAIL: {name}")
        for problem in problems:
            print(f"  {problem}")
    else:
        print(f"PASS: {name}")


with tempfile.TemporaryDirectory(prefix="msh-tests-") as workdir:
    check("empty input", "", "")
    check("blank lines", "\n   \nexit 0\n", "")

    check(
        "quoted and empty arguments",
        """printf '<%s>\\n' "hello world" ''\nexit\n""",
        "<hello world>\n<>\n",
    )

    check("success status", "true\nexit\n", "")
    check("failure status", "false\nexit\n", "", 1)

    check(
        "unknown command",
        "nosuchcommand_xyz\nexit\n",
        "",
        127,
        "nosuchcommand_xyz",
    )

    check(
        "three pipeline stages",
        """printf 'a\\nb\\n' | grep b | wc -l\nexit\n""",
        "1\n",
    )

    check("last stage succeeds", "false | true\nexit\n", "")
    check("last stage fails", "true | false\nexit\n", "", 1)
    check("early reader exit", "yes | head -n 1\nexit\n", "y\n")

    check(
        "overwrite append and input",
        "echo first > data\n"
        "echo second >> data\n"
        "cat < data\nexit\n",
        "first\nsecond\n",
    )

    check(
        "built-in stdout restoration",
        "pwd > location\ncat location\npwd\nexit\n",
        f"{workdir}\n{workdir}\n",
    )

    check(
        "redirection order",
        "echo data > first > second\n"
        "wc -c < first\ncat second\nexit\n",
        "0\ndata\n",
    )

    check(
        "redirection overrides pipe",
        "echo data > redirected | wc -c\n"
        "cat redirected\nexit\n",
        "0\ndata\n",
    )

    check(
        "failed redirection prevents cd",
        "cd / < missing-input\npwd\nexit 0\n",
        f"{workdir}\n",
        error_contains="missing-input",
    )

    check(
        "syntax error recovery",
        "echo forbidden; echo forbidden\n"
        "echo recovered\nexit 0\n",
        "recovered\n",
        error_contains="unsupported unquoted shell syntax",
    )

    check(
        "malformed pipeline",
        "echo forbidden |\nexit\n",
        "",
        2,
        "empty command in pipeline",
    )

    check(
        "128 arguments accepted",
        "echo " + " ".join(["x"] * 127) + "\nexit\n",
        " ".join(["x"] * 127) + "\n",
    )

    check(
        "129 arguments rejected",
        "echo " + " ".join(["x"] * 128) + "\nexit\n",
        "",
        2,
        "too many arguments",
    )

    check(
        "16 pipeline stages accepted",
        "echo x" + " | cat" * 15 + "\nexit\n",
        "x\n",
    )

    check(
        "17 pipeline stages rejected",
        "echo forbidden" + " | cat" * 16 + "\nexit\n",
        "",
        2,
        "maximum pipeline length",
    )

    check(
        "4096-byte line accepted",
        "echo " + "x" * 4091 + "\nexit\n",
        "x" * 4091 + "\n",
    )

    check(
        "oversized line discarded with recovery",
        "echo " + "x" * 4092 + "\necho recovered\nexit 0\n",
        "recovered\n",
        error_contains="line discarded",
    )

    check(
        "background rejected without terminal",
        "true &\nexit\n",
        "",
        2,
        "requires an interactive terminal",
    )

print(f"\nTests: {total}, failures: {failures}")
raise SystemExit(1 if failures else 0)
