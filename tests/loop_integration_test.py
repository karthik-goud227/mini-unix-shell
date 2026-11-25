#!/usr/bin/env python3
"""End-to-end checks for script-mode while and until blocks."""
import subprocess
import tempfile
from pathlib import Path

shell = Path(__file__).resolve().parent.parent / "msh"
cases = [
    ("while skips false condition", "while false\ndo\necho no\ndone\necho after\n", "after\n", 0, ""),
    ("until skips true condition", "until true\ndo\necho no\ndone\necho after\n", "after\n", 0, ""),
    ("while reevaluates condition", "X=one\nwhile test \"$X\" != done\ndo\necho \"$X\"\nif test \"$X\" = one\nthen\nX=two\nelse\nX=done\nfi\ndone\n", "one\ntwo\n", 0, ""),
    ("until reevaluates condition", "X=first\nuntil test \"$X\" = done\ndo\necho pass\nX=done\ndone\n", "pass\n", 0, ""),
    ("nested loops", "O=first\nwhile test \"$O\" = first\ndo\nI=first\nuntil test \"$I\" = done\ndo\necho nested\nI=done\ndone\nO=done\ndone\n", "nested\n", 0, ""),
    ("if skips loop", "if false\nthen\nwhile true\ndo\necho no\ndone\nelse\necho yes\nfi\n", "yes\n", 0, ""),
    ("while returns last body status", "X=first\nwhile test \"$X\" = first\ndo\nX=done\nfalse\ndone\n", "", 1, ""),
    ("until returns last body status", "X=first\nuntil test \"$X\" = done\ndo\nX=done\nfalse\ndone\n", "", 1, ""),
    ("break skips remaining body", "while true\ndo\necho once\nbreak\necho never\ndone\necho after\n", "once\nafter\n", 0, ""),
    ("continue reevaluates condition", "X=one\nwhile test \"$X\" != done\ndo\nif test \"$X\" = one\nthen\nX=two\ncontinue\nfi\necho two\nX=done\ndone\n", "two\n", 0, ""),
    ("break two levels", "while true\ndo\nwhile true\ndo\necho hit\nbreak 2\ndone\necho never\ndone\necho after\n", "hit\nafter\n", 0, ""),
    ("continue two levels", "X=one\nwhile test \"$X\" = one\ndo\nwhile true\ndo\nX=done\ncontinue 2\necho never\ndone\necho never\ndone\necho after\n", "after\n", 0, ""),
    ("skipped break does not run", "while true\ndo\nif false\nthen\nbreak\nfi\necho yes\nbreak\ndone\n", "yes\n", 0, ""),
    ("invalid break level", "while true\ndo\nbreak 0\ndone\n", "", 2, "invalid loop level"),
    ("continue outside loop", "continue\n", "", 2, "invalid loop level"),
    ("missing do", "while true\necho no\ndone\n", "", 2, "expected do"),
    ("unexpected done", "done\n", "", 2, "unexpected done"),
    ("missing done", "while false\ndo\n", "", 2, "unfinished control block"),
]

failures = 0
with tempfile.TemporaryDirectory(prefix="msh-loop-") as directory:
    script = Path(directory) / "case.msh"
    for name, source, output, status, error in cases:
        script.write_text(source)
        try:
            result = subprocess.run([str(shell), str(script)], capture_output=True,
                                    text=True, timeout=5, cwd=directory)
            ok = (result.stdout == output and result.returncode == status
                  and (error in result.stderr if error else result.stderr == ""))
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
