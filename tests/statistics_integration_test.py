#!/usr/bin/env python3
import re
import subprocess
import tempfile
from pathlib import Path

shell = Path(__file__).resolve().parent.parent / "msh"
total = 0
failures = 0

entry_pattern = re.compile(
    r"^\[(\d+)\] (.*)\n"
    r"  Measured processes: (\d+)/(\d+)\n"
    r"  User CPU: (\d+\.\d{6}) s; System CPU: (\d+\.\d{6}) s\n"
    r"  Largest process max RSS: (\d+) KiB$",
    re.MULTILINE,
)


def entries(output):
    return entry_pattern.findall(output)


def check(name, commands, verify, expected_status=0, error=None):
    global total, failures
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
        failures += 1
        print(f"FAIL: {name}: timed out")
        return

    stderr_ok = (
        result.stderr == "" if error is None
        else error in result.stderr
    )

    if (result.returncode == expected_status
            and stderr_ok and verify(result.stdout)):
        print(f"PASS: {name}")
    else:
        failures += 1
        print(f"FAIL: {name}")
        print(f"  status: {result.returncode}")
        print(f"  stdout: {result.stdout!r}")
        print(f"  stderr: {result.stderr!r}")


with tempfile.TemporaryDirectory(prefix="msh-stats-") as workdir:
    check(
        "empty history",
        "stats\nexit\n",
        lambda out: out == "No completed job statistics.\n",
    )

    check(
        "single process measurements",
        "true\nstats\nexit\n",
        lambda out: len(entries(out)) == 1
        and entries(out)[0][1:4] == ("true", "1", "1"),
    )

    check(
        "pipeline measurements",
        "true | cat\nstats\nexit\n",
        lambda out: len(entries(out)) == 1
        and entries(out)[0][1:4] == ("true | cat", "2", "2"),
    )

    check(
        "newest job first",
        "true\nfalse\nstats\nexit\n",
        lambda out: [entry[1] for entry in entries(out)]
        == ["false", "true"],
    )

    check(
        "stats does not create a job",
        "true\nstats\nstats\nexit\n",
        lambda out: len(entries(out)) == 2
        and entries(out)[0] == entries(out)[1],
    )

    check(
        "parent built-ins do not create jobs",
        "pwd > /dev/null\ncd .\nstats\nexit\n",
        lambda out: out == "No completed job statistics.\n",
    )

    check(
        "redirection and stdout restoration",
        "true\nstats > saved.txt\necho restored\ncat saved.txt\nexit\n",
        lambda out: out.startswith("restored\nCompleted jobs")
        and len(entries(out)) == 1
        and entries(out)[0][1:4] == ("true", "1", "1"),
    )

    check(
        "arguments rejected",
        "stats extra\nexit\n",
        lambda out: out == "",
        expected_status=2,
        error="stats: arguments are not supported",
    )

    check(
        "stats rejected inside pipeline",
        "stats | cat\nexit\n",
        lambda out: out == "",
        expected_status=2,
        error="stats is not supported in a pipeline or background job",
    )

    check(
        "latest 16 jobs retained",
        "true\n" * 18 + "stats\nexit\n",
        lambda out: [int(entry[0]) for entry in entries(out)]
        == list(range(18, 2, -1)),
    )

print(f"\nTests: {total}, failures: {failures}")
raise SystemExit(1 if failures else 0)
