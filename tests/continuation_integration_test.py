#!/usr/bin/env python3
"""Check shell backslash-newline continuation."""
import subprocess
import tempfile
from pathlib import Path

shell = Path(__file__).resolve().parent.parent / "msh"
cases = [
    ("join command word", 'echo hel\\\nlo\n', 'hello\n'),
    ("join inside double quotes", 'echo "a\\\nb"\n', 'ab\n'),
    ("join for word list", 'for X in a \\\nb\ndo\necho "$X"\ndone\n', 'a\nb\n'),
]

failures = 0
with tempfile.TemporaryDirectory(prefix="msh-continuation-") as directory:
    script = Path(directory) / "continuation.msh"
    for name, source, output in cases:
        script.write_text(source)
        result = subprocess.run([str(shell), str(script)], capture_output=True,
                                text=True, timeout=5, cwd=directory)
        ok = (result.stdout == output and result.stderr == ""
              and result.returncode == 0)
        print(f"{'PASS' if ok else 'FAIL'}: {name}")
        if not ok:
            print(f"  stdout={result.stdout!r}, stderr={result.stderr!r}, "
                  f"status={result.returncode}")
            failures += 1

    script.write_text('read X\necho "[$X]"\n')
    result = subprocess.run([str(shell), str(script)], input='abc\\\n',
                            capture_output=True, text=True, timeout=5,
                            cwd=directory)
    ok = (result.stdout == '[abc\\]\n' and result.stderr == ""
          and result.returncode == 0)
    print(f"{'PASS' if ok else 'FAIL'}: read preserves input backslash")
    if not ok:
        print(f"  stdout={result.stdout!r}, stderr={result.stderr!r}, "
              f"status={result.returncode}")
        failures += 1

print(f"Checks: {len(cases) + 1}, failures: {failures}")
raise SystemExit(bool(failures))
