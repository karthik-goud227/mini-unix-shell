#!/usr/bin/env python3
import subprocess
import tempfile
from pathlib import Path

shell = Path(__file__).resolve().parent.parent / "msh"
inspect = "python3 -c 'import sys; print(repr(sys.argv[1:]))'"
cases = [
    ("zero arguments", inspect + ' "$@"', [], "[]\n"),
    ("spaces and empty arguments", inspect + ' "$@"',
     ["hello world", "", "last"], "['hello world', '', 'last']\n"),
    ("braced argument forwarding", inspect + ' "${@}"',
     ["one two", ""], "['one two', '']\n"),
    ("argument contents remain literal", inspect + ' "$@"',
     ["$HOME", "|", "*.c"], "['$HOME', '|', '*.c']\n"),
    ("surrounding arguments remain separate",
     inspect + ' before "$@" after', ["a b", ""],
     "['before', 'a b', '', 'after']\n"),
    ("forwarding inside pipeline", inspect + ' "$@" | cat',
     ["a b", ""], "['a b', '']\n"),
    ("forwarded command name", '"$@"',
     ["printf", "<%s>\\n", "hello world", ""],
     "<hello world>\n<>\n"),
]

failures = 0
with tempfile.TemporaryDirectory(prefix="msh-quoted-at-") as directory:
    script = Path(directory) / "forward.msh"
    for name, command, arguments, expected in cases:
        script.write_text(command + "\n")
        result = subprocess.run(
            [str(shell), str(script), *arguments],
            text=True, capture_output=True, timeout=10,
        )
        passed = (
            result.returncode == 0
            and result.stderr == ""
            and result.stdout == expected
        )
        print(f"{'PASS' if passed else 'FAIL'}: {name}")
        if not passed:
            failures += 1
            print(f"  stdout={result.stdout!r}, expected={expected!r}")
            print(f"  stderr={result.stderr!r}, status={result.returncode}")

print(f"\nChecks: {len(cases)}, failures: {failures}")
raise SystemExit(bool(failures))
