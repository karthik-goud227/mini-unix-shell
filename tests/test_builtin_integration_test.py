#!/usr/bin/env python3
"""Check the shell's built-in test and [ condition commands."""
import subprocess
import tempfile
from pathlib import Path

shell = Path(__file__).resolve().parent.parent / "msh"
cases = [
    ("empty and nonempty strings", 'test ""\necho $?\ntest hello\necho $?\n', '1\n0\n', 0),
    ("quoted string comparisons", '[ "a b" = "a b" ]\necho $?\n[ a != b ]\necho $?\n', '0\n0\n', 0),
    ("integer comparisons and negation", 'test 3 -lt 5\necho $?\ntest ! 3 -ge 5\necho $?\n', '0\n0\n', 0),
    ("file checks", 'test -d .\necho $?\ntest -f sample.txt\necho $?\ntest -e absent\necho $?\n', '0\n0\n1\n', 0),
    ("if uses builtin", 'if [ 4 -gt 2 ]\nthen\necho yes\nelse\necho no\nfi\n', 'yes\n', 0),
    ("missing bracket", '[ hello\n', '', 2),
    ("invalid number", 'test abc -eq 2\n', '', 2),
    ("unsupported expression", 'test a -a b\n', '', 2),
]

failures = 0
with tempfile.TemporaryDirectory(prefix="msh-test-builtin-") as directory:
    path = Path(directory) / "conditions.msh"
    (Path(directory) / "sample.txt").write_text("sample")
    for name, source, output, status in cases:
        path.write_text(source)
        result = subprocess.run([str(shell), str(path)], capture_output=True,
                                text=True, timeout=5, cwd=directory)
        ok = (result.stdout == output and result.returncode == status
              and (bool(result.stderr) == (status == 2)))
        print(f"{'PASS' if ok else 'FAIL'}: {name}")
        if not ok:
            print(f"  stdout={result.stdout!r}, stderr={result.stderr!r}, status={result.returncode}")
            failures += 1

print(f"Checks: {len(cases)}, failures: {failures}")
raise SystemExit(bool(failures))
