
#!/usr/bin/env python3
import errno
import os
import pty
import re
import select
import signal
import subprocess
import tempfile
import time
from pathlib import Path

root = Path(__file__).resolve().parent.parent

harness = r"""
#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L
#include "chain.c"

static int checks;
static int failures;

static void check(const char *name, int passed)
{
    checks++;
    printf("%s: %s\n", passed ? "PASS" : "FAIL", name);
    if (!passed) {
        failures++;
    }
    fflush(stdout);
}

int main(void)
{
    if (terminal_init() == -1 || signals_init() == -1) {
        return 99;
    }

    TokenList tokens = {0};
    CommandList list = {0};
    char error[128];

    /*
     * Validate a foreground chain first. Then include its final '&'
     * in the token range to test the launcher independently of the
     * parser restriction that we have not removed yet.
     */
    if (lex_line("sleep 1 && echo CHAIN_COMPLETED",
                 &tokens, error, sizeof(error)) == -1
        || parse_command_list(&tokens, &list,
                              error, sizeof(error)) == -1) {
        return 99;
    }

    free_tokens(&tokens);
    const char *line = "sleep 1 && echo CHAIN_COMPLETED &";

    if (lex_line(line, &tokens, error, sizeof(error)) == -1) {
        return 99;
    }
    list.items[list.count - 1].token_count++;

    int result = execute_background_chain(
        &tokens, &list, 0, list.count, 0, line
    );
    check("background launch succeeds", result == 0);

    Job *job = jobs_find(1);
    check("parent registers one supervisor", job != NULL
          && job->process_count == 1);

    if (result != 0 || job == NULL) {
        return 1;
    }

    check("supervisor leads a separate process group",
          job->pgid != getpgrp()
          && getpgid(job->processes[0].pid) == job->pgid);
    check("parent retains terminal ownership",
          tcgetpgrp(STDIN_FILENO) == getpgrp());

    puts("PARENT_CONTINUES");
    fflush(stdout);

    for (int i = 0; i < 1000; i++) {
        jobs_reap_pending();
        if (jobs_get_state(job) == JOB_DONE) {
            break;
        }

        struct timespec pause_time = {0, 10000000L};
        nanosleep(&pause_time, NULL);
    }

    check("supervisor completes and is reaped",
          jobs_get_state(job) == JOB_DONE);

    int status = job->processes[0].wait_status;
    check("supervisor preserves successful chain status",
          job->processes[0].completed
          && WIFEXITED(status) && WEXITSTATUS(status) == 0);

    free_command_list(&list);
    free_tokens(&tokens);
    jobs_shutdown();
    signals_close();
    terminal_reclaim();

    printf("Checks: %d, failures: %d\n", checks, failures);
    return failures ? 1 : 0;
}
"""

with tempfile.TemporaryDirectory(prefix="msh-chain-launch-") as directory:
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

    pid, terminal = pty.fork()
    if pid == 0:
        os.chdir(workdir)
        os.execv(str(binary), [str(binary)])

    output = b""
    reaped = False
    failed = False

    try:
        deadline = time.monotonic() + 15
        while time.monotonic() < deadline:
            ready, _, _ = select.select([terminal], [], [], 0.1)
            if ready:
                try:
                    chunk = os.read(terminal, 65536)
                except OSError as error:
                    if error.errno != errno.EIO:
                        raise
                    break
                if not chunk:
                    break
                output += chunk
        else:
            raise RuntimeError("Launch test timed out")

        result, status = os.waitpid(pid, 0)
        reaped = True
        text = output.decode(errors="replace").replace("\r", "")
        print(text, end="")

        passed = (
            os.WIFEXITED(status) and os.WEXITSTATUS(status) == 0
            and "\nPARENT_CONTINUES\n" in text
            and "\nCHAIN_COMPLETED\n" in text
            and text.index("\nPARENT_CONTINUES\n")
                < text.index("\nCHAIN_COMPLETED\n")
        )
        print(f"{'PASS' if passed else 'FAIL'}: parent continues before chain finishes")
        failed = not passed

    except Exception as error:
        print(f"FAIL: {error}")
        failed = True
    finally:
        for group in re.findall(rb"^\[\d+\] (\d+)\r?$", output, re.MULTILINE):
            try:
                os.killpg(int(group), signal.SIGKILL)
            except ProcessLookupError:
                pass

        if not reaped:
            try:
                os.killpg(pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
            os.waitpid(pid, 0)

        os.close(terminal)

raise SystemExit(1 if failed else 0)
