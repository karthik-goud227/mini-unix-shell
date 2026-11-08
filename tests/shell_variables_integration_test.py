
#!/usr/bin/env python3
import os
import subprocess
import tempfile
from pathlib import Path

shell = Path(__file__).resolve().parent.parent / "msh"
checks = 0
failures = 0

env = os.environ.copy()
for name in ("MSH_LOCAL_TEST", "MSH_SECOND_TEST", "MSH_STATUS_TEST"):
    env.pop(name, None)


def check(name, commands, expected_out="", expected_status=0, error=None):
    global checks, failures
    checks += 1

    with tempfile.TemporaryDirectory(prefix="msh-local-") as directory:
        result = subprocess.run(
            [str(shell)], input=commands, text=True,
            capture_output=True, cwd=directory, env=env, timeout=10,
        )

    stderr_ok = result.stderr == "" if error is None else error in result.stderr
    passed = (
        result.stdout == expected_out
        and result.returncode == expected_status
        and stderr_ok
    )
    print(f"{'PASS' if passed else 'FAIL'}: {name}")
    if not passed:
        failures += 1
        print(f"  stdout={result.stdout!r}")
        print(f"  stderr={result.stderr!r}")
        print(f"  status={result.returncode}")


check("local assignment and expansion",
      'MSH_LOCAL_TEST=hello\necho "$MSH_LOCAL_TEST"\nexit\n',
      "hello\n")

check("local variable excluded from child environment",
      "MSH_LOCAL_TEST=hello\nprintenv MSH_LOCAL_TEST\nexit\n",
      expected_status=1)

check("quoted value preserves spaces",
      'MSH_LOCAL_TEST="hello world"\necho "$MSH_LOCAL_TEST"\nexit\n',
      "hello world\n")

check("assignments apply in order",
      'MSH_LOCAL_TEST=hello MSH_SECOND_TEST="$MSH_LOCAL_TEST world"\n'
      'echo "$MSH_SECOND_TEST"\nexit\n',
      "hello world\n")

check("empty local value remains usable",
      'MSH_LOCAL_TEST=\nprintf "<%s>\\n" "$MSH_LOCAL_TEST"\nexit\n',
      "<>\n")

check("single quotes prevent value expansion",
      "MSH_LOCAL_TEST='$HOME'\necho \"$MSH_LOCAL_TEST\"\nexit\n",
      "$HOME\n")

check("status expansion in assignment",
      'false; MSH_STATUS_TEST=$?; echo "$MSH_STATUS_TEST"\nexit\n',
      "1\n")

check("export replaces local value",
      "MSH_LOCAL_TEST=local\nexport MSH_LOCAL_TEST=exported\n"
      "printenv MSH_LOCAL_TEST\nexit\n",
      "exported\n")

check("reassignment preserves export status",
      "export MSH_LOCAL_TEST=first\nMSH_LOCAL_TEST=second\n"
      "printenv MSH_LOCAL_TEST\nexit\n",
      "second\n")

check("unset removes local value",
      'MSH_LOCAL_TEST=hello\nunset MSH_LOCAL_TEST\n'
      'printf "<%s>\\n" "$MSH_LOCAL_TEST"\nexit\n',
      "<>\n")

check("failed redirection prevents assignment",
      'MSH_LOCAL_TEST=kept\nMSH_LOCAL_TEST=forbidden < missing-input\n'
      'echo "$MSH_LOCAL_TEST"\nexit 0\n',
      "kept\n", error="missing-input")

check("successful redirection restores stdout",
      'MSH_LOCAL_TEST=hello > output\necho "$MSH_LOCAL_TEST"\nexit\n',
      "hello\n")

check("skipped assignment does not change value",
      'MSH_LOCAL_TEST=kept\nfalse && MSH_LOCAL_TEST=forbidden\n'
      'echo "$MSH_LOCAL_TEST"\nexit\n',
      "kept\n")

check("later syntax error prevents earlier assignment",
      'MSH_LOCAL_TEST=forbidden; echo broken |\n'
      'printf "<%s>\\n" "$MSH_LOCAL_TEST"\nexit 0\n',
      "<>\n", error="empty command in pipeline")

print(f"\nChecks: {checks}, failures: {failures}")
raise SystemExit(1 if failures else 0)
