#!/usr/bin/env python3
"""End-to-end checks for script-mode case blocks."""
import subprocess
import tempfile
from pathlib import Path

shell = Path(__file__).resolve().parent.parent / "msh"
cases = [
    ("literal match", 'case green in\nred)\necho no\n;;\ngreen)\necho yes\n;;\nesac\n', 'yes\n', 0),
    ("pattern alternatives", 'case b in\na|b)\necho yes\n;;\nesac\n', 'yes\n', 0),
    ("wildcard", 'case abc in\na*)\necho yes\n;;\nesac\n', 'yes\n', 0),
    ("bracket class containing pipe", 'case "|" in\n[a|b])\necho yes\n;;\nesac\n', 'yes\n', 0),
    ("escaped closing parenthesis", 'case ")" in\n\\))\necho yes\n;;\nesac\n', 'yes\n', 0),
    ("first match wins", 'case abc in\na*)\necho first\n;;\n*)\necho second\n;;\nesac\n', 'first\n', 0),
    ("no match", 'case x in\ny)\necho no\n;;\nesac\necho after\n', 'after\n', 0),
    ("variable expansion", 'X=green\ncase "$X" in\ngreen)\necho yes\n;;\nesac\n', 'yes\n', 0),
    ("nested cases", 'case a in\na)\ncase b in\nb)\necho yes\n;;\nesac\n;;\nesac\n', 'yes\n', 0),
    ("case inside loop", 'for X in a b\ndo\ncase "$X" in\na)\necho first\n;;\nb)\necho second\n;;\nesac\ndone\n', 'first\nsecond\n', 0),
    ("break inside case", 'for X in a b\ndo\ncase "$X" in\na)\necho first\nbreak\n;;\nesac\necho no\ndone\n', 'first\n', 0),
    ("skipped branch", 'if false\nthen\ncase x in\nx)\necho no\n;;\nesac\nfi\necho yes\n', 'yes\n', 0),
    ("invalid pattern", 'case x in\nx\necho no\nesac\n', '', 2),
    ("missing esac", 'case x in\nx)\necho no\n;;\n', 'no\n', 2),
]

failures = 0
with tempfile.TemporaryDirectory(prefix="msh-case-") as directory:
    script = Path(directory) / "case.msh"
    for name, source, output, status in cases:
        script.write_text(source)
        try:
            result = subprocess.run([str(shell), str(script)], capture_output=True,
                                    text=True, timeout=5, cwd=directory)
            ok = (result.stdout == output and result.returncode == status
                  and (bool(result.stderr) == (status != 0)))
            detail = (f"stdout={result.stdout!r}, stderr={result.stderr!r}, "
                      f"status={result.returncode}")
        except subprocess.TimeoutExpired:
            ok = False
            detail = "timed out"
        print(f"{'PASS' if ok else 'FAIL'}: {name}")
        if not ok:
            print(f"  {detail}")
            failures += 1

print(f"Checks: {len(cases)}, failures: {failures}")
raise SystemExit(bool(failures))
