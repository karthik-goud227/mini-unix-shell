# Mini-Unix Shell (msh)

An educational Unix shell implemented in C for a third-year CSE
Operating Systems project.

The project implements a defined subset of shell behavior, with
interactive job control and signal handling as its main focus.
It runs on Linux, including Ubuntu inside Windows WSL 2.

## Base paper

S. R. Bourne, "The UNIX Shell," The Bell System Technical Journal,
vol. 57, no. 6, July-August 1978, pp. 1971-1990.

The paper provides the conceptual foundation for command execution,
pipes, redirection and shell behavior. This project is an independent
educational implementation of a subset, not a reproduction of the
complete Bourne shell.

Background execution and signal handling already appear in the paper.
This project's extension focuses on process-group job management,
foreground terminal ownership, stopped-job resumption and testing.

Modern implementation reference:
https://sourceware.org/glibc/manual/latest/html_node/Implementing-a-Shell.html

## Requirements

- Linux or Windows with WSL 2 and Ubuntu
- GCC with C11 support
- GNU Make
- Python 3 for integration tests
- Standard Linux utilities for testing
- An interactive terminal for job-control tests
- Optional: Git, GDB and Valgrind

Verified development environment:
Ubuntu 24.04.3 LTS on WSL 2, GCC 13.3.0 and GNU Make 4.3.

## Build and run

```bash
make
./msh
```

Run these commands from the repository directory.
Type commands at the `msh>` prompt.

To remove generated executables and object files:

```bash
make clean
```

## Supported features

- External programs with arguments and PATH lookup
- Built-ins: cd, pwd, exit, jobs, fg and bg
- Single and double quotes for argument grouping
- Environment-variable expansion with $NAME
- Previous command status expansion with $?
- Empty quoted arguments and adjacent quoted/unquoted fragments
- Backslash escaping
- Input redirection: <
- Output overwrite: >
- Output append: >>
- Multiple redirections applied in source order
- Concurrent pipelines with multiple stages
- Background execution with a trailing &
- Process-group foreground/background job management
- Ctrl+C for foreground interruption and prompt cancellation
- Ctrl+Z for stopping foreground jobs
- SIGTTIN behavior for background terminal readers
- Terminal-mode preservation across stop and resume
- SIGCHLD-driven background reaping while idle
- Confirmed shutdown with active-job cleanup

## Examples

Enter these inside msh:

```text
echo "hello world"
pwd
cd /tmp
echo first > example.txt
echo second >> example.txt
cat < example.txt
printf "apple\nbanana\n" | grep banana | wc -l
sleep 30 &
jobs
fg %1
```

Replace `%1` with the actual job ID.
Job IDs increase during a shell session and need not be consecutive
among the jobs currently listed.

## Built-in behavior

- `cd [path]`: changes the shell's directory; no path uses HOME.
- `pwd`: prints the physical current directory; accepts no arguments.
- `exit [status]`: exits with a decimal status from 0 through 255.
  Without an argument, it preserves the previous status.
  Invalid arguments report an error and keep the shell running.
- `jobs`: lists tracked jobs; accepts no arguments.
- `fg %N`: places job N in the foreground and resumes it if stopped.
- `bg %N`: resumes job N without giving it the terminal.

All built-ins run in the parent for standalone foreground commands.
Only pwd is permitted inside pipelines or background jobs.
Redirection is supported for cd, pwd, exit and jobs.
Redirection on fg and bg is deliberately rejected.

Job-control built-ins and background execution require an interactive
terminal. Non-interactive foreground commands and pipelines are supported.

## Variable expansion

Expansion runs after lexing and before parsing, using the original
token spelling to preserve quote and escape boundaries.

- `$NAME` reads an inherited environment variable.
- `$?` expands to the previous command's status.
- Double quotes allow expansion; single quotes prevent it.
- A backslash before `$` prevents expansion outside quotes and inside
  double quotes.
- Variable names use letters, digits and underscores, and cannot
  start with a digit.
- Unset variables expand to an empty string.
- Expansion applies to command words, arguments and redirection paths.

Examples inside msh:

```text
echo "$HOME"
echo '$HOME'
echo \$HOME
false
echo $?
```

The first command prints the home directory. The next two print
literal `$HOME`. After `false`, `echo $?` prints `1`.

Each original word remains one argument, including empty values.
There is no field splitting or wildcard expansion, even for unquoted
variables. This intentionally differs from full POSIX shell behavior.

Inserted values are not expanded again or interpreted as shell syntax.
For example, a pipe character inside a variable does not create a pipeline.
All `$?` references on one input line use the status from before that line.

Only `$NAME` and `$?` are supported. Braced expansion such as `${HOME}`,
positional parameters such as `$1`, and special parameters such as `$$`
are not implemented. Shell assignments, export and unset built-ins are
also not implemented.

