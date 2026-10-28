#!/usr/bin/env python3
import errno
import os
import pty
import re
import select
import signal
import tempfile
import time
from pathlib import Path

shell = Path(__file__).resolve().parent.parent / "msh"
total = 0
failures = 0


def check(name, passed):
    global total, failures
    total += 1
    print(f"{'PASS' if passed else 'FAIL'}: {name}")
    if not passed:
        failures += 1


def history_rows(output):
    return re.findall(r"^(\d+)  (.*)$", output, re.MULTILINE)


with tempfile.TemporaryDirectory(prefix="msh-history-pty-") as workdir:
    pid, terminal = pty.fork()

    if pid == 0:
        os.chdir(workdir)
        os.execv(str(shell), [str(shell)])

    reaped = False

    def read_prompt():
        data = b""
        deadline = time.monotonic() + 10

        while not data.endswith(b"msh> "):
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise RuntimeError(f"Prompt timeout; received {data!r}")

            ready, _, _ = select.select([terminal], [], [], remaining)
            if not ready:
                continue

            try:
                chunk = os.read(terminal, 65536)
            except OSError as error:
                if error.errno == errno.EIO:
                    raise RuntimeError("Shell exited before its prompt")
                raise

            if not chunk:
                raise RuntimeError("Terminal closed before its prompt")
            data += chunk

        return data.decode(errors="replace").replace("\r", "")

    def command(line):
        os.write(terminal, (line + "\n").encode())
        return read_prompt()

    try:
        read_prompt()

        check(
            "history includes itself",
            history_rows(command("history")) == [("1", "history")],
        )

        command("history -c")
        check(
            "clear restarts numbering",
            history_rows(command("history")) == [("1", "history")],
        )

        command("history -c")
        command("")
        command("   ")
        check(
            "blank input excluded",
            history_rows(command("history")) == [("1", "history")],
        )

        command("history -c")
        command('echo "$HOME"')
        check(
            "original variable spelling retained",
            history_rows(command("history"))
            == [("1", 'echo "$HOME"'), ("2", "history")],
        )

        command("history -c")
        command("echo broken |")
        check(
            "submitted syntax error retained",
            history_rows(command("history"))
            == [("1", "echo broken |"), ("2", "history")],
        )

        command("history -c")
        command("history > saved.txt")
        saved = Path(workdir, "saved.txt").read_text()
        restored = command("echo restored")
        check(
            "redirection saves history and restores stdout",
            saved == "1  history > saved.txt\n"
            and "\nrestored\n" in restored,
        )

        error_output = command("history invalid")
        status_output = command("echo $?")
        check(
            "invalid arguments return status 2",
            "usage: history [-c]" in error_output
            and "\n2\n" in status_output,
        )

        command("history -c")
        for _ in range(105):
            command("pwd > /dev/null")

        rows = history_rows(command("history"))
        expected = [
            (str(i), "pwd > /dev/null") for i in range(7, 106)
        ] + [("106", "history")]

        check("latest 100 entries retained", rows == expected)

        os.write(terminal, b"exit 0\n")
        deadline = time.monotonic() + 10

        while time.monotonic() < deadline:
            result, status = os.waitpid(pid, os.WNOHANG)
            if result == pid:
                reaped = True
                check(
                    "shell exits normally",
                    os.WIFEXITED(status) and os.WEXITSTATUS(status) == 0,
                )
                break
            time.sleep(0.02)
        else:
            raise RuntimeError("Shell did not exit")

    except Exception as error:
        failures += 1
        print(f"FAIL: test execution: {error}")
    finally:
        if not reaped:
            try:
                os.kill(pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
            os.waitpid(pid, 0)
        os.close(terminal)

print(f"\nChecks: {total}, failures: {failures}")
raise SystemExit(1 if failures else 0)
