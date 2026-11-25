#!/usr/bin/env python3
"""Check script-mode command replacement with exec."""
import subprocess
import tempfile
from pathlib import Path

shell = Path(__file__).resolve().parent.parent / "msh"
cases = [
    ("replaces shell", 'exec /bin/echo replaced\necho unreachable\n', 'replaced\n', 0, ""),
    ("propagates command status", 'exec /bin/false\n', '', 1, ""),
    ("failure keeps script running", 'exec missing-command-xyz\necho after\n', 'after\n', 0, "msh: exec:"),
    ("missing command", 'exec\n', '', 2, "expected a command"),
]

failures = 0
with tempfile.TemporaryDirectory(prefix="msh-exec-") as directory:
    script = Path(directory) / "exec.msh"
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

    (Path(directory) / "data.txt").write_text("input from file\n")
    script.write_text('exec /bin/cat < data.txt\necho unreachable\n')
    result = subprocess.run([str(shell), str(script)], capture_output=True,
                            text=True, timeout=5, cwd=directory)
    ok = (result.stdout == "input from file\n" and result.stderr == ""
          and result.returncode == 0)
    print(f"{'PASS' if ok else 'FAIL'}: exec with redirection")
    if not ok:
        print(f"  stdout={result.stdout!r}, stderr={result.stderr!r}, "
              f"status={result.returncode}")
        failures += 1

print(f"Checks: {len(cases) + 1}, failures: {failures}")
raise SystemExit(bool(failures))
