#!/usr/bin/env python3
"""Check basic set -- positional-argument replacement."""
import subprocess
import tempfile
from pathlib import Path

shell = Path(__file__).resolve().parent.parent / "msh"
cases = [
    ("replace arguments", 'set -- x y\necho "$#:$1:$2"\n', ["old"], '2:x:y\n', 0),
    ("clear arguments", 'set --\necho "$#:$1"\n', ["old"], '0:\n', 0),
    ("preserve quoted values", 'set -- "two words" ""\necho "[$1][$2]:$#"\n', [], '[two words][]:2\n', 0),
    ("works with shift", 'set -- a b c\nshift\necho "$#:$1"\n', [], '2:b\n', 0),
    ("works with for", 'set -- a b\nfor X\ndo\necho "$X"\ndone\n', [], 'a\nb\n', 0),
    ("append to quoted positional list", 'set -- "$@" tail\necho "[$1][$2][$3]:$#"\n', ["two words", ""], '[two words][][tail]:3\n', 0),
    ("invalid form", 'set bad\n', [], '', 2),
]

failures = 0
with tempfile.TemporaryDirectory(prefix="msh-set-") as directory:
    script = Path(directory) / "set.msh"
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
