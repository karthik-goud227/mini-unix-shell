#!/usr/bin/env python3
"""Check that wait blocks for interactive background work."""
import errno
import os
import pty
import select
import tempfile
import time
from pathlib import Path

shell = Path(__file__).resolve().parent.parent / "msh"
checks = 0
failures = 0

with tempfile.TemporaryDirectory(prefix="msh-wait-") as directory:
    pid, terminal = pty.fork()
    if pid == 0:
        os.chdir(directory)
        os.execv(str(shell), [str(shell)])

    pending = b""

    def read_prompt():
        global pending
        deadline = time.monotonic() + 8
        while b"msh> " not in pending:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise RuntimeError(f"timed out waiting for prompt: {pending!r}")
            ready, _, _ = select.select([terminal], [], [], remaining)
            if not ready:
                continue
            try:
                data = os.read(terminal, 65536)
            except OSError as error:
                if error.errno == errno.EIO:
                    raise RuntimeError("shell terminal closed") from error
                raise
            if not data:
                raise RuntimeError("shell terminal reached EOF")
            pending += data
        end = pending.index(b"msh> ") + len(b"msh> ")
        output, pending = pending[:end], pending[end:]
        return output.decode(errors="replace").replace("\r", "")

    def command(line):
        os.write(terminal, (line + "\n").encode())
        return read_prompt()

    def check(name, passed):
        global checks, failures
        checks += 1
        failures += not passed
        print(f"{'PASS' if passed else 'FAIL'}: {name}")

    try:
        read_prompt()
        output = command("wait")
        check("wait with no jobs succeeds", "msh> " in output)
        command("sleep 0.4 &")
        start = time.monotonic()
        output = command("wait")
        elapsed = time.monotonic() - start
        check("wait blocks for background job", elapsed >= 0.25)
        check("wait returns to prompt", "msh> " in output)
        output = command("wait extra")
        check("wait rejects arguments", "arguments are not supported" in output)
        os.write(terminal, b"exit\n")
    finally:
        os.close(terminal)
        os.waitpid(pid, 0)

print(f"Checks: {checks}, failures: {failures}")
raise SystemExit(bool(failures))
