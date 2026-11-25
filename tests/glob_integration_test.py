#!/usr/bin/env python3
"""Check pathname expansion for unquoted command arguments."""
import subprocess
import tempfile
from pathlib import Path

shell = Path(__file__).resolve().parent.parent / "msh"
cases = [
    ("star", 'printf "%s\\n" *.txt\n', 'a.txt\nb.txt\n'),
    ("question mark", 'printf "%s\\n" ?.txt\n', 'a.txt\nb.txt\n'),
    ("bracket class", 'printf "%s\\n" [ab].txt\n', 'a.txt\nb.txt\n'),
    ("quoted star", 'printf "%s\\n" "*.txt"\n', '*.txt\n'),
    ("escaped star", 'printf "%s\\n" \\*.txt\n', '*.txt\n'),
    ("unmatched pattern", 'printf "%s\\n" missing-*.xyz\n', 'missing-*.xyz\n'),
    ("wildcard from variable", 'P=*.txt\nprintf "%s\\n" $P\n', 'a.txt\nb.txt\n'),
    ("quoted variable stays literal", 'P=*.txt\nprintf "%s\\n" "$P"\n', '*.txt\n'),
    ("for word list", 'for X in *.txt\ndo\necho "$X"\ndone\n', 'a.txt\nb.txt\n'),
]

failures = 0
with tempfile.TemporaryDirectory(prefix="msh-glob-") as directory:
    for name in ("a.txt", "b.txt", ".hidden.txt", "c.log"):
        (Path(directory) / name).write_text("")
    script = Path(directory) / "glob.msh"
    for name, source, output in cases:
        script.write_text(source)
        result = subprocess.run([str(shell), str(script)], capture_output=True,
                                text=True, timeout=5, cwd=directory)
        ok = (result.stdout == output and result.returncode == 0
              and result.stderr == "")
        print(f"{'PASS' if ok else 'FAIL'}: {name}")
        if not ok:
            print(f"  stdout={result.stdout!r}, stderr={result.stderr!r}, "
                  f"status={result.returncode}")
            failures += 1

    script.write_text('echo content > "*.out"\n')
    result = subprocess.run([str(shell), str(script)], capture_output=True,
                            text=True, timeout=5, cwd=directory)
    target = Path(directory) / "*.out"
    ok = (result.returncode == 0 and result.stderr == ""
          and target.exists() and target.read_text() == "content\n")
    print(f"{'PASS' if ok else 'FAIL'}: redirection target remains literal")
    if not ok:
        print(f"  status={result.returncode}, stderr={result.stderr!r}, "
              f"file_exists={target.exists()}")
        failures += 1

print(f"Checks: {len(cases) + 1}, failures: {failures}")
raise SystemExit(bool(failures))
