#!/usr/bin/env python3
"""End-to-end checks for script-mode if blocks."""
import subprocess
import tempfile
from pathlib import Path

shell = Path(__file__).resolve().parent.parent / "msh"
cases = [
    ("true branch", "if true\nthen\necho yes\nelse\necho no\nfi\n", "yes\n", 0, ""),
    ("false branch", "if false\nthen\necho no\nelse\necho yes\nfi\n", "yes\n", 0, ""),
    ("no else", "if false\nthen\necho no\nfi\n", "", 0, ""),
    ("nested branch", "if true\nthen\nif false\nthen\necho no\nelse\necho nested\nfi\nfi\n", "nested\n", 0, ""),
    ("skipped expansion", "if false\nthen\necho \"${MISSING?bad}\"\nelse\necho safe\nfi\n", "safe\n", 0, ""),
    ("condition command list", "if false || true\nthen\necho yes\nfi\n", "yes\n", 0, ""),
    ("first elif succeeds", "if false\nthen\necho no\nelif true\nthen\necho yes\nelse\necho no\nfi\n", "yes\n", 0, ""),
    ("later elif succeeds", "if false\nthen\necho no\nelif false\nthen\necho no\nelif true\nthen\necho last\nfi\n", "last\n", 0, ""),
    ("matched branch skips elif expansion", "if true\nthen\necho first\nelif : \"${MISSING?bad}\"\nthen\necho no\nfi\n", "first\n", 0, ""),
    ("nested elif", "if true\nthen\nif false\nthen\necho no\nelif true\nthen\necho nested\nfi\nfi\n", "nested\n", 0, ""),
    ("unexpected else", "else\n", "", 2, "unexpected else"),
    ("unexpected elif", "elif true\nthen\nfi\n", "", 2, "unexpected elif"),
    ("elif after else", "if false\nthen\nelse\nelif true\nthen\nfi\n", "", 2, "unexpected elif"),
    ("missing elif condition", "if false\nthen\nelif\n", "", 2, "elif requires a condition"),
    ("missing then", "if true\necho bad\nfi\n", "", 2, "expected then"),
    ("duplicate else", "if true\nthen\nelse\nelse\nfi\n", "", 2, "unexpected else"),
    ("missing fi", "if true\nthen\necho yes\n", "yes\n", 2, "unfinished control block"),
]

failures = 0
with tempfile.TemporaryDirectory(prefix="msh-if-") as directory:
    script = Path(directory) / "case.msh"
    for name, source, output, status, error in cases:
        script.write_text(source)
        result = subprocess.run([str(shell), str(script)], capture_output=True,
                                text=True, timeout=10, cwd=directory)
        ok = (result.stdout == output and result.returncode == status
              and (error in result.stderr if error else result.stderr == ""))
        print(f"{'PASS' if ok else 'FAIL'}: {name}")
        if not ok:
            print(f"  stdout={result.stdout!r}, stderr={result.stderr!r}, status={result.returncode}")
            failures += 1

print(f"Checks: {len(cases)}, failures: {failures}")
raise SystemExit(bool(failures))
