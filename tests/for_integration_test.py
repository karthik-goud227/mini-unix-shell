#!/usr/bin/env python3
"""End-to-end checks for script-mode for loops."""
import subprocess
import tempfile
from pathlib import Path

shell = Path(__file__).resolve().parent.parent / "msh"
cases = [
    ("basic list", 'for X in a b c\ndo\necho "$X"\ndone\n', 'a\nb\nc\n', 0),
    ("quoted values", 'for X in "one two" three\ndo\necho "$X"\ndone\n', 'one two\nthree\n', 0),
    ("empty list", 'for X in\ndo\necho no\ndone\necho yes\n', 'yes\n', 0),
    ("implicit list without arguments", 'for X\ndo\necho no\ndone\necho yes\n', 'yes\n', 0),
    ("expand list once", 'A=old\nfor X in "$A" next\ndo\necho "$X"\nA=new\ndone\n', 'old\nnext\n', 0),
    ("nested loops", 'for X in a b\ndo\nfor Y in 1 2\ndo\necho "$X$Y"\ndone\ndone\n', 'a1\na2\nb1\nb2\n', 0),
    ("break", 'for X in a b\ndo\necho "$X"\nbreak\ndone\n', 'a\n', 0),
    ("continue", 'for X in a b\ndo\ncontinue\necho no\ndone\necho yes\n', 'yes\n', 0),
    ("last body status", 'for X in a\ndo\nfalse\ndone\n', '', 1),
    ("break two levels", 'for X in a b\ndo\nfor Y in 1 2\ndo\necho "$X$Y"\nbreak 2\ndone\necho no\ndone\n', 'a1\n', 0),
    ("skip under if", 'if false\nthen\nfor X in a\ndo\necho no\ndone\nfi\necho yes\n', 'yes\n', 0),
    ("invalid name", 'for 2X in a\ndo\necho no\ndone\n', '', 2),
]

failures = 0
with tempfile.TemporaryDirectory(prefix="msh-for-") as directory:
    script = Path(directory) / "case.msh"
    for name, source, output, status in cases:
        script.write_text(source)
        try:
            result = subprocess.run([str(shell), str(script)], capture_output=True,
                                    text=True, timeout=5, cwd=directory)
            ok = (result.stdout == output and result.returncode == status
                  and (bool(result.stderr) == (status == 2)))
            detail = (f"stdout={result.stdout!r}, stderr={result.stderr!r}, "
                      f"status={result.returncode}")
        except subprocess.TimeoutExpired:
            ok = False
            detail = "timed out"
        print(f"{'PASS' if ok else 'FAIL'}: {name}")
        if not ok:
            print(f"  {detail}")
            failures += 1

    script.write_text('for X\ndo\necho "[$X]"\ndone\n')
    result = subprocess.run([str(shell), str(script), "one two", "", "three"],
                            capture_output=True, text=True, timeout=5,
                            cwd=directory)
    ok = (result.stdout == "[one two]\n[]\n[three]\n"
          and result.returncode == 0 and result.stderr == "")
    print(f"{'PASS' if ok else 'FAIL'}: implicit positional arguments")
    if not ok:
        print(f"  stdout={result.stdout!r}, stderr={result.stderr!r}, "
              f"status={result.returncode}")
        failures += 1

    script.write_text('for X in "$@"\ndo\necho "[$X]"\ndone\n')
    result = subprocess.run([str(shell), str(script), "one two", "", "three"],
                            capture_output=True, text=True, timeout=5,
                            cwd=directory)
    ok = (result.stdout == "[one two]\n[]\n[three]\n"
          and result.returncode == 0 and result.stderr == "")
    print(f"{'PASS' if ok else 'FAIL'}: explicit quoted positional list")
    if not ok:
        print(f"  stdout={result.stdout!r}, stderr={result.stderr!r}, "
              f"status={result.returncode}")
        failures += 1

print(f"Checks: {len(cases) + 2}, failures: {failures}")
raise SystemExit(bool(failures))
