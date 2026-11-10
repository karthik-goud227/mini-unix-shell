#!/usr/bin/env python3
import subprocess
import tempfile
from pathlib import Path

shell = Path(__file__).resolve().parent.parent / "msh"
cases = [
    ("comment-only input", "# ignored\n", "", 0),
    ("leading whitespace before comment", " \t# ignored\n", "", 0),
    ("trailing comment", "echo hello # ignored\n", "hello\n", 0),
    ("hash inside word stays literal", "echo hello#world\n", "hello#world\n", 0),
    ("quoted hashes stay literal",
     """printf '<%s>\\n' "#" '#'\n""", "<#>\n<#>\n", 0),
    ("escaped hash stays literal", "echo \\#literal\n", "#literal\n", 0),
    ("comment syntax is not parsed",
     "echo safe # ' | && > ( `\n", "safe\n", 0),
    ("comment after separator", "echo first;# ignored\n", "first\n", 0),
    ("comment does not change status", "false\n# ignored\n", "", 1),
    ("comment cannot complete a pipeline", "echo hello | # missing stage\n",
     "", 2),
    ("shebang line is accepted", "#!/usr/bin/env msh\necho script\n",
     "script\n", 0),
    ("later lines still execute", "# first\necho one\n# second\necho two\n",
     "one\ntwo\n", 0),
]

failures = 0
with tempfile.TemporaryDirectory(prefix="msh-comments-") as directory:
    script = Path(directory) / "comments.msh"
    for mode in ("stdin", "script"):
        for name, commands, expected, status in cases:
            script.write_text(commands)
            arguments = [str(shell)]
            if mode == "script":
                arguments.append(str(script))
            result = subprocess.run(
                arguments,
                input=commands if mode == "stdin" else "",
                text=True, capture_output=True, cwd=directory, timeout=10,
            )
            passed = (
                result.stdout == expected
                and result.returncode == status
                and (bool(result.stderr) if status == 2 else result.stderr == "")
            )
            print(f"{'PASS' if passed else 'FAIL'}: {mode}: {name}")
            if not passed:
                failures += 1
                print(f"  stdout={result.stdout!r}, expected={expected!r}")
                print(f"  stderr={result.stderr!r}, status={result.returncode}")

print(f"\nChecks: {2 * len(cases)}, failures: {failures}")
raise SystemExit(bool(failures))
