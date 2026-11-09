#!/usr/bin/env python3
import subprocess
import tempfile
from pathlib import Path

shell = Path(__file__).resolve().parent.parent / "msh"
checks = 0
failures = 0


def check(name, text, expected_out="", expected_status=0,
          stdin="", error=None, arguments=()):
    global checks, failures
    checks += 1

    with tempfile.TemporaryDirectory(prefix="msh-script-") as directory:
        script = Path(directory) / "commands.msh"
        script.write_text(text)
        try:
            result = subprocess.run(
                [str(shell), str(script), *arguments],
                input=stdin, text=True, capture_output=True,
                cwd=directory, timeout=10,
            )
            passed = (
                result.stdout == expected_out
                and result.returncode == expected_status
                and (result.stderr == "" if error is None
                     else error in result.stderr)
            )
        except subprocess.TimeoutExpired:
            print(f"FAIL: {name}: timed out")
            failures += 1
            return

    print(f"{'PASS' if passed else 'FAIL'}: {name}")
    if not passed:
        failures += 1
        print(f"  stdout={result.stdout!r}")
        print(f"  stderr={result.stderr!r}, status={result.returncode}")


check("empty script", "")
check("commands execute without prompts",
      "echo first\necho second\n", "first\nsecond\n")
check("final line without newline",
      "echo final", "final\n")
check("last command determines script status",
      "true\nfalse\n", expected_status=1)
check("explicit exit stops script",
      "exit 7\necho forbidden\n", expected_status=7)
check("script input stays separate from command stdin",
      "echo start\ncat\necho end\n",
      "start\npayload\nend\n", stdin="payload\n")
check("variables persist across script lines",
      'MSH_SCRIPT_TEST=hello\necho "$MSH_SCRIPT_TEST"\n',
      "hello\n")
check("pipelines and logical lists in script",
      "false || echo recovered; echo hello | cat\n",
      "recovered\nhello\n")
check("redirection does not replace script input",
      "echo saved > data\ncat < data\necho continued\n",
      "saved\ncontinued\n")
check("background execution remains restricted in scripts",
      "sleep 1 &\n", expected_status=2,
      error="background execution requires an interactive terminal")


with tempfile.TemporaryDirectory(prefix="msh-script-errors-") as directory:
    work = Path(directory)
    script = work / "valid.msh"
    script.write_text("echo forbidden\n")

    cases = [
        ("missing script reports failure",
         [str(work / "missing.msh")], 1, "missing.msh"),
        ("directory script rejected",
         [str(work)], 1, "script is a directory"),
    ]

    for name, arguments, expected_status, error in cases:
        checks += 1
        result = subprocess.run(
            [str(shell), *arguments],
            input="", text=True, capture_output=True, timeout=10,
        )
        passed = (
            result.stdout == ""
            and result.returncode == expected_status
            and error in result.stderr
        )
        print(f"{'PASS' if passed else 'FAIL'}: {name}")
        if not passed:
            failures += 1
            print(f"  stdout={result.stdout!r}")
            print(f"  stderr={result.stderr!r}, status={result.returncode}")


# A terminal on stdin must not enable prompts or interactive built-ins.
import os
import pty

with tempfile.TemporaryDirectory(prefix="msh-script-terminal-") as directory:
    script = Path(directory) / "terminal.msh"
    script.write_text("echo script-terminal\njobs\n")
    master, slave = pty.openpty()
    try:
        result = subprocess.run(
            [str(shell), str(script)],
            stdin=slave, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            text=True, timeout=10,
        )
        checks += 1
        passed = (
            result.stdout == "script-terminal\n"
            and result.returncode == 2
            and "jobs: requires an interactive terminal" in result.stderr
        )
        name = "script stays non-interactive with terminal stdin"
        print(f"{'PASS' if passed else 'FAIL'}: {name}")
        if not passed:
            failures += 1
            print(f"  stdout={result.stdout!r}")
            print(f"  stderr={result.stderr!r}, status={result.returncode}")
    finally:
        os.close(slave)
        os.close(master)


check("script arguments preserve spaces and empty values",
      'printf "<%s>|<%s>|<%s>\\n" "$1" "$2" "$3"\n',
      "<hello world>|<>|<third>\n",
      arguments=("hello world", "", "third"))

check("argument count and missing parameter",
      'printf "%s:<%s>\\n" "$#" "$1"\n',
      "0:<>\n")

check("ninth parameter and single-digit parsing",
      'printf "%s:%s:%s\\n" "$#" "$9" "$10"\n',
      "10:nine:one0\n",
      arguments=("one", "two", "three", "four", "five",
                 "six", "seven", "eight", "nine", "ten"))

check("argument contents remain literal data",
      'printf "%s\\n" "$1"\n',
      "$HOME; echo forbidden\n",
      arguments=("$HOME; echo forbidden",))

check("quotes and escapes control positional expansion",
      "printf '%s|%s|%s\\n' '$1' \"$1\" \\$1\n",
      "$1|value|$1\n", arguments=("value",))

check("positional parameters expand inside assignments",
      'MSH_SCRIPT_VALUE="$1"; echo "$MSH_SCRIPT_VALUE"; echo "$#"\n',
      "assigned value\n1\n", arguments=("assigned value",))

print(f"\nChecks: {checks}, failures: {failures}")
raise SystemExit(1 if failures else 0)
