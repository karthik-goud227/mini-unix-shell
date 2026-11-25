#!/usr/bin/env python3
"""Check unquoted home directory expansion in commands and assignments."""
import pwd
import os
import subprocess
import tempfile
from pathlib import Path

shell = Path(__file__).resolve().parent.parent / "msh"
user = pwd.getpwuid(os.getuid())
cases = [
    ("home and suffix", 'HOME=/tmp/msh-home\nprintf "<%s>\\n" ~ ~/docs\n',
     '</tmp/msh-home>\n</tmp/msh-home/docs>\n', 0),
    ("quoted and escaped remain literal", "printf '<%s>\\n' '~' \"~\" \\~\n",
     '<~>\n<~>\n<~>\n', 0),
    ("named user", f"printf '<%s>\\n' ~{user.pw_name}/docs\n",
     f'<{user.pw_dir}/docs>\n', 0),
    ("unknown user literal", "printf '<%s>\\n' ~msh_user_that_cannot_exist_8462/docs\n",
     '<~msh_user_that_cannot_exist_8462/docs>\n', 0),
    ("assignment value", 'HOME=/tmp/msh-home\nDEST=~/docs\nprintf "<%s>\\n" "$DEST"\n',
     '</tmp/msh-home/docs>\n', 0),
    ("middle of word remains literal", "printf '<%s>\\n' prefix~/docs\n",
     '<prefix~/docs>\n', 0),
]

failures = 0
with tempfile.TemporaryDirectory(prefix="msh-tilde-") as directory:
    script = Path(directory) / "tilde.msh"
    for name, source, output, status in cases:
        script.write_text(source)
        result = subprocess.run([str(shell), str(script)], capture_output=True,
                                text=True, timeout=5, cwd=directory)
        ok = result.stdout == output and result.stderr == "" and result.returncode == status
        print(f"{'PASS' if ok else 'FAIL'}: {name}")
        if not ok:
            print(f"  stdout={result.stdout!r}, stderr={result.stderr!r}, status={result.returncode}")
            failures += 1

print(f"Checks: {len(cases)}, failures: {failures}")
raise SystemExit(bool(failures))
