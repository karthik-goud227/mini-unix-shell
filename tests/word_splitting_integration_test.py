#!/usr/bin/env python3
import os
import subprocess
import tempfile
from pathlib import Path

shell = Path(__file__).resolve().parent.parent / "msh"
env = os.environ.copy()
env["MSH_SPLIT_VALUE"] = "alpha beta"
env["MSH_SPLIT_WHITE"] = " \talpha \t beta\n "
env["MSH_SPLIT_LITERAL"] = "$HOME | cat"
env["MSH_SPLIT_EMPTY"] = ""
env.pop("MSH_SPLIT_MISSING", None)

cases = [
    ("unquoted expansion splits",
     "printf '<%s>\\n' $MSH_SPLIT_VALUE",
     "<alpha>\n<beta>\n"),
    ("double quotes preserve spaces",
     'printf \'<%s>\\n\' "$MSH_SPLIT_VALUE"',
     "<alpha beta>\n"),
    ("whitespace runs do not create empty fields",
     "printf '<%s>\\n' $MSH_SPLIT_WHITE",
     "<alpha>\n<beta>\n"),
    ("missing unquoted value disappears",
     "printf '<%s>\\n' before $MSH_SPLIT_MISSING after",
     "<before>\n<after>\n"),
    ("empty unquoted value disappears",
     "printf '<%s>\\n' before $MSH_SPLIT_EMPTY after",
     "<before>\n<after>\n"),
    ("quoted missing value stays empty",
     'printf \'<%s>\\n\' before "$MSH_SPLIT_MISSING" after',
     "<before>\n<>\n<after>\n"),
    ("literal prefix and suffix attach",
     "printf '<%s>\\n' pre${MSH_SPLIT_VALUE}post",
     "<prealpha>\n<betapost>\n"),
    ("escaped space stays protected",
     "printf '<%s>\\n' alpha\\ beta",
     "<alpha beta>\n"),
    ("single quotes prevent expansion",
     "printf '<%s>\\n' '$MSH_SPLIT_VALUE'",
     "<$MSH_SPLIT_VALUE>\n"),
    ("expanded operators remain data",
     "printf '<%s>\\n' $MSH_SPLIT_LITERAL",
     "<$HOME>\n<|>\n<cat>\n"),
    ("quoted fallback stays one argument",
     'printf \'<%s>\\n\' "${MSH_SPLIT_MISSING-alpha beta}"',
     "<alpha beta>\n"),
    ("pipeline receives split arguments",
     "printf '<%s>\\n' $MSH_SPLIT_VALUE | cat",
     "<alpha>\n<beta>\n"),
    ("assignment value does not split",
     'MSH_SPLIT_LOCAL=$MSH_SPLIT_VALUE\n'
     'printf \'<%s>\\n\' "$MSH_SPLIT_LOCAL"',
     "<alpha beta>\n"),
    ("redirection filename does not split",
     'echo saved > "$MSH_SPLIT_VALUE"\ncat < $MSH_SPLIT_VALUE',
     "saved\n"),
    ("disappearing standalone command succeeds",
     "$MSH_SPLIT_MISSING\necho $?",
     "0\n"),
]

cases.extend([
    ("removed first pipeline stage succeeds",
     "$MSH_SPLIT_MISSING | cat",
     ""),
    ("removed final stage determines success",
     "false | $MSH_SPLIT_MISSING\necho $?",
     "0\n"),
    ("removed earlier stage preserves final failure",
     "$MSH_SPLIT_MISSING | false\necho $?",
     "1\n"),
    ("removed command still applies redirection",
     "$MSH_SPLIT_MISSING > empty-output\nwc -c < empty-output",
     "0\n"),
])

failures = 0
with tempfile.TemporaryDirectory(prefix="msh-word-splitting-") as workdir:
    for name, commands, expected in cases:
        result = subprocess.run(
            [str(shell)], input=commands + "\nexit\n",
            text=True, capture_output=True, env=env,
            cwd=workdir, timeout=10,
        )
        passed = (
            result.stdout == expected
            and result.stderr == ""
            and result.returncode == 0
        )
        print(f"{'PASS' if passed else 'FAIL'}: {name}")
        if not passed:
            failures += 1
            print(f"  stdout={result.stdout!r}, expected={expected!r}")
            print(f"  stderr={result.stderr!r}, status={result.returncode}")

print(f"\nChecks: {len(cases)}, failures: {failures}")
raise SystemExit(1 if failures else 0)
