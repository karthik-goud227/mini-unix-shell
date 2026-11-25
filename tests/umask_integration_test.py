#!/usr/bin/env python3
"""Check the shell's basic umask builtin."""
import stat
import subprocess
import tempfile
from pathlib import Path

shell = Path(__file__).resolve().parent.parent / "msh"
cases = [
    ("set and show", 'umask 077\numask\n', '0077\n', 0),
    ("set zero", 'umask 0000\numask\n', '0000\n', 0),
    ("invalid digit", 'umask 089\n', '', 2),
    ("invalid range", 'umask 1000\n', '', 2),
]

failures = 0
with tempfile.TemporaryDirectory(prefix="msh-umask-") as directory:
    script = Path(directory) / "umask.msh"
    for name, source, output, status in cases:
        script.write_text(source)
        result = subprocess.run([str(shell), str(script)], capture_output=True,
                                text=True, timeout=5, cwd=directory)
        ok = (result.stdout == output and result.returncode == status
              and (bool(result.stderr) == (status != 0)))
        print(f"{'PASS' if ok else 'FAIL'}: {name}")
        if not ok:
            print(f"  stdout={result.stdout!r}, stderr={result.stderr!r}, "
                  f"status={result.returncode}")
            failures += 1

    script.write_text('umask 077\n: > private.txt\n')
    result = subprocess.run([str(shell), str(script)], capture_output=True,
                            text=True, timeout=5, cwd=directory)
    private = Path(directory) / "private.txt"
    ok = (result.returncode == 0 and result.stderr == ""
          and private.exists() and stat.S_IMODE(private.stat().st_mode) == 0o600)
    print(f"{'PASS' if ok else 'FAIL'}: controls new file permissions")
    if not ok:
        print(f"  status={result.returncode}, stderr={result.stderr!r}, "
              f"mode={stat.S_IMODE(private.stat().st_mode) if private.exists() else 'missing'}")
        failures += 1

print(f"Checks: {len(cases) + 1}, failures: {failures}")
raise SystemExit(bool(failures))
