#!/usr/bin/env python3
import os
import subprocess
import tempfile
from pathlib import Path

shell = Path(__file__).resolve().parent.parent / "msh"
failures = 0
total = 0

env = os.environ.copy()
env["MSH_TEST_VALUE"] = "hello world"
env["MSH_TEST_LITERAL"] = "$HOME | cat"
env.pop("MSH_TEST_UNSET", None)


def check(name, commands, expected_out, expected_status=0):
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
        print(f"FAIL: {name}: timed out")
        failures += 1
        return

    if (result.stdout == expected_out
            and result.returncode == expected_status
            and result.stderr == ""):
        print(f"PASS: {name}")
    else:
        failures += 1
        print(f"FAIL: {name}")
        print(f"  stdout: expected {expected_out!r}, got {result.stdout!r}")
        print(f"  status: expected {expected_status}, got {result.returncode}")
        print(f"  stderr: {result.stderr!r}")


with tempfile.TemporaryDirectory(prefix="msh-expansion-") as workdir:
    env["MSH_TEST_FILE"] = str(Path(workdir) / "file with spaces.txt")
    env["MSH_TEST_DIR"] = workdir

    check(
        "unquoted expanded value splits into arguments",
        "printf '<%s>\\n' $MSH_TEST_VALUE\nexit\n",
        "<hello>\n<world>\n",
    )

    check(
        "quotes and escapes",
        """printf '<%s>\\n' "$MSH_TEST_VALUE" '$MSH_TEST_VALUE' \\$MSH_TEST_VALUE\nexit\n""",
        "<hello world>\n<$MSH_TEST_VALUE>\n<$MSH_TEST_VALUE>\n",
    )

    check(
        "unquoted unset variable disappears",
        "printf '<%s>\\n' before $MSH_TEST_UNSET after\nexit\n",
        "<before>\n<after>\n",
    )

    check(
        "previous command status",
        "false\necho $?\ntrue\necho $?\nexit\n",
        "1\n0\n",
    )

    check(
        "last pipeline stage determines status",
        "true | false\necho $?\nfalse | true\necho $?\nexit\n",
        "1\n0\n",
    )

    check(
        "expansion inside a pipeline",
        "printf '%s\\n' \"$MSH_TEST_VALUE\" | cat\nexit\n",
        "hello world\n",
    )

    check(
        "split variable contents remain literal data",
        "printf '%s\\n' $MSH_TEST_LITERAL\nexit\n",
        "$HOME\n|\ncat\n",
    )

    check(
        "expanded output append and input filenames",
        'echo first > "$MSH_TEST_FILE"\n'
        'echo second >> "$MSH_TEST_FILE"\n'
        'cat < "$MSH_TEST_FILE"\nexit\n',
        "first\nsecond\n",
    )

    check(
        "built-in redirection restores stdout",
        'pwd > "$MSH_TEST_FILE"\n'
        'cat < "$MSH_TEST_FILE"\npwd\nexit\n',
        f"{workdir}\n{workdir}\n",
    )

    check(
        "expanded built-in argument",
        'cd /\ncd "$MSH_TEST_DIR"\npwd\nexit\n',
        f"{workdir}\n",
    )

    check(
        "expanded exit status",
        "false\nexit $?\n",
        "",
        1,
    )

print(f"\nTests: {total}, failures: {failures}")
raise SystemExit(1 if failures else 0)
