#!/usr/bin/env python3
import os
import subprocess
import tempfile
from pathlib import Path

shell = Path(__file__).resolve().parent.parent / "msh"
checks = 0
failures = 0
env = os.environ.copy()
env.pop("MSH_BRACE_TEST", None)


def check(name, commands, expected="", status=0, error=None, arguments=()):
    global checks, failures
    checks += 1
    with tempfile.TemporaryDirectory(prefix="msh-braced-") as directory:
        script = Path(directory) / "commands.msh"
        script.write_text(commands)
        try:
            result = subprocess.run(
                [str(shell), str(script), *arguments],
                input="", text=True, capture_output=True,
                cwd=directory, env=env, timeout=10,
            )
        except subprocess.TimeoutExpired:
            failures += 1
            print(f"FAIL: {name}: timed out")
            return
        expected = expected.replace("@SCRIPT@", str(script))

    passed = (
        result.stdout == expected and result.returncode == status
        and (result.stderr == "" if error is None else error in result.stderr)
    )
    print(f"{'PASS' if passed else 'FAIL'}: {name}")
    if not passed:
        failures += 1
        print(f"  stdout={result.stdout!r}")
        print(f"  stderr={result.stderr!r}, status={result.returncode}")


check("braces delimit variable name",
      'MSH_BRACE_TEST=hello\necho "${MSH_BRACE_TEST}world"\n',
      "helloworld\n")
check("missing variable remains empty",
      'printf "<%s>\\n" "${MSH_BRACE_TEST}"\n', "<>\n")
check("single quotes and escapes stay literal",
      "echo '${MSH_BRACE_TEST}'\necho \\${MSH_BRACE_TEST}\n",
      "${MSH_BRACE_TEST}\n${MSH_BRACE_TEST}\n")
check("braced status",
      'false; echo "${?}"\n', "1\n")
check("braced zero and count",
      'printf "%s|%s\\n" "${0}" "${#}"\n',
      "@SCRIPT@|2\n", arguments=("one", "two"))
check("multi-digit parameter differs from unbraced form",
      'printf "%s|%s|<%s>\\n" "${10}" "$10" "${11}"\n',
      "ten|one0|<>\n",
      arguments=("one", "two", "three", "four", "five",
                 "six", "seven", "eight", "nine", "ten"))
check("braced value works in assignment",
      'MSH_BRACE_TEST="${1} world"\necho "${MSH_BRACE_TEST}"\n',
      "hello world\n", arguments=("hello",))
check("inserted value is not expanded again",
      'echo "${1}"\n', "${HOME}\n", arguments=("${HOME}",))
check("empty braces rejected",
      'echo "${}"\n', status=1, error="empty braced parameter")
check("missing closing brace rejected",
      'echo "${MSH_BRACE_TEST"\n', status=1,
      error="missing closing brace")
check("unsupported colon-default operator rejected",
      'echo "${MSH_BRACE_TEST:-default}"\n', status=1,
      error="invalid or unsupported braced parameter")
check("invalid numeric reference rejected",
      'echo "${1bad}"\n', status=1,
      error="invalid braced positional parameter")
check("overflowing numeric reference rejected",
      'echo "${' + "9" * 64 + '}"\n', status=1,
      error="positional parameter index is too large")


check("unset variable uses default without assignment",
      'echo "${MSH_BRACE_TEST-fallback}"\n'
      'printenv MSH_BRACE_TEST\necho $?\n',
      "fallback\n1\n")
check("empty variable does not use default",
      'MSH_BRACE_TEST=\nprintf "<%s>\\n" "${MSH_BRACE_TEST-fallback}"\n',
      "<>\n")
check("set variable keeps its value",
      'MSH_BRACE_TEST=kept\necho "${MSH_BRACE_TEST-fallback}"\n',
      "kept\n")
check("default preserves spaces inside double quotes",
      'echo "${MSH_BRACE_TEST-hello world}"\n',
      "hello world\n")
