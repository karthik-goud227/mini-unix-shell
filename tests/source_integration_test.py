#!/usr/bin/env python3
"""Check standalone dot sourcing in the current shell process."""
import subprocess
import tempfile
from pathlib import Path

shell = Path(__file__).resolve().parent.parent / "msh"
failures = 0
with tempfile.TemporaryDirectory(prefix="msh-source-") as directory:
    root = Path(directory)
    (root / "child.msh").write_text('VALUE=from_child\ncd sub\n')
    (root / "nested.msh").write_text('NESTED=visible\n')
    (root / "outer.msh").write_text('. nested.msh\nif test "$NESTED" = visible\nthen\nVALUE=nested_ok\nfi\n')
    (root / "sub").mkdir()
    cases = [
        ("variables and directory persist", '. child.msh\nprintf "<%s>\\n" "$VALUE"\npwd\n',
         f'<from_child>\n{root / "sub"}\n', 0, False),
        ("nested source and control block", '. outer.msh\nprintf "<%s>\\n" "$VALUE"\n',
         '<nested_ok>\n', 0, False),
        ("missing source is recoverable", '. missing.msh\necho $?\n', '1\n', 0, True),
        ("source requires one path", '.\n', '', 2, True),
        ("source rejects command list", '. nested.msh ; echo next\n', '', 2, True),
    ]
    script = root / "main.msh"
    for name, source, output, status, diagnostic in cases:
        script.write_text(source)
        result = subprocess.run([str(shell), str(script)], cwd=root,
                                capture_output=True, text=True, timeout=5)
        ok = (result.stdout == output and result.returncode == status
              and bool(result.stderr) == diagnostic)
        print(f"{'PASS' if ok else 'FAIL'}: {name}")
        if not ok:
            print(f"  stdout={result.stdout!r}, stderr={result.stderr!r}, status={result.returncode}")
            failures += 1

print(f"Checks: {len(cases)}, failures: {failures}")
raise SystemExit(bool(failures))
