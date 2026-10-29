#!/usr/bin/env python3
import errno
import os
import pty
import re
import select
import signal
import tempfile
import termios
import time
from pathlib import Path

shell = Path(__file__).resolve().parent.parent / "msh"
checks = 0
failures = 0
pending = b""


def check(name, passed):
    global checks, failures
    checks += 1
    print(f"{'PASS' if passed else 'FAIL'}: {name}")
    if not passed:
        failures += 1


with tempfile.TemporaryDirectory(prefix="msh-job-control-") as workdir:
    pid, terminal = pty.fork()

    if pid == 0:
        os.chdir(workdir)
        os.execv(str(shell), [str(shell)])

    reaped = False
    foreground_group = None

    def send(text):
        data = text.encode()
        while data:
            written = os.write(terminal, data)
            data = data[written:]

    def read_until(marker):
        global pending
        deadline = time.monotonic() + 10

        while marker not in pending:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise RuntimeError(
                    f"Timed out waiting for {marker!r}; received {pending!r}"
                )

            ready, _, _ = select.select([terminal], [], [], remaining)
            if not ready:
                continue

            try:
                chunk = os.read(terminal, 65536)
            except OSError as error:
                if error.errno == errno.EIO:
                    raise RuntimeError("Shell terminal closed unexpectedly")
                raise

            if not chunk:
                raise RuntimeError("Shell terminal reached EOF")

            pending += chunk

        end = pending.index(marker) + len(marker)
        result, pending = pending[:end], pending[end:]
        return result.decode(errors="replace").replace("\r", "")

    def command(line):
        send(line + "\n")
        return read_until(b"msh> ")

    try:
        read_until(b"msh> ")

        # The marker must occur on its own line, not just in echoed input.
        send("sh -c 'echo JOB_READY; exec sleep 300'\n")
        read_until(b"\r\nJOB_READY\r\n")

        foreground_group = os.tcgetpgrp(terminal)
        owns_terminal = foreground_group > 0 and foreground_group != pid
        check("foreground job owns terminal", owns_terminal)

        if not owns_terminal:
            raise RuntimeError("Foreground handoff failed")

        send("\x03")
        read_until(b"msh> ")

        reclaimed = os.tcgetpgrp(terminal) == pid
        check("shell reclaims terminal after Ctrl+C", reclaimed)
        if not reclaimed:
            raise RuntimeError("Shell did not reclaim terminal")

        output = command("echo $?")
        check("foreground Ctrl+C returns status 130", "\n130\n" in output)

        output = command("echo recovered")
        check("shell executes another command", "\nrecovered\n" in output)

        output = command("jobs")
        check(
            "interrupted job removed from active jobs",
            output.strip() == "jobs\nmsh>",
        )

        # Start another foreground job and stop it.
        send("sh -c 'echo STOP_READY; exec sleep 300'\n")
        read_until(b"\r\nSTOP_READY\r\n")

        foreground_group = os.tcgetpgrp(terminal)
        if foreground_group <= 0 or foreground_group == pid:
            raise RuntimeError("Second job did not receive terminal ownership")

        send("\x1a")
        output = read_until(b"msh> ")

        match = re.search(r"\[(\d+)\] Stopped", output)
        check("Ctrl+Z reports a stopped job", match is not None)
        if match is None:
            raise RuntimeError(f"No stopped job ID in {output!r}")

        job_id = match.group(1)
        check(
            "shell reclaims terminal after Ctrl+Z",
            os.tcgetpgrp(terminal) == pid,
        )

        output = command("jobs")
        check(
            "jobs lists the stopped job",
            re.search(rf"\[{job_id}\]\s+Stopped\b", output) is not None,
        )

        output = command(f"bg %{job_id}")
        check(
            "bg reports the resumed job",
            re.search(rf"\[{job_id}\]\s+Running\b", output) is not None,
        )
        check(
            "background resume leaves terminal with shell",
            os.tcgetpgrp(terminal) == pid,
        )

        output = command("jobs")
        check(
            "jobs lists the running background job",
            re.search(rf"\[{job_id}\]\s+Running\b", output) is not None,
        )

        # fg does not return a prompt until the job stops or finishes.
        send(f"fg %{job_id}\n")
        deadline = time.monotonic() + 10

        while time.monotonic() < deadline:
            if os.tcgetpgrp(terminal) == foreground_group:
                break
            time.sleep(0.02)

        handed_back = os.tcgetpgrp(terminal) == foreground_group
        check("fg returns terminal ownership to the job", handed_back)
        if not handed_back:
            raise RuntimeError("fg terminal handoff timed out")

        send("\x03")
        read_until(b"msh> ")

        output = command("echo $?")
        check(
            "resumed foreground job returns status 130 after Ctrl+C",
            "\n130\n" in output,
        )

        output = command("jobs")
        check(
            "resumed and interrupted job is removed",
            output.strip() == "jobs\nmsh>",
        )

        # Type without Enter, then wait until terminal echo confirms input.
        send("echo CANCELLED_COMMAND_MARKER")
        read_until(b"echo CANCELLED_COMMAND_MARKER")

        send("\x03")
        output = read_until(b"msh> ")
        check(
            "prompt Ctrl+C discards unfinished command",
            "CANCELLED_COMMAND_MARKER" not in output,
        )

        output = command("echo $?")
        check("prompt Ctrl+C sets status 130", "\n130\n" in output)

        output = command("echo prompt-recovered")
        check(
            "shell accepts commands after prompt cancellation",
            "\nprompt-recovered\n" in output
            and "CANCELLED_COMMAND_MARKER" not in output,
        )

        # The child disables echo, stops, then reports its resumed modes.
        output = command(
            "sh -c 'stty -echo; echo MODE_PID=$$; "
            "kill -STOP $$; stty -a; stty echo; echo MODE_RESTORED'"
        )

        child_match = re.search(r"^MODE_PID=(\d+)$", output, re.MULTILINE)
        if child_match is not None:
            foreground_group = int(child_match.group(1))

        stopped_match = re.search(r"\[(\d+)\] Stopped", output)
        check(
            "echo-disabled child stops successfully",
            child_match is not None and stopped_match is not None,
        )

        if child_match is None or stopped_match is None:
            raise RuntimeError(f"Missing terminal-mode test job: {output!r}")

        check(
            "shell restores echo while child is stopped",
            bool(termios.tcgetattr(terminal)[3] & termios.ECHO),
        )

        mode_job_id = stopped_match.group(1)
        output = command(f"fg %{mode_job_id}")

        check(
            "fg restores the child's saved echo-off mode",
            re.search(r"(?:^|[\s;])-echo(?=[\s;]|$)", output) is not None,
        )

        check(
            "resumed terminal-mode test completes",
            "\nMODE_RESTORED\n" in output,
        )

        check(
            "shell has echo and terminal ownership after completion",
            bool(termios.tcgetattr(terminal)[3] & termios.ECHO)
            and os.tcgetpgrp(terminal) == pid,
        )

        send("exit 0\n")
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
            # Clean up the test job if an assertion or timeout interrupted us.
            if foreground_group is not None and foreground_group > 0:
                if foreground_group != pid:
                    try:
                        os.killpg(foreground_group, signal.SIGKILL)
                    except ProcessLookupError:
                        pass

            try:
                os.kill(pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
            os.waitpid(pid, 0)

        os.close(terminal)

print(f"\nChecks: {checks}, failures: {failures}")
raise SystemExit(1 if failures else 0)
