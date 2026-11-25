#!/usr/bin/env python3
"""Check immutable shell variables in scripts."""
import subprocess
import tempfile
from pathlib import Path

shell = Path(__file__).resolve().parent.parent / "msh"
cases = [
    ("assign and read", 'readonly X=locked\necho "$X"\n', '', 'locked\n', 0, ""),
    ("reject reassignment", 'readonly X=locked\nX=changed\necho "$X"\n', '', 'locked\n', 0, "could not assign variable"),
    ("reject unset", 'readonly X=locked\nunset X\necho "$X"\n', '', 'locked\n', 0, "msh: unset"),
    ("export existing readonly", 'readonly X=locked\nexport X\nprintenv X\n', '', 'locked\n', 0, ""),
    ("reject export assignment", 'readonly X=locked\nexport X=changed\necho "$X"\n', '', 'locked\n', 0, "msh: export"),
    ("multiple names", 'readonly A=one B=two\necho "$A:$B"\n', '', 'one:two\n', 0, ""),
    ("read cannot overwrite", 'readonly X=locked\nread X\necho "$X"\n', 'new\n', 'locked\n', 0, "msh: read"),
    ("for cannot overwrite", 'readonly X=locked\nfor X in new\ndo\necho no\ndone\n', '', '', 1, "for assignment"),
    ("invalid name", 'readonly 2BAD=one\n', '', '', 2, "invalid variable name"),
]

failures = 0
with tempfile.TemporaryDirectory(prefix="msh-readonly-") as directory:
    script = Path(directory) / "readonly.msh"
    for name, source, input_text, output, status, error in cases:
        script.write_text(source)
        result = subprocess.run([str(shell), str(script)], input=input_text,
                                capture_output=True, text=True, timeout=5,
                                cwd=directory)
        ok = (result.stdout == output and result.returncode == status
              and (error in result.stderr if error else result.stderr == ""))
        print(f"{'PASS' if ok else 'FAIL'}: {name}")
        if not ok:
            print(f"  stdout={result.stdout!r}, stderr={result.stderr!r}, "
                  f"status={result.returncode}")
            failures += 1

print(f"Checks: {len(cases)}, failures: {failures}")
raise SystemExit(bool(failures))
