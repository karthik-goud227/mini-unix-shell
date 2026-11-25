#!/usr/bin/env python3
"""Check positional-argument shifting in script execution."""
import subprocess
import tempfile
from pathlib import Path

shell = Path(__file__).resolve().parent.parent / "msh"
cases = [
    ("default shift", 'echo "$#:$1:$2"\nshift\necho "$#:$1:$2"\n', ["a", "b"], '2:a:b\n1:b:\n', 0),
    ("shift two", 'shift 2\necho "$#:$1"\n', ["a", "b", "c"], '1:c\n', 0),
    ("shift zero", 'shift 0\necho "$#:$1"\n', ["a"], '1:a\n', 0),
    ("preserve empty argument", 'shift\necho "[$1]:$#"\n', ["a", ""], '[]:1\n', 0),
    ("too many", 'shift 2\n', ["a"], '', 1),
    ("invalid count", 'shift -1\n', ["a"], '', 2),
    ("no arguments", 'shift\n', [], '', 1),
    ("for after shift", 'shift\nfor X\ndo\necho "[$X]"\ndone\n', ["a", "b c", ""], '[b c]\n[]\n', 0),
]

failures = 0
with tempfile.TemporaryDirectory(prefix="msh-shift-") as directory:
    script = Path(directory) / "shift.msh"
    for name, source, arguments, output, status in cases:
        script.write_text(source)
        result = subprocess.run([str(shell), str(script), *arguments],
                                capture_output=True, text=True, timeout=5,
                                cwd=directory)
        ok = (result.stdout == output and result.returncode == status
              and (bool(result.stderr) == (status != 0)))
        print(f"{'PASS' if ok else 'FAIL'}: {name}")
        if not ok:
            print(f"  stdout={result.stdout!r}, stderr={result.stderr!r}, "
                  f"status={result.returncode}")
            failures += 1

print(f"Checks: {len(cases)}, failures: {failures}")
raise SystemExit(bool(failures))
