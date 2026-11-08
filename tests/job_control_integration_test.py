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
    background_groups = set()

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
        output = read_until(b"msh> ")
        check(
            "foreground Ctrl+C places prompt on a new line",
            output.endswith("\nmsh> "),
        )

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


        # A background separator lets the next command run immediately.
        output = command("sleep 300 & echo BACKGROUND_LIST_READY")
        launch = re.search(r"^\[(\d+)\]\s+(\d+)\s*$", output, re.MULTILINE)

        if launch is not None:
            background_id = launch.group(1)
            foreground_group = int(launch.group(2))

        check("background list launches a job", launch is not None)
        if launch is None:
            raise RuntimeError(f"Missing background launch: {output!r}")

        check(
            "foreground command runs after background separator",
            "\nBACKGROUND_LIST_READY\n" in output,
        )
        check(
            "shell owns terminal after background list",
            os.tcgetpgrp(terminal) == pid,
        )

        output = command("jobs")
        check(
            "background job retains only its own command text",
            re.search(
                rf"^\[{background_id}\]\s+Running\s+sleep 300 &$",
                output,
                re.MULTILINE,
            ) is not None,
        )

        send(f"fg %{background_id}\n")
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline:
            if os.tcgetpgrp(terminal) == foreground_group:
                break
            time.sleep(0.02)

        handed_back = os.tcgetpgrp(terminal) == foreground_group
        check("fg foregrounds the background-list job", handed_back)
        if not handed_back:
            raise RuntimeError("Background-list fg handoff timed out")

        send("\x03")
        read_until(b"msh> ")

        output = command("echo $?")
        check(
            "interrupted background-list job returns status 130",
            "\n130\n" in output,
        )

        output = command("jobs")
        check(
            "interrupted background-list job is removed",
            output.strip() == "jobs\nmsh>",
        )


        output = command(
            "sleep 300 & sleep 301 & echo MULTI_BACKGROUND_READY"
        )
        launches = re.findall(
            r"^\[(\d+)\]\s+(\d+)\s*$", output, re.MULTILINE
        )
        background_groups.update(int(group) for _, group in launches)

        distinct = (
            len(launches) == 2
            and len({job for job, _ in launches}) == 2
            and len({group for _, group in launches}) == 2
        )
        check("multiple background jobs have distinct IDs and groups", distinct)
        if not distinct:
            raise RuntimeError(f"Expected two background jobs: {output!r}")

        check(
            "foreground command runs after two background jobs",
            "\nMULTI_BACKGROUND_READY\n" in output,
        )
        check(
            "shell owns terminal after multiple background launches",
            os.tcgetpgrp(terminal) == pid,
        )

        output = command("jobs")
        check(
            "jobs lists both background commands separately",
            all(
                re.search(
                    rf"^\[{job_id}\]\s+Running\s+sleep {duration} &$",
                    output,
                    re.MULTILINE,
                ) is not None
                for (job_id, _), duration in zip(launches, (300, 301))
            ),
        )

        for index, (job_id, group) in enumerate(launches, 1):
            foreground_group = int(group)
            send(f"fg %{job_id}\n")
            deadline = time.monotonic() + 10

            while time.monotonic() < deadline:
                if os.tcgetpgrp(terminal) == foreground_group:
                    break
                time.sleep(0.02)

            handed_back = os.tcgetpgrp(terminal) == foreground_group
            check(f"fg selects background job {index}", handed_back)
            if not handed_back:
                raise RuntimeError("Multiple-job fg handoff timed out")

            send("\x03")
            read_until(b"msh> ")

            output = command("echo $?")
            check(
                f"background job {index} interruption returns 130",
                "\n130\n" in output,
            )
            background_groups.discard(foreground_group)
            foreground_group = None

        output = command("jobs")
        check(
            "both interrupted background jobs are removed",
            output.strip() == "jobs\nmsh>",
        )


        output = command(
            "sleep 300 && echo CHAIN_FORBIDDEN & echo CHAIN_PARENT_READY"
        )
        launch = re.search(
            r"^\[(\d+)\]\s+(\d+)\s*$", output, re.MULTILINE
        )

        if launch is not None:
            chain_job_id = launch.group(1)
            foreground_group = int(launch.group(2))

        check("logical chain launches as one background job",
              launch is not None)
        if launch is None:
            raise RuntimeError(f"Missing chain launch: {output!r}")

        check(
            "parent continues after background logical chain",
            "\nCHAIN_PARENT_READY\n" in output,
        )
        check(
            "background chain leaves terminal with shell",
            os.tcgetpgrp(terminal) == pid,
        )

        output = command("jobs")
        check(
            "job record contains the whole logical chain",
            re.search(
                rf"^\[{chain_job_id}\]\s+Running\s+"
                r"sleep 300 && echo CHAIN_FORBIDDEN &$",
                output,
                re.MULTILINE,
            ) is not None,
        )

        send(f"fg %{chain_job_id}\n")
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline:
            if os.tcgetpgrp(terminal) == foreground_group:
                break
            time.sleep(0.02)

        handed_back = os.tcgetpgrp(terminal) == foreground_group
        check("fg gives terminal to the logical chain", handed_back)
        if not handed_back:
            raise RuntimeError("Logical-chain fg handoff timed out")

        send("\x03")
        output = read_until(b"msh> ")
        check(
            "interruption skips remaining chain command",
            "\nCHAIN_FORBIDDEN\n" not in output,
        )

        output = command("echo $?")
        check(
            "interrupted logical chain returns status 130",
            "\n130\n" in output,
        )

        output = command("jobs")
        check(
            "interrupted logical chain leaves no active job",
            output.strip() == "jobs\nmsh>",
        )
        foreground_group = None


        # Stop and resume an entire background logical chain.
        output = command(
            "sleep 300 && echo RESUMED_CHAIN_FORBIDDEN & echo RESUME_CHAIN_READY"
        )
        launch = re.search(
            r"^\[(\d+)\]\s+(\d+)\s*$", output, re.MULTILINE
        )

        if launch is not None:
            resume_chain_id = launch.group(1)
            foreground_group = int(launch.group(2))

        check("resumable logical chain launches", launch is not None)
        if launch is None:
            raise RuntimeError(f"Missing resumable chain: {output!r}")

        def foreground_resume_chain(label):
            send(f"fg %{resume_chain_id}\n")
            deadline = time.monotonic() + 10
            while time.monotonic() < deadline:
                if os.tcgetpgrp(terminal) == foreground_group:
                    break
                time.sleep(0.02)

            handed_back = os.tcgetpgrp(terminal) == foreground_group
            check(label, handed_back)
            if not handed_back:
                raise RuntimeError("Resumable-chain fg handoff timed out")

        foreground_resume_chain("logical chain initially receives terminal")
        send("\x1a")
        output = read_until(b"msh> ")

        check(
            "Ctrl+Z reports the logical chain stopped",
            re.search(rf"\[{resume_chain_id}\]\s+Stopped\b", output)
            is not None,
        )
        check(
            "shell reclaims terminal after chain stops",
            os.tcgetpgrp(terminal) == pid,
        )

        output = command("jobs")
        check(
            "jobs lists the stopped logical chain",
            re.search(rf"\[{resume_chain_id}\]\s+Stopped\b", output)
            is not None,
        )

        output = command(f"bg %{resume_chain_id}")
        check(
            "bg resumes the logical chain",
            re.search(rf"\[{resume_chain_id}\]\s+Running\b", output)
            is not None,
        )
        check(
            "resuming chain in background preserves shell terminal ownership",
            os.tcgetpgrp(terminal) == pid,
        )

        output = command("jobs")
        check(
            "jobs lists the resumed logical chain running",
            re.search(rf"\[{resume_chain_id}\]\s+Running\b", output)
            is not None,
        )

        foreground_resume_chain("resumed logical chain receives terminal again")
        send("\x03")
        output = read_until(b"msh> ")
        check(
            "interrupted resumed chain skips remaining command",
            "\nRESUMED_CHAIN_FORBIDDEN\n" not in output,
        )

        output = command("echo $?")
        check(
            "interrupted resumed chain returns status 130",
            "\n130\n" in output,
        )

        output = command("jobs")
        check(
            "resumed and interrupted chain leaves no active job",
            output.strip() == "jobs\nmsh>",
        )
        foreground_group = None

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
        for group in background_groups:
            if group > 0 and group != pid:
                try:
                    os.killpg(group, signal.SIGKILL)
                except ProcessLookupError:
                    pass

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
