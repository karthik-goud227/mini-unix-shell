#!/usr/bin/env python3
"""Check basic read builtin behavior in scripts."""
import subprocess
import tempfile
from pathlib import Path

shell = Path(__file__).resolve().parent.parent / "msh"
cases = [
    ("read line", 'read X\necho "[$X]"\n', 'hello world\n', '[hello world]\n', 0),
    ("read two lines", 'read A\nread B\necho "$A:$B"\n', 'first\nsecond\n', 'first:second\n', 0),
    ("empty line", 'read X\necho "[$X]"\n', '\n', '[]\n', 0),
    ("EOF", 'read X\n', '', '', 1),
    ("invalid variable", 'read 2X\n', 'value\n', '', 2),
]

failures = 0
with tempfile.TemporaryDirectory(prefix="msh-read-") as directory:
    script = Path(directory) / "read.msh"
    for name, source, input_text, output, status in cases:
        script.write_text(source)
        result = subprocess.run([str(shell), str(script)], input=input_text,
                                capture_output=True, text=True, timeout=5,
                                cwd=directory)
        ok = (result.stdout == output and result.returncode == status
              and (bool(result.stderr) == (status == 2)))
        print(f"{'PASS' if ok else 'FAIL'}: {name}")
        if not ok:
            print(f"  stdout={result.stdout!r}, stderr={result.stderr!r}, "
                  f"status={result.returncode}")
            failures += 1

    (Path(directory) / "data.txt").write_text("from file\n")
    script.write_text('read X < data.txt\necho "[$X]"\n')
    result = subprocess.run([str(shell), str(script)], input="ignored\n",
                            capture_output=True, text=True, timeout=5,
                            cwd=directory)
    ok = (result.stdout == "[from file]\n" and result.stderr == ""
          and result.returncode == 0)
    print(f"{'PASS' if ok else 'FAIL'}: input redirection")
    if not ok:
        print(f"  stdout={result.stdout!r}, stderr={result.stderr!r}, "
              f"status={result.returncode}")
        failures += 1

print(f"Checks: {len(cases) + 1}, failures: {failures}")
raise SystemExit(bool(failures))
