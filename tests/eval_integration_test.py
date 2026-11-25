#!/usr/bin/env python3
"""Check limited standalone eval behavior."""
import subprocess
import tempfile
from pathlib import Path

shell = Path(__file__).resolve().parent.parent / "msh"
cases = [
    ("second parameter expansion", "X='$Y'\nY=hello\neval echo \"$X\"\n", 'hello\n', 0, ""),
    ("evaluated command list", "eval echo first ';' echo second\n", 'first\nsecond\n', 0, ""),
    ("empty eval succeeds", 'false\neval\necho "$?"\n', '0\n', 0, ""),
    ("nested eval", 'eval eval echo nested\n', 'nested\n', 0, ""),
    ("reject operator in first pass", 'eval echo hi | cat\necho after\n', 'after\n', 0, "standalone command line"),
]

failures = 0
with tempfile.TemporaryDirectory(prefix="msh-eval-") as directory:
    script = Path(directory) / "eval.msh"
    for name, source, output, status, error in cases:
        script.write_text(source)
        result = subprocess.run([str(shell), str(script)], capture_output=True,
                                text=True, timeout=5, cwd=directory)
        ok = (result.stdout == output and result.returncode == status
              and (error in result.stderr if error else result.stderr == ""))
        print(f"{'PASS' if ok else 'FAIL'}: {name}")
        if not ok:
            print(f"  stdout={result.stdout!r}, stderr={result.stderr!r}, "
                  f"status={result.returncode}")
            failures += 1

print(f"Checks: {len(cases)}, failures: {failures}")
raise SystemExit(bool(failures))
