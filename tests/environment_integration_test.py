#!/usr/bin/env python3
import os
import subprocess
import tempfile
from pathlib import Path

shell = Path(__file__).resolve().parent.parent / "msh"
total = 0
failures = 0

env = os.environ.copy()
env.pop("MSH_ENV_TEST", None)


def check(name, commands, expected_out="", expected_status=0, error=None):
    global total, failures
    total += 1

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
        print(f"  stdout: expected {expected_out!r}, got {result.stdout!r}")
        print(f"  status: expected {expected_status}, got {result.returncode}")
        print(f"  stderr: {result.stderr!r}")


with tempfile.TemporaryDirectory(prefix="msh-environment-") as workdir:
    check(
        "set and inherit",
        "export MSH_ENV_TEST=hello\nprintenv MSH_ENV_TEST\nexit\n",
        "hello\n",
    )
    check(
        "replace value and preserve spaces",
        'export MSH_ENV_TEST=first\n'
        'export MSH_ENV_TEST="hello world"\n'
        'printenv MSH_ENV_TEST\nexit\n',
        "hello world\n",
    )
    check(
        "empty value is present",
        "export MSH_ENV_TEST=\nprintenv MSH_ENV_TEST\nexit\n",
        "\n",
    )
    check(
        "value may contain equals signs",
        "export MSH_ENV_TEST=a=b=c\nprintenv MSH_ENV_TEST\nexit\n",
        "a=b=c\n",
    )
    check(
        "expand previous value during assignment",
        'export MSH_ENV_TEST=hello\n'
        'export MSH_ENV_TEST="$MSH_ENV_TEST world"\n'
        'printenv MSH_ENV_TEST\nexit\n',
        "hello world\n",
    )
    check(
        "unset removes child environment entry",
        "export MSH_ENV_TEST=hello\n"
        "unset MSH_ENV_TEST\nprintenv MSH_ENV_TEST\nexit\n",
        expected_status=1,
    )
    check(
        "unset missing variable succeeds",
        "unset MSH_ENV_TEST\nexit\n",
    )
    check(
        "invalid export name",
        "export 1BAD=value\nexit\n",
        expected_status=2,
        error="export: invalid variable name",
    )
    check(
        "export requires equals sign",
        "export MSH_ENV_TEST\nexit\n",
        expected_status=2,
        error="export: expected NAME=value",
    )
    check(
        "export rejects multiple assignments",
        "export MSH_ENV_TEST=one OTHER=two\nexit\n",
        expected_status=2,
        error="export: usage:",
    )
    check(
        "invalid unset name",
        "unset BAD=NAME\nexit\n",
        expected_status=2,
        error="unset: invalid variable name",
    )
    check(
        "unset requires a name",
        "unset\nexit\n",
        expected_status=2,
        error="unset: usage:",
    )
    check(
        "failed redirection prevents export",
        "export MSH_ENV_TEST=old\n"
        "export MSH_ENV_TEST=new < missing-input\n"
        "printenv MSH_ENV_TEST\nexit\n",
        "old\n",
        error="missing-input",
    )
    check(
        "failed redirection prevents unset",
        "export MSH_ENV_TEST=old\n"
        "unset MSH_ENV_TEST < missing-input\n"
        "printenv MSH_ENV_TEST\nexit\n",
        "old\n",
        error="missing-input",
    )
    check(
        "successful redirection restores stdout",
        "export MSH_ENV_TEST=hello > export-output\n"
        "printenv MSH_ENV_TEST\n"
        "unset MSH_ENV_TEST > unset-output\n"
        "echo restored\nexit\n",
        "hello\nrestored\n",
    )
    check(
        "export rejected in pipeline without changing value",
        "export MSH_ENV_TEST=old\n"
        "export MSH_ENV_TEST=new | cat\n"
        "printenv MSH_ENV_TEST\nexit\n",
        "old\n",
        error="export is not supported in a pipeline or background job",
    )
    check(
        "unset rejected in pipeline without removing value",
        "export MSH_ENV_TEST=old\n"
        "unset MSH_ENV_TEST | cat\n"
        "printenv MSH_ENV_TEST\nexit\n",
        "old\n",
        error="unset is not supported in a pipeline or background job",
    )

print(f"\nTests: {total}, failures: {failures}")
raise SystemExit(1 if failures else 0)
