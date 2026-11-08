
#!/usr/bin/env python3
import os
import subprocess
import tempfile
from pathlib import Path

shell = Path(__file__).resolve().parent.parent / "msh"
checks = 0
failures = 0

env = os.environ.copy()
for name in ("MSH_PREFIX_TEST", "MSH_PREFIX_SECOND"):
    env.pop(name, None)


def check(name, commands, expected_out="", expected_status=0, error=None):
    global checks, failures
    checks += 1

    with tempfile.TemporaryDirectory(prefix="msh-prefix-") as directory:
        try:
            result = subprocess.run(
                [str(shell)], input=commands, text=True,
                capture_output=True, cwd=directory, env=env, timeout=10,
            )
        except subprocess.TimeoutExpired:
            failures += 1
            print(f"FAIL: {name}: timed out")
            return

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


check("prefix enters child environment",
      "MSH_PREFIX_TEST=temporary printenv MSH_PREFIX_TEST\nexit\n",
      "temporary\n")

check("local parent value preserved",
      'MSH_PREFIX_TEST=parent\n'
      'MSH_PREFIX_TEST=temporary printenv MSH_PREFIX_TEST\n'
      'echo "$MSH_PREFIX_TEST"\nexit\n',
      "temporary\nparent\n")

check("exported parent value preserved",
      "export MSH_PREFIX_TEST=original\n"
      "MSH_PREFIX_TEST=temporary printenv MSH_PREFIX_TEST\n"
      "printenv MSH_PREFIX_TEST\nexit\n",
      "temporary\noriginal\n")

check("new prefix does not persist in parent",
      "MSH_PREFIX_TEST=temporary true\n"
      "printenv MSH_PREFIX_TEST\nexit\n",
      expected_status=1)

check("quoted prefix value",
      'MSH_PREFIX_TEST="hello world" printenv MSH_PREFIX_TEST\nexit\n',
      "hello world\n")

check("empty prefix value remains present",
      "MSH_PREFIX_TEST= printenv MSH_PREFIX_TEST\nexit\n",
      "\n")

check("later prefix sees earlier assignment",
      'MSH_PREFIX_TEST=hello MSH_PREFIX_SECOND="$MSH_PREFIX_TEST world" '
      'printenv MSH_PREFIX_SECOND\nexit\n',
      "hello world\n")

check("prefix expands preceding status",
      "false; MSH_PREFIX_TEST=$? printenv MSH_PREFIX_TEST\nexit\n",
      "1\n")

check("ordinary arguments expand before temporary assignment",
      'MSH_PREFIX_TEST=parent\n'
      'MSH_PREFIX_TEST=temporary printf "%s\\n" "$MSH_PREFIX_TEST"\nexit\n',
      "parent\n")

check("failed command leaves parent intact",
      'MSH_PREFIX_TEST=parent\n'
      'MSH_PREFIX_TEST=temporary nosuchcommand_msh_prefix_test\n'
      'echo "$MSH_PREFIX_TEST"\nexit\n',
      "parent\n", error="nosuchcommand_msh_prefix_test")

check("failed redirection leaves parent intact",
      'MSH_PREFIX_TEST=parent\n'
      'MSH_PREFIX_TEST=temporary printenv MSH_PREFIX_TEST < missing-input\n'
      'echo "$MSH_PREFIX_TEST"\nexit\n',
      "parent\n", error="missing-input")

check("prefixed output redirection",
      "MSH_PREFIX_TEST=saved printenv MSH_PREFIX_TEST > output\n"
      "cat output\nexit\n",
      "saved\n")

check("skipped prefix command has no effect",
      'MSH_PREFIX_TEST=parent\n'
      'false && MSH_PREFIX_TEST=temporary printenv MSH_PREFIX_TEST\n'
      'echo "$MSH_PREFIX_TEST"\nexit\n',
      "parent\n")

check("built-in prefix rejected for now",
      "MSH_PREFIX_TEST=temporary pwd\nexit\n",
      expected_status=2,
      error="assignments require a standalone foreground assignment-only command or an external")

check("pipeline prefix enters stage environment",
      "MSH_PREFIX_TEST=temporary printenv MSH_PREFIX_TEST | cat\nexit\n",
      "temporary\n")


check("pipeline stages have independent prefix values",
      "MSH_PREFIX_TEST=parent\n"
      "MSH_PREFIX_TEST=left printenv MSH_PREFIX_TEST | "
      "MSH_PREFIX_TEST=right sh -c "
      "'read line; printf \"%s:%s\\n\" \"$line\" \"$MSH_PREFIX_TEST\"'\n"
      'echo "$MSH_PREFIX_TEST"\nexit\n',
      "left:right\nparent\n")

check("prefix does not enter an unprefixed sibling",
      "export MSH_PREFIX_TEST=parent\n"
      "MSH_PREFIX_TEST=left printenv MSH_PREFIX_TEST | "
      "sh -c 'read line; printf \"%s:%s\\n\" \"$line\" \"$MSH_PREFIX_TEST\"'\n"
      "printenv MSH_PREFIX_TEST\nexit\n",
      "left:parent\nparent\n")

check("prefixed final stage determines failure status",
      "printf 'input\\n' | MSH_PREFIX_TEST=temporary false\nexit\n",
      expected_status=1)

check("prefixed earlier failure allows final success",
      "MSH_PREFIX_TEST=temporary false | cat\nexit\n")

print(f"\nChecks: {checks}, failures: {failures}")
raise SystemExit(1 if failures else 0)
