#!/usr/bin/env python3
"""Check that the added paper features work together in one script."""
import stat
import subprocess
import tempfile
from pathlib import Path

shell = Path(__file__).resolve().parent.parent / "msh"
source = '''read INPUT
set -- "$INPUT" second
shift
umask 077
for ITEM
do
case "$ITEM" in
second)
if true
then
echo "$ITEM"
: > private.txt
fi
;;
*)
echo unexpected
;;
esac
done
'''

with tempfile.TemporaryDirectory(prefix="msh-paper-") as directory:
    script = Path(directory) / "features.msh"
    script.write_text(source)
    result = subprocess.run([str(shell), str(script)], input="first value\n",
                            capture_output=True, text=True, timeout=5,
                            cwd=directory)
    private = Path(directory) / "private.txt"
    ok = (result.returncode == 0 and result.stdout == "second\n"
          and result.stderr == "" and private.exists()
          and stat.S_IMODE(private.stat().st_mode) == 0o600)
    print(f"{'PASS' if ok else 'FAIL'}: combined paper features")
    if not ok:
        print(f"  stdout={result.stdout!r}, stderr={result.stderr!r}, "
              f"status={result.returncode}, file_exists={private.exists()}")

raise SystemExit(not ok)
