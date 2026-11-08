
#!/usr/bin/env python3
import os
import signal
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parent.parent
checks = 0
failures = 0

harness = r"""
#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L
#include "chain.c"

int main(int argc, char **argv)
{
    if (argc != 2) {
        return 2;
    }

    TokenList tokens = {0};
    Pipeline pipeline = {0};
    char error[128];

    if (lex_line(argv[1], &tokens, error, sizeof(error)) == -1) {
        fprintf(stderr, "%s\n", error);
        return 2;
    }

    if (parse_pipeline(&tokens, &pipeline, error, sizeof(error)) == -1) {
        fprintf(stderr, "%s\n", error);
        free_tokens(&tokens);
        return 2;
    }

    int status = chain_execute_pipeline(&pipeline, 0);
    free_pipeline(&pipeline);
    free_tokens(&tokens);
    return status;
}
"""

def check(name, command, expected_out="", expected_status=0, groups=False):
    global checks, failures
    checks += 1

    process = subprocess.Popen(
        [str(binary), command],
        cwd=workdir,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        start_new_session=True,
    )

    try:
        out, err = process.communicate(timeout=10)
        expected_err = f"{process.pid}\n{process.pid}\n" if groups else ""
        passed = (
            out == expected_out
            and err == expected_err
            and process.returncode == expected_status
        )
    except subprocess.TimeoutExpired:
        passed = False
        out, err = "", "Timed out"
    finally:
        try:
            os.killpg(process.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        process.communicate()

    print(f"{'PASS' if passed else 'FAIL'}: {name}")
    if not passed:
        failures += 1
        print(f"  stdout={out!r}, stderr={err!r}, status={process.returncode}")

with tempfile.TemporaryDirectory(prefix="msh-chain-pipeline-") as directory:
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

    check("single command output", "echo hello", "hello\n")
    check("single command failure status", "false", expected_status=1)
    check("two-stage pipeline", "echo hello | cat", "hello\n")
    check("three-stage pipeline", "echo hello | cat | cat", "hello\n")
    check("last stage succeeds", "false | true")
    check("last stage fails", "true | false", expected_status=1)
    check("early reader exit does not hang", "seq 100000 | head -n 1", "1\n")
    check("file redirection", "echo saved > data")
    checks += 1
    saved = (workdir / "data").read_text() == "saved\n"
    print(f"{'PASS' if saved else 'FAIL'}: redirected file contents")
    failures += not saved

    command = (
        "python3 -c 'import os,sys; "
        'print(os.getpgrp(),file=sys.stderr); print("hello")'
        "' | python3 -c 'import os,sys; "
        "print(os.getpgrp(),file=sys.stderr); "
        'print(sys.stdin.read(),end="")'
        "'"
    )
    check("both stages inherit the supervisor process group",
          command, "hello\n", groups=True)

print(f"\nChecks: {checks}, failures: {failures}")
raise SystemExit(1 if failures else 0)
