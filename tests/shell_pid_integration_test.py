#!/usr/bin/env python3
import subprocess
import tempfile
from pathlib import Path

shell = Path(__file__).resolve().parent.parent / "msh"
checks = 0
failures = 0


def check(name, commands, expected, script_mode=False):
    global checks, failures
    checks += 1
    with tempfile.TemporaryDirectory(prefix="msh-pid-") as directory:
        arguments = [str(shell)]
        if script_mode:
            script = Path(directory) / "pid.msh"
            script.write_text(commands)
            arguments.append(str(script))
        process = subprocess.Popen(
            arguments, stdin=subprocess.PIPE,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            text=True, cwd=directory,
        )
        try:
            out, err = process.communicate(
                "" if script_mode else commands, timeout=10,
            )
        except subprocess.TimeoutExpired:
            process.kill()
            process.communicate()
            failures += 1
            print(f"FAIL: {name}: timed out")
            return

    expected = expected.replace("@PID@", str(process.pid))
    passed = out == expected and err == "" and process.returncode == 0
    print(f"{'PASS' if passed else 'FAIL'}: {name}")
    if not passed:
        failures += 1
        print(f"  stdout={out!r}, expected={expected!r}")
        print(f"  stderr={err!r}, status={process.returncode}")


check("shell PID matches process identity",
      'echo "$$"\necho "${$}"\n',
      "@PID@\n@PID@\n")
check("shell PID remains stable across commands",
      'echo "$$"; true; echo "$$"\n',
      "@PID@\n@PID@\n")
check("pipeline expansion uses shell PID",
      'echo "$$" | cat\n', "@PID@\n")
check("single quotes and escapes preserve literal PID syntax",
      "echo '$$'\necho \\$\\$\necho '${$}'\n",
      "$$\n$$\n${$}\n")
check("shell PID expands inside assignment",
      'MSH_PID_TEST="pid-$$"; echo "$MSH_PID_TEST"\n',
      "pid-@PID@\n")
check("shell PID expands inside fallback",
      'unset MSH_PID_TEST\necho "${MSH_PID_TEST-$$}"\n',
      "@PID@\n")
check("script PID matches script shell process",
      'echo "$$"\necho "${$}"\n',
      "@PID@\n@PID@\n", script_mode=True)

print(f"\nChecks: {checks}, failures: {failures}")
raise SystemExit(1 if failures else 0)