check("default expands positional parameter",
      'echo "${MSH_BRACE_TEST-$1}"\n',
      "argument value\n", arguments=("argument value",))
check("nested default expands when needed",
      'unset MSH_BRACE_INNER\n'
      'echo "${MSH_BRACE_TEST-${MSH_BRACE_INNER-nested}}"\n',
      "nested\n")
check("unused fallback is not evaluated",
      'MSH_BRACE_TEST=kept\necho "${MSH_BRACE_TEST-${1bad}}"\n',
      "kept\n")
check("empty default produces empty value",
      'printf "<%s>\\n" "${MSH_BRACE_TEST-}"\n',
      "<>\n")


check("default assignment stores a local value",
      'echo "${MSH_BRACE_TEST=hello world}"\n'
      'echo "$MSH_BRACE_TEST"\n'
      'printenv MSH_BRACE_TEST\necho $?\n',
      "hello world\nhello world\n1\n")

check("default assignment preserves an empty value",
      'MSH_BRACE_TEST=\n'
      'printf "<%s>\\n" "${MSH_BRACE_TEST=fallback}"\n',
      "<>\n")

check("set value skips assignment fallback",
      'MSH_BRACE_TEST=kept\n'
      'echo "${MSH_BRACE_TEST=${1bad}}"\n',
      "kept\n")

check("nested default assignments store both values",
      'unset MSH_BRACE_INNER\n'
      'echo "${MSH_BRACE_TEST=${MSH_BRACE_INNER=nested}}"\n'
      'echo "$MSH_BRACE_TEST:$MSH_BRACE_INNER"\n',
      "nested\nnested:nested\n")

check("empty default assignment creates a set variable",
      'printf "<%s>\\n" "${MSH_BRACE_TEST=}"\n'
      'printf "<%s>\\n" "${MSH_BRACE_TEST-fallback}"\n',
      "<>\n<>\n")

check("skipped command does not assign a parameter default",
      'false && echo "${MSH_BRACE_TEST=forbidden}"\n'
      'echo "${MSH_BRACE_TEST-still-unset}"\n',
      "still-unset\n")

check("positional default assignment remains rejected",
      'echo "${1=value}"\n', status=1,
      error="invalid braced positional parameter")


check("required unset variable stops script",
      'echo "${MSH_BRACE_TEST?custom message}"\n'
      'echo forbidden\n',
      status=1, error="MSH_BRACE_TEST: custom message")

check("required parameter supplies default diagnostic",
      'echo "${MSH_BRACE_TEST?}"\necho forbidden\n',
      status=1, error="MSH_BRACE_TEST: parameter is not set")

check("required empty value is accepted",
      'MSH_BRACE_TEST=\n'
      'printf "<%s>\\n" "${MSH_BRACE_TEST?must be set}"\n',
      "<>\n")

check("required set value skips message evaluation",
      'MSH_BRACE_TEST=kept\n'
      'echo "${MSH_BRACE_TEST?${1bad}}"\n',
      "kept\n")

check("required message expands script argument",
      'echo "${MSH_BRACE_TEST?missing $1}"\n',
      status=1, error="MSH_BRACE_TEST: missing argument",
      arguments=("argument",))

check("nested required failure propagates",
      'unset MSH_BRACE_INNER\n'
      'echo "${MSH_BRACE_TEST-${MSH_BRACE_INNER?inner required}}"\n'
      'echo forbidden\n',
      status=1, error="MSH_BRACE_INNER: inner required")

check("skipped required reference has no effect",
      'false && echo "${MSH_BRACE_TEST?forbidden}"\n'
      'echo continued\n',
      "continued\n")

check("required failure prevents remaining list execution",
      'echo "${MSH_BRACE_TEST?stop here}"; echo forbidden\n'
      'echo also-forbidden\n',
      status=1, error="MSH_BRACE_TEST: stop here")

print(f"\nChecks: {checks}, failures: {failures}")
raise SystemExit(1 if failures else 0)
