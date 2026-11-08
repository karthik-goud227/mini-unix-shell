
#!/usr/bin/env python3
import os
import select
import signal
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parent.parent

harness = r"""
#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L
#include "chain.c"

int main(void)
{
    if (chain_prepare_supervisor_signals() == -1) {
        return 99;
    }

    TokenList tokens = {0};
    CommandList list = {0};
    char error[128];

    const char *line =
        "sh -c 'echo STAGE_PID=$$; exec sleep 300'"
        " || echo FORBIDDEN_FALLBACK";

    if (lex_line(line, &tokens, error, sizeof(error)) == -1
        || parse_command_list(&tokens, &list,
                              error, sizeof(error)) == -1) {
        return 99;
    }

    int status = chain_evaluate(&tokens, &list, 0, list.count, 0);
    free_command_list(&list);
    free_tokens(&tokens);
    return fflush(NULL) == EOF ? 1 : status;
}
"""

checks = 0
failures = 0

def check(name, passed):
    global checks, failures
    checks += 1
    print(f"{'PASS' if passed else 'FAIL'}: {name}")
    if not passed:
        failures += 1

with tempfile.TemporaryDirectory(prefix="msh-chain-signal-") as directory:
    workdir = Path(directory)
    test_source = workdir / "runner.c"
    binary = workdir / "runner"
    test_source.write_text(harness)

    objects = sorted(
        str(p) for p in root.glob("*.o")
        if p.name not in ("main.o", "chain.o")
    )
    subprocess.run([
        "gcc", "-std=c11", "-Wall", "-Wextra", "-Wpedantic",
        "-Wno-unused-function", "-g", "-O0", "-I", str(root),
        str(test_source), *objects, "-o", str(binary),
    ], check=True)

    for number in (signal.SIGINT, signal.SIGTERM):
        name = signal.Signals(number).name
        process = subprocess.Popen(
            [str(binary)],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            start_new_session=True,
        )

        try:
            ready, _, _ = select.select([process.stdout], [], [], 10)
            if not ready:
                raise RuntimeError("Stage readiness timed out")

            line = process.stdout.readline().strip()
            if not line.startswith("STAGE_PID="):
                raise RuntimeError(f"Unexpected readiness line: {line!r}")

            stage_pid = int(line.split("=", 1)[1])
            os.kill(process.pid, number)
            out, err = process.communicate(timeout=10)

            check(f"{name}: supervisor returns signal status",
                  process.returncode == 128 + number)
            check(f"{name}: fallback does not execute",
                  "FORBIDDEN_FALLBACK" not in out and err == "")

            try:
                os.kill(stage_pid, 0)
                gone = False
            except ProcessLookupError:
                gone = True

            check(f"{name}: pipeline child terminated and reaped", gone)

        except Exception as error:
            failures += 1
            print(f"FAIL: {name}: {error}")
        finally:
            try:
                os.killpg(process.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
            process.communicate()

print(f"\nChecks: {checks}, failures: {failures}")
raise SystemExit(1 if failures else 0)