## Signals and job lifecycle

One pipeline is one job, with one process record per stage.

At the prompt, the shell owns the terminal. A foreground job receives
terminal ownership until it finishes or stops. Children restore default
signal actions before execution.

Signal handlers wake the main loop through a nonblocking self-pipe.
Job-table changes and waitpid calls happen outside signal handlers.

Background child statuses are collected while the shell waits for input.
Completion and stop notifications are displayed at the next clean prompt.

With active jobs, the first exit or EOF request warns.
A repeated exit/EOF request without an intervening command confirms
shutdown. Cleanup sends SIGHUP and SIGCONT, allows a brief grace period,
then escalates to SIGTERM and SIGKILL if necessary and reaps the children.

## Status codes

- 0: success
- 1: general operational failure
- 2: syntax, usage or unsupported-feature error
- 126: command could not be executed
- 127: command not found or empty command name
- 128 + signal number: signal termination
- 130: Ctrl+C input cancellation

A pipeline returns the last stage's status.
Background launch returns 0 when launch setup succeeds; this does not
mean the background program has finished successfully.

## Limits and unsupported features

- Maximum input line: 4096 bytes, excluding the newline
- Maximum arguments per command: 128, including the command name
- Maximum pipeline stages: 16
- Maximum simultaneously registered jobs: 64

Oversized input is discarded rather than partly executed.
NUL bytes are rejected.

The current implementation does not support:
- Tilde expansion or advanced parameter expansion
- Field splitting of expanded values
- export and unset built-ins
- Wildcard expansion
- Command substitution
- Semicolon command lists or logical operators && and ||
- Subshell/grouping syntax
- Here-documents or arbitrary file-descriptor redirection
- Shell assignments, loops, functions or a scripting language
- Command history or autocomplete
- A custom memory allocator

Single quotes preserve literal text; double quotes allow variable
expansion. Unquoted semicolons, parentheses and backticks are rejected.

Foreground Ctrl+C may leave `^C` adjacent to the next prompt.
This is a display limitation and does not affect process control.

## Architecture

| Module | Responsibility |
| --- | --- |
| main.c | Initialization, dispatch, status and shutdown |
| input.c | Bounded input and poll-based event waiting |
| lexer.c | Words, operators, quotes, escapes and original token spelling |
| expansion.c | Quote-aware environment and previous-status expansion |
| parser.c | Commands, ordered redirections and pipelines |
| executor.c | Pipes, fork/exec, grouping and launch synchronization |
| builtins.c | Parent built-ins and descriptor restoration |
| redirection.c | File opening and standard-stream replacement |
| terminal.c | Terminal ownership, modes and child signal defaults |
| jobs.c | Job records, process states, notifications and cleanup |
| job_control.c | Foreground waiting and job resumption |
| signals.c | Minimal signal handlers and self-pipe notifications |

## Automated tests

```bash
make test
```

This builds and runs lexer, parser and job-state test programs,
plus 17 expansion unit tests and 35 whole-shell integration tests
(24 general tests and 11 expansion tests).

Integration tests cover quoting, statuses, pipelines, redirection,
error recovery and configured input/argument/pipeline limits.
Expansion integration tests also cover quote handling, empty values,
pipeline statuses, expanded filenames and built-in arguments.
They use temporary directories and clean up their test files.

Interactive job control additionally requires manual or PTY tests.

## Manual job-control checks

- Interrupt sleep and a pipeline with Ctrl+C.
- Stop a pipeline with Ctrl+Z; inspect it with jobs.
- Resume it with bg, then fg.
- Launch cat with &; verify it stops, then accepts input through fg.
- Verify background completion is reaped while the shell is idle.
- Stop a program that disables terminal echo; verify shell echo is
  restored and fg resumes with the program's saved terminal settings.
- Confirm exit warnings and cleanup with a live job.
- Cancel unfinished prompt input with Ctrl+C.

## Memory and descriptor checks

```bash
valgrind --leak-check=full --show-leak-kinds=all \
  --track-fds=yes --child-silent-after-fork=yes \
  --error-exitcode=99 ./msh
```

Manual checks on 19 October 2026 found no heap leaks or memory errors,
with only three standard descriptors open at shell exit, for:
- Foreground execution, parsing failures and redirection failures
- Background launch, stop/resume, interruption and shutdown cleanup

The expansion unit tests were also checked under Valgrind:
all 17 passed, all 154 allocations were freed, zero memory errors
were reported, and only three standard descriptors remained open.

These results cover the exercised scenarios, not every possible failure.

## Future extensions

Possible additions include history, export/unset built-ins,
advanced parameter expansion, autocomplete, logical operators,
job resource statistics and a project-specific memory allocator.

Each extension should define its behavior, update the relevant module
interfaces and preserve the existing regression tests.
